#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <vector>

namespace audiocity::engine::rex
{
struct DecodedSlice
{
    juce::AudioBuffer<float> audio;
    int startSample = 0;
};

struct DecodedLoop
{
    juce::AudioBuffer<float> audio;
    double sampleRateHz = 44100.0;
    std::vector<DecodedSlice> slices;
};

[[nodiscard]] bool isRuntimeAvailable() noexcept;
[[nodiscard]] bool decodeFile(const juce::File& file, DecodedLoop& out) noexcept;
}
