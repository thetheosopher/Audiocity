#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace audiocity::engine::rex
{
struct DecodedLoop
{
    juce::AudioBuffer<float> audio;
    double sampleRateHz = 44100.0;
};

[[nodiscard]] bool isRuntimeAvailable() noexcept;
[[nodiscard]] bool decodeFile(const juce::File& file, DecodedLoop& out) noexcept;
}
