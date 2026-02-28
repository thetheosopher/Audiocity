#include "EngineCore.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace audiocity::engine
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
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
    applyEnvelopeParamsToVoices();

    if (getTotalSampleLength() == 0)
        generateFallbackSample();
}

void EngineCore::release() noexcept
{
    stopAllVoicesImmediate();
    voicePool_.reset();
    pendingEventCount_ = 0;
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
    if (metadata.containsKey("MidiUnityNote"))
        embeddedRootNote = juce::jlimit(0, 127, metadata.getValue("MidiUnityNote", juce::String(rootMidiNote_)).getIntValue());

    setSampleData(mono, reader->sampleRate, embeddedRootNote);
    displaySampleData_ = loaded;
    samplePath_ = file.getFullPathName();

    // Read embedded loop points from WAV smpl chunk or AIFF MARK/INST chunks
    const auto embeddedLoopStart = metadata.getValue("Loop0Start", "-1").getIntValue();
    const auto embeddedLoopEnd = metadata.getValue("Loop0End", "-1").getIntValue();

    if (embeddedLoopStart >= 0 && embeddedLoopEnd > embeddedLoopStart)
    {
        setLoopPoints(embeddedLoopStart, embeddedLoopEnd);
        setPlaybackMode(PlaybackMode::loop);
    }

    return true;
}

void EngineCore::setSampleData(const juce::AudioBuffer<float>& sampleData, const double sampleRate, const int rootNote) noexcept
{
    displaySampleData_ = sampleData;

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
                voice.samplePosition = static_cast<float>(effectiveLoopStart);

            const float ampLevel = voice.ampEnvelope.getNextSample() * voice.velocity;

            if (!voice.ampEnvelope.isActive())
            {
                voicePool_.stopVoiceAtIndex(voiceIndex);
                voice.noteHeld = false;
                voice.filterEnvelope.reset();
                continue;
            }

            const float rawSample = readSampleLinear(*segments, voice.samplePosition);
            const float filterEnvValue = voice.filterEnvelope.getNextSample();
            const float filteredSample = computeLpf(rawSample, filterEnvValue, voice.lpfState);
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
    filterSettings_ = settings;
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
    voice.lpfState = 0.0f;

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
        voice.lpfState = 0.0f;
        voice.glideSamplesRemaining = 0;
        voice.ampEnvelope.reset();
        voice.filterEnvelope.reset();
    }
}

void EngineCore::releaseVoicesForNote(const int noteNumber) noexcept
{
    std::array<int, VoicePool::maxVoices> indices{};
    const auto count = voicePool_.findActiveVoicesForNote(noteNumber, indices.data(), static_cast<int>(indices.size()));

    for (int index = 0; index < count; ++index)
    {
        auto& voice = voices_[static_cast<std::size_t>(indices[static_cast<std::size_t>(index)])];

        if (playbackMode_ == PlaybackMode::oneShot)
            continue;

        voice.noteHeld = false;

        voice.ampEnvelope.noteOff();
        voice.filterEnvelope.noteOff();
    }
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

    const auto nextIndex = juce::jmin(sampleIndex + 1, sampleLength - 1);
    const auto fraction = clampedPosition - static_cast<float>(sampleIndex);

    const auto sampleA = readSampleAt(segments, mappedIndex);
    const auto sampleB = readSampleAt(segments, mapPlaybackIndexToSampleIndex(segments, nextIndex));

    return (sampleA + (sampleB - sampleA) * fraction) * editGain;
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
            const auto start = (i * total) / peakCount;
            const auto endExclusive = juce::jmax(start + 1, ((i + 1) * total) / peakCount);

            float maxAbs = 0.0f;
            for (int s = start; s < endExclusive; ++s)
                maxAbs = juce::jmax(maxAbs, std::abs(samples[s]));

            allPeaks[static_cast<std::size_t>(channel)][static_cast<std::size_t>(i)] = juce::jlimit(0.0f, 1.0f, maxAbs);
        }
    }

    return allPeaks;
}

float EngineCore::computeLpf(const float inputSample, const float envValue, float& state) const noexcept
{
    const auto cutoff = juce::jlimit(20.0f, static_cast<float>(sampleRate_ * 0.45),
        filterSettings_.baseCutoffHz + envValue * filterSettings_.envAmountHz);

    // Resonant one-pole: feedback drives self-oscillation toward cutoff
    const auto fb = filterSettings_.resonance * 0.95f;
    const auto alpha = std::exp(-2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
    const auto filtered = (1.0f - alpha) * (inputSample + fb * (state - inputSample)) + alpha * state;
    state = filtered;
    return filtered;
}
}
