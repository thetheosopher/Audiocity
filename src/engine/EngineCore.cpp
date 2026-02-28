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

    setSampleData(mono, reader->sampleRate, rootMidiNote_);
    samplePath_ = file.getFullPathName();
    return true;
}

void EngineCore::setSampleData(const juce::AudioBuffer<float>& sampleData, const double sampleRate, const int rootNote) noexcept
{
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

    setLoopPoints(0, getTotalSampleLength() - 1);
}

void EngineCore::setPreloadSamples(const int preloadSamples) noexcept
{
    preloadSamples_ = juce::jmax(256, preloadSamples);

    const auto totalSamples = getTotalSampleLength();
    if (totalSamples <= 0)
        return;

    juce::AudioBuffer<float> mono(1, totalSamples);
    for (int i = 0; i < totalSamples; ++i)
        mono.setSample(0, i, readSampleAt(i));

    rebuildSampleSegments(mono);
    setLoopPoints(loopStartSample_, loopEndSample_);
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
            const auto sampleLength = getTotalSampleLength();

            if (sampleLength <= 1)
            {
                voicePool_.stopVoiceAtIndex(voiceIndex);
                voice.noteHeld = false;
                voice.ampEnvelope.reset();
                voice.filterEnvelope.reset();
                continue;
            }

            const auto effectiveLoopStart = juce::jlimit(0, sampleLength - 1, loopStartSample_);
            const auto effectiveLoopEnd = juce::jlimit(0, sampleLength - 1,
                loopEndSample_ > effectiveLoopStart ? loopEndSample_ : sampleLength - 1);

            const auto loopEnabled = sfzLoopMode_ != SfzLoopMode::noLoop && playbackMode_ == PlaybackMode::loop;
            const auto shouldLoopNow = loopEnabled
                && (sfzLoopMode_ == SfzLoopMode::loopContinuous || voice.noteHeld);

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

            const float rawSample = readSampleLinear(voice.samplePosition);
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

        if (sfzLoopMode_ == SfzLoopMode::loopContinuous)
            continue;

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

float EngineCore::readSampleLinear(const float position) const noexcept
{
    const auto sampleLength = getTotalSampleLength();

    if (sampleLength <= 1)
        return 0.0f;

    const auto clampedPosition = juce::jlimit(0.0f, static_cast<float>(sampleLength - 1), position);
    const auto sampleIndex = static_cast<int>(clampedPosition);
    const auto nextIndex = juce::jmin(sampleIndex + 1, sampleLength - 1);
    const auto fraction = clampedPosition - static_cast<float>(sampleIndex);

    const auto sampleA = readSampleAt(sampleIndex);
    const auto sampleB = readSampleAt(nextIndex);

    return sampleA + (sampleB - sampleA) * fraction;
}

void EngineCore::rebuildSampleSegments(const juce::AudioBuffer<float>& monoSampleData) noexcept
{
    ++segmentRebuildCount_;

    const auto totalSamples = monoSampleData.getNumSamples();
    const auto clampedPreload = juce::jlimit(0, totalSamples, preloadSamples_);
    const auto streamSamples = juce::jmax(0, totalSamples - clampedPreload);

    preloadData_.setSize(1, clampedPreload, false, true, true);
    streamData_.setSize(1, streamSamples, false, true, true);

    if (clampedPreload > 0)
        preloadData_.copyFrom(0, 0, monoSampleData, 0, 0, clampedPreload);

    if (streamSamples > 0)
        streamData_.copyFrom(0, 0, monoSampleData, 0, clampedPreload, streamSamples);
}

int EngineCore::getTotalSampleLength() const noexcept
{
    return preloadData_.getNumSamples() + streamData_.getNumSamples();
}

float EngineCore::readSampleAt(const int index) const noexcept
{
    const auto preloadSamples = preloadData_.getNumSamples();

    if (index < preloadSamples)
        return preloadData_.getSample(0, index);

    const auto streamIndex = index - preloadSamples;
    if (streamIndex >= 0 && streamIndex < streamData_.getNumSamples())
        return streamData_.getSample(0, streamIndex);

    return 0.0f;
}

float EngineCore::computeLpf(const float inputSample, const float envValue, float& state) const noexcept
{
    const auto cutoff = juce::jlimit(20.0f, static_cast<float>(sampleRate_ * 0.45),
        filterSettings_.baseCutoffHz + envValue * filterSettings_.envAmountHz);

    const auto alpha = std::exp(-2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
    const auto output = (1.0f - alpha) * inputSample + alpha * state;
    state = output;
    return output;
}
}
