#include <cstdio>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/AudioStateCodec.h"
#include "plugin/PluginProcessor.h"
#include "plugin/PresetJson.h"

#if JUCE_WINDOWS
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
constexpr auto kPatchRoot = "AudiocityPatch";
constexpr auto kGeneratedWaveformData = "generatedWaveformData";
constexpr auto kCapturedSampleData = "capturedSampleData";
constexpr auto kCapturedSampleAsset = "capturedSampleAssetV1";
constexpr auto kEmbeddedSampleData = "embeddedSampleData";
constexpr auto kEmbeddedSampleAsset = "embeddedSampleAssetV1";
constexpr auto kEmbeddedSampleChannels = "embeddedSampleChannels";
constexpr auto kAudioStateSizeWarning = "audioStateSizeWarning";
constexpr auto kSamplePath = "samplePath";
constexpr auto kSampleWindowStart = "sampleWindowStart";
constexpr auto kSampleWindowEnd = "sampleWindowEnd";
constexpr std::size_t kOneMiB = 1024u * 1024u;
constexpr std::size_t kTwentyInstanceFeatureStorageLimit = 20u * kOneMiB;
constexpr std::size_t kTwentyInstancePrivateMemoryLimit = 500u * kOneMiB;

std::optional<std::size_t> currentProcessPrivateUsageBytes()
{
#if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (::GetProcessMemoryInfo(::GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != FALSE)
    {
        return static_cast<std::size_t>(counters.PrivateUsage);
    }
#endif
    return std::nullopt;
}

int readEmbeddedFrameCount(const juce::ValueTree& state)
{
    if (const auto* asset = state.getProperty(kEmbeddedSampleAsset).getBinaryData(); asset != nullptr)
    {
        juce::AudioBuffer<float> decoded;
        return audiocity::plugin::decodeAudioStateAsset(*asset, decoded) ? decoded.getNumSamples() : 0;
    }

    const auto* embeddedData = state.getProperty(kEmbeddedSampleData).getBinaryData();
    if (embeddedData == nullptr)
        return 0;

    const auto channels = juce::jmax(1,
        static_cast<int>(state.getProperty(kEmbeddedSampleChannels, 1)));
    return static_cast<int>(embeddedData->getSize() / sizeof(float)) / channels;
}

int readEmbeddedChannelCount(const juce::ValueTree& state)
{
    if (const auto* asset = state.getProperty(kEmbeddedSampleAsset).getBinaryData(); asset != nullptr)
    {
        juce::AudioBuffer<float> decoded;
        return audiocity::plugin::decodeAudioStateAsset(*asset, decoded) ? decoded.getNumChannels() : 0;
    }

    return state.getProperty(kEmbeddedSampleData).getBinaryData() != nullptr
        ? juce::jmax(1, static_cast<int>(state.getProperty(kEmbeddedSampleChannels, 1)))
        : 0;
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

juce::MemoryBlock processorStateFromTree(const juce::ValueTree& state)
{
    juce::MemoryBlock result;
    if (auto xml = state.createXml())
        juce::AudioProcessor::copyXmlToBinary(*xml, result);
    return result;
}

juce::ValueTree processorStateTreeFromBlock(const juce::MemoryBlock& state)
{
    if (auto xml = juce::AudioProcessor::getXmlFromBinary(
            state.getData(), static_cast<int>(state.getSize())))
        return juce::ValueTree::fromXml(*xml);
    return {};
}

juce::Slider* findNamedSlider(juce::Component& component, const juce::String& name)
{
    if (auto* slider = dynamic_cast<juce::Slider*>(&component);
        slider != nullptr && slider->getName().equalsIgnoreCase(name))
    {
        return slider;
    }

    for (auto childIndex = 0; childIndex < component.getNumChildComponents(); ++childIndex)
    {
        if (auto* child = component.getChildComponent(childIndex); child != nullptr)
            if (auto* match = findNamedSlider(*child, name); match != nullptr)
                return match;
    }

    return nullptr;
}

bool runPreloadDragDebounceGestureTest()
{
    auto processor = std::make_unique<AudiocityAudioProcessor>();
    processor->prepareToPlay(48000.0, 256);

    std::vector<float> waveform(65536, 0.125f);
    processor->loadGeneratedWaveformAsSample(waveform, 60);
    const auto originalPreload = processor->getPreloadSamples();
    const auto rebuildsBeforeDrag = processor->getSegmentRebuildCount();

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    if (editor == nullptr)
        return false;

    auto* preload = findNamedSlider(*editor, "Preload");
    if (preload == nullptr || !preload->onDragStart || !preload->onDragEnd)
        return false;

    preload->onDragStart();
    preload->setValue(4096.0, juce::sendNotificationSync);
    preload->setValue(8192.0, juce::sendNotificationSync);
    preload->setValue(16384.0, juce::sendNotificationSync);
    if (processor->getPreloadSamples() != originalPreload
        || processor->getSegmentRebuildCount() != rebuildsBeforeDrag)
    {
        return false;
    }

    preload->onDragEnd();
    if (processor->getPreloadSamples() != 16384
        || processor->getSegmentRebuildCount() != rebuildsBeforeDrag + 1)
    {
        return false;
    }

    // A gesture which quantizes to the already-committed value must remain a no-op.
    preload->onDragStart();
    preload->onDragEnd();
    return processor->getSegmentRebuildCount() == rebuildsBeforeDrag + 1;
}

bool runTwentyIdleCaptureAllocationTest()
{
    // Warm one instance so process-wide JUCE/runtime one-time allocation is not
    // charged to the twenty-instance measurement.
    {
        AudiocityAudioProcessor warmProcessor;
        warmProcessor.prepareToPlay(48000.0, 256);
    }

    const auto privateBytesBefore = currentProcessPrivateUsageBytes();
    std::vector<std::unique_ptr<AudiocityAudioProcessor>> processors;
    processors.reserve(20);
    auto totalFeatureStorageBytes = std::size_t{ 0 };
    for (auto index = 0; index < 20; ++index)
    {
        auto processor = std::make_unique<AudiocityAudioProcessor>();
        processor->prepareToPlay(48000.0, 256);
        const auto featureStorageBytes = processor->getCurrentCaptureAndPreviewSampleStorageBytes();
        if (processor->getCaptureWorkingStorageBytes() != 0 || featureStorageBytes >= kOneMiB)
            return false;
        totalFeatureStorageBytes += featureStorageBytes;
        processors.push_back(std::move(processor));
    }

    if (totalFeatureStorageBytes >= kTwentyInstanceFeatureStorageLimit)
        return false;

    const auto privateBytesAfter = currentProcessPrivateUsageBytes();
    if (privateBytesBefore.has_value() && privateBytesAfter.has_value())
    {
        const auto privateDelta = *privateBytesAfter > *privateBytesBefore
            ? *privateBytesAfter - *privateBytesBefore
            : std::size_t{ 0 };
        std::printf("Twenty idle processors: feature storage %.2f MiB, private-memory delta %.2f MiB.\n",
            static_cast<double>(totalFeatureStorageBytes) / static_cast<double>(kOneMiB),
            static_cast<double>(privateDelta) / static_cast<double>(kOneMiB));
        if (privateDelta >= kTwentyInstancePrivateMemoryLimit)
            return false;
    }

    return true;
}

bool runLegacyNonFiniteStateRejectionTest()
{
    const std::array<float, 3> samples{
        0.25f,
        std::numeric_limits<float>::quiet_NaN(),
        -0.25f
    };
    juce::MemoryBlock sampleBytes(sizeof(samples));
    std::memcpy(sampleBytes.getData(), samples.data(), sizeof(samples));

    constexpr std::array<const char*, 3> legacyProperties{
        kEmbeddedSampleData,
        kGeneratedWaveformData,
        kCapturedSampleData
    };
    for (const auto* property : legacyProperties)
    {
        juce::ValueTree state(kPatchRoot);
        state.setProperty(property, juce::var(sampleBytes), nullptr);
        if (juce::String(property) == kEmbeddedSampleData)
            state.setProperty(kEmbeddedSampleChannels, 1, nullptr);

        const auto encodedState = processorStateFromTree(state);
        if (encodedState.isEmpty())
            return false;

        AudiocityAudioProcessor processor;
        processor.prepareToPlay(48000.0, 256);
        processor.setStateInformation(encodedState.getData(), static_cast<int>(encodedState.getSize()));
        if (processor.isEmbeddedSampleLoaded()
            || processor.isGeneratedWaveformLoaded()
            || processor.isCapturedAudioLoaded()
            || processor.getLastStateRestoreSourceLabel() != "none"
            || !processor.getLastImportDiagnosticSummary().containsIgnoreCase("non-finite"))
        {
            std::fprintf(stderr,
                         "Legacy property %s: embedded=%d generated=%d captured=%d source='%s' diagnostic='%s'.\n",
                         property,
                         processor.isEmbeddedSampleLoaded() ? 1 : 0,
                         processor.isGeneratedWaveformLoaded() ? 1 : 0,
                         processor.isCapturedAudioLoaded() ? 1 : 0,
                         processor.getLastStateRestoreSourceLabel().toRawUTF8(),
                         processor.getLastImportDiagnosticSummary().toRawUTF8());
            return false;
        }
    }
    return true;
}

bool runEmbeddedSerializationWarningTest()
{
    AudiocityAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> invalidSample(1, 3);
    invalidSample.setSample(0, 0, 0.25f);
    invalidSample.setSample(0, 1, std::numeric_limits<float>::infinity());
    invalidSample.setSample(0, 2, -0.25f);
    processor.loadEmbeddedSampleAsSample(invalidSample, 48000.0, 60, "NonFinite.wav");

    juce::MemoryBlock stateData;
    processor.getStateInformation(stateData);
    const auto state = processorStateTreeFromBlock(stateData);
    return processor.hasStateAssetSizeWarning()
        && processor.getLastSerializedAssetStateBytes() == 0
        && state.isValid()
        && state.getProperty(kAudioStateSizeWarning).toString().containsIgnoreCase("non-finite")
        && state.getProperty(kEmbeddedSampleAsset).getBinaryData() == nullptr;
}

bool runMaximumDurationCaptureCommitTest()
{
    constexpr auto sampleRate = 96000;
    constexpr auto captureSeconds = 30;
    constexpr auto expectedSamples = sampleRate * captureSeconds;
    constexpr auto blockSize = 8192;
    constexpr auto expectedStorageBytes = static_cast<std::size_t>(expectedSamples)
        * 2u * sizeof(float);

    auto processor = std::make_unique<AudiocityAudioProcessor>();
    processor->prepareToPlay(sampleRate, blockSize);
    processor->startInputCapture();
    if (!processor->isInputCaptureRecording()
        || processor->getCaptureWorkingStorageBytes() != expectedStorageBytes)
        return false;

    juce::AudioBuffer<float> input(2, blockSize);
    juce::MidiBuffer midi;
    const auto maximumBlocks = (expectedSamples + blockSize - 1) / blockSize;
    auto blocksProcessed = 0;
    while (processor->isInputCaptureRecording() && blocksProcessed <= maximumBlocks)
    {
        for (auto channel = 0; channel < input.getNumChannels(); ++channel)
            std::fill(input.getWritePointer(channel),
                      input.getWritePointer(channel) + input.getNumSamples(),
                      channel == 0 ? 0.125f : -0.0625f);
        midi.clear();
        processor->processBlock(input, midi);
        ++blocksProcessed;
    }

    if (blocksProcessed != maximumBlocks
        || processor->isInputCaptureRecording()
        || !processor->didInputCaptureOverflow()
        || processor->getCapturedInputSamples() != expectedSamples
        || !processor->loadCapturedAudioAsSample(0, expectedSamples)
        || !processor->isCapturedAudioLoaded()
        || processor->getLoadedSampleLength() != expectedSamples
        || processor->getCaptureWorkingStorageBytes() != 0
        || processor->getCapturedInputSamples() != 0)
        return false;

    juce::MemoryBlock stateData;
    processor->getStateInformation(stateData);
    const auto state = processorStateTreeFromBlock(stateData);
    const auto* capturedAsset = state.getProperty(kCapturedSampleAsset).getBinaryData();
    if (capturedAsset == nullptr
        || processor->getLastSerializedAssetStateBytes()
            != static_cast<std::uint64_t>(expectedSamples) * sizeof(float))
        return false;

    std::vector<float> restoredSamples;
    if (!audiocity::plugin::decodeMonoAudioStateAsset(*capturedAsset, restoredSamples)
        || restoredSamples.size() != static_cast<std::size_t>(expectedSamples))
        return false;

    processor.reset();
    AudiocityAudioProcessor restoredProcessor;
    restoredProcessor.prepareToPlay(sampleRate, blockSize);
    restoredProcessor.setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
    return restoredProcessor.isCapturedAudioLoaded()
        && restoredProcessor.getLoadedSampleLength() == expectedSamples
        && restoredProcessor.getCaptureWorkingStorageBytes() == 0;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;

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

    if (!runTwentyIdleCaptureAllocationTest())
    {
        std::fprintf(stderr, "One of 20 idle processor instances eagerly allocated capture storage.\n");
        return 102;
    }

    if (!runLegacyNonFiniteStateRejectionTest())
    {
        std::fprintf(stderr, "A legacy generated, captured, or embedded state accepted a non-finite sample.\n");
        return 103;
    }

    if (!runEmbeddedSerializationWarningTest())
    {
        std::fprintf(stderr, "Embedded serialization failure did not surface through state-asset warning telemetry.\n");
        return 104;
    }

    if (!runMaximumDurationCaptureCommitTest())
    {
        std::fprintf(stderr, "Maximum-duration capture did not clamp, commit, release storage, and round-trip.\n");
        return 105;
    }

    if (!runPreloadDragDebounceGestureTest())
    {
        std::fprintf(stderr, "A preload drag rebuilt before release, rebuilt more than once, or mishandled a no-op gesture.\n");
        return 106;
    }

    auto processor = std::make_unique<AudiocityAudioProcessor>();
    processor->prepareToPlay(48000.0, 256);

    if (processor->getCaptureWorkingStorageBytes() != 0)
    {
        std::fprintf(stderr, "An idle processor eagerly allocated capture working storage.\n");
        return 90;
    }

    processor->startInputCapture();
    if (processor->getCaptureWorkingStorageBytes() == 0 || !processor->isInputCaptureRecording())
    {
        std::fprintf(stderr, "Starting capture did not allocate its bounded working storage.\n");
        return 91;
    }

    processor->clearInputCapture();
    if (processor->getCaptureWorkingStorageBytes() != 0 || processor->isInputCaptureRecording())
    {
        std::fprintf(stderr, "Clearing capture did not release its working storage.\n");
        return 92;
    }

    std::vector<float> waveform(4096, 0.0f);
    for (std::size_t i = 0; i < waveform.size(); ++i)
    {
        const auto phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi
            * static_cast<double>(i) * 220.0 / 48000.0);
        waveform[i] = 0.25f * std::sin(phase);
    }

    processor->loadGeneratedWaveformAsSample(waveform, 60);
    processor->setSampleWindow(0, static_cast<int>(waveform.size()));
    processor->setLoopPoints(0, static_cast<int>(waveform.size()));
    if (processor->getSampleWindowEnd() != static_cast<int>(waveform.size()) - 1
        || processor->getLoopEnd() != static_cast<int>(waveform.size()) - 1)
    {
        std::fprintf(stderr, "Processor sample-relative controls did not clamp to the inclusive final sample.\n");
        return 35;
    }

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

    if (!wavePresetState.hasType(kPatchRoot)
        || wavePresetState.getProperty(kSamplePath).toString().isNotEmpty()
        || readEmbeddedChannelCount(wavePresetState) != 2
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

    std::atomic<bool> cancelledImport{ true };
    const auto cancelledPreparation = backgroundSampleProcessor->prepareBackgroundImport(
        sampleFile,
        audiocity::plugin::ImportedProgramFormat::unknown,
        -1,
        {},
        &cancelledImport);
    if (cancelledPreparation.ok || !cancelledPreparation.diagnosticSummary.containsIgnoreCase("cancelled"))
    {
        std::fprintf(stderr, "Cancelled background import did not reach a terminal cancelled state.\n");
        tempDirectory.deleteRecursively();
        return 32;
    }

    // MP-1 contract: controls are applied once at block start by the audio thread, and an
    // unchanged block does not replay the setter graph.
    auto controlProcessor = std::make_unique<AudiocityAudioProcessor>();
    controlProcessor->prepareToPlay(48000.0, 128);
    controlProcessor->loadGeneratedWaveformAsSample(waveform, 60);
    controlProcessor->setPan(0.25f);
    controlProcessor->setMasterVolume(0.8f);

    juce::AudioBuffer<float> controlBuffer(2, 128);
    juce::MidiBuffer controlMidi;
    controlMidi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    const auto applyCountBeforeFirstBlock = controlProcessor->getAppliedControlGroupCount();
    controlProcessor->processBlock(controlBuffer, controlMidi);
    const auto applyCountAfterFirstBlock = controlProcessor->getAppliedControlGroupCount();
    controlMidi.clear();
    controlProcessor->processBlock(controlBuffer, controlMidi);
    if (applyCountAfterFirstBlock <= applyCountBeforeFirstBlock
        || controlProcessor->getAppliedControlGroupCount() != applyCountAfterFirstBlock)
    {
        std::fprintf(stderr, "Audio-thread control snapshot reapplied unchanged control groups.\n");
        tempDirectory.deleteRecursively();
        return 33;
    }

    // Panic is a lossless atomic latch rather than another FIFO entry. Saturating the UI note
    // queue before requesting panic must still clear both the queued requests and live voices.
    for (int noteIndex = 0; noteIndex < 512; ++noteIndex)
        controlProcessor->enqueueUiMidiNoteOn(36 + (noteIndex % 48), 100);
    controlProcessor->panicAllAudio();
    controlBuffer.clear();
    controlMidi.clear();
    controlProcessor->processBlock(controlBuffer, controlMidi);
    if (controlProcessor->getActiveVoiceCount() != 0)
    {
        std::fprintf(stderr, "Panic was lost or reordered behind saturated UI note traffic.\n");
        tempDirectory.deleteRecursively();
        return 35;
    }

    // Exercise UI/automation-style writes and immutable sample publication concurrently with
    // MIDI rendering. This is intentionally short in CI; the same loop count can be raised for
    // soak runs without changing the test topology.
    std::atomic<bool> writerFinished{ false };
    std::thread controlWriter([&]
    {
        for (int iteration = 0; iteration < 300; ++iteration)
        {
            controlProcessor->setPan(static_cast<float>((iteration % 201) - 100) / 100.0f);
            controlProcessor->setMasterVolume(0.2f + 0.8f * static_cast<float>(iteration % 101) / 100.0f);
            controlProcessor->setRootMidiNote(36 + (iteration % 49));
            controlProcessor->setSampleWindow(iteration % 64, 512 + (iteration % 1024));

            if ((iteration % 125) == 0)
                controlProcessor->loadGeneratedWaveformAsSample(waveform, 48 + (iteration % 24));
        }
        writerFinished.store(true, std::memory_order_release);
    });

    auto renderedBlocks = 0;
    auto finiteOutput = true;
    auto firstInvalidBlock = -1;
    auto firstInvalidChannel = -1;
    auto firstInvalidSample = -1;
    while (!writerFinished.load(std::memory_order_acquire) || renderedBlocks < 300)
    {
        controlBuffer.clear();
        controlMidi.clear();
        const auto note = 48 + (renderedBlocks % 24);
        controlMidi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);
        controlMidi.addEvent(juce::MidiMessage::noteOff(1, note), 96);
        controlProcessor->processBlock(controlBuffer, controlMidi);

        for (int channel = 0; channel < controlBuffer.getNumChannels(); ++channel)
        {
            const auto* samples = controlBuffer.getReadPointer(channel);
            for (int sample = 0; sample < controlBuffer.getNumSamples(); ++sample)
            {
                if (!std::isfinite(samples[sample]) && firstInvalidBlock < 0)
                {
                    finiteOutput = false;
                    firstInvalidBlock = renderedBlocks;
                    firstInvalidChannel = channel;
                    firstInvalidSample = sample;
                }
            }
        }
        ++renderedBlocks;
    }
    controlWriter.join();

    if (!finiteOutput || controlProcessor->getAppliedControlGroupCount() <= applyCountAfterFirstBlock)
    {
        std::fprintf(stderr,
            "Concurrent control/publication/MIDI stress failed: finite=%d, first invalid=%d/%d/%d, applied before=%llu, applied after=%llu.\n",
            finiteOutput ? 1 : 0,
            firstInvalidBlock,
            firstInvalidChannel,
            firstInvalidSample,
            static_cast<unsigned long long>(applyCountAfterFirstBlock),
            static_cast<unsigned long long>(controlProcessor->getAppliedControlGroupCount()));
        tempDirectory.deleteRecursively();
        return 34;
    }

    // A moved reference-only instrument must restore automatically from a bounded known root,
    // or remain pending without replacing the currently playable program until one manual root
    // resolves every manifest entry.
    const auto originalLibrary = tempDirectory.getChildFile("RelinkOriginal");
    const auto originalSamples = originalLibrary.getChildFile("Samples");
    const auto originalProgram = originalLibrary.getChildFile("Instrument.sfz");
    const auto originalTone = originalSamples.getChildFile("Tone.wav");
    if (!originalSamples.createDirectory()
        || !writeStereoToneWav(originalTone, 48000, 384)
        || !originalProgram.replaceWithText("<region> sample=Samples/Tone.wav key=60\n"))
    {
        std::fprintf(stderr, "Failed to create missing-asset resolver runtime fixtures.\n");
        tempDirectory.deleteRecursively();
        return 93;
    }

    auto relinkSourceProcessor = std::make_unique<AudiocityAudioProcessor>();
    relinkSourceProcessor->prepareToPlay(48000.0, 256);
    relinkSourceProcessor->setSampleBrowserRootFolder(tempDirectory.getFullPathName());
    if (!relinkSourceProcessor->importSfzProgram(originalProgram))
    {
        std::fprintf(stderr, "Failed to import missing-asset resolver source fixture.\n");
        tempDirectory.deleteRecursively();
        return 94;
    }

    juce::MemoryBlock relinkState;
    relinkSourceProcessor->getStateInformation(relinkState);
    const auto collaboratorRoot = tempDirectory.getChildFile("Collaborator");
    const auto movedLibrary = collaboratorRoot.getChildFile("Library");
    if (!collaboratorRoot.createDirectory()
        || !originalLibrary.copyDirectoryTo(movedLibrary)
        || !originalLibrary.deleteRecursively())
    {
        std::fprintf(stderr, "Failed to move missing-asset resolver source fixture.\n");
        tempDirectory.deleteRecursively();
        return 95;
    }

    auto autoRelinkProcessor = std::make_unique<AudiocityAudioProcessor>();
    autoRelinkProcessor->prepareToPlay(48000.0, 256);
    autoRelinkProcessor->setStateInformation(relinkState.getData(), static_cast<int>(relinkState.getSize()));
    const auto movedProgram = movedLibrary.getChildFile("Instrument.sfz");
    if (autoRelinkProcessor->hasPendingImportedAssetRelink()
        || !autoRelinkProcessor->hasImportedProgram()
        || autoRelinkProcessor->getImportedProgramPath() != movedProgram.getFullPathName()
        || autoRelinkProcessor->getImportedProgramZoneCount() != 1)
    {
        std::fprintf(stderr, "Known-root state restore did not relink the complete moved library.\n");
        tempDirectory.deleteRecursively();
        return 96;
    }

    const auto manualRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_manual_relink", "");
    const auto manualLibrary = manualRoot.getChildFile("Library");
    if (!manualRoot.createDirectory()
        || !movedLibrary.copyDirectoryTo(manualLibrary)
        || !movedLibrary.deleteRecursively())
    {
        std::fprintf(stderr, "Failed to prepare manual missing-asset resolver fixture.\n");
        manualRoot.deleteRecursively();
        tempDirectory.deleteRecursively();
        return 97;
    }

    const auto seedLibrary = tempDirectory.getChildFile("SeedLibrary");
    const auto seedSamples = seedLibrary.getChildFile("Samples");
    const auto seedProgram = seedLibrary.getChildFile("Seed.sfz");
    if (!seedSamples.createDirectory()
        || !writeStereoToneWav(seedSamples.getChildFile("Seed.wav"), 48000, 256)
        || !seedProgram.replaceWithText("<region> sample=Samples/Seed.wav key=48\n"))
    {
        std::fprintf(stderr, "Failed to prepare the preserved-program relink fixture.\n");
        manualRoot.deleteRecursively();
        tempDirectory.deleteRecursively();
        return 98;
    }

    auto manualRelinkProcessor = std::make_unique<AudiocityAudioProcessor>();
    manualRelinkProcessor->prepareToPlay(48000.0, 256);
    if (!manualRelinkProcessor->importSfzProgram(seedProgram))
    {
        std::fprintf(stderr, "Failed to seed the program preserved during relink.\n");
        manualRoot.deleteRecursively();
        tempDirectory.deleteRecursively();
        return 99;
    }

    const auto preservedPath = manualRelinkProcessor->getImportedProgramPath();
    manualRelinkProcessor->setStateInformation(
        relinkState.getData(), static_cast<int>(relinkState.getSize()));
    if (!manualRelinkProcessor->hasPendingImportedAssetRelink()
        || manualRelinkProcessor->getImportedProgramPath() != preservedPath
        || !manualRelinkProcessor->getPendingImportedAssetRelinkDiagnostic().containsIgnoreCase(
            "choose the moved library root"))
    {
        std::fprintf(stderr, "A failed automatic relink replaced the playable program or hid recovery.\n");
        manualRoot.deleteRecursively();
        tempDirectory.deleteRecursively();
        return 100;
    }

    if (!manualRelinkProcessor->relinkPendingImportedProgramFromFolder(manualRoot)
        || manualRelinkProcessor->hasPendingImportedAssetRelink()
        || manualRelinkProcessor->getImportedProgramPath()
            != manualLibrary.getChildFile("Instrument.sfz").getFullPathName()
        || manualRelinkProcessor->getImportedProgramZoneCount() != 1)
    {
        std::fprintf(stderr, "Manual folder relink did not atomically restore all moved assets.\n");
        manualRoot.deleteRecursively();
        tempDirectory.deleteRecursively();
        return 101;
    }

    constexpr int largeZoneCount = 600;
    const auto largeProgram = tempDirectory.getChildFile("LargeMapping.sfz");
    const auto largeSample = tempDirectory.getChildFile("LargeMapping.wav");
    juce::String largeSfz;
    for (auto zoneIndex = 0; zoneIndex < largeZoneCount; ++zoneIndex)
    {
        const auto note = zoneIndex % 128;
        const auto velocity = 1 + (zoneIndex / 128);
        largeSfz << "<region> sample=LargeMapping.wav key=" << note
                 << " lovel=" << velocity << " hivel=" << velocity
                 << " offset=" << zoneIndex << "\n";
    }
    auto largeMappingProcessor = std::make_unique<AudiocityAudioProcessor>();
    largeMappingProcessor->prepareToPlay(48000.0, 256);
    if (!writeStereoToneWav(largeSample, 48000, 2048)
        || !largeProgram.replaceWithText(largeSfz)
        || !largeMappingProcessor->importSfzProgram(largeProgram))
    {
        std::fprintf(stderr, "Failed to import the large mapping-row fixture.\n");
        manualRoot.deleteRecursively();
        tempDirectory.deleteRecursively();
        return 102;
    }

    const auto largeRows = largeMappingProcessor->getImportedProgramZoneRows();
    if (static_cast<int>(largeRows.size()) != largeZoneCount
        || largeMappingProcessor->getImportedProgramZoneCount() != largeZoneCount
        || largeMappingProcessor->getPublishedRendererZoneCount() != largeZoneCount)
    {
        std::fprintf(stderr, "Mapping rows, model zones, and published renderer zones diverged.\n");
        manualRoot.deleteRecursively();
        tempDirectory.deleteRecursively();
        return 103;
    }

    // Release every stream source and join each processor-owned priming worker before removing
    // the WAV fixtures. Deleting an open streamed file is nondeterministic on Windows and can
    // otherwise make the concurrency smoke appear to hang during teardown.
    controlProcessor.reset();
    largeMappingProcessor.reset();
    manualRelinkProcessor.reset();
    autoRelinkProcessor.reset();
    relinkSourceProcessor.reset();
    editProcessor.reset();
    backgroundSampleProcessor.reset();
    preparedProcessor.reset();
    invalidPreparedProcessor.reset();
    restoredProcessor.reset();
    sourceProcessor.reset();
    processor.reset();
    manualRoot.deleteRecursively();
    tempDirectory.deleteRecursively();

    return 0;
}
