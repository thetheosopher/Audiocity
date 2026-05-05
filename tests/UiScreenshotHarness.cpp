#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numbers>
#include <vector>

namespace
{
constexpr double kSnapshotSampleRate = 48000.0;
constexpr int kSnapshotBlockSize = 512;
constexpr int kWaveformSampleCount = 32768;

struct SnapshotScenario
{
    const char* fileStem;
    int tabIndex;
};

void logProgress(const juce::String& message)
{
    juce::ignoreUnused(message);
}

std::vector<float> makeSnapshotWaveform()
{
    std::vector<float> waveform(static_cast<std::size_t>(kWaveformSampleCount), 0.0f);

    for (int index = 0; index < kWaveformSampleCount; ++index)
    {
        const auto phase = static_cast<float>(index) / static_cast<float>(kWaveformSampleCount);
        const auto angle = 2.0f * std::numbers::pi_v<float> * phase;
        waveform[static_cast<std::size_t>(index)] = 0.68f * std::sin(angle)
            + 0.22f * std::sin(angle * 2.0f + 0.45f)
            + 0.10f * std::sin(angle * 5.0f - 0.30f);
    }

    return waveform;
}

void configureProcessorForSnapshots(AudiocityAudioProcessor& processor)
{
    processor.prepareToPlay(kSnapshotSampleRate, kSnapshotBlockSize);
    processor.loadGeneratedWaveformAsSample(makeSnapshotWaveform(), 60);
    processor.setSampleWindow(kWaveformSampleCount / 12, (kWaveformSampleCount * 10) / 12);
    processor.setLoopPoints(kWaveformSampleCount / 4, (kWaveformSampleCount * 3) / 4);
    processor.setWaveformViewRange(kWaveformSampleCount / 16, (kWaveformSampleCount * 7) / 8);
    processor.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::loop);

    AudiocityAudioProcessor::AdsrSettings ampEnvelope;
    ampEnvelope.attackSeconds = 0.014f;
    ampEnvelope.decaySeconds = 0.160f;
    ampEnvelope.sustainLevel = 0.62f;
    ampEnvelope.releaseSeconds = 0.420f;
    processor.setAmpEnvelope(ampEnvelope);

    AudiocityAudioProcessor::AdsrSettings filterEnvelope;
    filterEnvelope.attackSeconds = 0.006f;
    filterEnvelope.decaySeconds = 0.240f;
    filterEnvelope.sustainLevel = 0.46f;
    filterEnvelope.releaseSeconds = 0.300f;
    processor.setFilterEnvelope(filterEnvelope);

    auto filterSettings = processor.getFilterSettings();
    filterSettings.baseCutoffHz = 2200.0f;
    filterSettings.resonance = 0.38f;
    filterSettings.envAmountHz = 4200.0f;
    processor.setFilterSettings(filterSettings);
}

bool writeSnapshot(juce::Component& component, const juce::File& outputFile)
{
    if (auto parent = outputFile.getParentDirectory(); !parent.exists() && !parent.createDirectory())
        return false;

    const auto image = component.createComponentSnapshot(component.getLocalBounds());
    if (!image.isValid())
        return false;

    juce::FileOutputStream stream(outputFile);
    if (!stream.openedOk())
        return false;

    juce::PNGImageFormat png;
    return png.writeImageToStream(image, stream);
}

juce::Result renderScenario(AudiocityAudioProcessor& processor,
    const SnapshotScenario& scenario,
    const juce::File& outputDirectory)
{
    logProgress("Rendering " + juce::String(scenario.fileStem));
    processor.setEditorTabIndex(scenario.tabIndex);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr)
        return juce::Result::fail("Failed to create editor for snapshot rendering.");

    logProgress("Created editor for " + juce::String(scenario.fileStem));
    editor->setBounds(0, 0, editor->getWidth(), editor->getHeight());
    editor->resized();
    logProgress("Laid out editor for " + juce::String(scenario.fileStem));

    const auto outputFile = outputDirectory.getChildFile(juce::String(scenario.fileStem) + ".png");
    if (!writeSnapshot(*editor, outputFile))
        return juce::Result::fail("Failed to write snapshot: " + outputFile.getFullPathName());

    logProgress("Wrote " + outputFile.getFullPathName());
    std::fprintf(stdout, "%s\n", outputFile.getFullPathName().toStdString().c_str());
    return juce::Result::ok();
}
}

int main(int argc, char* argv[])
{
    if (argc > 1 && std::strcmp(argv[1], "--smoke-exit") == 0)
        return 0;

    juce::String outputDirectoryPath;

    for (int index = 1; index < argc; ++index)
    {
        const juce::String argument(argv[index]);
        if (argument == "--output-dir" && index + 1 < argc)
        {
            outputDirectoryPath = juce::String(argv[++index]);
            continue;
        }

        if (argument.startsWith("--output-dir="))
        {
            outputDirectoryPath = argument.fromFirstOccurrenceOf("=", false, false);
            continue;
        }
    }

    auto outputDirectory = outputDirectoryPath.isNotEmpty()
        ? juce::File(outputDirectoryPath)
        : juce::File::getCurrentWorkingDirectory().getChildFile("ui-snapshots");

    if (!outputDirectory.exists() && !outputDirectory.createDirectory())
    {
        std::fprintf(stderr, "Failed to create snapshot directory: %s\n", outputDirectory.getFullPathName().toStdString().c_str());
        return 1;
    }

    logProgress("Entering main");

    juce::ScopedJuceInitialiser_GUI guiInitialiser;
    logProgress("Initialised JUCE GUI");

    // Keep the processor off the main stack frame; it is large enough to overflow before smoke-exit branches.
    auto processor = std::make_unique<AudiocityAudioProcessor>();
    logProgress("Created processor");
    configureProcessorForSnapshots(*processor);
    logProgress("Configured processor state");

    constexpr SnapshotScenario scenarios[] = {
        { "sample", 0 },
        { "library", 1 },
        { "mapping", 2 },
        { "player", 3 },
        { "generate", 4 },
        { "capture", 5 },
        { "about", 6 }
    };

    for (const auto& scenario : scenarios)
    {
        if (const auto result = renderScenario(*processor, scenario, outputDirectory); result.failed())
        {
            std::fprintf(stderr, "%s\n", result.getErrorMessage().toStdString().c_str());
            return 1;
        }
    }

    processor->releaseResources();
    return 0;
}
