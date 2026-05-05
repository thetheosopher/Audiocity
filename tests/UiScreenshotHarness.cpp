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
    unsigned int stateFlags = 0;
    int viewportScrollY = 0;
};

enum SnapshotStateFlags : unsigned int
{
    snapshotStateBaseline = 0u,
    snapshotStateSliceProgram = 1u << 0,
    snapshotStateModulation = 1u << 1
};

bool hasSnapshotState(const SnapshotScenario& scenario, const SnapshotStateFlags flag) noexcept
{
    return (scenario.stateFlags & static_cast<unsigned int>(flag)) != 0u;
}

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

std::vector<float> makeTransientSliceFixtureWaveform()
{
    constexpr int totalSamples = 12000;
    constexpr int hitSpacing = 4000;

    std::vector<float> waveform(static_cast<std::size_t>(totalSamples), 0.0f);
    for (int hit = 0; hit < 3; ++hit)
    {
        const auto hitStart = hit * hitSpacing;
        for (int offset = 0; offset < 480 && hitStart + offset < totalSamples; ++offset)
        {
            const auto decay = std::exp(-static_cast<float>(offset) / 70.0f);
            waveform[static_cast<std::size_t>(hitStart + offset)] = 0.95f * decay;
        }
    }

    return waveform;
}

bool writeMonoWaveformWav(const juce::File& file,
                         const std::vector<float>& waveform,
                         const double sampleRate)
{
    if (auto parent = file.getParentDirectory(); !parent.exists() && !parent.createDirectory())
        return false;

    if (file.existsAsFile() && !file.deleteFile())
        return false;

    juce::AudioBuffer<float> buffer(1, static_cast<int>(waveform.size()));
    auto* write = buffer.getWritePointer(0);
    for (int index = 0; index < buffer.getNumSamples(); ++index)
        write[index] = waveform[static_cast<std::size_t>(index)];

    auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream());
    if (stream == nullptr || !stream->openedOk())
        return false;

    juce::WavAudioFormat wav;
    auto* rawStream = stream.release();
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(rawStream,
                                                                        sampleRate,
                                                                        1,
                                                                        16,
                                                                        {},
                                                                        0));
    if (writer == nullptr)
    {
        delete rawStream;
        return false;
    }

    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

juce::Result prepareTransientSliceFixtureFile(juce::File& sliceFixtureFile)
{
    sliceFixtureFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("audiocity-ui-snapshot-transient.wav");

    if (!writeMonoWaveformWav(sliceFixtureFile,
                              makeTransientSliceFixtureWaveform(),
                              kSnapshotSampleRate))
    {
        return juce::Result::fail("Failed to write transient slice snapshot fixture.");
    }

    return juce::Result::ok();
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

void configureModulationSnapshotState(AudiocityAudioProcessor& processor)
{
    AudiocityAudioProcessor::ModulationRoutingSettings routing;
    routing.modWheel.toPitchCents = 240.0f;
    routing.modWheel.toFilterHz = 3600.0f;
    routing.modWheel.toAmp = 18.0f;

    routing.aftertouch.toPitchCents = -90.0f;
    routing.aftertouch.toFilterHz = 5200.0f;
    routing.aftertouch.toAmp = 12.0f;

    routing.velocity.toPitchCents = 0.0f;
    routing.velocity.toFilterHz = -2600.0f;
    routing.velocity.toAmp = 42.0f;

    routing.macros[0].toPitchCents = 120.0f;
    routing.macros[0].toFilterHz = 8400.0f;
    routing.macros[0].toAmp = 24.0f;

    routing.macros[1].toPitchCents = -340.0f;
    routing.macros[1].toFilterHz = -6800.0f;
    routing.macros[1].toAmp = -28.0f;

    processor.setModulationRoutingSettings(routing);
    processor.setMacroControlValues({ 0.72f, 0.38f });
}

juce::Result configureProcessorForScenario(AudiocityAudioProcessor& processor,
                                           const SnapshotScenario& scenario,
                                           const juce::File& transientSliceFixtureFile)
{
    configureProcessorForSnapshots(processor);

    if (hasSnapshotState(scenario, snapshotStateSliceProgram))
    {
        if (!processor.importTransientSliceProgram(transientSliceFixtureFile))
        {
            return juce::Result::fail("Failed to import transient slice snapshot fixture: "
                + processor.getLastImportDiagnosticSummary());
        }

        const auto sampleLength = processor.getLoadedSampleLength();
        processor.setSampleWindow(0, sampleLength);
        processor.setLoopPoints(sampleLength / 4, (sampleLength * 3) / 4);
        processor.setWaveformViewRange(0, sampleLength);
    }

    if (hasSnapshotState(scenario, snapshotStateModulation))
        configureModulationSnapshotState(processor);

    return juce::Result::ok();
}

bool setFirstVisibleVerticalViewportScroll(juce::Component& component, const int scrollY)
{
    if (auto* viewport = dynamic_cast<juce::Viewport*>(&component))
    {
        if (viewport->isVisible() && viewport->getVerticalScrollBar().isVisible())
        {
            viewport->setViewPosition(viewport->getViewPositionX(), scrollY);
            return true;
        }
    }

    for (auto* child : component.getChildren())
    {
        if (child != nullptr && setFirstVisibleVerticalViewportScroll(*child, scrollY))
            return true;
    }

    return false;
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
    const juce::File& transientSliceFixtureFile,
    const juce::File& outputDirectory)
{
    logProgress("Rendering " + juce::String(scenario.fileStem));

    if (const auto result = configureProcessorForScenario(processor, scenario, transientSliceFixtureFile);
        result.failed())
    {
        return result;
    }

    processor.setEditorTabIndex(scenario.tabIndex);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr)
        return juce::Result::fail("Failed to create editor for snapshot rendering.");

    logProgress("Created editor for " + juce::String(scenario.fileStem));
    editor->setBounds(0, 0, editor->getWidth(), editor->getHeight());
    editor->resized();

    if (scenario.viewportScrollY > 0
        && !setFirstVisibleVerticalViewportScroll(*editor, scenario.viewportScrollY))
    {
        return juce::Result::fail("Failed to scroll viewport for snapshot: " + juce::String(scenario.fileStem));
    }

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

    juce::File transientSliceFixtureFile;
    if (const auto result = prepareTransientSliceFixtureFile(transientSliceFixtureFile); result.failed())
    {
        std::fprintf(stderr, "%s\n", result.getErrorMessage().toStdString().c_str());
        return 1;
    }

    constexpr SnapshotScenario scenarios[] = {
        { "sample", 0, snapshotStateSliceProgram, 0 },
        { "sample_modulation", 0, snapshotStateModulation, 320 },
        { "library", 1, snapshotStateBaseline, 0 },
        { "mapping", 2, snapshotStateSliceProgram, 0 },
        { "player", 3, snapshotStateBaseline, 0 },
        { "generate", 4, snapshotStateBaseline, 0 },
        { "capture", 5, snapshotStateBaseline, 0 },
        { "about", 6, snapshotStateBaseline, 0 }
    };

    for (const auto& scenario : scenarios)
    {
        if (const auto result = renderScenario(*processor,
                                               scenario,
                                               transientSliceFixtureFile,
                                               outputDirectory);
            result.failed())
        {
            transientSliceFixtureFile.deleteFile();
            std::fprintf(stderr, "%s\n", result.getErrorMessage().toStdString().c_str());
            return 1;
        }
    }

    transientSliceFixtureFile.deleteFile();
    processor->releaseResources();
    return 0;
}
