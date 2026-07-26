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

    // The imported-program edit protocol used to be retyped in every mutation method. These
    // checks exercise it through the processor: derived state must follow every accepted edit,
    // and a rejected edit must leave the program exactly as it was.
    auto editProcessor = std::make_unique<AudiocityAudioProcessor>();
    editProcessor->prepareToPlay(48000.0, 256);

    // Every mutation must refuse to do anything at all before a program is loaded.
    if (editProcessor->updateImportedProgramZoneMapping({})
        || editProcessor->duplicateImportedProgramZone(0) >= 0
        || editProcessor->splitImportedProgramZone(0) >= 0
        || editProcessor->deleteImportedProgramZone(0)
        || editProcessor->mapImportedProgramZonesToRootNotes({ 0 })
        || editProcessor->spreadImportedProgramZonesAcrossKeyRange({ 0 })
        || editProcessor->deriveImportedProgramZoneRootsFromKeyRanges({ 0 })
        || editProcessor->remapImportedProgramZonesChromatically({ 0 })
        || editProcessor->applyImportedProgramMappingState({})
        || editProcessor->createImportedProgramMappingState().isValid()
        || !editProcessor->getImportedProgramZoneRows().empty()
        || editProcessor->getImportedProgramZoneCount() != 0)
    {
        std::fprintf(stderr, "Imported-program edits were accepted with no program loaded.\n");
        tempDirectory.deleteRecursively();
        return 24;
    }

    audiocity::engine::Program editProgram;
    editProgram.name = "Edit Protocol Program";

    audiocity::engine::SampleAsset editAsset;
    editAsset.sourcePath = tempDirectory.getChildFile("EditProtocol.wav").getFullPathName().toStdString();
    editAsset.displayName = "EditProtocol.wav";
    editAsset.lengthSamples = 256;
    editAsset.numChannels = 2;
    editAsset.sampleRateHz = 48000.0;
    editAsset.rootMidiNote = 60;
    editProgram.sampleAssets.push_back(editAsset);

    for (int index = 0; index < 2; ++index)
    {
        audiocity::engine::Zone zone;
        zone.sampleAssetIndex = 0;
        zone.rootMidiNote = 60 + index;
        zone.keyRange = audiocity::engine::MidiRange::single(60 + index);
        zone.sampleEndExclusive = editAsset.lengthSamples;
        editProgram.zones.push_back(zone);
    }

    std::vector<juce::AudioBuffer<float>> editSampleData;
    editSampleData.emplace_back(editAsset.numChannels, editAsset.lengthSamples);
    editSampleData[0].clear();

    if (!editProcessor->publishPreparedImportedProgram(
            tempDirectory.getChildFile("EditProtocol.dspreset"),
            audiocity::plugin::ImportedProgramFormat::decentSampler,
            std::move(editProgram),
            std::move(editSampleData),
            "Edit protocol import",
            0)
        || editProcessor->getImportedProgramZoneCount() != 2)
    {
        std::fprintf(stderr, "Failed to publish the edit-protocol test program.\n");
        tempDirectory.deleteRecursively();
        return 24;
    }

    // An accepted edit refreshes the zone rows and the diagnostic summary without being asked.
    audiocity::plugin::ProgramZoneEdit keyRangeEdit;
    keyRangeEdit.zoneIndex = 0;
    keyRangeEdit.keyLow = 24;
    keyRangeEdit.keyHigh = 36;
    keyRangeEdit.rootMidiNote = 30;

    if (!editProcessor->updateImportedProgramZoneMapping(keyRangeEdit))
    {
        std::fprintf(stderr, "Imported-program zone mapping edit was rejected.\n");
        tempDirectory.deleteRecursively();
        return 25;
    }

    const auto editedRows = editProcessor->getImportedProgramZoneRows();
    if (editedRows.size() != 2
        || editedRows[0].keyLow != 24
        || editedRows[0].keyHigh != 36
        || editedRows[0].rootMidiNote != 30
        || editProcessor->getImportedProgramMapSummary().isEmpty()
        || !editProcessor->getLastImportDiagnosticSummary().contains("Mapping updated"))
    {
        std::fprintf(stderr, "Zone mapping edit did not refresh derived state.\n");
        tempDirectory.deleteRecursively();
        return 25;
    }

    // A rejected edit must not disturb the program, the derived state, or the summary.
    const auto rowsBeforeRejectedEdit = editProcessor->getImportedProgramZoneRows();
    const auto summaryBeforeRejectedEdit = editProcessor->getLastImportDiagnosticSummary();

    audiocity::plugin::ProgramZoneEdit outOfRangeEdit;
    outOfRangeEdit.zoneIndex = 99;

    if (editProcessor->updateImportedProgramZoneMapping(outOfRangeEdit)
        || editProcessor->duplicateImportedProgramZone(99) >= 0
        || editProcessor->splitImportedProgramZone(99) >= 0
        || editProcessor->deleteImportedProgramZones({ 0, 99 })
        || editProcessor->mapImportedProgramZonesToRootNotes({ 99 })
        || editProcessor->remapImportedProgramZonesChromatically({ 99 }))
    {
        std::fprintf(stderr, "Out-of-range imported-program edits were accepted.\n");
        tempDirectory.deleteRecursively();
        return 26;
    }

    const auto rowsAfterRejectedEdit = editProcessor->getImportedProgramZoneRows();
    if (editProcessor->getImportedProgramZoneCount() != 2
        || rowsAfterRejectedEdit.size() != rowsBeforeRejectedEdit.size()
        || rowsAfterRejectedEdit[0].keyLow != rowsBeforeRejectedEdit[0].keyLow
        || rowsAfterRejectedEdit[0].keyHigh != rowsBeforeRejectedEdit[0].keyHigh
        || rowsAfterRejectedEdit[0].rootMidiNote != rowsBeforeRejectedEdit[0].rootMidiNote
        || editProcessor->getLastImportDiagnosticSummary() != summaryBeforeRejectedEdit)
    {
        std::fprintf(stderr, "A rejected imported-program edit changed the stored program.\n");
        tempDirectory.deleteRecursively();
        return 26;
    }

    // Structural edits keep the zone count and the derived rows in step.
    const auto duplicatedZoneIndex = editProcessor->duplicateImportedProgramZone(0);
    if (duplicatedZoneIndex < 0
        || editProcessor->getImportedProgramZoneCount() != 3
        || editProcessor->getImportedProgramZoneRows().size() != 3
        || !editProcessor->getLastImportDiagnosticSummary().contains("duplicated"))
    {
        std::fprintf(stderr, "Duplicating an imported-program zone did not update derived state.\n");
        tempDirectory.deleteRecursively();
        return 27;
    }

    if (!editProcessor->deleteImportedProgramZone(duplicatedZoneIndex)
        || editProcessor->getImportedProgramZoneCount() != 2
        || editProcessor->getImportedProgramZoneRows().size() != 2
        || !editProcessor->getLastImportDiagnosticSummary().contains("deleted"))
    {
        std::fprintf(stderr, "Deleting an imported-program zone did not update derived state.\n");
        tempDirectory.deleteRecursively();
        return 27;
    }

    // Slice edits are only meaningful for slice programs, and must decline other formats.
    if (editProcessor->splitImportedProgramSliceAtSample(64) >= 0
        || editProcessor->mergeImportedProgramSlicesAtSampleBoundary(64) >= 0
        || editProcessor->getImportedProgramZoneCount() != 2)
    {
        std::fprintf(stderr, "Slice edits were accepted for a non-slice program.\n");
        tempDirectory.deleteRecursively();
        return 28;
    }

    // Adding a sample asset commits the decoded audio alongside the program, and asking for the
    // same file twice reuses the asset that is already held.
    const auto addedSampleFile = tempDirectory.getChildFile("AddedAsset.wav");
    if (!writeStereoToneWav(addedSampleFile, 48000, 256))
    {
        std::fprintf(stderr, "Failed to write the sample used for asset-add coverage.\n");
        tempDirectory.deleteRecursively();
        return 29;
    }

    auto addedAssetIndex = -1;
    juce::String addAssetError;
    if (!editProcessor->ensureImportedProgramSampleAsset(addedSampleFile, addedAssetIndex, addAssetError)
        || addedAssetIndex != 1
        || editProcessor->getImportedProgramSampleAssetNames().size() != 2)
    {
        std::fprintf(stderr, "Adding an imported-program sample asset failed: %s\n", addAssetError.toRawUTF8());
        tempDirectory.deleteRecursively();
        return 29;
    }

    auto repeatedAssetIndex = -1;
    if (!editProcessor->ensureImportedProgramSampleAsset(addedSampleFile, repeatedAssetIndex, addAssetError)
        || repeatedAssetIndex != addedAssetIndex
        || editProcessor->getImportedProgramSampleAssetNames().size() != 2)
    {
        std::fprintf(stderr, "Re-adding an existing sample asset did not reuse the existing entry.\n");
        tempDirectory.deleteRecursively();
        return 30;
    }

    // A mapping state round-trip must reproduce the rows it was captured from.
    const auto capturedMappingState = editProcessor->createImportedProgramMappingState();
    const auto rowsBeforeMappingRestore = editProcessor->getImportedProgramZoneRows();
    if (!capturedMappingState.isValid()
        || !editProcessor->spreadImportedProgramZonesAcrossKeyRange({ 0, 1 })
        || !editProcessor->applyImportedProgramMappingState(capturedMappingState))
    {
        std::fprintf(stderr, "Imported-program mapping state round-trip failed.\n");
        tempDirectory.deleteRecursively();
        return 31;
    }

    const auto rowsAfterMappingRestore = editProcessor->getImportedProgramZoneRows();
    if (rowsAfterMappingRestore.size() != rowsBeforeMappingRestore.size()
        || rowsAfterMappingRestore[0].keyLow != rowsBeforeMappingRestore[0].keyLow
        || rowsAfterMappingRestore[0].keyHigh != rowsBeforeMappingRestore[0].keyHigh
        || rowsAfterMappingRestore[1].keyLow != rowsBeforeMappingRestore[1].keyLow
        || rowsAfterMappingRestore[1].keyHigh != rowsBeforeMappingRestore[1].keyHigh)
    {
        std::fprintf(stderr, "Restoring a captured mapping state did not reproduce the zone rows.\n");
        tempDirectory.deleteRecursively();
        return 31;
    }

    tempDirectory.deleteRecursively();

    return 0;
}