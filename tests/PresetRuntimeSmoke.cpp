#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "plugin/PluginProcessor.h"
#include "plugin/PresetJson.h"

namespace
{
constexpr auto kPatchRoot = "AudiocityPatch";
constexpr auto kEmbeddedSampleData = "embeddedSampleData";
constexpr auto kEmbeddedSampleChannels = "embeddedSampleChannels";
constexpr auto kSamplePath = "samplePath";
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

bool writeStereoToneWav(const juce::File& wavFile, const int sampleRate, const int sampleLength)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> out(wavFile.createOutputStream());
    if (out == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(out.get(), sampleRate, 2, 16, {}, 0));
    if (writer == nullptr)
        return false;

    out.release();

    juce::AudioBuffer<float> buffer(2, sampleLength);
    for (int i = 0; i < sampleLength; ++i)
    {
        const auto leftPhase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 220.0 / sampleRate);
        const auto rightPhase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 330.0 / sampleRate);
        buffer.setSample(0, i, 0.25f * std::sin(leftPhase));
        buffer.setSample(1, i, 0.15f * std::cos(rightPhase));
    }

    return writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength);
}

bool peaksMatch(const std::vector<std::vector<float>>& expected,
                const std::vector<std::vector<float>>& actual,
                const float tolerance)
{
    if (expected.size() != actual.size())
        return false;

    for (std::size_t channel = 0; channel < expected.size(); ++channel)
    {
        if (expected[channel].size() != actual[channel].size())
            return false;

        for (std::size_t index = 0; index < expected[channel].size(); ++index)
        {
            if (std::abs(expected[channel][index] - actual[channel][index]) > tolerance)
                return false;
        }
    }

    return true;
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

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_wave_preset_runtime", "");
    if (!tempDirectory.createDirectory())
    {
        std::fprintf(stderr, "Failed to create temp directory for WAV preset runtime smoke.\n");
        return 11;
    }

    const auto sampleFile = tempDirectory.getChildFile("StereoRoundTrip.wav");
    if (!writeStereoToneWav(sampleFile, 48000, 512))
    {
        std::fprintf(stderr, "Failed to write stereo WAV fixture for preset runtime smoke.\n");
        tempDirectory.deleteRecursively();
        return 12;
    }

    auto sourceProcessor = std::make_unique<AudiocityAudioProcessor>();
    sourceProcessor->prepareToPlay(48000.0, 256);
    if (!sourceProcessor->loadSampleFromFile(sampleFile))
    {
        std::fprintf(stderr, "Failed to load stereo WAV fixture into source processor.\n");
        tempDirectory.deleteRecursively();
        return 13;
    }

    sourceProcessor->setRootMidiNote(67);
    sourceProcessor->setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::loop);
    sourceProcessor->setSampleWindow(24, 300);
    sourceProcessor->setLoopPoints(48, 220);
    sourceProcessor->setLoopCrossfadeSamples(17);
    sourceProcessor->setReversePlayback(true);

    const auto sourcePeaks = sourceProcessor->getLoadedSamplePeaksByChannel(64);
    if (sourceProcessor->getLoadedSampleChannels() != 2 || sourcePeaks.size() != 2)
    {
        std::fprintf(stderr, "Expected source WAV fixture to preserve two display channels before preset save.\n");
        tempDirectory.deleteRecursively();
        return 14;
    }

    const auto wavePresetXml = sourceProcessor->createPlaybackPresetXml();
    if (wavePresetXml.isEmpty())
    {
        std::fprintf(stderr, "Failed to create preset XML from loaded WAV sample.\n");
        tempDirectory.deleteRecursively();
        return 15;
    }

    juce::ValueTree wavePresetState;
    errorMessage.clear();
    if (!audiocity::plugin::decodePresetXml(wavePresetXml, wavePresetState, errorMessage))
    {
        std::fprintf(stderr, "Failed to decode WAV preset XML: %s\n", errorMessage.toRawUTF8());
        tempDirectory.deleteRecursively();
        return 16;
    }

    const auto* waveEmbeddedData = wavePresetState.getProperty(kEmbeddedSampleData).getBinaryData();
    if (!wavePresetState.hasType(kPatchRoot)
        || waveEmbeddedData == nullptr
        || wavePresetState.getProperty(kSamplePath).toString().isNotEmpty()
        || static_cast<int>(wavePresetState.getProperty(kEmbeddedSampleChannels, 0)) != 2
        || readEmbeddedFrameCount(wavePresetState) != 512)
    {
        std::fprintf(stderr, "Loaded WAV preset did not embed the current sample as a two-channel payload.\n");
        tempDirectory.deleteRecursively();
        return 17;
    }

    auto restoredProcessor = std::make_unique<AudiocityAudioProcessor>();
    restoredProcessor->prepareToPlay(48000.0, 256);

    std::vector<float> staleWaveform(128, 0.0f);
    for (std::size_t index = 0; index < staleWaveform.size(); ++index)
        staleWaveform[index] = (index % 7 == 0) ? 0.5f : -0.25f;
    restoredProcessor->loadGeneratedWaveformAsSample(staleWaveform, 48);
    restoredProcessor->setSampleWindow(2, 10);

    errorMessage.clear();
    if (!restoredProcessor->loadPlaybackPresetXml(wavePresetXml, errorMessage))
    {
        std::fprintf(stderr, "Runtime WAV preset load failed: %s\n", errorMessage.toRawUTF8());
        tempDirectory.deleteRecursively();
        return 18;
    }

    const auto restoredPeaks = restoredProcessor->getLoadedSamplePeaksByChannel(64);
    if (!restoredProcessor->isEmbeddedSampleLoaded()
        || restoredProcessor->getLoadedSampleChannels() != 2
        || restoredProcessor->getLoadedSampleLength() != 512
        || restoredProcessor->getRootMidiNote() != 67
        || restoredProcessor->getPlaybackMode() != AudiocityAudioProcessor::PlaybackMode::loop
        || restoredProcessor->getSampleWindowStart() != 24
        || restoredProcessor->getSampleWindowEnd() != 300
        || restoredProcessor->getLoopStart() != 48
        || restoredProcessor->getLoopEnd() != 220
        || restoredProcessor->getLoopCrossfadeSamples() != 17
        || !restoredProcessor->getReversePlayback()
        || !peaksMatch(sourcePeaks, restoredPeaks, 1.0e-6f))
    {
        std::fprintf(stderr, "Loaded WAV preset did not restore the same embedded sample and playback state.\n");
        tempDirectory.deleteRecursively();
        return 19;
    }

    auto invalidPreparedProcessor = std::make_unique<AudiocityAudioProcessor>();
    invalidPreparedProcessor->prepareToPlay(48000.0, 256);
    audiocity::engine::Program invalidProgram;
    std::vector<juce::AudioBuffer<float>> invalidSampleData;
    if (invalidPreparedProcessor->publishPreparedImportedProgram(
            tempDirectory.getChildFile("InvalidPrepared.dspreset"),
            audiocity::plugin::ImportedProgramFormat::decentSampler,
            std::move(invalidProgram),
            std::move(invalidSampleData),
            "Should not publish",
            0)
        || invalidPreparedProcessor->hasImportedProgram()
        || !invalidPreparedProcessor->getLastImportDiagnosticSummary().contains("display sample index"))
    {
        std::fprintf(stderr, "Prepared import publish accepted invalid display sample data.\n");
        tempDirectory.deleteRecursively();
        return 20;
    }

    auto preparedProcessor = std::make_unique<AudiocityAudioProcessor>();
    preparedProcessor->prepareToPlay(48000.0, 256);

    audiocity::engine::Program preparedProgram;
    preparedProgram.name = "Worker Prepared Program";

    audiocity::engine::SampleAsset preparedAsset;
    preparedAsset.sourcePath = tempDirectory.getChildFile("WorkerPrepared.wav").getFullPathName().toStdString();
    preparedAsset.displayName = "WorkerPrepared.wav";
    preparedAsset.lengthSamples = 128;
    preparedAsset.numChannels = 2;
    preparedAsset.sampleRateHz = 48000.0;
    preparedAsset.rootMidiNote = 64;
    preparedProgram.sampleAssets.push_back(preparedAsset);

    audiocity::engine::Zone preparedZone;
    preparedZone.sampleAssetIndex = 0;
    preparedZone.rootMidiNote = preparedAsset.rootMidiNote;
    preparedZone.sampleEndExclusive = preparedAsset.lengthSamples;
    preparedProgram.zones.push_back(preparedZone);

    std::vector<juce::AudioBuffer<float>> preparedSampleData;
    preparedSampleData.emplace_back(preparedAsset.numChannels, preparedAsset.lengthSamples);
    for (int sample = 0; sample < preparedAsset.lengthSamples; ++sample)
    {
        const auto value = static_cast<float>(sample) / static_cast<float>(preparedAsset.lengthSamples);
        preparedSampleData[0].setSample(0, sample, value);
        preparedSampleData[0].setSample(1, sample, -value);
    }

    const auto preparedFile = tempDirectory.getChildFile("WorkerPrepared.dspreset");
    if (!preparedProcessor->publishPreparedImportedProgram(
            preparedFile,
            audiocity::plugin::ImportedProgramFormat::decentSampler,
            std::move(preparedProgram),
            std::move(preparedSampleData),
            "Prepared async import summary",
            0,
            7)
        || !preparedProcessor->hasImportedProgram()
        || preparedProcessor->getImportedProgramFormat() != audiocity::plugin::ImportedProgramFormat::decentSampler
        || preparedProcessor->getImportedProgramPath() != preparedFile.getFullPathName()
        || preparedProcessor->getImportedProgramName() != "Worker Prepared Program"
        || preparedProcessor->getImportedProgramZoneCount() != 1
        || preparedProcessor->getLoadedSampleChannels() != 2
        || preparedProcessor->getLoadedSampleLength() != 128
        || preparedProcessor->getRootMidiNote() != 64
        || preparedProcessor->getLoadedSamplePath().isNotEmpty()
        || !preparedProcessor->getLastImportDiagnosticSummary().contains("Prepared async import summary"))
    {
        std::fprintf(stderr, "Prepared import publish did not restore program metadata and display audio.\n");
        tempDirectory.deleteRecursively();
        return 21;
    }

    auto backgroundSampleProcessor = std::make_unique<AudiocityAudioProcessor>();
    backgroundSampleProcessor->prepareToPlay(48000.0, 256);

    auto preparedSampleImport = backgroundSampleProcessor->prepareBackgroundImport(
        sampleFile,
        audiocity::plugin::ImportedProgramFormat::unknown);
    if (!preparedSampleImport.ok
        || preparedSampleImport.importedProgram
        || !preparedSampleImport.displaySample.ok)
    {
        std::fprintf(stderr, "Background sample preparation did not produce a publishable sample payload.\n");
        tempDirectory.deleteRecursively();
        return 22;
    }

    if (!backgroundSampleProcessor->publishPreparedBackgroundImport(sampleFile, std::move(preparedSampleImport))
        || backgroundSampleProcessor->hasImportedProgram()
        || backgroundSampleProcessor->getLoadedSamplePath() != sampleFile.getFullPathName()
        || backgroundSampleProcessor->getLoadedSampleChannels() != 2
        || backgroundSampleProcessor->getLoadedSampleLength() != 512
        || !peaksMatch(sourcePeaks, backgroundSampleProcessor->getLoadedSamplePeaksByChannel(64), 1.0e-6f))
    {
        std::fprintf(stderr, "Background sample publish did not restore the expected loaded sample state.\n");
        tempDirectory.deleteRecursively();
        return 23;
    }

    tempDirectory.deleteRecursively();

    return 0;
}