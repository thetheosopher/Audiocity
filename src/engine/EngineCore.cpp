#include "EngineCore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace audiocity::engine
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

float computeLfoWave(const audiocity::engine::EngineCore::FilterSettings::LfoShape shape,
                     const float phase) noexcept
{
    const auto p = phase - std::floor(phase);
    switch (shape)
    {
        case audiocity::engine::EngineCore::FilterSettings::LfoShape::triangle:
            return 1.0f - 4.0f * std::abs(p - 0.5f);
        case audiocity::engine::EngineCore::FilterSettings::LfoShape::square:
            return p < 0.5f ? 1.0f : -1.0f;
        case audiocity::engine::EngineCore::FilterSettings::LfoShape::sawUp:
            return (2.0f * p) - 1.0f;
        case audiocity::engine::EngineCore::FilterSettings::LfoShape::sawDown:
            return 1.0f - (2.0f * p);
        case audiocity::engine::EngineCore::FilterSettings::LfoShape::sine:
        default:
            return std::sin(2.0f * kPi * p);
    }
}

float unitHashToFloat(std::uint32_t x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<float>(x) * (1.0f / 4294967295.0f);
}

float deterministicBipolarFromNoteAndOrder(const int noteNumber, const std::uint64_t startOrder) noexcept
{
    const auto seedA = static_cast<std::uint32_t>(noteNumber * 2654435761u);
    const auto seedB = static_cast<std::uint32_t>((startOrder & 0xffffffffull) ^ (startOrder >> 32));
    const auto unit = unitHashToFloat(seedA ^ seedB ^ 0x9e3779b9U);
    return (2.0f * unit) - 1.0f;
}

std::optional<juce::String> getMetadataValueCaseInsensitive(const juce::StringPairArray& metadata,
                                                            const juce::String& key)
{
    const auto keys = metadata.getAllKeys();
    for (int i = 0; i < keys.size(); ++i)
    {
        if (keys[i].equalsIgnoreCase(key))
            return metadata.getValue(keys[i], {});
    }

    return std::nullopt;
}

std::optional<int> parseMidiNoteFromMetadataString(const juce::String& raw)
{
    const auto trimmed = raw.trim();
    if (trimmed.isEmpty())
        return std::nullopt;

    if (trimmed.containsOnly("-0123456789"))
        return juce::jlimit(0, 127, trimmed.getIntValue());

    const juce::String upper = trimmed.toUpperCase();
    static constexpr const char* noteNames[] =
    {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    for (int n = 0; n < 12; ++n)
    {
        const juce::String noteName(noteNames[n]);
        if (!upper.startsWith(noteName))
            continue;

        const auto octavePart = upper.substring(noteName.length()).trim();
        if (!octavePart.containsOnly("-0123456789"))
            continue;

        const auto octave = octavePart.getIntValue();
        const auto midi = (octave + 2) * 12 + n;
        return juce::jlimit(0, 127, midi);
    }

    return std::nullopt;
}

std::optional<int> findEmbeddedRootMidiNote(const juce::StringPairArray& metadata)
{
    static const juce::StringArray candidateKeys
    {
        "MidiUnityNote",
        "RootNote",
        "ACID Root Note",
        "AcidRootNote",
        "acidrootnote"
    };

    for (const auto& key : candidateKeys)
    {
        const auto maybeValue = getMetadataValueCaseInsensitive(metadata, key);
        if (!maybeValue.has_value())
            continue;

        const auto parsed = parseMidiNoteFromMetadataString(*maybeValue);
        if (parsed.has_value())
            return parsed;
    }

    return std::nullopt;
}

juce::String detectLoopFormatBadge(const juce::File& file, const juce::StringPairArray& metadata)
{
    const auto hasRootNote = findEmbeddedRootMidiNote(metadata).has_value();
    const auto loopStart = metadata.getValue("Loop0Start", "-1").getIntValue();
    const auto loopEnd = metadata.getValue("Loop0End", "-1").getIntValue();
    const auto hasLoop = loopStart >= 0 && loopEnd > loopStart;

    if (!(hasRootNote && hasLoop))
        return {};

    const auto ext = file.getFileExtension().toLowerCase();
    if (ext == ".wav")
        return "Acidized";

    if (ext == ".aif" || ext == ".aiff")
        return "Apple Loop";

    return {};
}
}

struct EngineCore::SampleSegments
{
    juce::AudioBuffer<float> preloadData;
    juce::AudioBuffer<float> streamData;
};

void EngineCore::prepare(const double sampleRate, const int maxSamplesPerBlock, const int outputChannels) noexcept
{
    sampleRate_ = sampleRate;
    maxSamplesPerBlock_ = maxSamplesPerBlock;
    outputChannels_ = outputChannels;

    voicePool_.prepare(maxSamplesPerBlock_);
    pendingEventCount_ = 0;
    globalFilterLfoPhase_ = 0.0f;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate_;
    spec.maximumBlockSize = static_cast<juce::uint32>(juce::jmax(1, maxSamplesPerBlock_));
    spec.numChannels = 1;

    for (auto& voice : voices_)
    {
        voice.filterA.prepare(spec);
        voice.filterB.prepare(spec);
        voice.filterA.reset();
        voice.filterB.reset();
    }

    applyEnvelopeParamsToVoices();
    applyFilterParamsToVoices();
    reverb_.setSampleRate(sampleRate_);
    updateReverbParameters();

    if (getTotalSampleLength() == 0)
        generateFallbackSample();
}

void EngineCore::release() noexcept
{
    stopAllVoicesImmediate();
    voicePool_.reset();
    pendingEventCount_ = 0;
}

void EngineCore::setReverbMix(const float mix) noexcept
{
    reverbMix_ = juce::jlimit(0.0f, 1.0f, mix);
    updateReverbParameters();
}

int EngineCore::getLoadedPreloadSamples() const noexcept
{
    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? segments->preloadData.getNumSamples() : 0;
}

int EngineCore::getLoadedStreamSamples() const noexcept
{
    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? segments->streamData.getNumSamples() : 0;
}

bool EngineCore::loadSampleFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return false;

    const auto lengthInSamples = static_cast<int>(reader->lengthInSamples);
    if (lengthInSamples <= 0)
        return false;

    juce::AudioBuffer<float> loaded(static_cast<int>(reader->numChannels), lengthInSamples);
    if (!reader->read(&loaded, 0, lengthInSamples, 0, true, true))
        return false;

    juce::AudioBuffer<float> mono(1, lengthInSamples);

    for (int sampleIndex = 0; sampleIndex < lengthInSamples; ++sampleIndex)
    {
        float sum = 0.0f;

        for (int channel = 0; channel < static_cast<int>(reader->numChannels); ++channel)
            sum += loaded.getSample(channel, sampleIndex);

        mono.setSample(0, sampleIndex, sum / static_cast<float>(reader->numChannels));
    }

    // Read embedded root note from WAV smpl chunk or AIFF INST chunk
    int embeddedRootNote = rootMidiNote_;
    const auto& metadata = reader->metadataValues;
    const auto loadedLoopFormatBadge = detectLoopFormatBadge(file, metadata);
    if (const auto parsedRootNote = findEmbeddedRootMidiNote(metadata); parsedRootNote.has_value())
        embeddedRootNote = *parsedRootNote;

    setSampleData(mono, reader->sampleRate, embeddedRootNote);
    displaySampleData_ = loaded;
    samplePath_ = file.getFullPathName();
    loadedSampleLoopFormatBadge_ = {};

    // Read embedded loop points from WAV smpl chunk or AIFF MARK/INST chunks
    const auto embeddedLoopStart = metadata.getValue("Loop0Start", "-1").getIntValue();
    const auto embeddedLoopEnd = metadata.getValue("Loop0End", "-1").getIntValue();

    if (loadedLoopFormatBadge.isNotEmpty() && embeddedLoopStart >= 0 && embeddedLoopEnd > embeddedLoopStart)
    {
        setLoopPoints(embeddedLoopStart, embeddedLoopEnd);
        setPlaybackMode(PlaybackMode::loop);
        loadedSampleLoopFormatBadge_ = loadedLoopFormatBadge;
    }
    else
    {
        // No embedded loop metadata: reset loop region to full file
        const auto fullEnd = juce::jmax(0, getTotalSampleLength() - 1);
        setLoopPoints(0, fullEnd);
    }

    return true;
}

void EngineCore::setSampleData(const juce::AudioBuffer<float>& sampleData, const double sampleRate, const int rootNote) noexcept
{
    displaySampleData_ = sampleData;
    loadedSampleLoopFormatBadge_ = {};

    auto monoSample = sampleData;
    sampleDataRate_ = sampleRate > 0.0 ? sampleRate : sampleRate_;
    setRootMidiNote(rootNote);

    if (monoSample.getNumChannels() > 1)
    {
        juce::AudioBuffer<float> mono(1, monoSample.getNumSamples());

        for (int sampleIndex = 0; sampleIndex < monoSample.getNumSamples(); ++sampleIndex)
        {
            float sum = 0.0f;

            for (int channel = 0; channel < monoSample.getNumChannels(); ++channel)
                sum += monoSample.getSample(channel, sampleIndex);

            mono.setSample(0, sampleIndex, sum / static_cast<float>(monoSample.getNumChannels()));
        }

        monoSample = mono;
    }

    rebuildSampleSegments(monoSample);

    if (getTotalSampleLength() <= 0)
        generateFallbackSample();

    setSampleWindow(0, getTotalSampleLength() - 1);
    setFadeSamples(0, 0);
    reversePlayback_ = false;
    setLoopPoints(0, getTotalSampleLength() - 1);
}

void EngineCore::setPreloadSamples(const int preloadSamples) noexcept
{
    preloadSamples_ = juce::jmax(256, preloadSamples);

    const auto segments = getSampleSegmentsSnapshot();
    if (segments == nullptr)
        return;

    const auto totalSamples = getTotalSampleLength(*segments);
    if (totalSamples <= 0)
        return;

    juce::AudioBuffer<float> mono(1, totalSamples);
    for (int i = 0; i < totalSamples; ++i)
        mono.setSample(0, i, readSampleAt(*segments, i));

    rebuildSampleSegments(mono);
    setSampleWindow(sampleWindowStart_, sampleWindowEnd_);
    setFadeSamples(fadeInSamples_, fadeOutSamples_);
    setLoopPoints(loopStartSample_, loopEndSample_);
}

int EngineCore::getLoadedSampleLength() const noexcept
{
    return getTotalSampleLength();
}

int EngineCore::getLoadedSampleChannels() const noexcept
{
    return juce::jmax(1, displaySampleData_.getNumChannels());
}

void EngineCore::setSampleWindow(const int startSample, const int endSample) noexcept
{
    const auto totalSamples = getTotalSampleLength();
    const auto maxValid = juce::jmax(0, totalSamples - 1);

    sampleWindowStart_ = juce::jlimit(0, maxValid, startSample);
    sampleWindowEnd_ = juce::jlimit(0, maxValid, endSample);

    if (sampleWindowEnd_ <= sampleWindowStart_)
        sampleWindowEnd_ = maxValid;
}

void EngineCore::setFadeSamples(const int fadeInSamples, const int fadeOutSamples) noexcept
{
    const auto maxFade = juce::jmax(0, getEffectivePlaybackLength() - 1);
    fadeInSamples_ = juce::jlimit(0, maxFade, fadeInSamples);
    fadeOutSamples_ = juce::jlimit(0, maxFade, fadeOutSamples);
}

void EngineCore::noteOn(const int noteNumber, const float velocity, const int sampleOffsetInBlock) noexcept
{
    if (pendingEventCount_ >= static_cast<int>(pendingEvents_.size()))
        return;

    auto& event = pendingEvents_[static_cast<std::size_t>(pendingEventCount_++)];
    event.type = EventType::noteOn;
    event.noteNumber = juce::jlimit(0, 127, noteNumber);
    event.velocity = juce::jlimit(0.0f, 1.0f, velocity);
    event.offset = juce::jmax(0, sampleOffsetInBlock);
}

void EngineCore::noteOff(const int noteNumber, const int sampleOffsetInBlock) noexcept
{
    if (pendingEventCount_ >= static_cast<int>(pendingEvents_.size()))
        return;

    auto& event = pendingEvents_[static_cast<std::size_t>(pendingEventCount_++)];
    event.type = EventType::noteOff;
    event.noteNumber = juce::jlimit(0, 127, noteNumber);
    event.velocity = 0.0f;
    event.offset = juce::jmax(0, sampleOffsetInBlock);
}

void EngineCore::render(float** outputs, const int numChannels, const int numSamples) noexcept
{
    if (outputs == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    const auto segments = getSampleSegmentsSnapshot();
    if (segments == nullptr)
        return;

    for (int channel = 0; channel < numChannels; ++channel)
        juce::FloatVectorOperations::clear(outputs[channel], numSamples);

    std::stable_sort(pendingEvents_.begin(), pendingEvents_.begin() + pendingEventCount_,
        [](const PendingEvent& left, const PendingEvent& right)
        {
            return left.offset < right.offset;
        });

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        flushPendingEventsAtOffset(sampleIndex);

        float globalLfoValue = 0.0f;
        const bool hasActiveLfo = filterSettings_.lfoRateHz > 0.0f && std::abs(filterSettings_.lfoAmountHz) > 0.0001f;
        if (hasActiveLfo)
        {
            globalLfoValue = computeLfoWave(filterSettings_.lfoShape, globalFilterLfoPhase_);
            globalFilterLfoPhase_ += filterSettings_.lfoRateHz / static_cast<float>(sampleRate_);
            if (globalFilterLfoPhase_ >= 1.0f)
                globalFilterLfoPhase_ -= std::floor(globalFilterLfoPhase_);
        }

        float mixed = 0.0f;

        for (int voiceIndex = 0; voiceIndex < static_cast<int>(VoicePool::maxVoices); ++voiceIndex)
        {
            if (!voicePool_.isActive(voiceIndex))
                continue;

            auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
            const auto sampleLength = getEffectivePlaybackLength();

            if (sampleLength <= 1)
            {
                voicePool_.stopVoiceAtIndex(voiceIndex);
                voice.noteHeld = false;
                voice.ampEnvelope.reset();
                voice.filterEnvelope.reset();
                continue;
            }

            const auto toPlaybackIndex = [this](const int absoluteSample)
            {
                if (reversePlayback_)
                    return sampleWindowEnd_ - absoluteSample;

                return absoluteSample - sampleWindowStart_;
            };

            const auto rawLoopStart = toPlaybackIndex(loopStartSample_);
            const auto rawLoopEnd = toPlaybackIndex(loopEndSample_);

            auto effectiveLoopStart = juce::jlimit(0, sampleLength - 1, juce::jmin(rawLoopStart, rawLoopEnd));
            auto effectiveLoopEnd = juce::jlimit(0, sampleLength - 1, juce::jmax(rawLoopStart, rawLoopEnd));
            if (effectiveLoopEnd <= effectiveLoopStart)
            {
                effectiveLoopStart = 0;
                effectiveLoopEnd = sampleLength - 1;
            }

            const auto loopEnabled = playbackMode_ == PlaybackMode::loop;
            const auto shouldLoopNow = loopEnabled && voice.noteHeld;
            const auto loopLength = juce::jmax(1, effectiveLoopEnd - effectiveLoopStart + 1);

            if (voice.samplePosition >= static_cast<float>(sampleLength - 1))
            {
                if (shouldLoopNow)
                {
                    voice.samplePosition = static_cast<float>(effectiveLoopStart);
                }
                else
                {
                    voicePool_.stopVoiceAtIndex(voiceIndex);
                    voice.noteHeld = false;
                    voice.ampEnvelope.reset();
                    voice.filterEnvelope.reset();
                    continue;
                }
            }

            if (shouldLoopNow && voice.samplePosition >= static_cast<float>(effectiveLoopEnd))
            {
                while (voice.samplePosition >= static_cast<float>(effectiveLoopEnd))
                    voice.samplePosition -= static_cast<float>(loopLength);

                while (voice.samplePosition < static_cast<float>(effectiveLoopStart))
                    voice.samplePosition += static_cast<float>(loopLength);
            }

            const float mappedVelocity = mapVelocity(voice.velocity);
            const float ampLevel = voice.ampEnvelope.getNextSample() * mappedVelocity;

            if (!voice.ampEnvelope.isActive())
            {
                voicePool_.stopVoiceAtIndex(voiceIndex);
                voice.noteHeld = false;
                voice.filterEnvelope.reset();
                continue;
            }

            float lfoValue = 0.0f;
            if (hasActiveLfo)
            {
                if (filterSettings_.lfoRetrigger)
                {
                    lfoValue = computeLfoWave(filterSettings_.lfoShape, voice.filterLfoPhase);
                    auto lfoRateKeyTrackRatio = 1.0f;
                    if (!filterSettings_.lfoTempoSync || filterSettings_.lfoRateKeytrackInTempoSync)
                    {
                        const auto noteSemitoneOffset = static_cast<float>(voicePool_.noteAt(voiceIndex) - rootMidiNote_);
                        if (filterSettings_.lfoKeytrackLinear)
                        {
                            lfoRateKeyTrackRatio = juce::jmax(0.0f,
                                1.0f + (noteSemitoneOffset / 12.0f) * filterSettings_.lfoRateKeyTracking);
                        }
                        else
                        {
                            lfoRateKeyTrackRatio = std::pow(2.0f,
                                (noteSemitoneOffset / 12.0f) * filterSettings_.lfoRateKeyTracking);
                        }
                    }
                    const auto trackedLfoRateHz = juce::jlimit(0.0f, 40.0f,
                        filterSettings_.lfoRateHz * lfoRateKeyTrackRatio);
                    voice.filterLfoPhase += trackedLfoRateHz / static_cast<float>(sampleRate_);
                    if (voice.filterLfoPhase >= 1.0f)
                        voice.filterLfoPhase -= std::floor(voice.filterLfoPhase);
                }
                else
                {
                    lfoValue = globalLfoValue;
                }

                if (filterSettings_.lfoUnipolar)
                    lfoValue = 0.5f * (lfoValue + 1.0f);

                if (voice.filterLfoFadeSamplesTotal > 0 && voice.filterLfoFadeSamplesRemaining > 0)
                {
                    const auto remaining = static_cast<float>(voice.filterLfoFadeSamplesRemaining);
                    const auto total = static_cast<float>(voice.filterLfoFadeSamplesTotal);
                    const auto depthScale = juce::jlimit(0.0f, 1.0f, 1.0f - (remaining / total));
                    lfoValue *= depthScale;
                    --voice.filterLfoFadeSamplesRemaining;
                }
            }

            float rawSample = readSampleLinear(*segments, voice.samplePosition);
            if (shouldLoopNow && loopCrossfadeSamples_ > 0)
            {
                const auto crossfadeSamples = juce::jlimit(0, juce::jmax(0, loopLength / 2), loopCrossfadeSamples_);
                if (crossfadeSamples > 0)
                {
                    const auto crossfadeStart = static_cast<float>(effectiveLoopEnd - crossfadeSamples);
                    if (voice.samplePosition >= crossfadeStart)
                    {
                        const auto progress = juce::jlimit(0.0f, 1.0f,
                            (voice.samplePosition - crossfadeStart) / static_cast<float>(crossfadeSamples));

                        const auto headPosition = static_cast<float>(effectiveLoopStart)
                            + progress * static_cast<float>(crossfadeSamples);

                        const auto headSample = readSampleLinear(*segments, headPosition);
                        rawSample = rawSample + (headSample - rawSample) * progress;
                    }
                }
            }
            const float filterEnvValue = voice.filterEnvelope.getNextSample();
            const float filteredSample = computeFilterSample(
                rawSample,
                filterEnvValue,
                lfoValue,
                voicePool_.noteAt(voiceIndex),
                voice.velocity,
                voice);
            mixed += filteredSample * ampLevel;

            voice.lastAmpLevel = ampLevel;
            voicePool_.setCurrentLevel(voiceIndex, ampLevel);

            if (voice.glideSamplesRemaining > 0)
            {
                const auto glideStep = (voice.glideTargetIncrement - voice.sampleIncrement)
                    / static_cast<float>(voice.glideSamplesRemaining);
                voice.sampleIncrement += glideStep;
                --voice.glideSamplesRemaining;
            }

            voice.samplePosition += voice.sampleIncrement;
        }

        for (int channel = 0; channel < numChannels; ++channel)
            outputs[channel][sampleIndex] += mixed;
    }

    if (reverbMix_ > 0.0001f)
    {
        if (numChannels >= 2)
            reverb_.processStereo(outputs[0], outputs[1], numSamples);
        else if (numChannels == 1)
            reverb_.processMono(outputs[0], numSamples);
    }

    pendingEventCount_ = 0;
}

void EngineCore::render(juce::AudioBuffer<float>& audioBuffer, const juce::MidiBuffer& midiBuffer) noexcept
{
    const auto numSamples = audioBuffer.getNumSamples();
    const auto numChannels = audioBuffer.getNumChannels();

    audioBuffer.clear();

    for (const auto metadata : midiBuffer)
    {
        const auto message = metadata.getMessage();
        const auto offset = juce::jlimit(0, juce::jmax(0, numSamples - 1), metadata.samplePosition);

        if (message.isNoteOn())
            noteOn(message.getNoteNumber(), message.getFloatVelocity(), offset);
        else if (message.isNoteOff())
            noteOff(message.getNoteNumber(), offset);
    }

    std::array<float*, 32> outputPointers{};
    const auto clampedChannels = juce::jmin(numChannels, static_cast<int>(outputPointers.size()));

    for (int channel = 0; channel < clampedChannels; ++channel)
        outputPointers[static_cast<std::size_t>(channel)] = audioBuffer.getWritePointer(channel);

    render(outputPointers.data(), clampedChannels, numSamples);
}

void EngineCore::setAmpEnvelope(const AdsrSettings& settings) noexcept
{
    ampEnvelopeSettings_ = settings;
    applyEnvelopeParamsToVoices();
}

void EngineCore::setFilterEnvelope(const AdsrSettings& settings) noexcept
{
    filterEnvelopeSettings_ = settings;
    applyEnvelopeParamsToVoices();
}

void EngineCore::setFilterSettings(const FilterSettings& settings) noexcept
{
    filterSettings_.baseCutoffHz = juce::jlimit(20.0f, 20000.0f, settings.baseCutoffHz);
    filterSettings_.envAmountHz = juce::jmax(0.0f, settings.envAmountHz);
    filterSettings_.resonance = juce::jlimit(0.0f, 1.0f, settings.resonance);
    filterSettings_.mode = settings.mode;
    filterSettings_.keyTracking = juce::jlimit(-1.0f, 2.0f, settings.keyTracking);
    filterSettings_.velocityAmountHz = juce::jmax(0.0f, settings.velocityAmountHz);
    filterSettings_.lfoRateHz = juce::jlimit(0.0f, 40.0f, settings.lfoRateHz);
    filterSettings_.lfoRateKeyTracking = juce::jlimit(-1.0f, 2.0f, settings.lfoRateKeyTracking);
    filterSettings_.lfoAmountHz = juce::jlimit(-20000.0f, 20000.0f, settings.lfoAmountHz);
    filterSettings_.lfoAmountKeyTracking = juce::jlimit(-1.0f, 2.0f, settings.lfoAmountKeyTracking);
    filterSettings_.lfoStartPhaseDegrees = juce::jlimit(0.0f, 360.0f, settings.lfoStartPhaseDegrees);
    filterSettings_.lfoStartPhaseRandomDegrees = juce::jlimit(0.0f, 180.0f, settings.lfoStartPhaseRandomDegrees);
    filterSettings_.lfoFadeInMs = juce::jlimit(0.0f, 5000.0f, settings.lfoFadeInMs);
    filterSettings_.lfoKeytrackLinear = settings.lfoKeytrackLinear;
    filterSettings_.lfoUnipolar = settings.lfoUnipolar;
    filterSettings_.lfoShape = settings.lfoShape;
    filterSettings_.lfoRetrigger = settings.lfoRetrigger;
    filterSettings_.lfoTempoSync = settings.lfoTempoSync;
    filterSettings_.lfoRateKeytrackInTempoSync = settings.lfoRateKeytrackInTempoSync;
    filterSettings_.lfoSyncDivision = juce::jlimit(0, 11, settings.lfoSyncDivision);
    applyFilterParamsToVoices();
}

void EngineCore::setRootMidiNote(const int rootMidiNote) noexcept
{
    rootMidiNote_ = juce::jlimit(0, 127, rootMidiNote);
}

void EngineCore::setLoopPoints(const int loopStart, const int loopEnd) noexcept
{
    const auto sampleLength = getTotalSampleLength();
    const auto maxValid = juce::jmax(0, sampleLength - 1);

    loopStartSample_ = juce::jlimit(0, maxValid, loopStart);
    loopEndSample_ = juce::jlimit(0, maxValid, loopEnd);

    if (loopEndSample_ <= loopStartSample_)
        loopEndSample_ = maxValid;

    setLoopCrossfadeSamples(loopCrossfadeSamples_);
}

void EngineCore::setLoopCrossfadeSamples(const int crossfadeSamples) noexcept
{
    const auto loopLength = juce::jmax(0, loopEndSample_ - loopStartSample_ + 1);
    const auto maxCrossfade = juce::jmax(0, loopLength / 2);
    loopCrossfadeSamples_ = juce::jlimit(0, maxCrossfade, crossfadeSamples);
}

int EngineCore::activeVoiceCount() const noexcept
{
    return voicePool_.activeVoiceCount();
}

int EngineCore::stealCount() const noexcept
{
    return voicePool_.stealCount();
}

void EngineCore::resetStealCount() noexcept
{
    voicePool_.resetStealCount();
}

bool EngineCore::isNoteActive(const int noteNumber) const noexcept
{
    return voicePool_.isNoteActive(noteNumber);
}

void EngineCore::generateFallbackSample() noexcept
{
    constexpr int sampleLength = 4096;
    juce::AudioBuffer<float> fallback(1, sampleLength);

    for (int sampleIndex = 0; sampleIndex < sampleLength; ++sampleIndex)
    {
        const auto phase = 2.0f * kPi * static_cast<float>(sampleIndex) * 220.0f / static_cast<float>(sampleRate_);
        fallback.setSample(0, sampleIndex, 0.2f * std::sin(phase));
    }

    setSampleData(fallback, sampleRate_, rootMidiNote_);
    samplePath_.clear();
}

void EngineCore::flushPendingEventsAtOffset(const int offset) noexcept
{
    for (int eventIndex = 0; eventIndex < pendingEventCount_; ++eventIndex)
    {
        const auto& event = pendingEvents_[static_cast<std::size_t>(eventIndex)];

        if (event.offset != offset)
            continue;

        if (event.type == EventType::noteOn)
        {
            if (chokeGroup_ > 0)
                stopAllVoicesImmediate();

            if (monoMode_)
            {
                const auto activeIndex = voicePool_.firstActiveVoiceIndex();

                if (activeIndex >= 0 && legatoMode_)
                {
                    retargetVoiceLegato(activeIndex, event.noteNumber, event.velocity);
                    continue;
                }

                stopAllVoicesImmediate();
            }

            const auto voiceIndex = voicePool_.startVoiceForNote(event.noteNumber);
            startVoice(voiceIndex, event.noteNumber, event.velocity);
        }
        else
        {
            releaseVoicesForNote(event.noteNumber);
        }
    }
}

void EngineCore::startVoice(const int voiceIndex, const int noteNumber, const float velocity) noexcept
{
    if (voiceIndex < 0 || voiceIndex >= static_cast<int>(VoicePool::maxVoices))
        return;

    auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
    const auto targetIncrement = computeSampleIncrementForNote(noteNumber);

    voice.samplePosition = 0.0f;
    voice.sampleIncrement = targetIncrement;
    voice.glideTargetIncrement = targetIncrement;
    voice.glideSamplesRemaining = 0;
    voice.velocity = velocity;
    voice.noteHeld = true;
    voice.lastAmpLevel = 0.0f;
    voice.filterA.reset();
    voice.filterB.reset();
    if (filterSettings_.lfoRetrigger)
    {
        auto startPhase = filterSettings_.lfoStartPhaseDegrees;
        if (filterSettings_.lfoStartPhaseRandomDegrees > 0.0f)
        {
            const auto startOrder = voicePool_.startOrderAt(voiceIndex);
            const auto bipolar = deterministicBipolarFromNoteAndOrder(noteNumber, startOrder);
            startPhase += bipolar * filterSettings_.lfoStartPhaseRandomDegrees;
        }

        auto phaseNorm = startPhase / 360.0f;
        phaseNorm -= std::floor(phaseNorm);
        voice.filterLfoPhase = phaseNorm;
    }
    else
    {
        voice.filterLfoPhase = globalFilterLfoPhase_;
    }
    voice.filterLfoFadeSamplesTotal = static_cast<int>(std::round(
        (filterSettings_.lfoFadeInMs / 1000.0f) * static_cast<float>(sampleRate_)));
    voice.filterLfoFadeSamplesRemaining = voice.filterLfoFadeSamplesTotal;

    voice.ampEnvelope.reset();
    voice.ampEnvelope.noteOn();

    voice.filterEnvelope.reset();
    voice.filterEnvelope.noteOn();
}

void EngineCore::retargetVoiceLegato(const int voiceIndex, const int noteNumber, const float velocity) noexcept
{
    if (voiceIndex < 0 || voiceIndex >= static_cast<int>(VoicePool::maxVoices))
        return;

    auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
    const auto targetIncrement = computeSampleIncrementForNote(noteNumber);

    voice.velocity = velocity;
    voice.noteHeld = true;
    voicePool_.setNoteAtIndex(voiceIndex, noteNumber);

    const auto glideSamples = static_cast<int>(std::round(glideSeconds_ * static_cast<float>(sampleRate_)));
    if (glideSamples > 0)
    {
        voice.glideTargetIncrement = targetIncrement;
        voice.glideSamplesRemaining = glideSamples;
    }
    else
    {
        voice.sampleIncrement = targetIncrement;
        voice.glideTargetIncrement = targetIncrement;
        voice.glideSamplesRemaining = 0;
    }
}

void EngineCore::stopAllVoicesImmediate() noexcept
{
    voicePool_.stopAllVoices();

    for (auto& voice : voices_)
    {
        voice.noteHeld = false;
        voice.lastAmpLevel = 0.0f;
        voice.filterA.reset();
        voice.filterB.reset();
        voice.filterLfoPhase = 0.0f;
        voice.filterLfoFadeSamplesTotal = 0;
        voice.filterLfoFadeSamplesRemaining = 0;
        voice.glideSamplesRemaining = 0;
        voice.ampEnvelope.reset();
        voice.filterEnvelope.reset();
    }
}

void EngineCore::releaseVoicesForNote(const int noteNumber) noexcept
{
    std::array<int, VoicePool::maxVoices> indices{};
    const auto count = voicePool_.findActiveVoicesForNote(noteNumber, indices.data(), static_cast<int>(indices.size()));

    if (count <= 0)
        return;

    auto releaseVoiceByIndex = [this](const int voiceIndex)
    {
        auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];

        if (playbackMode_ == PlaybackMode::oneShot)
            return;

        voice.noteHeld = false;

        voice.ampEnvelope.noteOff();
        voice.filterEnvelope.noteOff();
    };

    if (monoMode_)
    {
        for (int index = 0; index < count; ++index)
            releaseVoiceByIndex(indices[static_cast<std::size_t>(index)]);
        return;
    }

    int selectedVoice = indices[0];
    std::uint64_t oldestStartOrder = voicePool_.startOrderAt(selectedVoice);

    for (int index = 1; index < count; ++index)
    {
        const auto candidateVoice = indices[static_cast<std::size_t>(index)];
        const auto candidateStartOrder = voicePool_.startOrderAt(candidateVoice);

        if (candidateStartOrder < oldestStartOrder)
        {
            oldestStartOrder = candidateStartOrder;
            selectedVoice = candidateVoice;
        }
    }

    releaseVoiceByIndex(selectedVoice);
}

void EngineCore::applyEnvelopeParamsToVoices() noexcept
{
    juce::ADSR::Parameters ampParameters;
    ampParameters.attack = juce::jmax(0.0001f, ampEnvelopeSettings_.attackSeconds);
    ampParameters.decay = juce::jmax(0.0001f, ampEnvelopeSettings_.decaySeconds);
    ampParameters.sustain = juce::jlimit(0.0f, 1.0f, ampEnvelopeSettings_.sustainLevel);
    ampParameters.release = juce::jmax(0.0001f, ampEnvelopeSettings_.releaseSeconds);

    juce::ADSR::Parameters filterParameters;
    filterParameters.attack = juce::jmax(0.0001f, filterEnvelopeSettings_.attackSeconds);
    filterParameters.decay = juce::jmax(0.0001f, filterEnvelopeSettings_.decaySeconds);
    filterParameters.sustain = juce::jlimit(0.0f, 1.0f, filterEnvelopeSettings_.sustainLevel);
    filterParameters.release = juce::jmax(0.0001f, filterEnvelopeSettings_.releaseSeconds);

    for (auto& voice : voices_)
    {
        voice.ampEnvelope.setSampleRate(sampleRate_);
        voice.ampEnvelope.setParameters(ampParameters);

        voice.filterEnvelope.setSampleRate(sampleRate_);
        voice.filterEnvelope.setParameters(filterParameters);
    }
}

void EngineCore::applyFilterParamsToVoices() noexcept
{
    auto juceType = juce::dsp::StateVariableTPTFilterType::lowpass;
    switch (filterSettings_.mode)
    {
        case FilterSettings::Mode::highPass12:
        case FilterSettings::Mode::highPass24:
            juceType = juce::dsp::StateVariableTPTFilterType::highpass;
            break;
        case FilterSettings::Mode::bandPass12:
            juceType = juce::dsp::StateVariableTPTFilterType::bandpass;
            break;
        case FilterSettings::Mode::notch12:
            juceType = juce::dsp::StateVariableTPTFilterType::bandpass;
            break;
        case FilterSettings::Mode::lowPass12:
        case FilterSettings::Mode::lowPass24:
        default:
            juceType = juce::dsp::StateVariableTPTFilterType::lowpass;
            break;
    }

    const auto defaultCutoff = juce::jlimit(20.0f, static_cast<float>(sampleRate_ * 0.45), filterSettings_.baseCutoffHz);
    const auto q = juce::jlimit(0.5f, 20.0f, 0.5f + filterSettings_.resonance * 19.5f);

    for (auto& voice : voices_)
    {
        voice.filterA.setType(juceType);
        voice.filterB.setType(juceType);
        voice.filterA.setCutoffFrequency(defaultCutoff);
        voice.filterB.setCutoffFrequency(defaultCutoff);
        voice.filterA.setResonance(q);
        voice.filterB.setResonance(q);
    }
}

float EngineCore::computeSampleIncrementForNote(const int noteNumber) const noexcept
{
    const auto semitoneOffset = static_cast<float>(noteNumber - rootMidiNote_);
    const auto pitchRatio = std::pow(2.0f, semitoneOffset / 12.0f);
    const auto sourceToOutputRatio = sampleDataRate_ > 0.0 ? sampleDataRate_ / sampleRate_ : 1.0;
    return static_cast<float>(pitchRatio * sourceToOutputRatio);
}

float EngineCore::readSampleLinear(const SampleSegments& segments, const float position) const noexcept
{
    const auto sampleLength = getEffectivePlaybackLength(segments);

    if (sampleLength <= 1)
        return 0.0f;

    const auto clampedPosition = juce::jlimit(0.0f, static_cast<float>(sampleLength - 1), position);
    const auto sampleIndex = static_cast<int>(clampedPosition);
    const auto mappedIndex = mapPlaybackIndexToSampleIndex(segments, sampleIndex);
    const auto editGain = computeEditGain(clampedPosition, sampleLength);

    if (qualityTier_ == QualityTier::cpu)
        return readSampleAt(segments, mappedIndex) * editGain;

    if (qualityTier_ == QualityTier::ultra)
        return readSampleCubic(segments, clampedPosition) * editGain;

    const auto nextIndex = juce::jmin(sampleIndex + 1, sampleLength - 1);
    const auto fraction = clampedPosition - static_cast<float>(sampleIndex);

    const auto sampleA = readSampleAt(segments, mappedIndex);
    const auto sampleB = readSampleAt(segments, mapPlaybackIndexToSampleIndex(segments, nextIndex));

    return (sampleA + (sampleB - sampleA) * fraction) * editGain;
}

float EngineCore::readSampleCubic(const SampleSegments& segments, const float position) const noexcept
{
    const auto sampleLength = getEffectivePlaybackLength(segments);
    if (sampleLength <= 1)
        return 0.0f;

    const auto clampedPosition = juce::jlimit(0.0f, static_cast<float>(sampleLength - 1), position);
    const auto i1 = static_cast<int>(std::floor(clampedPosition));
    const auto i0 = juce::jmax(0, i1 - 1);
    const auto i2 = juce::jmin(sampleLength - 1, i1 + 1);
    const auto i3 = juce::jmin(sampleLength - 1, i1 + 2);
    const auto t = clampedPosition - static_cast<float>(i1);

    const auto y0 = readSampleAt(segments, mapPlaybackIndexToSampleIndex(segments, i0));
    const auto y1 = readSampleAt(segments, mapPlaybackIndexToSampleIndex(segments, i1));
    const auto y2 = readSampleAt(segments, mapPlaybackIndexToSampleIndex(segments, i2));
    const auto y3 = readSampleAt(segments, mapPlaybackIndexToSampleIndex(segments, i3));

    const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;
    const auto a3 = y1;

    return ((a0 * t + a1) * t + a2) * t + a3;
}

float EngineCore::readSampleLinear(const float position) const noexcept
{
    const auto segments = getSampleSegmentsSnapshot();
    if (segments == nullptr)
        return 0.0f;

    return readSampleLinear(*segments, position);
}

void EngineCore::rebuildSampleSegments(const juce::AudioBuffer<float>& monoSampleData) noexcept
{
    ++segmentRebuildCount_;

    sampleSegments_.store(buildSampleSegments(monoSampleData, preloadSamples_), std::memory_order_release);
}

std::shared_ptr<const EngineCore::SampleSegments> EngineCore::getSampleSegmentsSnapshot() const noexcept
{
    return sampleSegments_.load(std::memory_order_acquire);
}

std::shared_ptr<const EngineCore::SampleSegments> EngineCore::buildSampleSegments(
    const juce::AudioBuffer<float>& monoSampleData,
    const int preloadSamples) noexcept
{
    auto segments = std::make_shared<SampleSegments>();

    const auto totalSamples = monoSampleData.getNumSamples();
    const auto clampedPreload = juce::jlimit(0, totalSamples, preloadSamples);
    const auto streamSamples = juce::jmax(0, totalSamples - clampedPreload);

    segments->preloadData.setSize(1, clampedPreload, false, true, true);
    segments->streamData.setSize(1, streamSamples, false, true, true);

    if (clampedPreload > 0)
        segments->preloadData.copyFrom(0, 0, monoSampleData, 0, 0, clampedPreload);

    if (streamSamples > 0)
        segments->streamData.copyFrom(0, 0, monoSampleData, 0, clampedPreload, streamSamples);

    return segments;
}

int EngineCore::getTotalSampleLength(const SampleSegments& segments) const noexcept
{
    return segments.preloadData.getNumSamples() + segments.streamData.getNumSamples();
}

int EngineCore::getTotalSampleLength() const noexcept
{
    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? getTotalSampleLength(*segments) : 0;
}

int EngineCore::getEffectivePlaybackLength(const SampleSegments& segments) const noexcept
{
    const auto totalLength = getTotalSampleLength(segments);
    if (totalLength <= 0)
        return 0;

    const auto clampedStart = juce::jlimit(0, totalLength - 1, sampleWindowStart_);
    const auto clampedEnd = juce::jlimit(clampedStart, totalLength - 1, sampleWindowEnd_);
    return clampedEnd - clampedStart + 1;
}

int EngineCore::getEffectivePlaybackLength() const noexcept
{
    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? getEffectivePlaybackLength(*segments) : 0;
}

int EngineCore::mapPlaybackIndexToSampleIndex(const SampleSegments& segments, const int playbackIndex) const noexcept
{
    const auto totalLength = getTotalSampleLength(segments);
    if (totalLength <= 0)
        return 0;

    const auto clampedStart = juce::jlimit(0, totalLength - 1, sampleWindowStart_);
    const auto clampedEnd = juce::jlimit(clampedStart, totalLength - 1, sampleWindowEnd_);
    const auto clampedPlayback = juce::jlimit(0, clampedEnd - clampedStart, playbackIndex);

    if (reversePlayback_)
        return clampedEnd - clampedPlayback;

    return clampedStart + clampedPlayback;
}

float EngineCore::computeEditGain(const float playbackPosition, const int playbackLength) const noexcept
{
    if (playbackLength <= 1)
        return 0.0f;

    auto gain = 1.0f;

    if (fadeInSamples_ > 0 && playbackPosition < static_cast<float>(fadeInSamples_))
        gain = juce::jmin(gain, playbackPosition / static_cast<float>(fadeInSamples_));

    if (fadeOutSamples_ > 0)
    {
        const auto fadeOutStart = static_cast<float>(juce::jmax(0, playbackLength - fadeOutSamples_ - 1));
        if (playbackPosition >= fadeOutStart)
        {
            const auto remaining = static_cast<float>(playbackLength - 1) - playbackPosition;
            gain = juce::jmin(gain, remaining / static_cast<float>(fadeOutSamples_));
        }
    }

    return juce::jlimit(0.0f, 1.0f, gain);
}

float EngineCore::readSampleAt(const SampleSegments& segments, const int index) const noexcept
{
    if (index < 0)
        return 0.0f;

    const auto preloadSamples = segments.preloadData.getNumSamples();

    if (index < preloadSamples)
        return segments.preloadData.getSample(0, index);

    const auto streamIndex = index - preloadSamples;
    if (streamIndex >= 0 && streamIndex < segments.streamData.getNumSamples())
        return segments.streamData.getSample(0, streamIndex);

    return 0.0f;
}

float EngineCore::readSampleAt(const int index) const noexcept
{
    const auto segments = getSampleSegmentsSnapshot();
    if (segments == nullptr)
        return 0.0f;

    return readSampleAt(*segments, index);
}

std::vector<float> EngineCore::buildDisplayPeaks(const int maxPeaks) const noexcept
{
    const auto byChannel = buildDisplayPeaksByChannel(maxPeaks);
    if (byChannel.empty())
        return {};

    return byChannel.front();
}

std::vector<std::vector<float>> EngineCore::buildDisplayPeaksByChannel(const int maxPeaks) const noexcept
{
    const auto channels = juce::jmax(1, displaySampleData_.getNumChannels());
    const auto total = juce::jmax(0, displaySampleData_.getNumSamples());
    if (total <= 0 || maxPeaks <= 0)
        return {};

    const auto peakCount = juce::jmax(1, juce::jmin(maxPeaks, total));
    std::vector<std::vector<float>> allPeaks(static_cast<std::size_t>(channels),
        std::vector<float>(static_cast<std::size_t>(peakCount), 0.0f));

    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* samples = displaySampleData_.getReadPointer(channel);
        for (int i = 0; i < peakCount; ++i)
        {
            // Use int64_t to avoid overflow when i * total exceeds INT_MAX
            const auto start = static_cast<int>((static_cast<int64_t>(i) * total) / peakCount);
            const auto endExclusive = juce::jmax(start + 1,
                static_cast<int>((static_cast<int64_t>(i + 1) * total) / peakCount));

            float maxAbs = 0.0f;
            for (int s = start; s < endExclusive; ++s)
                maxAbs = juce::jmax(maxAbs, std::abs(samples[s]));

            allPeaks[static_cast<std::size_t>(channel)][static_cast<std::size_t>(i)] = juce::jlimit(0.0f, 1.0f, maxAbs);
        }
    }

    return allPeaks;
}

float EngineCore::computeFilterSample(const float inputSample,
                                      const float envValue,
                                      const float lfoValue,
                                      const int noteNumber,
                                      const float velocity,
                                      VoiceState& voice) const noexcept
{
    const auto semitoneOffset = static_cast<float>(noteNumber - rootMidiNote_);
    const auto lfoAmountKeyTrackRatio = filterSettings_.lfoKeytrackLinear
        ? juce::jmax(0.0f, 1.0f + (semitoneOffset / 12.0f) * filterSettings_.lfoAmountKeyTracking)
        : std::pow(2.0f, (semitoneOffset / 12.0f) * filterSettings_.lfoAmountKeyTracking);

    auto cutoff = filterSettings_.baseCutoffHz
        + envValue * filterSettings_.envAmountHz
        + velocity * filterSettings_.velocityAmountHz
        + lfoValue * (filterSettings_.lfoAmountHz * lfoAmountKeyTrackRatio);

    const auto keyTrackRatio = std::pow(2.0f, (semitoneOffset / 12.0f) * filterSettings_.keyTracking);
    cutoff *= keyTrackRatio;
    cutoff = juce::jlimit(20.0f, static_cast<float>(sampleRate_ * 0.45), cutoff);

    const auto q = juce::jlimit(0.5f, 20.0f, 0.5f + filterSettings_.resonance * 19.5f);
    voice.filterA.setCutoffFrequency(cutoff);
    voice.filterA.setResonance(q);

    auto out = voice.filterA.processSample(0, inputSample);

    if (filterSettings_.mode == FilterSettings::Mode::notch12)
        out = inputSample - out;

    if (filterSettings_.mode == FilterSettings::Mode::lowPass24
        || filterSettings_.mode == FilterSettings::Mode::highPass24)
    {
        voice.filterB.setCutoffFrequency(cutoff);
        voice.filterB.setResonance(q);
        out = voice.filterB.processSample(0, out);
    }

    return out;
}

float EngineCore::mapVelocity(const float velocity) const noexcept
{
    const auto clamped = juce::jlimit(0.0f, 1.0f, velocity);
    switch (velocityCurve_)
    {
        case VelocityCurve::soft:
            return std::sqrt(clamped);
        case VelocityCurve::hard:
            return clamped * clamped;
        case VelocityCurve::linear:
        default:
            return clamped;
    }
}

void EngineCore::updateReverbParameters() noexcept
{
    juce::Reverb::Parameters params;
    params.roomSize = 0.55f;
    params.damping = 0.40f;
    params.wetLevel = reverbMix_;
    params.dryLevel = 1.0f - (reverbMix_ * 0.65f);
    params.width = 1.0f;
    params.freezeMode = 0.0f;
    reverb_.setParameters(params);
}
}
