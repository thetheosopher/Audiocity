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

void registerAudioFormats(juce::AudioFormatManager& formatManager);

[[nodiscard]] ReaderOpenResult openReaderForFile(juce::AudioFormatManager& formatManager,
                                                 const juce::File& sourceFile);
} // namespace audiocity::engine::audio_file