#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "plugin/PluginProcessor.h"
#include "plugin/PresetJson.h"

namespace
{
constexpr auto kPatchRoot = "AudiocityPatch";
constexpr auto kEmbeddedSampleData = "embeddedSampleData";
constexpr auto kEmbeddedSampleChannels = "embeddedSampleChannels";
constexpr auto kSampleWindowStart = "sampleWindowStart";
constexpr auto kSampleWindowEnd = "sampleWindowEnd";

int readEmbeddedFrameCount(const juce::ValueTree& state)
{
    const auto* embeddedData = state.getProperty(kEmbeddedSampleData).getBinaryData();
    if (embeddedData == nullptr)
        return 0;

    const auto channels = juce::jmax(1,
        static_cast<int>(state.getProperty(kEmbeddedSampleChannels, 1)));
    return static_cast<int>(embeddedData->getSize() / sizeof(float)) / channels;
}
}

int main()
{
    const auto presetFile = juce::File(AUDIOCITY_SOURCE_DIR)
        .getChildFile("assets")
        .getChildFile("factory_presets")
        .getChildFile("001 - Bass -  Acoustic Bass.acp");

    if (!presetFile.existsAsFile())
    {
        std::fprintf(stderr, "Factory preset missing: %s\n", presetFile.getFullPathName().toRawUTF8());
        return 1;
    }

    const auto presetXml = presetFile.loadFileAsString();
    if (presetXml.isEmpty())
    {
        std::fprintf(stderr, "Failed to read preset XML: %s\n", presetFile.getFullPathName().toRawUTF8());
        return 2;
    }

    juce::ValueTree presetState;
    juce::String errorMessage;
    if (!audiocity::plugin::decodePresetXml(presetXml, presetState, errorMessage))
    {
        std::fprintf(stderr, "Failed to decode preset XML: %s\n", errorMessage.toRawUTF8());
        return 3;
    }

    if (!presetState.hasType(kPatchRoot))
    {
        std::fprintf(stderr, "Preset did not decode to an Audiocity patch.\n");
        return 4;
    }

    if (presetState.hasProperty(kSampleWindowStart) || presetState.hasProperty(kSampleWindowEnd))
    {
        std::fprintf(stderr, "Factory preset unexpectedly serializes sample-window properties.\n");
        return 5;
    }

    const auto embeddedFrames = readEmbeddedFrameCount(presetState);
    if (embeddedFrames <= 1)
    {
        std::fprintf(stderr, "Factory preset embedded audio was not readable.\n");
        return 6;
    }

    auto processor = std::make_unique<AudiocityAudioProcessor>();
    processor->prepareToPlay(48000.0, 256);

    std::vector<float> waveform(4096, 0.0f);
    for (std::size_t i = 0; i < waveform.size(); ++i)
    {
        const auto phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi
            * static_cast<double>(i) * 220.0 / 48000.0);
        waveform[i] = 0.25f * std::sin(phase);
    }

    processor->loadGeneratedWaveformAsSample(waveform, 60);
    processor->setSampleWindow(32, 255);

    if (processor->getSampleWindowStart() != 32 || processor->getSampleWindowEnd() != 255)
    {
        std::fprintf(stderr, "Failed to seed a stale runtime sample window before preset load.\n");
        return 7;
    }

    if (!processor->loadPlaybackPresetXml(presetXml, errorMessage))
    {
        std::fprintf(stderr, "Runtime preset load failed: %s\n", errorMessage.toRawUTF8());
        return 8;
    }

    if (!processor->isEmbeddedSampleLoaded())
    {
        std::fprintf(stderr, "Expected runtime preset load to restore embedded audio.\n");
        return 9;
    }

    if (processor->getSampleWindowStart() != 0 || processor->getSampleWindowEnd() != embeddedFrames - 1)
    {
        std::fprintf(stderr,
            "Expected runtime sample window to reset to 0-%d, got %d-%d.\n",
            embeddedFrames - 1,
            processor->getSampleWindowStart(),
            processor->getSampleWindowEnd());
        return 10;
    }

    return 0;
}