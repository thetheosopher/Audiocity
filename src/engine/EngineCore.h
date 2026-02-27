#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "VoicePool.h"
#include "ZoneMap.h"

namespace audiocity::engine
{
class EngineCore
{
public:
    void prepare(double sampleRate, int maxSamplesPerBlock, int outputChannels) noexcept;
    void release() noexcept;

    void render(juce::AudioBuffer<float>& audioBuffer, const juce::MidiBuffer& midiBuffer) noexcept;

    [[nodiscard]] int activeVoiceCount() const noexcept;

private:
    VoicePool voicePool_;
    ZoneMap zoneMap_;

    double sampleRate_ = 44100.0;
    int maxSamplesPerBlock_ = 0;
    int outputChannels_ = 2;
};
}
