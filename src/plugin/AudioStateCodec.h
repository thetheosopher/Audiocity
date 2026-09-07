#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cstddef>
#include <vector>

namespace audiocity::plugin
{
struct AudioStateCodecLimits
{
    static constexpr std::size_t maximumDecodedBytes = 64u * 1024u * 1024u;
    static constexpr std::size_t maximumEncodedBytes = 64u * 1024u * 1024u;
    static constexpr int maximumChannels = 8;
};

/** Encodes planar IEEE float samples into a versioned, gzip-compressed and CRC32-protected
    binary asset. An empty block means the input was invalid or exceeded a hard limit. */
[[nodiscard]] juce::MemoryBlock encodeAudioStateAsset(const juce::AudioBuffer<float>& buffer,
                                                       juce::String* errorMessage = nullptr);

[[nodiscard]] juce::MemoryBlock encodeMonoAudioStateAsset(const std::vector<float>& samples,
                                                           juce::String* errorMessage = nullptr);

/** Decodes a bounded asset. The destination is changed only after the complete header,
    compressed payload, decoded length, checksum, and finite-sample checks have passed. */
[[nodiscard]] bool decodeAudioStateAsset(const juce::MemoryBlock& encoded,
                                         juce::AudioBuffer<float>& destination,
                                         juce::String* errorMessage = nullptr);

[[nodiscard]] bool decodeMonoAudioStateAsset(const juce::MemoryBlock& encoded,
                                             std::vector<float>& destination,
                                             juce::String* errorMessage = nullptr);
} // namespace audiocity::plugin
