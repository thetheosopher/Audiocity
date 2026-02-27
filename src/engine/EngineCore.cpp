#include "EngineCore.h"

namespace audiocity::engine
{
void EngineCore::prepare(const double sampleRate, const int maxSamplesPerBlock, const int outputChannels) noexcept
{
    sampleRate_ = sampleRate;
    maxSamplesPerBlock_ = maxSamplesPerBlock;
    outputChannels_ = outputChannels;

    voicePool_.prepare(maxSamplesPerBlock_);
    zoneMap_.clear();
}

void EngineCore::release() noexcept
{
    voicePool_.reset();
    zoneMap_.clear();
}

void EngineCore::render(juce::AudioBuffer<float>& audioBuffer, const juce::MidiBuffer& midiBuffer) noexcept
{
    juce::ignoreUnused(midiBuffer, sampleRate_, maxSamplesPerBlock_, outputChannels_);

    audioBuffer.clear();
}

int EngineCore::activeVoiceCount() const noexcept
{
    return voicePool_.activeVoiceCount();
}
}
