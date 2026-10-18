#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace audiocity::engine::audio_file
{
struct ReaderOpenResult
{
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::File readableFile;
    juce::String errorMessage;
};

struct EmbeddedSamplerMetadata
{
    int rootMidiNote = -1;
    float defaultTuneCents = 0.0f;
    int defaultLoopStart = -1;
    int defaultLoopEndExclusive = -1;
    bool metadataChunkPresent = false;
    juce::String warningMessage;
    juce::String errorMessage;

    [[nodiscard]] bool hasForwardLoop() const noexcept
    {
        return defaultLoopStart >= 0 && defaultLoopEndExclusive > defaultLoopStart;
    }
};

void registerAudioFormats(juce::AudioFormatManager& formatManager);

[[nodiscard]] bool isNcwConverterAvailable();

[[nodiscard]] ReaderOpenResult openReaderForFile(juce::AudioFormatManager& formatManager,
                                                 const juce::File& sourceFile);
[[nodiscard]] EmbeddedSamplerMetadata readWaveSamplerMetadata(const juce::File& sourceFile,
                                                               juce::int64 decodedFrameCount = -1);
[[nodiscard]] EmbeddedSamplerMetadata readAiffSamplerMetadata(const juce::File& sourceFile,
                                                               juce::int64 decodedFrameCount = -1);
[[nodiscard]] EmbeddedSamplerMetadata readEmbeddedSamplerMetadata(const juce::File& sourceFile,
                                                                  juce::int64 decodedFrameCount = -1);
} // namespace audiocity::engine::audio_file
