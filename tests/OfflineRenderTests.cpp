#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "../src/engine/EngineCore.h"
#include "../src/engine/ImportCancellation.h"
#include "../src/engine/LegacyNkiProbe.h"
#include "../src/engine/ProgramModel.h"
#include "../src/engine/ProgramSnapshot.h"
#include "../src/engine/RexSliceProgram.h"
#include "../src/engine/SettingsUndoHistory.h"
#include "../src/engine/TransientSliceProgram.h"
#include "../src/engine/SfzImporter.h"
#include "../src/engine/SfzExporter.h"
#include "../src/engine/Sf2Importer.h"
#include "../src/engine/DecentSamplerImporter.h"
#include "../src/engine/DecentSamplerExporter.h"
#include "../src/engine/BitwigMultisampleImporter.h"
#include "../src/engine/XmlMultisampleImporters.h"
#include "../src/engine/BinaryMultisampleImporters.h"
#include "../src/plugin/CcLearnDial.h"
#include "../src/plugin/AudioStateCodec.h"
#include "../src/plugin/ImportFormatRegistry.h"
#include "../src/plugin/ImportedAssetResolver.h"
#include "../src/plugin/LibraryFileIndex.h"
#include "../src/plugin/LibraryMetadata.h"
#include "../src/plugin/OwnedJobWorker.h"
#include "../src/plugin/PeakPreviewCache.h"
#include "../src/plugin/PresetJson.h"
#include "../src/plugin/ImportedProgramState.h"
#include "../src/plugin/ImportedProgramStore.h"
#include "../src/plugin/PlayerPadState.h"
#include "../src/plugin/ProgramMappingModel.h"
#include "../src/plugin/ProgramMappingUndoHistory.h"
#include "../src/plugin/SampleBrowserTooltip.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>

namespace
{
bool waitUntil(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

bool runOwnedJobWorkerReplacementIsSerialTest()
{
    audiocity::plugin::OwnedJobWorker worker;
    std::atomic<int> activeJobs{ 0 };
    std::atomic<int> maximumActiveJobs{ 0 };
    std::atomic<bool> firstStarted{ false };
    std::atomic<bool> firstCancelled{ false };
    std::atomic<bool> replacementFinished{ false };

    if (!worker.submit([&](const audiocity::plugin::OwnedJobWorker::CancellationFlag& cancelled)
        {
            const auto active = activeJobs.fetch_add(1, std::memory_order_acq_rel) + 1;
            maximumActiveJobs.store(juce::jmax(maximumActiveJobs.load(), active));
            firstStarted.store(true, std::memory_order_release);
            while (!cancelled.load(std::memory_order_acquire))
                std::this_thread::yield();
            firstCancelled.store(true, std::memory_order_release);
            activeJobs.fetch_sub(1, std::memory_order_acq_rel);
        }))
        return false;

    if (!waitUntil([&] { return firstStarted.load(std::memory_order_acquire); }, std::chrono::seconds(1)))
        return false;

    if (!worker.submit([&](const audiocity::plugin::OwnedJobWorker::CancellationFlag& cancelled)
        {
            if (cancelled.load(std::memory_order_acquire))
                return;
            const auto active = activeJobs.fetch_add(1, std::memory_order_acq_rel) + 1;
            maximumActiveJobs.store(juce::jmax(maximumActiveJobs.load(), active));
            activeJobs.fetch_sub(1, std::memory_order_acq_rel);
            replacementFinished.store(true, std::memory_order_release);
        }))
        return false;

    if (!waitUntil([&] { return replacementFinished.load(std::memory_order_acquire); }, std::chrono::seconds(1)))
        return false;

    worker.shutdown();
    if (!firstCancelled.load(std::memory_order_acquire))
        return false;
    if (maximumActiveJobs.load(std::memory_order_acquire) != 1)
        return false;
    return true;
}

bool runOwnedJobWorkerDestructorCancelsAndJoinsTest()
{
    std::atomic<bool> started{ false };
    std::atomic<bool> exited{ false };
    {
        audiocity::plugin::OwnedJobWorker worker;
        if (!worker.submit([&](const audiocity::plugin::OwnedJobWorker::CancellationFlag& cancelled)
            {
                started.store(true, std::memory_order_release);
                while (!cancelled.load(std::memory_order_acquire))
                    std::this_thread::yield();
                exited.store(true, std::memory_order_release);
            }))
            return false;

        if (!waitUntil([&] { return started.load(std::memory_order_acquire); }, std::chrono::seconds(1)))
            return false;
    }

    return exited.load(std::memory_order_acquire);
}

bool runCancellableAudioReadStopsAtChunkBoundaryTest()
{
    struct Reader
    {
        std::atomic<bool>& cancellation;
        int calls = 0;

        bool read(juce::AudioBuffer<float>* destination,
                  const int destinationStart,
                  const int sampleCount,
                  const juce::int64 sourceStart,
                  const bool,
                  const bool)
        {
            juce::ignoreUnused(sourceStart);
            ++calls;
            destination->clear(destinationStart, sampleCount);
            cancellation.store(true, std::memory_order_release);
            return true;
        }
    };

    std::atomic<bool> cancellation{ false };
    Reader reader{ cancellation };
    juce::AudioBuffer<float> destination(1, 200000);
    const audiocity::engine::ImportCancellationScope scope(&cancellation);
    const auto completed = audiocity::engine::readAudioInCancellableChunks(
        reader, destination, destination.getNumSamples(), 65536);

    return !completed && reader.calls == 1;
}

juce::File fixtureFile(const juce::String& relativePath)
{
    return juce::File(AUDIOCITY_SOURCE_DIR).getChildFile(relativePath);
}

bool setEnvironmentVariableForTest(const juce::String& name, const juce::String& value)
{
#if JUCE_WINDOWS
    return _wputenv_s(name.toWideCharPointer(), value.toWideCharPointer()) == 0;
#else
    if (value.isEmpty())
        return unsetenv(name.toRawUTF8()) == 0;

    return setenv(name.toRawUTF8(), value.toRawUTF8(), 1) == 0;
#endif
}

struct ScopedEnvironmentVariable
{
    explicit ScopedEnvironmentVariable(juce::String variableName, juce::String variableValue)
        : name(std::move(variableName)),
          priorValue(juce::SystemStats::getEnvironmentVariable(name, {})),
          hadPriorValue(priorValue.isNotEmpty()),
          valid(setEnvironmentVariableForTest(name, variableValue))
    {
    }

    ~ScopedEnvironmentVariable()
    {
        setEnvironmentVariableForTest(name, hadPriorValue ? priorValue : juce::String{});
    }

    juce::String name;
    juce::String priorValue;
    bool hadPriorValue = false;
    bool valid = false;
};

bool buffersAreEqual(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, const float tolerance)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;

    for (int channel = 0; channel < a.getNumChannels(); ++channel)
    {
        const auto* aData = a.getReadPointer(channel);
        const auto* bData = b.getReadPointer(channel);

        for (int sample = 0; sample < a.getNumSamples(); ++sample)
        {
            if (std::abs(aData[sample] - bData[sample]) > tolerance)
                return false;
        }
    }

    return true;
}

float maxAbsDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    auto maxDifference = 0.0f;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
    {
        const auto* aData = a.getReadPointer(channel);
        const auto* bData = b.getReadPointer(channel);

        for (int sample = 0; sample < a.getNumSamples(); ++sample)
            maxDifference = juce::jmax(maxDifference, std::abs(aData[sample] - bData[sample]));
    }

    return maxDifference;
}

double rmsDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    auto sumSquares = 0.0;
    auto sampleCount = 0;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
    {
        const auto* aData = a.getReadPointer(channel);
        const auto* bData = b.getReadPointer(channel);

        for (int sample = 0; sample < a.getNumSamples(); ++sample)
        {
            const auto delta = static_cast<double>(aData[sample] - bData[sample]);
            sumSquares += delta * delta;
            ++sampleCount;
        }
    }

    return sampleCount > 0 ? std::sqrt(sumSquares / static_cast<double>(sampleCount)) : 0.0;
}

juce::AudioBuffer<float> createTestSample(const int length)
{
    juce::AudioBuffer<float> buffer(1, length);

    for (int i = 0; i < length; ++i)
    {
        const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 220.0 / 48000.0);
        buffer.setSample(0, i, 0.35f * std::sin(phase));
    }

    return buffer;
}

juce::AudioBuffer<float> renderSequence(audiocity::engine::EngineCore& engine)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 8;

    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

        if (block == 3)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 64);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            output.copyFrom(channel, block * blockSize, blockBuffer, channel, 0, blockSize);
    }

    return output;
}

juce::AudioBuffer<float> renderSequenceWithOffsets(audiocity::engine::EngineCore& engine,
                                                   const int totalSamples,
                                                   const int blockSize,
                                                   const int noteOnSample,
                                                   const int noteOffSample)
{
    constexpr int channels = 2;
    juce::AudioBuffer<float> output(channels, totalSamples);

    auto renderedSamples = 0;
    while (renderedSamples < totalSamples)
    {
        const auto blockSamples = juce::jmin(blockSize, totalSamples - renderedSamples);
        juce::AudioBuffer<float> blockBuffer(channels, blockSamples);
        juce::MidiBuffer midi;

        if (noteOnSample >= renderedSamples && noteOnSample < renderedSamples + blockSamples)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), noteOnSample - renderedSamples);

        if (noteOffSample >= renderedSamples && noteOffSample < renderedSamples + blockSamples)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), noteOffSample - renderedSamples);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            output.copyFrom(channel, renderedSamples, blockBuffer, channel, 0, blockSamples);

        renderedSamples += blockSamples;
    }

    return output;
}

bool runDeterminismTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(2048);

    audiocity::engine::EngineCore firstEngine;
    firstEngine.prepare(sampleRate, blockSize, channels);
    firstEngine.setSampleData(sample, sampleRate, 60);

    audiocity::engine::EngineCore secondEngine;
    secondEngine.prepare(sampleRate, blockSize, channels);
    secondEngine.setSampleData(sample, sampleRate, 60);

    const auto first = renderSequence(firstEngine);
    const auto second = renderSequence(secondEngine);

    return buffersAreEqual(first, second, 1.0e-7f);
}

bool runRenderSegmentationMatchesSubBlockSequenceTest()
{
    constexpr int channels = 2;
    constexpr int singleBlockSize = 96;
    constexpr int splitBlockSize = 24;
    constexpr int totalSamples = 96;
    constexpr int noteOnSample = 13;
    constexpr int noteOffSample = 70;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(4096);

    audiocity::engine::EngineCore::AmpLfoSettings ampLfo;
    ampLfo.rateHz = 3.0f;
    ampLfo.depth = 0.35f;
    ampLfo.shape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;

    audiocity::engine::EngineCore::PitchLfoSettings pitchLfo;
    pitchLfo.rateHz = 4.0f;
    pitchLfo.depthCents = 12.0f;

    audiocity::engine::EngineCore::FilterSettings filterSettings;
    filterSettings.baseCutoffHz = 1800.0f;
    filterSettings.envAmountHz = 900.0f;
    filterSettings.lfoRateHz = 2.5f;
    filterSettings.lfoAmountHz = 750.0f;
    filterSettings.lfoRetrigger = false;
    filterSettings.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;

    auto configureEngine = [&](audiocity::engine::EngineCore& engine, const int blockSize)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setAmpLfoSettings(ampLfo);
        engine.setPitchLfoSettings(pitchLfo);
        engine.setFilterSettings(filterSettings);
    };

    audiocity::engine::EngineCore singleBlockEngine;
    configureEngine(singleBlockEngine, singleBlockSize);

    audiocity::engine::EngineCore splitBlockEngine;
    configureEngine(splitBlockEngine, splitBlockSize);

    const auto singleBlock = renderSequenceWithOffsets(
        singleBlockEngine,
        totalSamples,
        singleBlockSize,
        noteOnSample,
        noteOffSample);
    const auto splitBlocks = renderSequenceWithOffsets(
        splitBlockEngine,
        totalSamples,
        splitBlockSize,
        noteOnSample,
        noteOffSample);

    return buffersAreEqual(singleBlock, splitBlocks, 1.0e-6f);
}

bool runStaticFilterSegmentationMatchesSubBlockSequenceTest()
{
    constexpr int channels = 2;
    constexpr int singleBlockSize = 96;
    constexpr int splitBlockSize = 24;
    constexpr int totalSamples = 96;
    constexpr int noteOnSample = 13;
    constexpr int noteOffSample = 70;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(4096);

    audiocity::engine::EngineCore::FilterSettings filterSettings;
    filterSettings.baseCutoffHz = 1600.0f;
    filterSettings.envAmountHz = 0.0f;
    filterSettings.resonance = 0.72f;
    filterSettings.lfoRateHz = 0.0f;
    filterSettings.lfoAmountHz = 0.0f;
    filterSettings.lfoRetrigger = false;

    auto configureEngine = [&](audiocity::engine::EngineCore& engine, const int blockSize)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setFilterSettings(filterSettings);
    };

    audiocity::engine::EngineCore singleBlockEngine;
    configureEngine(singleBlockEngine, singleBlockSize);

    audiocity::engine::EngineCore splitBlockEngine;
    configureEngine(splitBlockEngine, splitBlockSize);

    const auto singleBlock = renderSequenceWithOffsets(
        singleBlockEngine,
        totalSamples,
        singleBlockSize,
        noteOnSample,
        noteOffSample);
    const auto splitBlocks = renderSequenceWithOffsets(
        splitBlockEngine,
        totalSamples,
        splitBlockSize,
        noteOnSample,
        noteOffSample);

    return buffersAreEqual(singleBlock, splitBlocks, 1.0e-6f);
}

bool runDynamic24dBFilterSegmentationMatchesSubBlockSequenceTest()
{
    constexpr int channels = 2;
    constexpr int singleBlockSize = 96;
    constexpr int splitBlockSize = 24;
    constexpr int totalSamples = 96;
    constexpr int noteOnSample = 13;
    constexpr int noteOffSample = 70;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(4096);

    audiocity::engine::EngineCore::AmpLfoSettings ampLfo;
    ampLfo.rateHz = 3.0f;
    ampLfo.depth = 0.35f;
    ampLfo.shape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;

    audiocity::engine::EngineCore::PitchLfoSettings pitchLfo;
    pitchLfo.rateHz = 4.0f;
    pitchLfo.depthCents = 12.0f;

    audiocity::engine::EngineCore::FilterSettings filterSettings;
    filterSettings.baseCutoffHz = 1800.0f;
    filterSettings.envAmountHz = 900.0f;
    filterSettings.resonance = 0.35f;
    filterSettings.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass24;
    filterSettings.lfoRateHz = 2.5f;
    filterSettings.lfoAmountHz = 750.0f;
    filterSettings.lfoRetrigger = false;
    filterSettings.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;

    auto configureEngine = [&](audiocity::engine::EngineCore& engine, const int blockSize)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setAmpLfoSettings(ampLfo);
        engine.setPitchLfoSettings(pitchLfo);
        engine.setFilterSettings(filterSettings);
    };

    audiocity::engine::EngineCore singleBlockEngine;
    configureEngine(singleBlockEngine, singleBlockSize);

    audiocity::engine::EngineCore splitBlockEngine;
    configureEngine(splitBlockEngine, splitBlockSize);

    const auto singleBlock = renderSequenceWithOffsets(
        singleBlockEngine,
        totalSamples,
        singleBlockSize,
        noteOnSample,
        noteOffSample);
    const auto splitBlocks = renderSequenceWithOffsets(
        splitBlockEngine,
        totalSamples,
        splitBlockSize,
        noteOnSample,
        noteOffSample);

    return buffersAreEqual(singleBlock, splitBlocks, 1.0e-6f);
}

bool runLowDepth24dBFilterSegmentationMatchesSubBlockSequenceTest()
{
    constexpr int channels = 2;
    constexpr int singleBlockSize = 96;
    constexpr int splitBlockSize = 24;
    constexpr int totalSamples = 96;
    constexpr int noteOnSample = 13;
    constexpr int noteOffSample = 70;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(4096);

    audiocity::engine::EngineCore::FilterSettings filterSettings;
    filterSettings.baseCutoffHz = 1600.0f;
    filterSettings.envAmountHz = 60.0f;
    filterSettings.resonance = 0.35f;
    filterSettings.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass24;
    filterSettings.lfoRateHz = 1.5f;
    filterSettings.lfoAmountHz = 48.0f;
    filterSettings.lfoRetrigger = false;
    filterSettings.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;

    auto configureEngine = [&](audiocity::engine::EngineCore& engine, const int blockSize)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setFilterSettings(filterSettings);
    };

    audiocity::engine::EngineCore singleBlockEngine;
    configureEngine(singleBlockEngine, singleBlockSize);

    audiocity::engine::EngineCore splitBlockEngine;
    configureEngine(splitBlockEngine, splitBlockSize);

    const auto singleBlock = renderSequenceWithOffsets(
        singleBlockEngine,
        totalSamples,
        singleBlockSize,
        noteOnSample,
        noteOffSample);
    const auto splitBlocks = renderSequenceWithOffsets(
        splitBlockEngine,
        totalSamples,
        splitBlockSize,
        noteOnSample,
        noteOffSample);

    return buffersAreEqual(singleBlock, splitBlocks, 1.0e-6f);
}

bool runFilterCutoffHysteresisMatchesReferenceWithinToleranceTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int totalSamples = 4096;
    constexpr int noteOnSample = 13;
    constexpr int noteOffSample = 3072;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(8192);

    EngineCore::AdsrSettings ampEnvelope;
    ampEnvelope.attackSeconds = 0.0005f;
    ampEnvelope.decaySeconds = 0.010f;
    ampEnvelope.sustainLevel = 1.0f;
    ampEnvelope.releaseSeconds = 0.100f;

    EngineCore::AdsrSettings filterEnvelope;
    filterEnvelope.attackSeconds = 0.0005f;
    filterEnvelope.decaySeconds = 0.050f;
    filterEnvelope.sustainLevel = 0.70f;
    filterEnvelope.releaseSeconds = 0.120f;

    EngineCore::FilterSettings filterSettings;
    filterSettings.baseCutoffHz = 1600.0f;
    filterSettings.envAmountHz = 2400.0f;
    filterSettings.resonance = 0.18f;
    filterSettings.mode = EngineCore::FilterSettings::Mode::lowPass24;
    filterSettings.velocityAmountHz = 700.0f;
    filterSettings.lfoRateHz = 2.25f;
    filterSettings.lfoAmountHz = 900.0f;
    filterSettings.lfoRetrigger = false;
    filterSettings.lfoShape = EngineCore::FilterSettings::LfoShape::sine;

    auto configureEngine = [&](EngineCore& engine)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setQualityTier(EngineCore::QualityTier::fidelity);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setPlaybackMode(EngineCore::PlaybackMode::loop);
        engine.setLoopPoints(384, 7167);
        engine.setLoopCrossfadeSamples(48);
        engine.setAmpEnvelope(ampEnvelope);
        engine.setFilterEnvelope(filterEnvelope);
        engine.setFilterSettings(filterSettings);
    };

    EngineCore referenceEngine;
    configureEngine(referenceEngine);
    referenceEngine.setFilterCutoffUpdateThresholdsForTesting(0.0f, 1.0e-9f);

    EngineCore hysteresisEngine;
    configureEngine(hysteresisEngine);
    hysteresisEngine.setFilterCutoffUpdateThresholdsForTesting(6.0f, 0.005f);

    const auto reference = renderSequenceWithOffsets(
        referenceEngine,
        totalSamples,
        blockSize,
        noteOnSample,
        noteOffSample);
    const auto hysteresis = renderSequenceWithOffsets(
        hysteresisEngine,
        totalSamples,
        blockSize,
        noteOnSample,
        noteOffSample);

    const auto maxDifference = maxAbsDifference(reference, hysteresis);
    const auto rms = rmsDifference(reference, hysteresis);
    if (maxDifference > 0.01f || rms > 0.001f)
    {
        std::fprintf(stderr,
            "Hysteresis diff too large: max=%f rms=%f\n",
            maxDifference,
            rms);
        return false;
    }

    return true;
}

bool runStereoFidelitySegmentationMatchesSubBlockSequenceTest()
{
    constexpr int channels = 2;
    constexpr int singleBlockSize = 96;
    constexpr int splitBlockSize = 24;
    constexpr int totalSamples = 96;
    constexpr int noteOnSample = 13;
    constexpr int noteOffSample = 70;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    juce::AudioBuffer<float> stereoSample(channels, sampleLength);
    stereoSample.clear();

    const auto leftTone = createTestSample(sampleLength);
    stereoSample.copyFrom(0, 0, leftTone, 0, 0, sampleLength);

    for (int sampleIndex = 0; sampleIndex < sampleLength; ++sampleIndex)
    {
        const auto phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * sampleIndex * 330.0 / sampleRate);
        stereoSample.setSample(1, sampleIndex, 0.2f * std::cos(phase));
    }

    audiocity::engine::EngineCore::AdsrSettings flatAdsr;
    flatAdsr.attackSeconds = 0.0001f;
    flatAdsr.decaySeconds = 0.0001f;
    flatAdsr.sustainLevel = 1.0f;
    flatAdsr.releaseSeconds = 0.001f;

    audiocity::engine::EngineCore::FilterSettings filterSettings;
    filterSettings.baseCutoffHz = 1600.0f;
    filterSettings.envAmountHz = 60.0f;
    filterSettings.resonance = 0.35f;
    filterSettings.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass24;
    filterSettings.lfoRateHz = 1.5f;
    filterSettings.lfoAmountHz = 48.0f;
    filterSettings.lfoRetrigger = false;
    filterSettings.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;

    auto configureEngine = [&](audiocity::engine::EngineCore& engine, const int blockSize)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::fidelity);
        engine.setAmpEnvelope(flatAdsr);
        engine.setFilterSettings(filterSettings);
        engine.setSampleData(stereoSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setSampleWindow(64, 192);
        engine.setReversePlayback(true);
        engine.setFadeSamples(11, 13);
    };

    audiocity::engine::EngineCore singleBlockEngine;
    configureEngine(singleBlockEngine, singleBlockSize);

    audiocity::engine::EngineCore splitBlockEngine;
    configureEngine(splitBlockEngine, splitBlockSize);

    const auto singleBlock = renderSequenceWithOffsets(
        singleBlockEngine,
        totalSamples,
        singleBlockSize,
        noteOnSample,
        noteOffSample);
    const auto splitBlocks = renderSequenceWithOffsets(
        splitBlockEngine,
        totalSamples,
        splitBlockSize,
        noteOnSample,
        noteOffSample);

    return buffersAreEqual(singleBlock, splitBlocks, 1.0e-6f);
}

bool runMonoFidelityLoopCrossfadeSegmentationMatchesSubBlockSequenceTest()
{
    constexpr int channels = 2;
    constexpr int singleBlockSize = 192;
    constexpr int splitBlockSize = 48;
    constexpr int totalSamples = 3072;
    constexpr int noteOnSample = 13;
    constexpr int noteOffSample = totalSamples + splitBlockSize;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(4096);

    audiocity::engine::EngineCore::AdsrSettings flatAdsr;
    flatAdsr.attackSeconds = 0.0001f;
    flatAdsr.decaySeconds = 0.0001f;
    flatAdsr.sustainLevel = 1.0f;
    flatAdsr.releaseSeconds = 0.001f;

    audiocity::engine::EngineCore::FilterSettings filterSettings;
    filterSettings.baseCutoffHz = 1600.0f;
    filterSettings.envAmountHz = 60.0f;
    filterSettings.resonance = 0.35f;
    filterSettings.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass24;
    filterSettings.lfoRateHz = 1.5f;
    filterSettings.lfoAmountHz = 48.0f;
    filterSettings.lfoRetrigger = false;
    filterSettings.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;

    auto configureEngine = [&](audiocity::engine::EngineCore& engine, const int blockSize)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::fidelity);
        engine.setAmpEnvelope(flatAdsr);
        engine.setFilterSettings(filterSettings);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setLoopPoints(384, 2047);
        engine.setLoopCrossfadeSamples(48);
    };

    audiocity::engine::EngineCore singleBlockEngine;
    configureEngine(singleBlockEngine, singleBlockSize);

    audiocity::engine::EngineCore splitBlockEngine;
    configureEngine(splitBlockEngine, splitBlockSize);

    const auto singleBlock = renderSequenceWithOffsets(
        singleBlockEngine,
        totalSamples,
        singleBlockSize,
        noteOnSample,
        noteOffSample);
    const auto splitBlocks = renderSequenceWithOffsets(
        splitBlockEngine,
        totalSamples,
        splitBlockSize,
        noteOnSample,
        noteOffSample);

    return buffersAreEqual(singleBlock, splitBlocks, 1.0e-6f);
}

bool runEditedSampleSegmentationMatchesSubBlockSequenceTest()
{
    constexpr int channels = 2;
    constexpr int singleBlockSize = 96;
    constexpr int splitBlockSize = 24;
    constexpr int totalSamples = 96;
    constexpr int noteOnSample = 13;
    constexpr int noteOffSample = 70;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(4096);

    audiocity::engine::EngineCore::AdsrSettings flatAdsr;
    flatAdsr.attackSeconds = 0.0001f;
    flatAdsr.decaySeconds = 0.0001f;
    flatAdsr.sustainLevel = 1.0f;
    flatAdsr.releaseSeconds = 0.001f;

    audiocity::engine::EngineCore::FilterSettings openFilter;
    openFilter.baseCutoffHz = 18000.0f;
    openFilter.envAmountHz = 0.0f;

    auto configureEngine = [&](audiocity::engine::EngineCore& engine, const int blockSize)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::fidelity);
        engine.setAmpEnvelope(flatAdsr);
        engine.setFilterSettings(openFilter);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setSampleWindow(64, 192);
        engine.setReversePlayback(true);
        engine.setFadeSamples(11, 13);
    };

    audiocity::engine::EngineCore singleBlockEngine;
    configureEngine(singleBlockEngine, singleBlockSize);

    audiocity::engine::EngineCore splitBlockEngine;
    configureEngine(splitBlockEngine, splitBlockSize);

    const auto singleBlock = renderSequenceWithOffsets(
        singleBlockEngine,
        totalSamples,
        singleBlockSize,
        noteOnSample,
        noteOffSample);
    const auto splitBlocks = renderSequenceWithOffsets(
        splitBlockEngine,
        totalSamples,
        splitBlockSize,
        noteOnSample,
        noteOffSample);

    return buffersAreEqual(singleBlock, splitBlocks, 1.0e-6f);
}

bool runStereoFilterChannelIsolationTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    EngineCore::FilterSettings filterSettings;
    filterSettings.baseCutoffHz = 1200.0f;
    filterSettings.envAmountHz = 0.0f;
    filterSettings.resonance = 0.78f;
    filterSettings.mode = EngineCore::FilterSettings::Mode::lowPass24;
    engine.setFilterSettings(filterSettings);

    EngineCore::DcFilterSettings dc;
    dc.enabled = false;
    engine.setDcFilterSettings(dc);

    Program program;
    SampleAsset asset;
    asset.lengthSamples = sampleLength;
    asset.numChannels = channels;
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(60);
    zone.velocityRange = VelocityRange::full();
    zone.rootMidiNote = 60;
    program.zones.push_back(zone);

    juce::AudioBuffer<float> stereoSample(channels, sampleLength);
    stereoSample.clear();

    const auto leftTone = createTestSample(sampleLength);
    stereoSample.copyFrom(0, 0, leftTone, 0, 0, sampleLength);

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(stereoSample);
    engine.setProgram(program, samples);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    auto leftEnergy = 0.0f;
    auto rightEnergy = 0.0f;
    for (int sample = 0; sample < blockSize; ++sample)
    {
        leftEnergy += std::abs(block.getSample(0, sample));
        rightEnergy += std::abs(block.getSample(1, sample));
    }

    return leftEnergy > 0.01f && rightEnergy < leftEnergy * 0.05f;
}

bool runProgramModelRangeAndZoneMatchingTest()
{
    using namespace audiocity::engine;

    const auto reversedKeyRange = MidiRange::fromUnordered(80, 40);
    if (reversedKeyRange.low != 40 || reversedKeyRange.high != 80 || !reversedKeyRange.isValid())
        return false;

    const auto clippedKeyRange = MidiRange::fromUnordered(-12, 140);
    if (clippedKeyRange.low != kMidiNoteMin || clippedKeyRange.high != kMidiNoteMax)
        return false;

    const auto reversedVelocityRange = VelocityRange::fromUnordered(120, 12);
    if (reversedVelocityRange.low != 12 || reversedVelocityRange.high != 120 || !reversedVelocityRange.isValid())
        return false;

    SampleAsset sample;
    sample.sourcePath = "Samples/Kick.wav";
    sample.displayName = "Kick";
    sample.lengthSamples = 2048;
    sample.numChannels = 2;
    sample.sampleRateHz = 48000.0;
    sample.rootMidiNote = 36;

    if (!sample.hasAudio())
        return false;

    Program program;
    program.name = "Mapped Kit";
    program.sampleAssets.push_back(sample);

    Zone kickZone;
    kickZone.sampleAssetIndex = 0;
    kickZone.keyRange = MidiRange::single(36);
    kickZone.velocityRange = VelocityRange::fromUnordered(32, 127);
    kickZone.rootMidiNote = 36;
    program.zones.push_back(kickZone);

    Zone missingSampleZone;
    missingSampleZone.sampleAssetIndex = 9;
    missingSampleZone.keyRange = MidiRange::single(37);
    program.zones.push_back(missingSampleZone);

    if (!program.hasPlayableZones())
        return false;

    if (!program.isZoneSampleIndexValid(program.zones[0]) || program.isZoneSampleIndexValid(program.zones[1]))
        return false;

    if (program.findFirstMatchingZoneIndex(36, 31) != -1)
        return false;

    if (program.findFirstMatchingZoneIndex(36, 32) != 0)
        return false;

    if (program.findFirstMatchingZoneIndex(37, 96) != -1)
        return false;

    return true;
}

bool runProgramSnapshotBuildAndMatchTest()
{
    using namespace audiocity::engine;

    Program program;

    SampleAsset sample;
    sample.lengthSamples = 2048;
    sample.numChannels = 1;
    sample.sampleRateHz = 48000.0;
    sample.rootMidiNote = 40;
    program.sampleAssets.push_back(sample);

    Group group;
    group.keyRange = MidiRange::fromUnordered(36, 48);
    group.velocityRange = VelocityRange::fromUnordered(80, 127);
    program.groups.push_back(group);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.groupIndex = 0;
    zone.keyRange = MidiRange::single(40);
    zone.velocityRange = VelocityRange::fromUnordered(64, 127);
    zone.rootMidiNote = 40;
    program.zones.push_back(zone);

    const auto snapshot = ProgramSnapshot::fromProgram(program);
    if (snapshot.sampleAssetCount != 1 || snapshot.groupCount != 1 || snapshot.zoneCount != 1)
        return false;

    if (snapshot.truncated || !snapshot.hasPlayableZones())
        return false;

    if (snapshot.findFirstMatchingZoneIndex(40, 79) != -1)
        return false;

    if (snapshot.findFirstMatchingZoneIndex(39, 100) != -1)
        return false;

    if (snapshot.findFirstMatchingZoneIndex(40, 100) != 0)
        return false;

    return snapshot.zones[0].rootMidiNote == 40;
}

bool runProgramMappingRowsTest()
{
    using namespace audiocity::engine;

    Program program;
    program.name = "Mapping Test";

    SampleAsset sample;
    sample.displayName = "snare_rr1.wav";
    program.sampleAssets.push_back(sample);

    Group group;
    group.roundRobinGroup = 4;
    group.roundRobinMode = RoundRobinMode::cycleRandom;
    group.chokeGroup = 2;
    group.triggerMode = ZoneTriggerMode::oneShot;
    group.gainDb = -3.0f;
    group.pan = 0.25f;
    program.groups.push_back(group);

    Zone zone;
    zone.groupIndex = 0;
    zone.sampleAssetIndex = 0;
    zone.keyRange = { 38, 40 };
    zone.velocityRange = { 32, 96 };
    zone.velocityFadeIn = VelocityFadeRange::fromUnordered(40, 64);
    zone.velocityFadeOut = VelocityFadeRange::fromUnordered(92, 120);
    zone.rootMidiNote = 39;
    zone.roundRobinPosition = 2;
    zone.loopMode = ZoneLoopMode::sustain;
    zone.gainDb = 1.5f;
    zone.pan = -0.5f;
    zone.triggerMode = ZoneTriggerMode::release;
    program.zones.push_back(zone);

    const auto rows = audiocity::plugin::buildProgramZoneListRows(program);
    if (rows.size() != 1)
        return false;

    const auto& row = rows.front();
    return row.zoneIndex == 0
        && row.keyLow == 38
        && row.keyHigh == 40
        && row.velocityLow == 32
        && row.velocityHigh == 96
        && row.velocityFadeInLow == 40
        && row.velocityFadeInHigh == 64
        && row.velocityFadeOutLow == 92
        && row.velocityFadeOutHigh == 120
        && row.rootMidiNote == 39
        && row.roundRobinGroup == 4
        && row.roundRobinPosition == 2
        && row.chokeGroupId == 2
        && std::abs(row.gainDbValue - (-1.5f)) <= 1.0e-6f
        && std::abs(row.panValue - (-0.25f)) <= 1.0e-6f
        && row.roundRobinModeValue == RoundRobinMode::cycleRandom
        && row.triggerModeValue == ZoneTriggerMode::release
        && row.loopModeValue == ZoneLoopMode::sustain
        && row.sampleName == "snare_rr1.wav"
        && row.keyRange == "38-40"
        && row.velocityRange == "32-96"
        && row.velocityFadeIn == "40-64"
        && row.velocityFadeOut == "92-120"
        && row.rootNote == "39"
        && row.triggerMode == "release"
        && row.loopMode == "sustain"
        && row.roundRobin == "4:2"
        && row.roundRobinMode == "cycle-random"
        && row.chokeGroup == "2"
        && row.gainDb == "-1.5 dB"
        && row.pan == "-25"
        && row.detailText.contains("Vel Fade In: 40-64")
        && row.detailText.contains("Vel Fade Out: 92-120")
        && row.detailText.contains("RR mode: cycle-random")
        && row.detailText.contains("Sample: snare_rr1.wav");
}

bool runProgramMappingEditTest()
{
    using namespace audiocity::engine;

    Program program;

    SampleAsset sample;
    sample.displayName = "tone.wav";
    sample.lengthSamples = 512;
    program.sampleAssets.push_back(sample);

    SampleAsset alternateSample;
    alternateSample.displayName = "alt.wav";
    alternateSample.lengthSamples = 96;
    alternateSample.rootMidiNote = 67;
    program.sampleAssets.push_back(alternateSample);

    Group group;
    group.keyRange = MidiRange::single(60);
    group.velocityRange = VelocityRange::fromUnordered(20, 80);
    group.gainDb = -3.0f;
    group.pan = 0.25f;
    program.groups.push_back(group);

    Zone first;
    first.groupIndex = 0;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::single(60);
    first.velocityRange = VelocityRange::fromUnordered(20, 80);
    first.rootMidiNote = 60;
    first.velocityFadeIn = VelocityFadeRange::disabled();
    first.velocityFadeOut = VelocityFadeRange::disabled();
    first.gainDb = 1.0f;
    first.pan = -0.25f;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::single(62);
    second.velocityRange = VelocityRange::fromUnordered(30, 90);
    second.rootMidiNote = 62;
    second.gainDb = 2.0f;
    second.pan = 0.10f;
    second.roundRobinGroup = 3;
    second.roundRobinPosition = 1;
    second.chokeGroup = 5;
    second.triggerMode = ZoneTriggerMode::oneShot;
    second.loopMode = ZoneLoopMode::sustain;
    second.velocityFadeIn = VelocityFadeRange::fromUnordered(16, 48);
    second.velocityFadeOut = VelocityFadeRange::fromUnordered(96, 120);
    second.sampleStart = 12;
    second.sampleEndExclusive = 160;
    second.loopStart = 24;
    second.loopEndExclusive = 150;
    program.zones.push_back(second);

    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = 75;
    edit.keyHigh = 72;
    edit.velocityLow = 127;
    edit.velocityHigh = 96;
    edit.velocityFadeInLow = 12;
    edit.velocityFadeInHigh = 72;
    edit.velocityFadeOutLow = 100;
    edit.velocityFadeOutHigh = 118;
    edit.rootMidiNote = 200;
    edit.gainDb = -6.0f;
    edit.pan = 0.5f;
    edit.roundRobinGroup = 9;
    edit.roundRobinPosition = 2;
    edit.roundRobinMode = RoundRobinMode::cycleRandom;
    edit.chokeGroupId = 7;
    edit.triggerMode = ZoneTriggerMode::release;
    edit.loopMode = ZoneLoopMode::continuous;
    edit.hasGainDb = true;
    edit.hasPan = true;
    edit.hasVelocityFadeIn = true;
    edit.hasVelocityFadeOut = true;
    edit.hasRoundRobinGroup = true;
    edit.hasRoundRobinPosition = true;
    edit.hasRoundRobinMode = true;
    edit.hasChokeGroupId = true;
    edit.hasTriggerMode = true;
    edit.hasLoopMode = true;

    if (!audiocity::plugin::applyProgramZoneEdit(program, edit))
        return false;

    audiocity::plugin::ProgramZoneEdit rangeOnlyEdit;
    rangeOnlyEdit.zoneIndex = 1;
    rangeOnlyEdit.keyLow = 64;
    rangeOnlyEdit.keyHigh = 64;
    rangeOnlyEdit.velocityLow = 10;
    rangeOnlyEdit.velocityHigh = 11;
    rangeOnlyEdit.velocityFadeInLow = -1;
    rangeOnlyEdit.velocityFadeInHigh = -1;
    rangeOnlyEdit.velocityFadeOutLow = -1;
    rangeOnlyEdit.velocityFadeOutHigh = -1;
    rangeOnlyEdit.rootMidiNote = 65;
    rangeOnlyEdit.hasVelocityFadeIn = true;
    rangeOnlyEdit.hasVelocityFadeOut = true;
    if (!audiocity::plugin::applyProgramZoneEdit(program, rangeOnlyEdit))
        return false;

    audiocity::plugin::ProgramZoneEdit sampleSwapEdit;
    sampleSwapEdit.zoneIndex = 1;
    sampleSwapEdit.sampleAssetIndex = 1;
    sampleSwapEdit.hasSampleAssetIndex = true;
    sampleSwapEdit.keyLow = 64;
    sampleSwapEdit.keyHigh = 64;
    sampleSwapEdit.velocityLow = 10;
    sampleSwapEdit.velocityHigh = 11;
    sampleSwapEdit.rootMidiNote = 65;
    if (!audiocity::plugin::applyProgramZoneEdit(program, sampleSwapEdit))
        return false;

    audiocity::plugin::ProgramZoneEdit invalidEdit;
    invalidEdit.zoneIndex = 99;
    invalidEdit.keyLow = 0;
    invalidEdit.keyHigh = 127;
    invalidEdit.velocityLow = 0;
    invalidEdit.velocityHigh = 127;
    invalidEdit.rootMidiNote = 60;
    if (audiocity::plugin::applyProgramZoneEdit(program, invalidEdit))
        return false;

    const auto& editedZone = program.zones[0];
    const auto& untouchedZone = program.zones[1];
    const auto& updatedGroup = program.groups[0];

    return editedZone.keyRange.low == 72
        && editedZone.keyRange.high == 75
        && editedZone.velocityRange.low == 96
        && editedZone.velocityRange.high == 127
        && editedZone.velocityFadeIn.low == 12
        && editedZone.velocityFadeIn.high == 72
        && editedZone.velocityFadeOut.low == 100
        && editedZone.velocityFadeOut.high == 118
        && editedZone.rootMidiNote == 127
        && std::abs(editedZone.gainDb - (-3.0f)) <= 1.0e-6f
        && std::abs(editedZone.pan - 0.25f) <= 1.0e-6f
        && editedZone.roundRobinGroup == 9
        && editedZone.roundRobinPosition == 2
        && editedZone.roundRobinMode == RoundRobinMode::cycleRandom
        && editedZone.chokeGroup == 7
        && editedZone.triggerMode == ZoneTriggerMode::release
        && editedZone.loopMode == ZoneLoopMode::continuous
        && untouchedZone.keyRange.low == 64
        && untouchedZone.keyRange.high == 64
        && untouchedZone.velocityRange.low == 10
        && untouchedZone.velocityRange.high == 11
        && !untouchedZone.velocityFadeIn.isEnabled()
        && !untouchedZone.velocityFadeOut.isEnabled()
        && untouchedZone.rootMidiNote == 65
        && untouchedZone.sampleAssetIndex == 1
        && untouchedZone.sampleStart == 12
        && untouchedZone.sampleEndExclusive == 96
        && untouchedZone.loopStart == 24
        && untouchedZone.loopEndExclusive == 96
        && std::abs(untouchedZone.gainDb - 2.0f) <= 1.0e-6f
        && std::abs(untouchedZone.pan - 0.10f) <= 1.0e-6f
        && untouchedZone.roundRobinGroup == 3
        && untouchedZone.roundRobinPosition == 1
        && untouchedZone.roundRobinMode == RoundRobinMode::ordered
        && untouchedZone.chokeGroup == 5
        && untouchedZone.triggerMode == ZoneTriggerMode::oneShot
        && untouchedZone.loopMode == ZoneLoopMode::sustain
        && updatedGroup.keyRange.low == 64
        && updatedGroup.keyRange.high == 75
        && updatedGroup.velocityRange.low == 10
        && updatedGroup.velocityRange.high == 127;
}

bool runProgramMappingOverviewEditTest()
{
    audiocity::plugin::ProgramZoneListRow row;
    row.zoneIndex = 3;
    row.keyLow = 40;
    row.keyHigh = 44;
    row.velocityLow = 20;
    row.velocityHigh = 40;
    row.rootMidiNote = 42;

    const auto moved = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::move,
        52,
        32,
        10,
        12);
    if (!(moved.zoneIndex == 3
        && moved.keyLow == 50
        && moved.keyHigh == 54
        && moved.velocityLow == 32
        && moved.velocityHigh == 52
        && moved.rootMidiNote == 42))
    {
        return false;
    }

    const auto clippedMove = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::move,
        127,
        127,
        100,
        120);
    if (!(clippedMove.keyLow == 123
        && clippedMove.keyHigh == 127
        && clippedMove.velocityLow == 107
        && clippedMove.velocityHigh == 127))
    {
        return false;
    }

    const auto resizedKeyLow = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::keyLow,
        60,
        20);
    if (!(resizedKeyLow.keyLow == 44 && resizedKeyLow.keyHigh == 44))
        return false;

    const auto resizedKeyHigh = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::keyHigh,
        12,
        20);
    if (!(resizedKeyHigh.keyLow == 40 && resizedKeyHigh.keyHigh == 40))
        return false;

    const auto resizedVelocityLow = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::velocityLow,
        40,
        100);
    if (!(resizedVelocityLow.velocityLow == 40 && resizedVelocityLow.velocityHigh == 40))
        return false;

    const auto resizedVelocityHigh = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::velocityHigh,
        40,
        2);
    return resizedVelocityHigh.velocityLow == 20
        && resizedVelocityHigh.velocityHigh == 20;
}

bool runProgramMappingSampleWindowEditTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "window.wav";
    sample.lengthSamples = 240;
    program.sampleAssets.push_back(sample);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(60);
    zone.velocityRange = VelocityRange::full();
    zone.rootMidiNote = 60;
    zone.sampleStart = 12;
    zone.sampleEndExclusive = 181;
    zone.loopStart = 40;
    zone.loopEndExclusive = 97;
    program.zones.push_back(zone);

    const auto rows = audiocity::plugin::buildProgramZoneListRows(program);
    if (rows.size() != 1)
        return false;

    const auto& row = rows.front();
    if (!(row.sampleLength == 240
        && row.sampleStart == 12
        && row.sampleEnd == 180
        && row.loopStart == 40
        && row.loopEnd == 96
        && row.sampleWindow == "12-180"
        && row.loopPoints == "40-96"
        && row.detailText.contains("Window: 12-180")
        && row.detailText.contains("Loop Points: 40-96")))
    {
        return false;
    }

    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = zone.keyRange.low;
    edit.keyHigh = zone.keyRange.high;
    edit.velocityLow = zone.velocityRange.low;
    edit.velocityHigh = zone.velocityRange.high;
    edit.rootMidiNote = zone.rootMidiNote;
    edit.sampleStart = 200;
    edit.sampleEnd = 500;
    edit.loopStart = 5;
    edit.loopEnd = 400;
    edit.hasSampleStart = true;
    edit.hasSampleEnd = true;
    edit.hasLoopStart = true;
    edit.hasLoopEnd = true;

    if (!audiocity::plugin::applyProgramZoneEdit(program, edit))
        return false;

    return program.zones[0].sampleStart == 200
        && program.zones[0].sampleEndExclusive == 240
        && program.zones[0].loopStart == 200
        && program.zones[0].loopEndExclusive == 240;
}

bool runProgramMappingZoneOperationsTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "ops.wav";
    sample.lengthSamples = 512;
    program.sampleAssets.push_back(sample);

    Group group;
    group.keyRange = MidiRange::fromUnordered(36, 44);
    group.velocityRange = VelocityRange::full();
    program.groups.push_back(group);

    Zone first;
    first.groupIndex = 0;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::fromUnordered(36, 40);
    first.velocityRange = VelocityRange::fromUnordered(20, 100);
    first.rootMidiNote = 38;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::fromUnordered(42, 44);
    second.rootMidiNote = 43;
    program.zones.push_back(second);

    const auto duplicatedIndex = audiocity::plugin::duplicateProgramZone(program, 0);
    if (!(duplicatedIndex == 2
        && program.zones.size() == 3
        && program.zones[2].keyRange.low == 36
        && program.zones[2].keyRange.high == 40))
    {
        return false;
    }

    const auto splitIndex = audiocity::plugin::splitProgramZoneByKey(program, 2);
    if (!(splitIndex == 3
        && program.zones.size() == 4
        && program.zones[2].keyRange.low == 36
        && program.zones[2].keyRange.high == 38
        && program.zones[3].keyRange.low == 39
        && program.zones[3].keyRange.high == 40))
    {
        return false;
    }

    if (!audiocity::plugin::deleteProgramZone(program, 1))
        return false;

    if (audiocity::plugin::deleteProgramZone(program, 99))
        return false;

    if (audiocity::plugin::splitProgramZoneByKey(program, 99) >= 0)
        return false;

    Program singleKeyProgram;
    singleKeyProgram.sampleAssets.push_back(sample);
    singleKeyProgram.groups.push_back(group);
    Zone singleKeyZone = first;
    singleKeyZone.keyRange = MidiRange::single(60);
    singleKeyProgram.zones.push_back(singleKeyZone);

    if (audiocity::plugin::splitProgramZoneByKey(singleKeyProgram, 0) >= 0)
        return false;

    return program.zones.size() == 3
        && program.groups[0].keyRange.low == 36
        && program.groups[0].keyRange.high == 40;
}

bool runProgramMappingChromaticRemapTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "remap.wav";
    sample.lengthSamples = 512;
    sample.numChannels = 1;
    sample.sampleRateHz = 48000.0;
    program.sampleAssets.push_back(sample);

    Group group;
    group.keyRange = MidiRange::fromUnordered(40, 60);
    group.velocityRange = VelocityRange::full();
    program.groups.push_back(group);

    Zone first;
    first.groupIndex = 0;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::fromUnordered(40, 44);
    first.velocityRange = VelocityRange::fromUnordered(1, 127);
    first.rootMidiNote = 42;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::fromUnordered(50, 52);
    second.rootMidiNote = 51;
    program.zones.push_back(second);

    Zone third = first;
    third.keyRange = MidiRange::fromUnordered(58, 60);
    third.rootMidiNote = 59;
    program.zones.push_back(third);

    if (!audiocity::plugin::remapProgramZonesChromatically(program, { 2, 0 }, 36))
        return false;

    if (program.zones[2].keyRange.low != 36
        || program.zones[2].keyRange.high != 36
        || program.zones[2].rootMidiNote != 36
        || program.zones[0].keyRange.low != 37
        || program.zones[0].keyRange.high != 37
        || program.zones[0].rootMidiNote != 37
        || program.zones[1].keyRange.low != 50
        || program.zones[1].keyRange.high != 52
        || program.zones[1].rootMidiNote != 51)
    {
        return false;
    }

    if (program.groups[0].keyRange.low != 36 || program.groups[0].keyRange.high != 52)
        return false;

    return !audiocity::plugin::remapProgramZonesChromatically(program, { 99 }, 36);
}

bool runProgramMappingKeyRangeSpreadTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "spread.wav";
    sample.lengthSamples = 512;
    sample.numChannels = 1;
    sample.sampleRateHz = 48000.0;
    program.sampleAssets.push_back(sample);

    Group group;
    group.keyRange = MidiRange::fromUnordered(40, 60);
    group.velocityRange = VelocityRange::full();
    program.groups.push_back(group);

    Zone first;
    first.groupIndex = 0;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::fromUnordered(40, 44);
    first.velocityRange = VelocityRange::fromUnordered(1, 127);
    first.rootMidiNote = 42;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::fromUnordered(50, 52);
    second.rootMidiNote = 51;
    program.zones.push_back(second);

    Zone third = first;
    third.keyRange = MidiRange::fromUnordered(58, 60);
    third.rootMidiNote = 59;
    program.zones.push_back(third);

    if (!audiocity::plugin::spreadProgramZonesAcrossKeyRange(program, { 2, 0, 1 }))
        return false;

    if (program.zones[0].keyRange.low != 40
        || program.zones[0].keyRange.high != 46
        || program.zones[0].rootMidiNote != 43
        || program.zones[1].keyRange.low != 47
        || program.zones[1].keyRange.high != 53
        || program.zones[1].rootMidiNote != 50
        || program.zones[2].keyRange.low != 54
        || program.zones[2].keyRange.high != 60
        || program.zones[2].rootMidiNote != 57)
    {
        return false;
    }

    if (program.groups[0].keyRange.low != 40 || program.groups[0].keyRange.high != 60)
        return false;

    return !audiocity::plugin::spreadProgramZonesAcrossKeyRange(program, { 0 });
}

bool runProgramSliceSplitAtSampleTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "slices.wav";
    sample.lengthSamples = 1000;
    sample.numChannels = 1;
    sample.sampleRateHz = 48000.0;
    program.sampleAssets.push_back(sample);

    Group group;
    group.keyRange = MidiRange::fromUnordered(36, 38);
    group.velocityRange = VelocityRange::full();
    program.groups.push_back(group);

    Zone first;
    first.groupIndex = 0;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::single(36);
    first.velocityRange = VelocityRange::full();
    first.rootMidiNote = 36;
    first.sampleStart = 0;
    first.sampleEndExclusive = 300;
    first.triggerMode = ZoneTriggerMode::oneShot;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::single(37);
    second.rootMidiNote = 37;
    second.sampleStart = 300;
    second.sampleEndExclusive = 700;
    program.zones.push_back(second);

    Zone third = first;
    third.keyRange = MidiRange::single(38);
    third.rootMidiNote = 38;
    third.sampleStart = 700;
    third.sampleEndExclusive = 1000;
    program.zones.push_back(third);

    const auto newZoneIndex = audiocity::plugin::splitProgramSliceAtSample(program, 450);
    if (newZoneIndex != 2 || program.zones.size() != 4)
        return false;

    if (program.zones[0].sampleStart != 0
        || program.zones[0].sampleEndExclusive != 300
        || program.zones[0].keyRange.low != 36
        || program.zones[0].rootMidiNote != 36
        || program.zones[1].sampleStart != 300
        || program.zones[1].sampleEndExclusive != 450
        || program.zones[1].keyRange.low != 37
        || program.zones[1].rootMidiNote != 37
        || program.zones[2].sampleStart != 450
        || program.zones[2].sampleEndExclusive != 700
        || program.zones[2].keyRange.low != 38
        || program.zones[2].rootMidiNote != 38
        || program.zones[3].sampleStart != 700
        || program.zones[3].sampleEndExclusive != 1000
        || program.zones[3].keyRange.low != 39
        || program.zones[3].rootMidiNote != 39)
    {
        return false;
    }

    if (program.groups[0].keyRange.low != 36 || program.groups[0].keyRange.high != 39)
        return false;

    return audiocity::plugin::splitProgramSliceAtSample(program, 300) < 0;
}

bool runProgramSliceMergeAtBoundaryTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "merge.wav";
    sample.lengthSamples = 1000;
    sample.numChannels = 1;
    sample.sampleRateHz = 48000.0;
    program.sampleAssets.push_back(sample);

    Group group;
    group.keyRange = MidiRange::fromUnordered(36, 38);
    group.velocityRange = VelocityRange::full();
    program.groups.push_back(group);

    Zone first;
    first.groupIndex = 0;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::single(36);
    first.velocityRange = VelocityRange::full();
    first.rootMidiNote = 36;
    first.sampleStart = 0;
    first.sampleEndExclusive = 300;
    first.triggerMode = ZoneTriggerMode::oneShot;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::single(37);
    second.rootMidiNote = 37;
    second.sampleStart = 300;
    second.sampleEndExclusive = 700;
    program.zones.push_back(second);

    Zone third = first;
    third.keyRange = MidiRange::single(38);
    third.rootMidiNote = 38;
    third.sampleStart = 700;
    third.sampleEndExclusive = 1000;
    program.zones.push_back(third);

    const auto mergedZoneIndex = audiocity::plugin::mergeProgramSlicesAtSampleBoundary(program, 300);
    if (mergedZoneIndex != 0 || program.zones.size() != 2)
        return false;

    if (program.zones[0].sampleStart != 0
        || program.zones[0].sampleEndExclusive != 700
        || program.zones[0].keyRange.low != 36
        || program.zones[0].keyRange.high != 36
        || program.zones[0].rootMidiNote != 36
        || program.zones[1].sampleStart != 700
        || program.zones[1].sampleEndExclusive != 1000
        || program.zones[1].keyRange.low != 37
        || program.zones[1].keyRange.high != 37
        || program.zones[1].rootMidiNote != 37)
    {
        return false;
    }

    if (program.groups[0].keyRange.low != 36 || program.groups[0].keyRange.high != 37)
        return false;

    return audiocity::plugin::mergeProgramSlicesAtSampleBoundary(program, 450) < 0;
}

bool runProgramMappingDeriveRootNotesTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "roots.wav";
    sample.lengthSamples = 512;
    sample.numChannels = 1;
    sample.sampleRateHz = 48000.0;
    program.sampleAssets.push_back(sample);

    Zone first;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::fromUnordered(36, 43);
    first.velocityRange = VelocityRange::full();
    first.rootMidiNote = 60;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::single(60);
    second.rootMidiNote = 90;
    program.zones.push_back(second);

    Zone third = first;
    third.keyRange = MidiRange::fromUnordered(72, 79);
    third.rootMidiNote = 48;
    program.zones.push_back(third);

    if (!audiocity::plugin::deriveProgramZoneRootNotesFromKeyRanges(program, { 0, 2 }))
        return false;

    return program.zones[0].rootMidiNote == 39
        && program.zones[1].rootMidiNote == 90
        && program.zones[2].rootMidiNote == 75
        && !audiocity::plugin::deriveProgramZoneRootNotesFromKeyRanges(program, { 99 });
}

bool runProgramMappingMapToRootNotesTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "map_to_root.wav";
    sample.lengthSamples = 512;
    sample.numChannels = 1;
    sample.sampleRateHz = 48000.0;
    program.sampleAssets.push_back(sample);

    Group group;
    group.keyRange = MidiRange::fromUnordered(40, 60);
    group.velocityRange = VelocityRange::full();
    program.groups.push_back(group);

    Zone first;
    first.groupIndex = 0;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::fromUnordered(40, 44);
    first.velocityRange = VelocityRange::full();
    first.rootMidiNote = 42;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::fromUnordered(50, 52);
    second.rootMidiNote = 51;
    program.zones.push_back(second);

    Zone third = first;
    third.keyRange = MidiRange::fromUnordered(58, 60);
    third.rootMidiNote = 59;
    program.zones.push_back(third);

    if (!audiocity::plugin::mapProgramZonesToRootNotes(program, { 2, 0 }))
        return false;

    if (program.zones[0].keyRange.low != 42
        || program.zones[0].keyRange.high != 42
        || program.zones[1].keyRange.low != 50
        || program.zones[1].keyRange.high != 52
        || program.zones[2].keyRange.low != 59
        || program.zones[2].keyRange.high != 59)
    {
        return false;
    }

    if (program.groups[0].keyRange.low != 42 || program.groups[0].keyRange.high != 59)
        return false;

    return !audiocity::plugin::mapProgramZonesToRootNotes(program, { 99 });
}

bool runProgramMappingCreateZoneTest()
{
    using namespace audiocity::engine;

    Program program;

    SampleAsset firstSample;
    firstSample.displayName = "first.wav";
    firstSample.lengthSamples = 512;
    firstSample.rootMidiNote = 60;
    program.sampleAssets.push_back(firstSample);

    SampleAsset secondSample;
    secondSample.displayName = "second.wav";
    secondSample.lengthSamples = 256;
    secondSample.rootMidiNote = 67;
    program.sampleAssets.push_back(secondSample);

    Group group;
    group.keyRange = MidiRange::fromUnordered(24, 96);
    group.velocityRange = VelocityRange::full();
    program.groups.push_back(group);

    const auto createdIndex = audiocity::plugin::createProgramZone(program);
    if (!(createdIndex == 0
        && program.zones.size() == 1
        && program.zones[0].sampleAssetIndex == 0
        && program.zones[0].groupIndex == 0
        && program.zones[0].keyRange.low == 60
        && program.zones[0].keyRange.high == 60
        && program.zones[0].velocityRange.low == 0
        && program.zones[0].velocityRange.high == 127
        && program.zones[0].sampleStart == 0
        && program.zones[0].sampleEndExclusive == 512))
    {
        return false;
    }

    program.zones[0].sampleAssetIndex = 1;
    program.zones[0].rootMidiNote = 69;
    program.zones[0].keyRange = MidiRange::fromUnordered(60, 72);
    program.zones[0].velocityRange = VelocityRange::fromUnordered(8, 110);
    program.zones[0].sampleStart = 12;
    program.zones[0].sampleEndExclusive = 200;
    program.zones[0].loopStart = 24;
    program.zones[0].loopEndExclusive = 144;
    program.zones[0].tuneCents = 12.5f;

    const auto seededIndex = audiocity::plugin::createProgramZone(program, 0);
    if (!(seededIndex == 1
        && program.zones.size() == 2
        && program.zones[1].sampleAssetIndex == 1
        && program.zones[1].groupIndex == 0
        && program.zones[1].rootMidiNote == 69
        && program.zones[1].keyRange.low == 69
        && program.zones[1].keyRange.high == 69
        && program.zones[1].velocityRange.low == 0
        && program.zones[1].velocityRange.high == 127
        && program.zones[1].sampleStart == 0
        && program.zones[1].sampleEndExclusive == 256
        && program.zones[1].loopStart == -1
        && program.zones[1].loopEndExclusive == -1
        && std::abs(program.zones[1].tuneCents - 12.5f) <= 1.0e-6f))
    {
        return false;
    }

    const auto explicitAssetIndex = audiocity::plugin::createProgramZoneForSampleAsset(program, 0, 0);
    if (!(explicitAssetIndex == 2
        && program.zones.size() == 3
        && program.zones[2].sampleAssetIndex == 0
        && program.zones[2].groupIndex == 0
        && program.zones[2].rootMidiNote == 69
        && program.zones[2].keyRange.low == 69
        && program.zones[2].keyRange.high == 69
        && program.zones[2].sampleEndExclusive == 512))
    {
        return false;
    }

    Program emptyProgram;
    return audiocity::plugin::createProgramZone(emptyProgram) < 0;
}

bool runProgramMappingAtomicBatchEditRollbackTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "batch.wav";
    sample.lengthSamples = 512;
    program.sampleAssets.push_back(sample);

    Zone first;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::single(60);
    first.velocityRange = VelocityRange::full();
    first.rootMidiNote = 60;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::single(61);
    second.rootMidiNote = 61;
    program.zones.push_back(second);

    const auto beforeFailure = audiocity::plugin::createProgramZoneMappingState(program);

    std::vector<audiocity::plugin::ProgramZoneEdit> failingEdits;
    audiocity::plugin::ProgramZoneEdit firstFailingEdit;
    firstFailingEdit.zoneIndex = 0;
    firstFailingEdit.keyLow = 48;
    firstFailingEdit.keyHigh = 52;
    firstFailingEdit.velocityLow = 8;
    firstFailingEdit.velocityHigh = 96;
    firstFailingEdit.rootMidiNote = 50;
    failingEdits.push_back(firstFailingEdit);
    audiocity::plugin::ProgramZoneEdit secondFailingEdit;
    secondFailingEdit.zoneIndex = 99;
    secondFailingEdit.keyLow = 0;
    secondFailingEdit.keyHigh = 127;
    secondFailingEdit.velocityLow = 0;
    secondFailingEdit.velocityHigh = 127;
    secondFailingEdit.rootMidiNote = 60;
    failingEdits.push_back(secondFailingEdit);

    if (audiocity::plugin::applyProgramZoneEditsAtomic(program, failingEdits))
        return false;

    const auto afterFailure = audiocity::plugin::createProgramZoneMappingState(program);
    if (!beforeFailure.isEquivalentTo(afterFailure))
        return false;

    std::vector<audiocity::plugin::ProgramZoneEdit> successfulEdits;
    audiocity::plugin::ProgramZoneEdit firstSuccessfulEdit;
    firstSuccessfulEdit.zoneIndex = 0;
    firstSuccessfulEdit.keyLow = 48;
    firstSuccessfulEdit.keyHigh = 52;
    firstSuccessfulEdit.velocityLow = 8;
    firstSuccessfulEdit.velocityHigh = 96;
    firstSuccessfulEdit.rootMidiNote = 50;
    successfulEdits.push_back(firstSuccessfulEdit);
    audiocity::plugin::ProgramZoneEdit secondSuccessfulEdit;
    secondSuccessfulEdit.zoneIndex = 1;
    secondSuccessfulEdit.keyLow = 53;
    secondSuccessfulEdit.keyHigh = 57;
    secondSuccessfulEdit.velocityLow = 16;
    secondSuccessfulEdit.velocityHigh = 100;
    secondSuccessfulEdit.rootMidiNote = 55;
    successfulEdits.push_back(secondSuccessfulEdit);

    if (!audiocity::plugin::applyProgramZoneEditsAtomic(program, successfulEdits))
        return false;

    return program.zones[0].keyRange.low == 48
        && program.zones[0].keyRange.high == 52
        && program.zones[0].velocityRange.low == 8
        && program.zones[0].velocityRange.high == 96
        && program.zones[0].rootMidiNote == 50
        && program.zones[1].keyRange.low == 53
        && program.zones[1].keyRange.high == 57
        && program.zones[1].velocityRange.low == 16
        && program.zones[1].velocityRange.high == 100
        && program.zones[1].rootMidiNote == 55;
}

bool runProgramMappingAtomicBatchDeleteRollbackTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "batch_delete.wav";
    sample.lengthSamples = 512;
    program.sampleAssets.push_back(sample);

    Zone first;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::single(60);
    first.rootMidiNote = 60;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::single(61);
    second.rootMidiNote = 61;
    program.zones.push_back(second);

    Zone third = first;
    third.keyRange = MidiRange::single(62);
    third.rootMidiNote = 62;
    program.zones.push_back(third);

    const auto beforeFailure = audiocity::plugin::createProgramZoneMappingState(program);
    if (audiocity::plugin::deleteProgramZonesAtomic(program, { 2, 99 }))
        return false;

    const auto afterFailure = audiocity::plugin::createProgramZoneMappingState(program);
    if (!beforeFailure.isEquivalentTo(afterFailure))
        return false;

    if (!audiocity::plugin::deleteProgramZonesAtomic(program, { 2, 0 }))
        return false;

    return program.zones.size() == 1
        && program.zones.front().rootMidiNote == 61;
}

bool runProgramMappingStateRoundTripTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    SampleAsset sample;
    sample.displayName = "roundtrip.wav";
    sample.lengthSamples = 240;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    group.gainDb = -3.0f;
    group.pan = 0.25f;
    group.roundRobinGroup = 4;
    group.chokeGroup = 2;
    group.triggerMode = ZoneTriggerMode::gate;
    baseProgram.groups.push_back(group);

    Zone zone;
    zone.groupIndex = 0;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(36, 40);
    zone.velocityRange = VelocityRange::fromUnordered(24, 100);
    zone.velocityFadeIn = VelocityFadeRange::fromUnordered(20, 60);
    zone.velocityFadeOut = VelocityFadeRange::fromUnordered(90, 120);
    zone.rootMidiNote = 38;
    zone.sampleStart = 8;
    zone.sampleEndExclusive = 160;
    zone.loopStart = 20;
    zone.loopEndExclusive = 80;
    zone.gainDb = 1.5f;
    zone.pan = -0.5f;
    zone.tuneCents = 7.0f;
    zone.roundRobinGroup = 9;
    zone.roundRobinPosition = 2;
    zone.roundRobinMode = RoundRobinMode::cycleRandom;
    zone.triggerMode = ZoneTriggerMode::oneShot;
    zone.chokeGroup = 11;
    zone.loopMode = ZoneLoopMode::sustain;
    baseProgram.zones.push_back(zone);

    Program editedProgram = baseProgram;
    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = 41;
    edit.keyHigh = 45;
    edit.velocityLow = 32;
    edit.velocityHigh = 96;
    edit.velocityFadeInLow = 40;
    edit.velocityFadeInHigh = 72;
    edit.velocityFadeOutLow = -1;
    edit.velocityFadeOutHigh = -1;
    edit.rootMidiNote = 43;
    edit.sampleStart = 24;
    edit.sampleEnd = 180;
    edit.loopStart = 48;
    edit.loopEnd = 120;
    edit.gainDb = -1.0f;
    edit.pan = -0.25f;
    edit.roundRobinGroup = 7;
    edit.roundRobinPosition = 3;
    edit.roundRobinMode = RoundRobinMode::ordered;
    edit.chokeGroupId = 6;
    edit.triggerMode = ZoneTriggerMode::release;
    edit.loopMode = ZoneLoopMode::continuous;
    edit.hasSampleStart = true;
    edit.hasSampleEnd = true;
    edit.hasLoopStart = true;
    edit.hasLoopEnd = true;
    edit.hasVelocityFadeIn = true;
    edit.hasVelocityFadeOut = true;
    edit.hasGainDb = true;
    edit.hasPan = true;
    edit.hasRoundRobinGroup = true;
    edit.hasRoundRobinPosition = true;
    edit.hasRoundRobinMode = true;
    edit.hasChokeGroupId = true;
    edit.hasTriggerMode = true;
    edit.hasLoopMode = true;

    if (!audiocity::plugin::applyProgramZoneEdit(editedProgram, edit))
        return false;

    const auto state = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    if (!state.isValid() || !state.hasType(audiocity::plugin::programZoneMappingStateType()))
        return false;

    Program restoredProgram = baseProgram;
    if (!audiocity::plugin::restoreProgramZoneStructureFromState(restoredProgram, state))
        return false;

    const auto editedRows = audiocity::plugin::buildProgramZoneListRows(editedProgram);
    const auto restoredRows = audiocity::plugin::buildProgramZoneListRows(restoredProgram);
    if (editedRows.size() != 1 || restoredRows.size() != 1)
        return false;

    const auto& editedZone = editedProgram.zones.front();
    const auto& restoredZone = restoredProgram.zones.front();
    const auto& editedRow = editedRows.front();
    const auto& restoredRow = restoredRows.front();
    return editedZone.sampleAssetIndex == restoredZone.sampleAssetIndex
        && editedZone.groupIndex == restoredZone.groupIndex
        && editedZone.velocityFadeIn.low == restoredZone.velocityFadeIn.low
        && editedZone.velocityFadeIn.high == restoredZone.velocityFadeIn.high
        && editedZone.velocityFadeOut.low == restoredZone.velocityFadeOut.low
        && editedZone.velocityFadeOut.high == restoredZone.velocityFadeOut.high
        && std::abs(editedZone.gainDb - restoredZone.gainDb) <= 1.0e-6f
        && std::abs(editedZone.pan - restoredZone.pan) <= 1.0e-6f
        && std::abs(editedZone.tuneCents - restoredZone.tuneCents) <= 1.0e-6f
        && editedZone.roundRobinGroup == restoredZone.roundRobinGroup
        && editedZone.roundRobinPosition == restoredZone.roundRobinPosition
        && editedZone.roundRobinMode == restoredZone.roundRobinMode
        && editedZone.triggerMode == restoredZone.triggerMode
        && editedZone.chokeGroup == restoredZone.chokeGroup
        && editedZone.loopMode == restoredZone.loopMode
        && editedRow.keyLow == restoredRow.keyLow
        && editedRow.keyHigh == restoredRow.keyHigh
        && editedRow.velocityLow == restoredRow.velocityLow
        && editedRow.velocityHigh == restoredRow.velocityHigh
        && editedRow.velocityFadeInLow == restoredRow.velocityFadeInLow
        && editedRow.velocityFadeInHigh == restoredRow.velocityFadeInHigh
        && editedRow.velocityFadeOutLow == restoredRow.velocityFadeOutLow
        && editedRow.velocityFadeOutHigh == restoredRow.velocityFadeOutHigh
        && editedRow.rootMidiNote == restoredRow.rootMidiNote
        && editedRow.sampleStart == restoredRow.sampleStart
        && editedRow.sampleEnd == restoredRow.sampleEnd
        && editedRow.loopStart == restoredRow.loopStart
        && editedRow.loopEnd == restoredRow.loopEnd
        && std::abs(editedRow.gainDbValue - restoredRow.gainDbValue) <= 1.0e-6f
        && std::abs(editedRow.panValue - restoredRow.panValue) <= 1.0e-6f
        && editedRow.roundRobinGroup == restoredRow.roundRobinGroup
        && editedRow.roundRobinPosition == restoredRow.roundRobinPosition
        && editedRow.roundRobinModeValue == restoredRow.roundRobinModeValue
        && editedRow.chokeGroupId == restoredRow.chokeGroupId
        && editedRow.triggerModeValue == restoredRow.triggerModeValue
        && editedRow.loopModeValue == restoredRow.loopModeValue;
}

bool runProgramMappingStructuralStateRoundTripTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    SampleAsset sample;
    sample.displayName = "structural.wav";
    sample.lengthSamples = 512;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    group.roundRobinGroup = 3;
    group.roundRobinMode = RoundRobinMode::ordered;
    group.velocityRange = VelocityRange::full();
    baseProgram.groups.push_back(group);

    Zone first;
    first.groupIndex = 0;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::fromUnordered(36, 40);
    first.velocityRange = VelocityRange::fromUnordered(10, 90);
    first.rootMidiNote = 38;
    first.roundRobinGroup = 3;
    first.roundRobinPosition = 1;
    baseProgram.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::fromUnordered(41, 48);
    second.rootMidiNote = 45;
    second.roundRobinPosition = 2;
    second.roundRobinMode = RoundRobinMode::cycleRandom;
    second.loopMode = ZoneLoopMode::sustain;
    baseProgram.zones.push_back(second);

    Program editedProgram = baseProgram;
    if (audiocity::plugin::duplicateProgramZone(editedProgram, 0) != 2)
        return false;

    if (audiocity::plugin::splitProgramZoneByKey(editedProgram, 2) != 3)
        return false;

    if (!audiocity::plugin::deleteProgramZone(editedProgram, 1))
        return false;

    editedProgram.zones[1].roundRobinMode = RoundRobinMode::cycleRandom;
    editedProgram.zones[1].roundRobinPosition = 4;
    editedProgram.zones[2].velocityFadeIn = VelocityFadeRange::fromUnordered(18, 64);
    editedProgram.zones[2].velocityFadeOut = VelocityFadeRange::fromUnordered(88, 118);

    const auto state = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    if (!state.isValid())
        return false;

    Program restoredProgram = baseProgram;
    if (!audiocity::plugin::restoreProgramZoneStructureFromState(restoredProgram, state))
        return false;

    const auto restoredState = audiocity::plugin::createProgramZoneMappingState(restoredProgram);
    const auto editedRows = audiocity::plugin::buildProgramZoneListRows(editedProgram);
    const auto restoredRows = audiocity::plugin::buildProgramZoneListRows(restoredProgram);
    return editedRows.size() == 3
        && restoredRows.size() == editedRows.size()
        && state.toXmlString() == restoredState.toXmlString();
}

bool runImportedProgramStateSubtreeRoundTripTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    SampleAsset sample;
    sample.displayName = "imported.wav";
    sample.lengthSamples = 256;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    group.roundRobinGroup = 5;
    group.roundRobinMode = RoundRobinMode::ordered;
    group.triggerMode = ZoneTriggerMode::gate;
    baseProgram.groups.push_back(group);

    Zone zone;
    zone.groupIndex = 0;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(48, 52);
    zone.velocityRange = VelocityRange::fromUnordered(20, 110);
    zone.rootMidiNote = 50;
    zone.roundRobinGroup = 5;
    zone.roundRobinPosition = 3;
    zone.roundRobinLength = 4;
    zone.triggerMode = ZoneTriggerMode::release;
    baseProgram.zones.push_back(zone);

    const auto mappingState = audiocity::plugin::createProgramZoneMappingState(baseProgram);
    juce::ValueTree patchState("patch");
    const juce::String importedProgramPath = "C:/Library/Kits/ImportedKit.rx2";
    audiocity::plugin::appendImportedProgramState(patchState, importedProgramPath, mappingState);

    if (audiocity::plugin::readImportedProgramStatePath(patchState) != importedProgramPath
        || audiocity::plugin::detectImportedProgramFormat(importedProgramPath)
            != audiocity::plugin::ImportedProgramFormat::rex
        || audiocity::plugin::importedProgramFormatBadge(importedProgramPath) != "REX"
        || audiocity::plugin::importedProgramFormatDescription(importedProgramPath) != "REX loop")
    {
        return false;
    }

    juce::ValueTree slicePatchState("patch");
    const juce::String sliceProgramPath = "C:/Library/Loops/Break.wav";
    audiocity::plugin::appendImportedProgramState(slicePatchState,
                                                  sliceProgramPath,
                                                  mappingState,
                                                  audiocity::plugin::ImportedProgramFormat::sampleSlices);
    if (audiocity::plugin::readImportedProgramStatePath(slicePatchState) != sliceProgramPath
        || audiocity::plugin::readImportedProgramStateFormat(slicePatchState)
            != audiocity::plugin::ImportedProgramFormat::sampleSlices
        || audiocity::plugin::detectImportedProgramFormat(sliceProgramPath)
            != audiocity::plugin::ImportedProgramFormat::unknown
        || audiocity::plugin::importedProgramFormatBadge(
               audiocity::plugin::ImportedProgramFormat::sampleSlices)
            != "SLICE")
    {
        return false;
    }

    juce::ValueTree nkiPatchState("patch");
    const juce::String nkiProgramPath = "C:/Library/Kits/LegacyKit.nki";
    audiocity::plugin::appendImportedProgramState(nkiPatchState,
                                                  nkiProgramPath,
                                                  mappingState,
                                                  audiocity::plugin::ImportedProgramFormat::nki);
    if (audiocity::plugin::readImportedProgramStatePath(nkiPatchState) != nkiProgramPath
        || audiocity::plugin::readImportedProgramStateFormat(nkiPatchState)
            != audiocity::plugin::ImportedProgramFormat::nki
        || audiocity::plugin::detectImportedProgramFormat(nkiProgramPath)
            != audiocity::plugin::ImportedProgramFormat::nki
        || audiocity::plugin::importedProgramFormatBadge(audiocity::plugin::ImportedProgramFormat::nki)
            != "NKI")
    {
        return false;
    }

    juce::ValueTree sf2PatchState("patch");
    const juce::String sf2ProgramPath = "C:/Library/Banks/Legacy.sf2";
    audiocity::plugin::appendImportedProgramState(sf2PatchState,
                                                  sf2ProgramPath,
                                                  mappingState,
                                                  audiocity::plugin::ImportedProgramFormat::sf2,
                                                  1);
    if (audiocity::plugin::readImportedProgramStatePath(sf2PatchState) != sf2ProgramPath
        || audiocity::plugin::readImportedProgramStateFormat(sf2PatchState)
            != audiocity::plugin::ImportedProgramFormat::sf2
        || audiocity::plugin::readImportedProgramStateSelectionIndex(sf2PatchState) != 1
        || audiocity::plugin::importedProgramFormatBadge(audiocity::plugin::ImportedProgramFormat::sf2)
            != "SF2")
    {
        return false;
    }

    juce::ValueTree legacyPatchState("patch");
    legacyPatchState.setProperty("sfzProgramPath", "C:/Library/Kits/LegacyImport.sfz", nullptr);
    if (audiocity::plugin::readImportedProgramStatePath(legacyPatchState) != "C:/Library/Kits/LegacyImport.sfz"
        || audiocity::plugin::readImportedProgramStateFormat(legacyPatchState)
            != audiocity::plugin::ImportedProgramFormat::sfz
        || audiocity::plugin::readImportedProgramStateSelectionIndex(legacyPatchState) != -1)
    {
        return false;
    }

    const auto extractedMappingState = audiocity::plugin::readImportedProgramMappingState(patchState);
    if (!extractedMappingState.isValid()
        || !extractedMappingState.hasType(audiocity::plugin::programZoneMappingStateType()))
    {
        return false;
    }

    Program restoredProgram = baseProgram;
    restoredProgram.zones.front().roundRobinLength = 0;
    restoredProgram.zones.front().triggerMode = ZoneTriggerMode::gate;
    if (!audiocity::plugin::restoreProgramZoneStructureFromState(restoredProgram, extractedMappingState))
        return false;

    return restoredProgram.zones.front().roundRobinLength == 4
        && restoredProgram.zones.front().triggerMode == ZoneTriggerMode::release
        && extractedMappingState.toXmlString()
            == audiocity::plugin::createProgramZoneMappingState(restoredProgram).toXmlString();
}

bool runImportedProgramStateLegacyReplayFallbackTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    SampleAsset sample;
    sample.displayName = "legacy.wav";
    sample.lengthSamples = 512;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    group.roundRobinGroup = 2;
    group.roundRobinMode = RoundRobinMode::ordered;
    baseProgram.groups.push_back(group);

    Zone zone;
    zone.groupIndex = 0;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(36, 42);
    zone.velocityRange = VelocityRange::fromUnordered(16, 96);
    zone.rootMidiNote = 40;
    baseProgram.zones.push_back(zone);

    Program editedProgram = baseProgram;
    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = 48;
    edit.keyHigh = 60;
    edit.velocityLow = 20;
    edit.velocityHigh = 110;
    edit.velocityFadeInLow = 24;
    edit.velocityFadeInHigh = 64;
    edit.velocityFadeOutLow = 100;
    edit.velocityFadeOutHigh = 120;
    edit.rootMidiNote = 52;
    edit.sampleStart = 0;
    edit.sampleEnd = 511;
    edit.gainDb = -4.0f;
    edit.pan = -0.35f;
    edit.roundRobinGroup = 7;
    edit.roundRobinPosition = 3;
    edit.roundRobinMode = RoundRobinMode::cycleRandom;
    edit.chokeGroupId = 5;
    edit.triggerMode = ZoneTriggerMode::release;
    edit.loopMode = ZoneLoopMode::continuous;
    edit.hasVelocityFadeIn = true;
    edit.hasVelocityFadeOut = true;
    edit.hasSampleStart = true;
    edit.hasSampleEnd = true;
    edit.hasGainDb = true;
    edit.hasPan = true;
    edit.hasRoundRobinGroup = true;
    edit.hasRoundRobinPosition = true;
    edit.hasRoundRobinMode = true;
    edit.hasChokeGroupId = true;
    edit.hasTriggerMode = true;
    edit.hasLoopMode = true;

    if (!audiocity::plugin::applyProgramZoneEdit(editedProgram, edit))
        return false;

    auto legacyState = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    legacyState.setProperty("formatVersion", 1, nullptr);

    Program restoredProgram = baseProgram;
    if (!audiocity::plugin::restoreImportedProgramMappingState(restoredProgram, legacyState))
        return false;

    const auto editedState = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    const auto restoredState = audiocity::plugin::createProgramZoneMappingState(restoredProgram);
    return editedState.toXmlString() == restoredState.toXmlString();
}

bool runImportedProgramRestoreResultSuccessTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    baseProgram.name = "Restore Result";

    SampleAsset sample;
    sample.displayName = "restore.wav";
    sample.lengthSamples = 512;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    group.roundRobinGroup = 4;
    group.roundRobinMode = RoundRobinMode::ordered;
    baseProgram.groups.push_back(group);

    Zone zone;
    zone.groupIndex = 0;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(36, 48);
    zone.velocityRange = VelocityRange::fromUnordered(20, 80);
    zone.rootMidiNote = 40;
    baseProgram.zones.push_back(zone);

    Program editedProgram = baseProgram;
    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = 48;
    edit.keyHigh = 60;
    edit.velocityLow = 30;
    edit.velocityHigh = 110;
    edit.rootMidiNote = 52;
    edit.roundRobinGroup = 6;
    edit.roundRobinPosition = 2;
    edit.triggerMode = ZoneTriggerMode::release;
    edit.hasRoundRobinGroup = true;
    edit.hasRoundRobinPosition = true;
    edit.hasTriggerMode = true;

    if (!audiocity::plugin::applyProgramZoneEdit(editedProgram, edit))
        return false;

    const auto mappingState = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    const auto restoredResult = audiocity::plugin::buildImportedProgramRestoreResult(baseProgram, mappingState);
    if (!restoredResult.has_value())
        return false;

    return restoredResult->hasPublishableZones
        && restoredResult->program.zones.size() == 1
        && restoredResult->program.zones.front().keyRange.low == 48
        && restoredResult->program.zones.front().keyRange.high == 60
        && restoredResult->program.zones.front().triggerMode == ZoneTriggerMode::release
        && restoredResult->derivedState.zoneRows.size() == 1
        && restoredResult->derivedState.zoneRows.front().keyRange == "48-60"
        && restoredResult->derivedState.mapSummary.contains("Program: Restore Result");
}

bool runImportedProgramRestoreResultAtomicFailureTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    SampleAsset sample;
    sample.displayName = "atomic.wav";
    sample.lengthSamples = 512;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    baseProgram.groups.push_back(group);

    Zone firstZone;
    firstZone.groupIndex = 0;
    firstZone.sampleAssetIndex = 0;
    firstZone.keyRange = MidiRange::fromUnordered(24, 36);
    firstZone.velocityRange = VelocityRange::fromUnordered(1, 64);
    baseProgram.zones.push_back(firstZone);

    Zone secondZone = firstZone;
    secondZone.keyRange = MidiRange::fromUnordered(37, 48);
    secondZone.velocityRange = VelocityRange::fromUnordered(65, 127);
    baseProgram.zones.push_back(secondZone);

    Program editedProgram = baseProgram;
    audiocity::plugin::ProgramZoneEdit firstEdit;
    firstEdit.zoneIndex = 0;
    firstEdit.keyLow = 30;
    firstEdit.keyHigh = 40;
    firstEdit.velocityLow = 10;
    firstEdit.velocityHigh = 70;
    firstEdit.rootMidiNote = 34;

    audiocity::plugin::ProgramZoneEdit secondEdit;
    secondEdit.zoneIndex = 1;
    secondEdit.keyLow = 50;
    secondEdit.keyHigh = 72;
    secondEdit.velocityLow = 71;
    secondEdit.velocityHigh = 120;
    secondEdit.rootMidiNote = 60;

    if (!audiocity::plugin::applyProgramZoneEdit(editedProgram, firstEdit)
        || !audiocity::plugin::applyProgramZoneEdit(editedProgram, secondEdit))
    {
        return false;
    }

    auto legacyState = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    legacyState.setProperty("formatVersion", 1, nullptr);
    if (legacyState.getNumChildren() < 2)
        return false;

    legacyState.getChild(1).setProperty("zoneIndex", 99, nullptr);
    const auto baseStateXml = audiocity::plugin::createProgramZoneMappingState(baseProgram).toXmlString();
    const auto restoredResult = audiocity::plugin::buildImportedProgramRestoreResult(baseProgram, legacyState);
    const auto afterAttemptXml = audiocity::plugin::createProgramZoneMappingState(baseProgram).toXmlString();

    return !restoredResult.has_value()
        && baseStateXml == afterAttemptXml;
}

bool runImportedProgramDerivedStateSummaryTest()
{
    using namespace audiocity::engine;

    Program program;
    program.name = "Imported Summary";

    SampleAsset sample;
    sample.displayName = "summary.wav";
    sample.lengthSamples = 1024;
    program.sampleAssets.push_back(sample);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(60, 67);
    zone.velocityRange = VelocityRange::fromUnordered(10, 120);
    zone.rootMidiNote = 64;
    zone.roundRobinGroup = 3;
    zone.roundRobinPosition = 2;
    zone.triggerMode = ZoneTriggerMode::release;
    zone.loopMode = ZoneLoopMode::sustain;
    zone.chokeGroup = 8;
    program.zones.push_back(zone);

    const auto derivedState = audiocity::plugin::buildImportedProgramDerivedState(program);
    return derivedState.zoneRows.size() == 1
        && derivedState.zoneRows.front().sampleName == "summary.wav"
        && derivedState.mapSummary.contains("Program: Imported Summary")
        && derivedState.mapSummary.contains("1. summary.wav")
        && derivedState.mapSummary.contains("key 60-67")
        && derivedState.mapSummary.contains("vel 10-120")
        && derivedState.mapSummary.contains("release")
        && derivedState.mapSummary.contains("loop sustain")
        && derivedState.mapSummary.contains("rr 3:2")
        && derivedState.mapSummary.contains("choke 8");
}

bool runSfzImportIncludeDefineDefaultPathTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_import_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto samplesDirectory = tempDirectory.getChildFile("Samples");
    if (!samplesDirectory.createDirectory())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto sampleFile = samplesDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto includeFile = tempDirectory.getChildFile("regions.sfz");
    const auto rootFile = tempDirectory.getChildFile("root.sfz");

    if (!includeFile.replaceWithText(
            "<group> key=60 lovel=10 hivel=120 pan=-50\n"
            "<region> sample=Tone.wav pitch_keycenter=60 tune=7 transpose=1 offset=4 end=120 loop_start=16 loop_end=31 loop_mode=loop_sustain\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    if (!rootFile.replaceWithText(
            "#define $SAMPLE_DIR Samples\n"
            "<control> default_path=$SAMPLE_DIR/\n"
            "#include \"regions.sfz\"\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(rootFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors())
        return false;

    const auto& program = result.program;
    if (program.sampleAssets.size() != 1
        || result.sampleDataByAsset.size() != 1
        || program.groups.size() != 1
        || program.zones.size() != 1)
    {
        return false;
    }

    const auto& asset = program.sampleAssets[0];
    if (asset.lengthSamples != sampleLength
        || asset.numChannels != 1
        || std::abs(asset.sampleRateHz - sampleRate) > 0.01
        || asset.rootMidiNote != 60)
    {
        return false;
    }

    if (result.sampleDataByAsset[0].getNumChannels() != 1
        || result.sampleDataByAsset[0].getNumSamples() != sampleLength)
    {
        return false;
    }

    const auto& group = program.groups[0];
    if (group.keyRange.low != 60
        || group.keyRange.high != 60
        || group.velocityRange.low != 10
        || group.velocityRange.high != 120
        || std::abs(group.pan - (-0.5f)) > 1.0e-6f)
    {
        return false;
    }

    const auto& zone = program.zones[0];
    if (!(zone.sampleAssetIndex == 0
        && zone.groupIndex == 0
        && zone.keyRange.low == 60
        && zone.keyRange.high == 60
        && zone.velocityRange.low == 10
        && zone.velocityRange.high == 120
        && zone.rootMidiNote == 60
        && zone.sampleStart == 4
        && zone.sampleEndExclusive == 121
        && zone.loopStart == 16
        && zone.loopEndExclusive == 32
        && std::abs(zone.tuneCents - 107.0f) <= 1.0e-6f
        && zone.loopMode == ZoneLoopMode::sustain))
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, 128, 2);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);
    engine.setProgram(program, result.sampleDataByAsset);

    juce::AudioBuffer<float> block(2, 128);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    engine.render(block, midi);

    auto energy = 0.0f;
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const auto* data = block.getReadPointer(channel);
        for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); ++sampleIndex)
            energy += std::abs(data[sampleIndex]);
    }

    return engine.activeVoiceCount() == 1 && energy > 0.01f;
}

bool runSfzImportRoundRobinPlaybackTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_rr_import_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("rr.sfz");
    if (!sfzFile.replaceWithText(
            "<group> key=60\n"
            "<region> sample=Tone.wav seq_position=2\n"
            "<region> sample=Tone.wav seq_position=1\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.zones.size() != 2
        || result.sampleDataByAsset.size() != 1
        || result.program.zones[0].roundRobinPosition != 2
        || result.program.zones[1].roundRobinPosition != 1
        || result.program.zones[0].roundRobinGroup <= 0
        || result.program.zones[0].roundRobinGroup != result.program.zones[1].roundRobinGroup)
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);
    engine.setProgram(result.program, result.sampleDataByAsset);

    auto triggerAndReadZone = [&]()
    {
        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        auto selectedZone = -1;
        const auto states = engine.getVoicePlaybackStates();
        for (const auto& state : states)
        {
            if (state.active)
            {
                selectedZone = state.zoneIndex;
                break;
            }
        }

        engine.panic();
        return selectedZone;
    };

    const auto firstSelected = triggerAndReadZone();
    const auto secondSelected = triggerAndReadZone();
    const auto thirdSelected = triggerAndReadZone();
    if (firstSelected != 1 || secondSelected != 0 || thirdSelected != 1)
    {
        std::fprintf(stderr, "SFZ round-robin zones: %d, %d, %d\n",
                     firstSelected, secondSelected, thirdSelected);
        return false;
    }

    return true;
}

bool runSfzImportSeqModeRandomTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_seq_mode_random_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("seq_mode_random.sfz");
    if (!sfzFile.replaceWithText(
            "<group> key=60 seq_mode=random\n"
            "<region> sample=Tone.wav\n"
            "<region> sample=Tone.wav tune=50\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.groups.size() != 1
        || result.program.zones.size() != 2
        || result.program.groups[0].roundRobinMode != RoundRobinMode::cycleRandom
        || result.program.groups[0].roundRobinGroup != 1
        || result.program.zones[0].roundRobinMode != RoundRobinMode::cycleRandom
        || result.program.zones[1].roundRobinMode != RoundRobinMode::cycleRandom
        || result.program.zones[0].roundRobinGroup != 1
        || result.program.zones[1].roundRobinGroup != 1)
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);
    engine.setProgram(result.program, result.sampleDataByAsset);

    auto triggerAndReadZone = [&]()
    {
        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        auto selectedZone = -1;
        const auto states = engine.getVoicePlaybackStates();
        for (const auto& state : states)
        {
            if (state.active)
            {
                selectedZone = state.zoneIndex;
                break;
            }
        }

        engine.panic();
        return selectedZone;
    };

    const auto firstZone = triggerAndReadZone();
    const auto secondZone = triggerAndReadZone();
    return firstZone >= 0
        && secondZone >= 0
        && firstZone != secondZone;
}

bool runSfzImportSeqLengthPlaybackTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 44100.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_seq_length_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("seq_length.sfz");
    if (!sfzFile.replaceWithText(
            "<group> key=60 seq_length=4\n"
            "<region> sample=Tone.wav seq_position=1\n"
            "<region> sample=Tone.wav seq_position=3\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.zones.size() != 2
        || result.program.zones[0].roundRobinLength != 4
        || result.program.zones[1].roundRobinLength != 4)
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);
    engine.setProgram(result.program, result.sampleDataByAsset);

    auto triggerAndReadZone = [&]()
    {
        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        auto selectedZone = -1;
        const auto states = engine.getVoicePlaybackStates();
        for (const auto& state : states)
        {
            if (state.active)
            {
                selectedZone = state.zoneIndex;
                break;
            }
        }

        engine.panic();
        return selectedZone;
    };

    return triggerAndReadZone() == 0
        && triggerAndReadZone() == -1
        && triggerAndReadZone() == 1
        && triggerAndReadZone() == -1
        && triggerAndReadZone() == 0;
}

bool runSfzImportReleaseTriggerPlaybackTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 44100.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_release_trigger_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("release.sfz");
    if (!sfzFile.replaceWithText(
            "<group> key=60\n"
            "<region> sample=Tone.wav trigger=release\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.zones.size() != 1
        || result.program.zones.front().triggerMode != ZoneTriggerMode::release)
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);
    engine.setProgram(result.program, result.sampleDataByAsset);

    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);
    if (engine.activeVoiceCount() != 0)
        return false;

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);
    return engine.activeVoiceCount() == 1 && engine.isNoteActive(60);
}

bool runSfzImportLoopContinuousPlaybackTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 128;
    constexpr int zoneStart = 32;
    constexpr int zoneEnd = 96;
    constexpr int loopStart = 48;
    constexpr int loopEnd = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_loop_continuous_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Loop.wav");
    {
        juce::AudioBuffer<float> sample(1, sampleLength);
        sample.clear();
        for (int index = loopStart; index < loopEnd; ++index)
            sample.setSample(0, index, 0.8f);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("loop_continuous.sfz");
    if (!sfzFile.replaceWithText(
            "<region> sample=Loop.wav key=60 offset=32 end=95 loop_start=48 loop_end=63 loop_mode=loop_continuous\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.zones.size() != 1
        || result.sampleDataByAsset.size() != 1)
    {
        return false;
    }

    const auto& zone = result.program.zones.front();
    if (zone.loopMode != ZoneLoopMode::continuous
        || zone.sampleStart != zoneStart
        || zone.sampleEndExclusive != zoneEnd
        || zone.loopStart != loopStart
        || zone.loopEndExclusive != loopEnd)
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.5f;
    engine.setAmpEnvelope(amp);

    EngineCore::FilterSettings openFilter;
    openFilter.baseCutoffHz = 20000.0f;
    openFilter.envAmountHz = 0.0f;
    openFilter.resonance = 0.0f;
    engine.setFilterSettings(openFilter);

    EngineCore::DcFilterSettings dc;
    dc.enabled = false;
    engine.setDcFilterSettings(dc);
    engine.setProgram(result.program, result.sampleDataByAsset);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    midi.clear();
    engine.render(block, midi);

    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);
    midi.clear();

    engine.render(block, midi);

    auto energy = 0.0f;
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const auto* data = block.getReadPointer(channel);
        for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); ++sampleIndex)
            energy += std::abs(data[sampleIndex]);
    }

    return energy > 50.0f;
}

bool runSfzImportOneShotPlaybackTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_one_shot_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("one_shot.sfz");
    if (!sfzFile.replaceWithText("<region> sample=Tone.wav key=60 loop_mode=one_shot\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.zones.size() != 1
        || result.sampleDataByAsset.size() != 1)
    {
        return false;
    }

    const auto& zone = result.program.zones.front();
    if (zone.triggerMode != ZoneTriggerMode::oneShot
        || zone.loopMode != ZoneLoopMode::noLoop)
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setProgram(result.program, result.sampleDataByAsset);

    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);
    if (engine.activeVoiceCount() != 1 || !engine.isNoteActive(60))
        return false;

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);
    if (engine.activeVoiceCount() != 1 || !engine.isNoteActive(60))
        return false;

    midi.clear();
    engine.render(block, midi);

    auto energy = 0.0f;
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const auto* data = block.getReadPointer(channel);
        for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); ++sampleIndex)
            energy += std::abs(data[sampleIndex]);
    }

    return energy > 0.01f;
}

bool runSfzImportGainPanTunePlaybackTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 1024;
    constexpr int blockSize = 256;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_gain_pan_tune_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("gain_pan_tune.sfz");
    if (!sfzFile.replaceWithText("<region> sample=Tone.wav key=60 volume=-6 pan=75 tune=12.5 transpose=1\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.zones.size() != 1
        || result.sampleDataByAsset.size() != 1)
    {
        return false;
    }

    const auto& zone = result.program.zones.front();
    if (std::abs(zone.gainDb - (-6.0f)) > 1.0e-6f
        || std::abs(zone.pan - 0.75f) > 1.0e-6f
        || std::abs(zone.tuneCents - 112.5f) > 1.0e-6f)
    {
        return false;
    }

    auto renderProgram = [&](const Program& program)
    {
        EngineCore engine;
        engine.prepare(sampleRate, blockSize, 2);
        engine.setProgram(program, result.sampleDataByAsset);

        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto importedBlock = renderProgram(result.program);

    auto neutralProgram = result.program;
    neutralProgram.zones.front().gainDb = 0.0f;
    neutralProgram.zones.front().pan = 0.0f;
    neutralProgram.zones.front().tuneCents = 0.0f;
    const auto neutralBlock = renderProgram(neutralProgram);

    auto untunedProgram = result.program;
    untunedProgram.zones.front().tuneCents = 0.0f;
    const auto untunedBlock = renderProgram(untunedProgram);

    auto channelEnergy = [](const juce::AudioBuffer<float>& buffer, const int channel)
    {
        auto energy = 0.0f;
        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
            energy += std::abs(buffer.getSample(channel, sampleIndex));
        return energy;
    };

    const auto importedLeftEnergy = channelEnergy(importedBlock, 0);
    const auto importedRightEnergy = channelEnergy(importedBlock, 1);
    const auto neutralTotalEnergy = channelEnergy(neutralBlock, 0) + channelEnergy(neutralBlock, 1);
    const auto importedTotalEnergy = importedLeftEnergy + importedRightEnergy;

    auto differenceFromUntuned = 0.0f;
    for (int channel = 0; channel < importedBlock.getNumChannels(); ++channel)
    {
        for (int sampleIndex = 0; sampleIndex < importedBlock.getNumSamples(); ++sampleIndex)
            differenceFromUntuned += std::abs(importedBlock.getSample(channel, sampleIndex)
                - untunedBlock.getSample(channel, sampleIndex));
    }

    return importedRightEnergy > 0.01f
        && importedRightEnergy > importedLeftEnergy * 1.5f
        && importedTotalEnergy < neutralTotalEnergy * 0.75f
        && differenceFromUntuned > 1.0f;
}

bool runSfzImportChokeGroupPlaybackTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_choke_group_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("choke.sfz");
    if (!sfzFile.replaceWithText(
            "<region> sample=Tone.wav key=60 off_by=9\n"
            "<region> sample=Tone.wav key=62 off_by=9\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.zones.size() != 2
        || result.sampleDataByAsset.size() != 1
        || result.program.zones[0].chokeGroup != 9
        || result.program.zones[1].chokeGroup != 9)
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setProgram(result.program, result.sampleDataByAsset);

    {
        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
    }

    if (engine.activeVoiceCount() != 1 || !engine.isNoteActive(60))
        return false;

    {
        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, 1.0f), 0);
        engine.render(block, midi);
    }

    return engine.activeVoiceCount() == 1
        && !engine.isNoteActive(60)
        && engine.isNoteActive(62);
}

bool runSfzImportVelocityCrossfadePlaybackTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 128;
    constexpr int channels = 2;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_velocity_crossfade_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("velocity_crossfade.sfz");
    if (!sfzFile.replaceWithText(
            "<group> key=60\n"
            "<region> sample=Tone.wav xfin_lovel=32 xfin_hivel=64 xfout_lovel=96 xfout_hivel=120\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.zones.size() != 1
        || result.sampleDataByAsset.size() != 1
        || result.program.zones.front().velocityFadeIn.low != 32
        || result.program.zones.front().velocityFadeIn.high != 64
        || result.program.zones.front().velocityFadeOut.low != 96
        || result.program.zones.front().velocityFadeOut.high != 120)
    {
        return false;
    }

    auto renderVelocity = [&](const float velocity)
    {
        EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

        EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.001f;
        engine.setAmpEnvelope(amp);
        engine.setProgram(result.program, result.sampleDataByAsset);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, velocity), 0);
        engine.render(block, midi);

        auto energy = 0.0f;
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
        {
            const auto* data = block.getReadPointer(channel);
            for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); ++sampleIndex)
                energy += std::abs(data[sampleIndex]);
        }

        return energy;
    };

    const auto lowEnergy = renderVelocity(0.20f);
    const auto midEnergy = renderVelocity(0.70f);
    const auto highEnergy = renderVelocity(1.0f);

    return midEnergy > 0.01f
        && lowEnergy < midEnergy * 0.1f
        && highEnergy < midEnergy * 0.1f;
}

bool runSfzImporterDiagnosticsTest()
{
    using namespace audiocity::engine;

    auto hasDiagnostic = [](const std::vector<SfzDiagnostic>& diagnostics,
                            const SfzDiagnostic::Severity severity,
                            const std::string& text)
    {
        for (const auto& diagnostic : diagnostics)
        {
            if (diagnostic.severity == severity
                && diagnostic.message.find(text) != std::string::npos)
            {
                return true;
            }
        }

        return false;
    };

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_diagnostics_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto cycleA = tempDirectory.getChildFile("cycle_a.sfz");
    const auto cycleB = tempDirectory.getChildFile("cycle_b.sfz");
    const auto missingSample = tempDirectory.getChildFile("missing_sample.sfz");

    if (!cycleA.replaceWithText("#include \"cycle_b.sfz\"\n")
        || !cycleB.replaceWithText("#include \"cycle_a.sfz\"\n")
        || !missingSample.replaceWithText(
            "<region> sample=DoesNotExist.wav trigger=legato loop_mode=spin_cycle seq_length=4 seq_mode=shuffle unsupported_opcode=12\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto cycleResult = importer.importFile(cycleA);
    const auto missingResult = importer.importFile(missingSample);
    tempDirectory.deleteRecursively();

    if (!cycleResult.hasErrors()
        || !hasDiagnostic(cycleResult.diagnostics, SfzDiagnostic::Severity::error, "cycle"))
    {
        return false;
    }

    if (!missingResult.hasErrors()
        || !missingResult.program.zones.empty()
        || !hasDiagnostic(missingResult.diagnostics, SfzDiagnostic::Severity::warning, "Unknown SFZ opcode")
        || !hasDiagnostic(missingResult.diagnostics, SfzDiagnostic::Severity::warning, "Unsupported SFZ trigger value")
        || !hasDiagnostic(missingResult.diagnostics, SfzDiagnostic::Severity::warning, "Unsupported SFZ loop_mode value")
        || !hasDiagnostic(missingResult.diagnostics, SfzDiagnostic::Severity::warning, "Unsupported SFZ seq_mode value")
        || !hasDiagnostic(missingResult.diagnostics, SfzDiagnostic::Severity::error, "Missing SFZ sample"))
    {
        return false;
    }

    return true;
}

bool runDecentSamplerImporterTest()
{
    namespace ds = audiocity::engine::dspreset;
    using namespace audiocity::engine;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_dspreset_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto wavFile = tempDirectory.getChildFile("tone.wav");
    constexpr int sampleRate = 44100;
    constexpr int sampleLength = 2048;
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(wavFile.createOutputStream());
        if (output == nullptr) { tempDirectory.deleteRecursively(); return false; }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr) { tempDirectory.deleteRecursively(); return false; }
        output.release();

        juce::AudioBuffer<float> buf(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
            buf.setSample(0, i, 0.25f * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 220.0 / sampleRate)));
        if (!writer->writeFromAudioSampleBuffer(buf, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto presetFile = tempDirectory.getChildFile("Preset.dspreset");
    const juce::String xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<DecentSampler>
  <groups>
        <group volume="-6dB" pan="0" seqMode="round_robin" seqLength="2">
            <sample path="tone.wav" rootNote="60" loNote="48" hiNote="72" loVel="0" hiVel="127" volume="0dB" pan="-50" seqPosition="1" />
            <sample path="tone.wav" rootNote="72" loNote="73" hiNote="84" loopStart="100" loopEnd="900" loopEnabled="true" trigger="release" seqPosition="2" />
    </group>
  </groups>
</DecentSampler>
)";
    if (!presetFile.replaceWithText(xml))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto result = ds::importFile(presetFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors() || !result.hasPlayableProgram())
        return false;
    if (result.program.zones.size() != 2 || result.program.sampleAssets.size() != 1 || result.program.groups.size() != 1)
        return false;

    const auto& group = result.program.groups[0];
    if (std::abs(group.gainDb - (-6.0f)) > 0.5f || std::abs(group.pan) > 0.05f)
        return false;
    if (group.roundRobinGroup <= 0 || group.roundRobinMode != RoundRobinMode::ordered)
        return false;

    const auto& z0 = result.program.zones[0];
    if (z0.keyRange.low != 48 || z0.keyRange.high != 72 || z0.rootMidiNote != 60)
        return false;
    if (z0.groupIndex != 0)
        return false;
    if (z0.roundRobinPosition != 1 || z0.roundRobinLength != 2)
        return false;
    // Sample-level pan -50 (percent) is stored locally on the zone.
    if (std::abs(z0.pan + 0.5f) > 0.05f)
        return false;
    // Group volume stays on the group; sample volume 0 dB remains local on the zone.
    if (std::abs(z0.gainDb) > 0.05f)
        return false;

    const auto& z1 = result.program.zones[1];
    if (z1.loopMode != audiocity::engine::ZoneLoopMode::continuous
        || z1.loopStart != 100 || z1.loopEndExclusive != 900)
        return false;
    if (z1.triggerMode != audiocity::engine::ZoneTriggerMode::release)
        return false;
    if (z1.roundRobinPosition != 2 || z1.roundRobinLength != 2)
        return false;

    const auto snapshot = ProgramSnapshot::fromProgram(result.program);
    const auto& snapshotZone0 = snapshot.zones[0];
    if (std::abs(snapshot.getZoneGainDb(snapshotZone0) - (-6.0f)) > 0.5f)
        return false;
    if (std::abs(snapshot.getZonePan(snapshotZone0) - (-0.5f)) > 0.05f)
        return false;
    if (snapshot.getZoneRoundRobinGroup(snapshotZone0) <= 0
        || snapshot.getZoneRoundRobinMode(snapshotZone0) != RoundRobinMode::ordered)
    {
        return false;
    }

    return true;
}

namespace
{
void appendU16LE(juce::MemoryOutputStream& os, juce::uint16 v)
{
    const juce::uint8 b[2] = { static_cast<juce::uint8>(v & 0xFF), static_cast<juce::uint8>((v >> 8) & 0xFF) };
    os.write(b, 2);
}
void appendS16LE(juce::MemoryOutputStream& os, juce::int16 v)
{
    appendU16LE(os, static_cast<juce::uint16>(v));
}
void appendU32LE(juce::MemoryOutputStream& os, juce::uint32 v)
{
    const juce::uint8 b[4] = {
        static_cast<juce::uint8>(v & 0xFF), static_cast<juce::uint8>((v >> 8) & 0xFF),
        static_cast<juce::uint8>((v >> 16) & 0xFF), static_cast<juce::uint8>((v >> 24) & 0xFF) };
    os.write(b, 4);
}
void appendFourcc(juce::MemoryOutputStream& os, const char* tag)
{
    os.write(tag, 4);
}
void appendName20(juce::MemoryOutputStream& os, const char* text)
{
    char buf[20] = {};
    const auto len = std::min<std::size_t>(std::strlen(text), 19);
    std::memcpy(buf, text, len);
    os.write(buf, 20);
}
juce::MemoryBlock buildMinimalSf2()
{
    // Build a one-preset / one-instrument / one-zone / one-sample SF2 around a 256-sample sine.
    constexpr int sampleCount = 256;
    juce::MemoryOutputStream smpl;
    for (int i = 0; i < sampleCount; ++i)
    {
        const double phase = 2.0 * juce::MathConstants<double>::pi * i * 8.0 / sampleCount;
        const auto value = static_cast<juce::int16>(std::round(std::sin(phase) * 16000.0));
        appendS16LE(smpl, value);
    }

    // pdta arrays.
    juce::MemoryOutputStream phdr;
    // Preset 0 "Tone", bank 0, program 0, bagIdx 0
    appendName20(phdr, "Tone"); appendU16LE(phdr, 0); appendU16LE(phdr, 0); appendU16LE(phdr, 0);
    appendU32LE(phdr, 0); appendU32LE(phdr, 0); appendU32LE(phdr, 0);
    // Terminal "EOP"
    appendName20(phdr, "EOP"); appendU16LE(phdr, 0); appendU16LE(phdr, 0); appendU16LE(phdr, 1);
    appendU32LE(phdr, 0); appendU32LE(phdr, 0); appendU32LE(phdr, 0);

    juce::MemoryOutputStream pbag;
    // Zone 0 -> pgen[0..1], pmod[0]
    appendU16LE(pbag, 0); appendU16LE(pbag, 0);
    // Terminal -> pgen[1], pmod[0]
    appendU16LE(pbag, 1); appendU16LE(pbag, 0);

    juce::MemoryOutputStream pmod;
    // single terminal zero record (10 bytes)
    for (int i = 0; i < 10; ++i) { const juce::uint8 z = 0; pmod.write(&z, 1); }

    juce::MemoryOutputStream pgen;
    // pgen[0]: instrument(41) = 0
    appendU16LE(pgen, 41); appendU16LE(pgen, 0);
    // pgen[1]: terminal
    appendU16LE(pgen, 0); appendU16LE(pgen, 0);

    juce::MemoryOutputStream inst;
    appendName20(inst, "Inst"); appendU16LE(inst, 0);
    appendName20(inst, "EOI"); appendU16LE(inst, 1);

    juce::MemoryOutputStream ibag;
    appendU16LE(ibag, 0); appendU16LE(ibag, 0);
    appendU16LE(ibag, 4); appendU16LE(ibag, 0);

    juce::MemoryOutputStream imod;
    for (int i = 0; i < 10; ++i) { const juce::uint8 z = 0; imod.write(&z, 1); }

    juce::MemoryOutputStream igen;
    // igen[0]: keyRange 48..72
    appendU16LE(igen, 43); appendU16LE(igen, static_cast<juce::uint16>(48 | (72 << 8)));
    // igen[1]: velRange 0..127
    appendU16LE(igen, 44); appendU16LE(igen, static_cast<juce::uint16>(0 | (127 << 8)));
    // igen[2]: overridingRootKey = 60
    appendU16LE(igen, 58); appendU16LE(igen, 60);
    // igen[3]: sampleID = 0 (terminator generator for instrument zone)
    appendU16LE(igen, 53); appendU16LE(igen, 0);
    // igen[4]: terminal
    appendU16LE(igen, 0); appendU16LE(igen, 0);

    juce::MemoryOutputStream shdr;
    // Sample 0
    appendName20(shdr, "Sample"); appendU32LE(shdr, 0); appendU32LE(shdr, sampleCount);
    appendU32LE(shdr, 0); appendU32LE(shdr, sampleCount);
    appendU32LE(shdr, 44100); { const juce::uint8 root = 60; shdr.write(&root, 1); }
    { const juce::int8 corr = 0; shdr.write(&corr, 1); }
    appendU16LE(shdr, 0); appendU16LE(shdr, 1); // sampleType=monoSample
    // Terminal EOS
    appendName20(shdr, "EOS"); appendU32LE(shdr, 0); appendU32LE(shdr, 0);
    appendU32LE(shdr, 0); appendU32LE(shdr, 0);
    appendU32LE(shdr, 0); { const juce::uint8 root = 0; shdr.write(&root, 1); }
    { const juce::int8 corr = 0; shdr.write(&corr, 1); }
    appendU16LE(shdr, 0); appendU16LE(shdr, 0);

    auto writeListChunk = [](juce::MemoryOutputStream& dst, const char* listType,
                             const std::vector<std::pair<const char*, juce::MemoryBlock>>& subs)
    {
        juce::MemoryOutputStream payload;
        appendFourcc(payload, listType);
        for (const auto& [tag, blk] : subs)
        {
            appendFourcc(payload, tag);
            appendU32LE(payload, static_cast<juce::uint32>(blk.getSize()));
            payload.write(blk.getData(), blk.getSize());
            if (blk.getSize() & 1u) { const juce::uint8 z = 0; payload.write(&z, 1); }
        }
        appendFourcc(dst, "LIST");
        appendU32LE(dst, static_cast<juce::uint32>(payload.getDataSize()));
        dst.write(payload.getData(), payload.getDataSize());
    };

    juce::MemoryOutputStream ifil;
    appendU16LE(ifil, 2); appendU16LE(ifil, 4);

    juce::MemoryOutputStream out;
    juce::MemoryOutputStream body;
    appendFourcc(body, "sfbk");

    writeListChunk(body, "INFO", {
        { "ifil", juce::MemoryBlock(ifil.getData(), ifil.getDataSize()) },
    });
    writeListChunk(body, "sdta", {
        { "smpl", juce::MemoryBlock(smpl.getData(), smpl.getDataSize()) },
    });
    writeListChunk(body, "pdta", {
        { "phdr", juce::MemoryBlock(phdr.getData(), phdr.getDataSize()) },
        { "pbag", juce::MemoryBlock(pbag.getData(), pbag.getDataSize()) },
        { "pmod", juce::MemoryBlock(pmod.getData(), pmod.getDataSize()) },
        { "pgen", juce::MemoryBlock(pgen.getData(), pgen.getDataSize()) },
        { "inst", juce::MemoryBlock(inst.getData(), inst.getDataSize()) },
        { "ibag", juce::MemoryBlock(ibag.getData(), ibag.getDataSize()) },
        { "imod", juce::MemoryBlock(imod.getData(), imod.getDataSize()) },
        { "igen", juce::MemoryBlock(igen.getData(), igen.getDataSize()) },
        { "shdr", juce::MemoryBlock(shdr.getData(), shdr.getDataSize()) },
    });

    appendFourcc(out, "RIFF");
    appendU32LE(out, static_cast<juce::uint32>(body.getDataSize()));
    out.write(body.getData(), body.getDataSize());
    return juce::MemoryBlock(out.getData(), out.getDataSize());
}

juce::MemoryBlock buildDualPresetSf2()
{
    constexpr int sampleCount = 256;
    juce::MemoryOutputStream smpl;
    for (int i = 0; i < sampleCount; ++i)
    {
        const double phase = 2.0 * juce::MathConstants<double>::pi * i * 8.0 / sampleCount;
        const auto value = static_cast<juce::int16>(std::round(std::sin(phase) * 16000.0));
        appendS16LE(smpl, value);
    }
    for (int i = 0; i < sampleCount; ++i)
    {
        const double phase = 2.0 * juce::MathConstants<double>::pi * i * 5.0 / sampleCount;
        const auto value = static_cast<juce::int16>(std::round(std::cos(phase) * 12000.0));
        appendS16LE(smpl, value);
    }

    juce::MemoryOutputStream phdr;
    appendName20(phdr, "Tone A"); appendU16LE(phdr, 0); appendU16LE(phdr, 0); appendU16LE(phdr, 0);
    appendU32LE(phdr, 0); appendU32LE(phdr, 0); appendU32LE(phdr, 0);
    appendName20(phdr, "Tone B"); appendU16LE(phdr, 1); appendU16LE(phdr, 0); appendU16LE(phdr, 1);
    appendU32LE(phdr, 0); appendU32LE(phdr, 0); appendU32LE(phdr, 0);
    appendName20(phdr, "EOP"); appendU16LE(phdr, 0); appendU16LE(phdr, 0); appendU16LE(phdr, 2);
    appendU32LE(phdr, 0); appendU32LE(phdr, 0); appendU32LE(phdr, 0);

    juce::MemoryOutputStream pbag;
    appendU16LE(pbag, 0); appendU16LE(pbag, 0);
    appendU16LE(pbag, 1); appendU16LE(pbag, 0);
    appendU16LE(pbag, 2); appendU16LE(pbag, 0);

    juce::MemoryOutputStream pmod;
    for (int i = 0; i < 10; ++i) { const juce::uint8 z = 0; pmod.write(&z, 1); }

    juce::MemoryOutputStream pgen;
    appendU16LE(pgen, 41); appendU16LE(pgen, 0);
    appendU16LE(pgen, 41); appendU16LE(pgen, 1);
    appendU16LE(pgen, 0); appendU16LE(pgen, 0);

    juce::MemoryOutputStream inst;
    appendName20(inst, "Inst A"); appendU16LE(inst, 0);
    appendName20(inst, "Inst B"); appendU16LE(inst, 1);
    appendName20(inst, "EOI"); appendU16LE(inst, 2);

    juce::MemoryOutputStream ibag;
    appendU16LE(ibag, 0); appendU16LE(ibag, 0);
    appendU16LE(ibag, 4); appendU16LE(ibag, 0);
    appendU16LE(ibag, 8); appendU16LE(ibag, 0);

    juce::MemoryOutputStream imod;
    for (int i = 0; i < 10; ++i) { const juce::uint8 z = 0; imod.write(&z, 1); }

    juce::MemoryOutputStream igen;
    appendU16LE(igen, 43); appendU16LE(igen, static_cast<juce::uint16>(48 | (60 << 8)));
    appendU16LE(igen, 44); appendU16LE(igen, static_cast<juce::uint16>(0 | (127 << 8)));
    appendU16LE(igen, 58); appendU16LE(igen, 60);
    appendU16LE(igen, 53); appendU16LE(igen, 0);
    appendU16LE(igen, 43); appendU16LE(igen, static_cast<juce::uint16>(61 | (72 << 8)));
    appendU16LE(igen, 44); appendU16LE(igen, static_cast<juce::uint16>(0 | (127 << 8)));
    appendU16LE(igen, 58); appendU16LE(igen, 72);
    appendU16LE(igen, 53); appendU16LE(igen, 1);
    appendU16LE(igen, 0); appendU16LE(igen, 0);

    juce::MemoryOutputStream shdr;
    appendName20(shdr, "SampleA"); appendU32LE(shdr, 0); appendU32LE(shdr, sampleCount);
    appendU32LE(shdr, 0); appendU32LE(shdr, sampleCount);
    appendU32LE(shdr, 44100); { const juce::uint8 root = 60; shdr.write(&root, 1); }
    { const juce::int8 corr = 0; shdr.write(&corr, 1); }
    appendU16LE(shdr, 0); appendU16LE(shdr, 1);
    appendName20(shdr, "SampleB"); appendU32LE(shdr, sampleCount); appendU32LE(shdr, sampleCount * 2);
    appendU32LE(shdr, sampleCount); appendU32LE(shdr, sampleCount * 2);
    appendU32LE(shdr, 44100); { const juce::uint8 root = 72; shdr.write(&root, 1); }
    { const juce::int8 corr = 0; shdr.write(&corr, 1); }
    appendU16LE(shdr, 0); appendU16LE(shdr, 1);
    appendName20(shdr, "EOS"); appendU32LE(shdr, 0); appendU32LE(shdr, 0);
    appendU32LE(shdr, 0); appendU32LE(shdr, 0);
    appendU32LE(shdr, 0); { const juce::uint8 root = 0; shdr.write(&root, 1); }
    { const juce::int8 corr = 0; shdr.write(&corr, 1); }
    appendU16LE(shdr, 0); appendU16LE(shdr, 0);

    auto writeListChunk = [](juce::MemoryOutputStream& dst, const char* listType,
                             const std::vector<std::pair<const char*, juce::MemoryBlock>>& subs)
    {
        juce::MemoryOutputStream payload;
        appendFourcc(payload, listType);
        for (const auto& [tag, blk] : subs)
        {
            appendFourcc(payload, tag);
            appendU32LE(payload, static_cast<juce::uint32>(blk.getSize()));
            payload.write(blk.getData(), blk.getSize());
            if (blk.getSize() & 1u) { const juce::uint8 z = 0; payload.write(&z, 1); }
        }
        appendFourcc(dst, "LIST");
        appendU32LE(dst, static_cast<juce::uint32>(payload.getDataSize()));
        dst.write(payload.getData(), payload.getDataSize());
    };

    juce::MemoryOutputStream ifil;
    appendU16LE(ifil, 2); appendU16LE(ifil, 4);

    juce::MemoryOutputStream out;
    juce::MemoryOutputStream body;
    appendFourcc(body, "sfbk");

    writeListChunk(body, "INFO", {
        { "ifil", juce::MemoryBlock(ifil.getData(), ifil.getDataSize()) },
    });
    writeListChunk(body, "sdta", {
        { "smpl", juce::MemoryBlock(smpl.getData(), smpl.getDataSize()) },
    });
    writeListChunk(body, "pdta", {
        { "phdr", juce::MemoryBlock(phdr.getData(), phdr.getDataSize()) },
        { "pbag", juce::MemoryBlock(pbag.getData(), pbag.getDataSize()) },
        { "pmod", juce::MemoryBlock(pmod.getData(), pmod.getDataSize()) },
        { "pgen", juce::MemoryBlock(pgen.getData(), pgen.getDataSize()) },
        { "inst", juce::MemoryBlock(inst.getData(), inst.getDataSize()) },
        { "ibag", juce::MemoryBlock(ibag.getData(), ibag.getDataSize()) },
        { "imod", juce::MemoryBlock(imod.getData(), imod.getDataSize()) },
        { "igen", juce::MemoryBlock(igen.getData(), igen.getDataSize()) },
        { "shdr", juce::MemoryBlock(shdr.getData(), shdr.getDataSize()) },
    });

    appendFourcc(out, "RIFF");
    appendU32LE(out, static_cast<juce::uint32>(body.getDataSize()));
    out.write(body.getData(), body.getDataSize());
    return juce::MemoryBlock(out.getData(), out.getDataSize());
}
} // namespace

bool runSf2ImporterMinimalTest()
{
    namespace sf = audiocity::engine::sf2;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sf2_test", ".sf2");

    const auto blob = buildMinimalSf2();
    if (!tempFile.replaceWithData(blob.getData(), blob.getSize()))
        return false;

    const auto result = sf::importFile(tempFile);
    tempFile.deleteFile();

    if (result.hasErrors() || !result.hasPlayableProgram())
        return false;
    if (result.availablePresets.size() != 1)
        return false;
    if (result.program.zones.size() != 1 || result.program.sampleAssets.size() != 1)
        return false;

    const auto& z = result.program.zones[0];
    if (z.keyRange.low != 48 || z.keyRange.high != 72 || z.rootMidiNote != 60)
        return false;
    if (result.sampleDataByAsset[0].getNumSamples() != 256)
        return false;

    return true;
}

bool runSf2ImporterPresetSelectionTest()
{
    namespace sf = audiocity::engine::sf2;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sf2_preset_test", ".sf2");

    const auto blob = buildDualPresetSf2();
    if (!tempFile.replaceWithData(blob.getData(), blob.getSize()))
        return false;

    const auto choiceProbe = audiocity::plugin::probeImportedProgramChoices(tempFile);
    if (choiceProbe.format != audiocity::plugin::ImportedProgramFormat::sf2
        || !choiceProbe.hasMultipleChoices()
        || choiceProbe.choices.size() != 2
        || choiceProbe.choices[1].choiceIndex != 1
        || choiceProbe.choices[1].label != "Tone B"
        || choiceProbe.choices[1].detail != "Bank 0, Program 1")
    {
        tempFile.deleteFile();
        return false;
    }

    const auto result = sf::importFilePreset(tempFile, 1);
    tempFile.deleteFile();

    if (result.hasErrors() || !result.hasPlayableProgram())
        return false;
    if (result.availablePresets.size() != 2 || result.chosenPresetIndex != 1)
        return false;
    if (result.program.name != "Tone B")
        return false;
    if (result.program.zones.size() != 1 || result.program.sampleAssets.size() != 1)
        return false;

    const auto& zone = result.program.zones[0];
    return zone.keyRange.low == 61 && zone.keyRange.high == 72 && zone.rootMidiNote == 72;
}

bool runSf2ImporterRejectsShortListChunkTest()
{
    namespace sf = audiocity::engine::sf2;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sf2_short_list_test", ".sf2");

    juce::MemoryOutputStream body;
    appendFourcc(body, "sfbk");
    appendFourcc(body, "LIST");
    appendU32LE(body, 2);
    const juce::uint8 payload[2] = { 0, 0 };
    body.write(payload, sizeof(payload));

    juce::MemoryOutputStream out;
    appendFourcc(out, "RIFF");
    appendU32LE(out, static_cast<juce::uint32>(body.getDataSize()));
    out.write(body.getData(), body.getDataSize());

    if (!tempFile.replaceWithData(out.getData(), out.getDataSize()))
        return false;

    const auto result = sf::importFile(tempFile);
    tempFile.deleteFile();

    bool sawShortList = false;
    for (const auto& diagnostic : result.diagnostics)
        sawShortList = sawShortList || diagnostic.message.find("LIST chunk too small") != std::string::npos;

    return result.hasErrors() && !result.hasPlayableProgram() && sawShortList;
}

bool runSf2ImporterRejectsEmptyRequiredTablesTest()
{
    namespace sf = audiocity::engine::sf2;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sf2_empty_tables_test", ".sf2");

    auto writeListChunk = [](juce::MemoryOutputStream& dst, const char* listType,
                             const std::vector<const char*>& subchunkTags)
    {
        juce::MemoryOutputStream payload;
        appendFourcc(payload, listType);
        for (const auto* tag : subchunkTags)
        {
            appendFourcc(payload, tag);
            appendU32LE(payload, 0);
        }
        appendFourcc(dst, "LIST");
        appendU32LE(dst, static_cast<juce::uint32>(payload.getDataSize()));
        dst.write(payload.getData(), payload.getDataSize());
    };

    juce::MemoryOutputStream body;
    appendFourcc(body, "sfbk");
    writeListChunk(body, "sdta", { "smpl" });
    writeListChunk(body, "pdta", { "phdr", "pbag", "pgen", "inst", "ibag", "igen", "shdr" });

    juce::MemoryOutputStream out;
    appendFourcc(out, "RIFF");
    appendU32LE(out, static_cast<juce::uint32>(body.getDataSize()));
    out.write(body.getData(), body.getDataSize());

    if (!tempFile.replaceWithData(out.getData(), out.getDataSize()))
        return false;

    const auto result = sf::importFile(tempFile);
    tempFile.deleteFile();

    bool sawEmptyTables = false;
    for (const auto& diagnostic : result.diagnostics)
        sawEmptyTables = sawEmptyTables || diagnostic.message.find("empty or missing terminal pdta records") != std::string::npos;

    return result.hasErrors() && !result.hasPlayableProgram() && sawEmptyTables;
}

bool runBitwigMultisampleImporterTest()
{
    namespace bw = audiocity::engine::bitwig;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_bitwig_multisample_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto wavFile = tempDirectory.getChildFile("tone.wav");
    constexpr int sampleRate = 44100;
    constexpr int sampleLength = 1024;
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> out(wavFile.createOutputStream());
        if (out == nullptr) { tempDirectory.deleteRecursively(); return false; }
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(out.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr) { tempDirectory.deleteRecursively(); return false; }
        out.release();
        juce::AudioBuffer<float> buf(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
            buf.setSample(0, i, 0.2f * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 220.0 / sampleRate)));
        if (!writer->writeFromAudioSampleBuffer(buf, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const juce::String manifestXml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<multisample name="Test">
  <generator>Audiocity</generator>
  <category>Pad</category>
  <layer name="Default">
    <sample file="tone.wav" sample-start="0.0" sample-stop="1024.0" gain="0.0" tune="0.0">
      <key root="60" low="48" high="72"/>
      <velocity low="0" high="127"/>
      <loop mode="loop" start="100.0" stop="900.0"/>
    </sample>
  </layer>
</multisample>
)";
    const auto manifestFile = tempDirectory.getChildFile("multisample.xml");
    if (!manifestFile.replaceWithText(manifestXml))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto multisampleFile = tempDirectory.getChildFile("Test.multisample");
    {
        juce::ZipFile::Builder builder;
        builder.addFile(manifestFile, /*compression*/ 9, "multisample.xml");
        builder.addFile(wavFile, /*compression*/ 0, "tone.wav");
        std::unique_ptr<juce::FileOutputStream> out(multisampleFile.createOutputStream());
        if (out == nullptr) { tempDirectory.deleteRecursively(); return false; }
        if (!builder.writeToStream(*out, nullptr))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto result = bw::importFile(multisampleFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors() || !result.hasPlayableProgram())
        return false;
    if (result.program.zones.size() != 1 || result.program.sampleAssets.size() != 1)
        return false;

    const auto& z = result.program.zones[0];
    if (z.keyRange.low != 48 || z.keyRange.high != 72 || z.rootMidiNote != 60)
        return false;
    if (z.loopMode != audiocity::engine::ZoneLoopMode::continuous
        || z.loopStart != 100 || z.loopEndExclusive != 900)
        return false;
    if (result.sampleDataByAsset[0].getNumSamples() != sampleLength)
        return false;

    return true;
}

namespace
{
// Helper for the new XML multisample importer tests.
bool writeMonoToneWav(const juce::File& wavFile, int sampleRate, int sampleLength, double freq = 220.0)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> out(wavFile.createOutputStream());
    if (out == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(out.get(), sampleRate, 1, 16, {}, 0));
    if (writer == nullptr) return false;
    out.release();
    juce::AudioBuffer<float> buf(1, sampleLength);
    for (int i = 0; i < sampleLength; ++i)
        buf.setSample(0, i, 0.2f * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * freq / sampleRate)));
    return writer->writeFromAudioSampleBuffer(buf, 0, sampleLength);
}

bool writeRepeatedBytes(const juce::File& file, juce::int64 bytesToWrite, const char byte)
{
    std::unique_ptr<juce::FileOutputStream> out(file.createOutputStream());
    if (out == nullptr) return false;

    std::array<char, 4096> buffer;
    buffer.fill(byte);
    while (bytesToWrite > 0)
    {
        const auto chunk = static_cast<size_t>(juce::jmin<juce::int64>(static_cast<juce::int64>(buffer.size()), bytesToWrite));
        if (!out->write(buffer.data(), chunk)) return false;
        bytesToWrite -= static_cast<juce::int64>(chunk);
    }
    return true;
}

bool writeGzipRepeatedBytes(const juce::File& file, juce::int64 bytesToWrite, const char byte)
{
    std::unique_ptr<juce::FileOutputStream> raw(file.createOutputStream());
    if (raw == nullptr) return false;

    juce::GZIPCompressorOutputStream gz(raw.get(), 9, false, juce::GZIPCompressorOutputStream::windowBitsGZIP);
    std::array<char, 4096> buffer;
    buffer.fill(byte);
    while (bytesToWrite > 0)
    {
        const auto chunk = static_cast<size_t>(juce::jmin<juce::int64>(static_cast<juce::int64>(buffer.size()), bytesToWrite));
        if (!gz.write(buffer.data(), chunk)) return false;
        bytesToWrite -= static_cast<juce::int64>(chunk);
    }
    gz.flush();
    return true;
}
} // namespace

bool runArchiveRelativePathSafetyTest()
{
    using audiocity::engine::xml_multi::isSafeArchiveRelativePath;

    return isSafeArchiveRelativePath("tone.wav")
        && isSafeArchiveRelativePath("Samples/tone.wav")
        && isSafeArchiveRelativePath("Samples\\tone.wav")
        && !isSafeArchiveRelativePath({})
        && !isSafeArchiveRelativePath("../tone.wav")
        && !isSafeArchiveRelativePath("Samples/../tone.wav")
        && !isSafeArchiveRelativePath("/tone.wav")
        && !isSafeArchiveRelativePath("C:/Samples/tone.wav")
        && !isSafeArchiveRelativePath("file:///Samples/tone.wav");
}

bool runBitwigMultisampleRejectsUnsafeArchivePathTest()
{
    namespace bw = audiocity::engine::bitwig;

    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_bitwig_unsafe_path_test", "");
    if (!dir.createDirectory()) return false;

    const auto wav = dir.getChildFile("tone.wav");
    if (!writeMonoToneWav(wav, 44100, 256)) { dir.deleteRecursively(); return false; }

    const auto manifest = dir.getChildFile("multisample.xml");
    const juce::String xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<multisample name="Unsafe">
  <sample file="../tone.wav">
    <key root="60" low="60" high="60"/>
  </sample>
</multisample>
)";
    if (!manifest.replaceWithText(xml)) { dir.deleteRecursively(); return false; }

    const auto archive = dir.getChildFile("Unsafe.multisample");
    {
        juce::ZipFile::Builder builder;
        builder.addFile(manifest, 9, "multisample.xml");
        builder.addFile(wav, 0, "tone.wav");
        std::unique_ptr<juce::FileOutputStream> out(archive.createOutputStream());
        if (out == nullptr || !builder.writeToStream(*out, nullptr)) { dir.deleteRecursively(); return false; }
    }

    const auto result = bw::importFile(archive);
    dir.deleteRecursively();

    return result.hasErrors() && !result.hasPlayableProgram();
}

bool runKorgMultisampleRejectsUnsafeArchivePathTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_korg_unsafe_path_test", "");
    if (!dir.createDirectory()) return false;

    const auto wav = dir.getChildFile("bell.wav");
    if (!writeMonoToneWav(wav, 44100, 256)) { dir.deleteRecursively(); return false; }

    const auto manifest = dir.getChildFile("multisample.xml");
    const juce::String xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<KorgMultiSample name="Unsafe">
  <sample file="../bell.wav" rootkey="72" lokey="72" hikey="72"/>
</KorgMultiSample>)";
    if (!manifest.replaceWithText(xml)) { dir.deleteRecursively(); return false; }

    const auto archive = dir.getChildFile("Unsafe.korgmultisample");
    {
        juce::ZipFile::Builder builder;
        builder.addFile(manifest, 9, "multisample.xml");
        builder.addFile(wav, 0, "bell.wav");
        std::unique_ptr<juce::FileOutputStream> out(archive.createOutputStream());
        if (out == nullptr || !builder.writeToStream(*out, nullptr)) { dir.deleteRecursively(); return false; }
    }

    const auto result = audiocity::engine::korgmulti::importFile(archive);
    dir.deleteRecursively();

    return result.hasErrors() && !result.hasPlayableProgram();
}

bool runArchiveImportersRejectOversizedManifestTest()
{
    constexpr juce::int64 oversizedManifestBytes = static_cast<juce::int64>(16) * 1024 * 1024 + 1;

    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_oversized_manifest_test", "");
    if (!dir.createDirectory()) return false;

    const auto manifest = dir.getChildFile("multisample.xml");
    if (!writeRepeatedBytes(manifest, oversizedManifestBytes, ' ')) { dir.deleteRecursively(); return false; }

    const auto bitwigArchive = dir.getChildFile("Oversized.multisample");
    {
        juce::ZipFile::Builder builder;
        builder.addFile(manifest, 9, "multisample.xml");
        std::unique_ptr<juce::FileOutputStream> out(bitwigArchive.createOutputStream());
        if (out == nullptr || !builder.writeToStream(*out, nullptr)) { dir.deleteRecursively(); return false; }
    }

    const auto bitwigResult = audiocity::engine::bitwig::importFile(bitwigArchive);
    bool bitwigSawTooLarge = false;
    for (const auto& diagnostic : bitwigResult.diagnostics)
        bitwigSawTooLarge = bitwigSawTooLarge || diagnostic.message.find("manifest too large") != std::string::npos;
    const bool bitwigRejected = bitwigResult.hasErrors() && !bitwigResult.hasPlayableProgram() && bitwigSawTooLarge;

    const auto korgArchive = dir.getChildFile("Oversized.korgmultisample");
    {
        juce::ZipFile::Builder builder;
        builder.addFile(manifest, 9, "multisample.xml");
        std::unique_ptr<juce::FileOutputStream> out(korgArchive.createOutputStream());
        if (out == nullptr || !builder.writeToStream(*out, nullptr)) { dir.deleteRecursively(); return false; }
    }

    const auto korgResult = audiocity::engine::korgmulti::importFile(korgArchive);
    bool korgSawTooLarge = false;
    for (const auto& diagnostic : korgResult.diagnostics)
        korgSawTooLarge = korgSawTooLarge || diagnostic.message.find("manifest too large") != std::string::npos;
    const bool korgRejected = korgResult.hasErrors() && !korgResult.hasPlayableProgram() && korgSawTooLarge;

    dir.deleteRecursively();
    return bitwigRejected && korgRejected;
}

bool runMpcKeygroupImporterTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_mpc_xpm_test", "");
    if (!dir.createDirectory()) return false;

    const auto wav = dir.getChildFile("kick.wav");
    if (!writeMonoToneWav(wav, 44100, 512)) { dir.deleteRecursively(); return false; }

    const auto xpm = dir.getChildFile("Kit.xpm");
    const juce::String xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<MPCVObject>
  <Program type="Keygroup">
    <ProgramName>Kit</ProgramName>
    <Instruments>
      <Instrument number="1">
        <LowNote>36</LowNote><HighNote>48</HighNote>
        <Layers>
          <Layer number="1">
            <SampleFile>kick.wav</SampleFile>
            <RootNote>40</RootNote>
            <VelStart>0</VelStart><VelEnd>127</VelEnd>
            <Volume>1.0</Volume><Pan>0.5</Pan>
            <SampleStart>0</SampleStart><SampleEnd>512</SampleEnd>
          </Layer>
        </Layers>
      </Instrument>
    </Instruments>
  </Program>
</MPCVObject>)";
    if (!xpm.replaceWithText(xml)) { dir.deleteRecursively(); return false; }

    const auto r = audiocity::engine::mpc::importFile(xpm);
    dir.deleteRecursively();
    if (r.hasErrors() || !r.hasPlayableProgram()) return false;
    if (r.program.zones.size() != 1 || r.program.sampleAssets.size() != 1) return false;
    const auto& z = r.program.zones[0];
    return z.keyRange.low == 36 && z.keyRange.high == 48 && z.rootMidiNote == 40;
}

bool run1010MusicPresetImporterTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_1010_test", "");
    if (!dir.createDirectory()) return false;

    const auto wav = dir.getChildFile("snare.wav");
    if (!writeMonoToneWav(wav, 44100, 1024)) { dir.deleteRecursively(); return false; }

    const auto preset = dir.getChildFile("preset.xml");
    const juce::String xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<document>
  <session>
    <cell type="sample" filename="snare.wav" rootnote="62" lonote="60" hinote="64"
          lovel="0" hivel="127" samstart="0" samend="1024"
          playmode="loop" loopstart="100" loopend="900" gaindb="0" pan="0"/>
  </session>
</document>)";
    if (!preset.replaceWithText(xml)) { dir.deleteRecursively(); return false; }

    const auto r = audiocity::engine::bento::importFile(preset);
    dir.deleteRecursively();
    if (r.hasErrors() || !r.hasPlayableProgram()) return false;
    const auto& z = r.program.zones[0];
    return z.keyRange.low == 60 && z.keyRange.high == 64 && z.rootMidiNote == 62
        && z.loopMode == audiocity::engine::ZoneLoopMode::continuous
        && z.loopStart == 100 && z.loopEndExclusive == 900;
}

bool runTalSamplerImporterTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_tal_test", "");
    if (!dir.createDirectory()) return false;

    const auto wav = dir.getChildFile("pad.wav");
    if (!writeMonoToneWav(wav, 44100, 2048)) { dir.deleteRecursively(); return false; }

    const auto preset = dir.getChildFile("Pad.talsmpl");
    const juce::String xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<tal>
  <programs>
    <program programname="Pad">
      <multisample>
        <sample url="pad.wav" rootkey="60" startnote="48" endnote="72"
                startvelocity="0" endvelocity="127"
                startsample="0" endsample="2048"
                loopactive="1" loopstart="200" loopend="2000"
                volume="1.0" pan="0.5" finetune="5" tune="0"/>
      </multisample>
    </program>
  </programs>
</tal>)";
    if (!preset.replaceWithText(xml)) { dir.deleteRecursively(); return false; }

    const auto r = audiocity::engine::talsmpl::importFile(preset);
    dir.deleteRecursively();
    if (r.hasErrors() || !r.hasPlayableProgram()) return false;
    const auto& z = r.program.zones[0];
    return z.keyRange.low == 48 && z.keyRange.high == 72 && z.rootMidiNote == 60
        && z.loopMode == audiocity::engine::ZoneLoopMode::continuous
        && z.loopStart == 200 && z.loopEndExclusive == 2000;
}

bool runTx16WxImporterTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_tx16wx_test", "");
    if (!dir.createDirectory()) return false;

    const auto wav = dir.getChildFile("lead.wav");
    if (!writeMonoToneWav(wav, 44100, 1024)) { dir.deleteRecursively(); return false; }

    const auto preset = dir.getChildFile("Lead.txprog");
    const juce::String xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<program name="Lead">
  <group name="g1">
    <region sample="lead.wav" rootkey="64" lokey="60" hikey="72"
            lovel="0" hivel="127" start="0" end="1024"
            loop="forward" loopstart="50" loopend="950" volume="0" pan="0" tune="0"/>
  </group>
</program>)";
    if (!preset.replaceWithText(xml)) { dir.deleteRecursively(); return false; }

    const auto r = audiocity::engine::tx16wx::importFile(preset);
    dir.deleteRecursively();
    if (r.hasErrors() || !r.hasPlayableProgram()) return false;
    const auto& z = r.program.zones[0];
    return z.keyRange.low == 60 && z.keyRange.high == 72 && z.rootMidiNote == 64
        && z.loopMode == audiocity::engine::ZoneLoopMode::continuous;
}

bool runKorgMultisampleImporterTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_korg_test", "");
    if (!dir.createDirectory()) return false;

    const auto wav = dir.getChildFile("bell.wav");
    if (!writeMonoToneWav(wav, 44100, 1024)) { dir.deleteRecursively(); return false; }

    const auto manifest = dir.getChildFile("multisample.xml");
    const juce::String xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<KorgMultiSample name="Bell">
  <sample file="bell.wav" rootkey="72" lokey="60" hikey="84" lovel="0" hivel="127"
          start="0" end="1024" loop="loop" loopstart="100" loopend="900"/>
</KorgMultiSample>)";
    if (!manifest.replaceWithText(xml)) { dir.deleteRecursively(); return false; }

    const auto archive = dir.getChildFile("Bell.korgmultisample");
    {
        juce::ZipFile::Builder b;
        b.addFile(manifest, 9, "multisample.xml");
        b.addFile(wav, 0, "bell.wav");
        std::unique_ptr<juce::FileOutputStream> out(archive.createOutputStream());
        if (out == nullptr || !b.writeToStream(*out, nullptr)) { dir.deleteRecursively(); return false; }
    }

    const auto r = audiocity::engine::korgmulti::importFile(archive);
    dir.deleteRecursively();
    if (r.hasErrors() || !r.hasPlayableProgram()) return false;
    const auto& z = r.program.zones[0];
    return z.keyRange.low == 60 && z.keyRange.high == 84 && z.rootMidiNote == 72
        && z.loopMode == audiocity::engine::ZoneLoopMode::continuous;
}

bool runAbletonAdvImporterTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_ableton_test", "");
    if (!dir.createDirectory()) return false;

    const auto wav = dir.getChildFile("pad.wav");
    if (!writeMonoToneWav(wav, 44100, 4096)) { dir.deleteRecursively(); return false; }

    const auto plain = dir.getChildFile("Pad.adv.xml");
    const juce::String xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<Ableton>
  <MultiSampler>
    <Player>
      <MultiSampleMap>
        <SampleParts>
          <MultiSamplePart>
            <Name Value="Pad"/>
            <KeyRange><Min Value="48"/><Max Value="72"/></KeyRange>
            <VelocityRange><Min Value="1"/><Max Value="127"/></VelocityRange>
            <RootKey Value="60"/>
            <SampleStart Value="0"/><SampleEnd Value="4096"/>
            <SustainLoop>
              <Mode Value="1"/><Start Value="200"/><End Value="3500"/>
            </SustainLoop>
            <Volume Value="1.0"/>
            <Panorama Value="0.0"/>
            <Detune Value="0"/>
            <SampleRef>
              <FileRef>
                <Path Value=")" + wav.getFullPathName().replace("\\", "/") + R"("/>
                <Name Value="pad.wav"/>
              </FileRef>
            </SampleRef>
          </MultiSamplePart>
        </SampleParts>
      </MultiSampleMap>
    </Player>
  </MultiSampler>
</Ableton>)";

    const auto adv = dir.getChildFile("Pad.adv");
    {
        std::unique_ptr<juce::FileOutputStream> raw(adv.createOutputStream());
        if (raw == nullptr) { dir.deleteRecursively(); return false; }
        juce::GZIPCompressorOutputStream gz(raw.get(), 9, false, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        const auto utf8 = xml.toUTF8();
        gz.write(utf8.getAddress(), (size_t) std::strlen(utf8));
        gz.flush();
    }

    const auto r = audiocity::engine::ableton::importFile(adv);
    dir.deleteRecursively();
    if (r.hasErrors() || !r.hasPlayableProgram()) return false;
    const auto& z = r.program.zones[0];
    return z.keyRange.low == 48 && z.keyRange.high == 72 && z.rootMidiNote == 60
        && z.loopMode == audiocity::engine::ZoneLoopMode::continuous
        && z.loopStart == 200 && z.loopEndExclusive == 3500;
}

bool runAbletonAdvRejectsOversizedXmlTest()
{
    constexpr juce::int64 oversizedXmlBytes = static_cast<juce::int64>(16) * 1024 * 1024 + 1;

    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_ableton_oversized_test", "");
    if (!dir.createDirectory()) return false;

    const auto adv = dir.getChildFile("Oversized.adv");
    if (!writeGzipRepeatedBytes(adv, oversizedXmlBytes, ' ')) { dir.deleteRecursively(); return false; }

    const auto result = audiocity::engine::ableton::importFile(adv);
    dir.deleteRecursively();

    bool sawTooLarge = false;
    for (const auto& diagnostic : result.diagnostics)
        sawTooLarge = sawTooLarge || diagnostic.message.find("XML payload too large") != std::string::npos;

    return result.hasErrors() && !result.hasPlayableProgram() && sawTooLarge;
}

bool runDistingExPresetImporterTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_dex_test", "");
    if (!dir.createDirectory()) return false;

    const auto wav = dir.getChildFile("clip.wav");
    if (!writeMonoToneWav(wav, 44100, 1024)) { dir.deleteRecursively(); return false; }

    const auto preset = dir.getChildFile("Pad.dexpreset");
    const juce::String text =
        "Name: Pad\n"
        "[Sample 1]\n"
        "file=clip.wav\n"
        "root=60\n"
        "lokey=48\n"
        "hikey=72\n"
        "lovel=1\n"
        "hivel=127\n"
        "gain=0\n"
        "pan=0\n";
    if (!preset.replaceWithText(text)) { dir.deleteRecursively(); return false; }

    const auto r = audiocity::engine::distingex::importFile(preset);
    dir.deleteRecursively();
    if (r.hasErrors() || !r.hasPlayableProgram()) return false;
    const auto& z = r.program.zones[0];
    return z.keyRange.low == 48 && z.keyRange.high == 72 && z.rootMidiNote == 60;
}

bool runKorgKmpImporterTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_kmp_test", "");
    if (!dir.createDirectory()) return false;

    // Sample is "Bell.wav"; KMP RLP1 entry references "Bell.KSF" (we resolve .wav fallback)
    const auto wav = dir.getChildFile("Bell.wav");
    if (!writeMonoToneWav(wav, 44100, 1024)) { dir.deleteRecursively(); return false; }

    const auto kmp = dir.getChildFile("Bell.kmp");
    juce::MemoryBlock blob;
    auto appendChunk = [&](const char id[4], const std::vector<juce::uint8>& payload)
    {
        juce::uint8 hdr[8];
        std::memcpy(hdr, id, 4);
        juce::uint32 sz = (juce::uint32) payload.size();
        hdr[4] = (juce::uint8)((sz >> 24) & 0xFF);
        hdr[5] = (juce::uint8)((sz >> 16) & 0xFF);
        hdr[6] = (juce::uint8)((sz >> 8) & 0xFF);
        hdr[7] = (juce::uint8)( sz        & 0xFF);
        blob.append(hdr, 8);
        blob.append(payload.data(), payload.size());
    };

    {
        std::vector<juce::uint8> msp1(18, 0);
        const char* name = "Bell";
        std::memcpy(msp1.data(), name, std::strlen(name));
        msp1[16] = 1; // numSamples
        appendChunk("MSP1", msp1);
    }
    {
        std::vector<juce::uint8> rlp1(18, 0);
        rlp1[0] = 84;   // topKey
        rlp1[1] = 72;   // rootKey
        rlp1[2] = 0;    // tune cents
        rlp1[3] = 0;    // level dB
        rlp1[4] = 0;    // pan
        const char* fname = "Bell.KSF";
        std::memcpy(rlp1.data() + 6, fname, std::strlen(fname));
        appendChunk("RLP1", rlp1);
    }
    if (!kmp.replaceWithData(blob.getData(), blob.getSize())) { dir.deleteRecursively(); return false; }

    const auto r = audiocity::engine::korgkmp::importFile(kmp);
    dir.deleteRecursively();
    if (r.hasErrors() || !r.hasPlayableProgram()) return false;
    const auto& z = r.program.zones[0];
    return z.rootMidiNote == 72 && z.keyRange.low == 0 && z.keyRange.high == 84;
}

bool runKorgKmpImporterCapsRlp1EntriesTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_kmp_rlp1_cap_test", "");
    if (!dir.createDirectory()) return false;

    const auto kmp = dir.getChildFile("Oversized.kmp");
    juce::MemoryBlock blob;
    auto appendChunk = [&](const char id[4], const std::vector<juce::uint8>& payload)
    {
        juce::uint8 hdr[8];
        std::memcpy(hdr, id, 4);
        juce::uint32 sz = (juce::uint32) payload.size();
        hdr[4] = (juce::uint8)((sz >> 24) & 0xFF);
        hdr[5] = (juce::uint8)((sz >> 16) & 0xFF);
        hdr[6] = (juce::uint8)((sz >> 8) & 0xFF);
        hdr[7] = (juce::uint8)( sz        & 0xFF);
        blob.append(hdr, 8);
        blob.append(payload.data(), payload.size());
    };

    {
        std::vector<juce::uint8> msp1(18, 0);
        const char* name = "Oversized";
        std::memcpy(msp1.data(), name, std::strlen(name));
        msp1[16] = 2;
        appendChunk("MSP1", msp1);
    }
    {
        std::vector<juce::uint8> rlp1(18 * 4, 0);
        const std::array<const char*, 4> names { "A.KSF", "B.KSF", "C.KSF", "D.KSF" };
        for (size_t i = 0; i < names.size(); ++i)
        {
            auto* entry = rlp1.data() + i * 18;
            entry[0] = static_cast<juce::uint8>(48 + i);
            entry[1] = 60;
            std::memcpy(entry + 6, names[i], std::strlen(names[i]));
        }
        appendChunk("RLP1", rlp1);
    }
    if (!kmp.replaceWithData(blob.getData(), blob.getSize())) { dir.deleteRecursively(); return false; }

    const auto r = audiocity::engine::korgkmp::importFile(kmp);
    dir.deleteRecursively();

    bool sawCapWarning = false;
    bool sawFirstDeclaredEntry = false;
    bool sawIgnoredEntry = false;
    for (const auto& diagnostic : r.diagnostics)
    {
        sawCapWarning = sawCapWarning || diagnostic.message.find("extra entries ignored") != std::string::npos;
        sawFirstDeclaredEntry = sawFirstDeclaredEntry || diagnostic.message.find("A.KSF") != std::string::npos;
        sawIgnoredEntry = sawIgnoredEntry || diagnostic.message.find("C.KSF") != std::string::npos;
    }

    return r.hasErrors() && !r.hasPlayableProgram()
        && sawCapWarning
        && sawFirstDeclaredEntry
        && !sawIgnoredEntry;
}

bool runLogicExs24ImporterRejectsOversizedChunkTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_exs_oversized_chunk_test", "");
    if (!dir.createDirectory()) return false;

    const auto exs = dir.getChildFile("Oversized.exs");
    juce::uint8 header[84] = {};
    header[4] = 0xff;
    header[5] = 0xff;
    header[6] = 0xff;
    header[7] = 0x7f;
    header[8] = 0x02;
    header[9] = 0x01;
    header[10] = 0x00;
    header[11] = 0x01;
    const char* name = "OversizedZone";
    std::memcpy(header + 20, name, std::strlen(name));
    if (!exs.replaceWithData(header, sizeof(header))) { dir.deleteRecursively(); return false; }

    const auto r = audiocity::engine::exs24::importFile(exs);
    dir.deleteRecursively();

    bool sawTruncation = false;
    for (const auto& diagnostic : r.diagnostics)
        sawTruncation = sawTruncation || diagnostic.message.find("Truncated EXS24 chunk") != std::string::npos;

    return r.hasErrors() && !r.hasPlayableProgram() && sawTruncation;
}

bool runLogicExs24ImporterTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_exs_test", "");
    if (!dir.createDirectory()) return false;

    const auto wav = dir.getChildFile("piano.wav");
    if (!writeMonoToneWav(wav, 44100, 4096)) { dir.deleteRecursively(); return false; }

    const auto exs = dir.getChildFile("Piano.exs");

    auto writeChunk = [](juce::MemoryBlock& blob, juce::uint32 type, juce::uint32 payloadSize,
                         const juce::String& name, const std::vector<juce::uint8>& payload)
    {
        juce::uint8 hdr[84] = {};
        // sig (we leave 0 - parser ignores it)
        hdr[4] = (juce::uint8)( payloadSize        & 0xFF);
        hdr[5] = (juce::uint8)((payloadSize >> 8)  & 0xFF);
        hdr[6] = (juce::uint8)((payloadSize >> 16) & 0xFF);
        hdr[7] = (juce::uint8)((payloadSize >> 24) & 0xFF);
        hdr[8]  = (juce::uint8)( type        & 0xFF);
        hdr[9]  = (juce::uint8)((type >> 8)  & 0xFF);
        hdr[10] = (juce::uint8)((type >> 16) & 0xFF);
        hdr[11] = (juce::uint8)((type >> 24) & 0xFF);
        const auto utf8 = name.toUTF8();
        const auto nameLen = (int) std::strlen(utf8);
        std::memcpy(hdr + 20, utf8.getAddress(), (size_t) juce::jmin(nameLen, 64));
        blob.append(hdr, 84);
        blob.append(payload.data(), payload.size());
    };

    juce::MemoryBlock blob;
    // Header chunk with program name
    writeChunk(blob, 0x01000101u, 84, "Piano", std::vector<juce::uint8>(84, 0));

    // Sample chunk - filename in first 64 bytes of payload
    {
        std::vector<juce::uint8> sample(64 + 256, 0);
        const char* fname = "piano.wav";
        std::memcpy(sample.data(), fname, std::strlen(fname));
        writeChunk(blob, 0x01000104u, (juce::uint32) sample.size(), "piano", sample);
    }

    // Zone chunk
    {
        std::vector<juce::uint8> zone(184, 0);
        zone[1] = 0x00; // loop off
        zone[5] = 60;   // root
        zone[6] = 48;   // lo key
        zone[7] = 72;   // hi key
        zone[10] = 1;   // lo vel
        zone[11] = 127; // hi vel
        // sample start
        zone[12] = 0; zone[13] = 0; zone[14] = 0; zone[15] = 0;
        // sample end = 4096
        zone[16] = 0x00; zone[17] = 0x10; zone[18] = 0; zone[19] = 0;
        // sample index = 0
        zone[180] = 0; zone[181] = 0; zone[182] = 0; zone[183] = 0;
        writeChunk(blob, 0x01000102u, (juce::uint32) zone.size(), "z1", zone);
    }

    if (!exs.replaceWithData(blob.getData(), blob.getSize())) { dir.deleteRecursively(); return false; }

    const auto r = audiocity::engine::exs24::importFile(exs);
    dir.deleteRecursively();
    if (r.hasErrors() || !r.hasPlayableProgram()) return false;
    const auto& z = r.program.zones[0];
    return z.rootMidiNote == 60 && z.keyRange.low == 48 && z.keyRange.high == 72;
}

bool runNnxtImporterDiagnosticTest()
{
    // NN-XT is recognised but not yet supported - the importer must surface an error.
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_nnxt_test", "");
    if (!dir.createDirectory()) return false;

    const auto sxt = dir.getChildFile("Patch.sxt");
    juce::MemoryBlock blob;
    blob.setSize(64, true);
    auto* bytes = static_cast<juce::uint8*>(blob.getData());
    const char* tag = "CAT NN-XT";
    std::memcpy(bytes, tag, std::strlen(tag));
    if (!sxt.replaceWithData(blob.getData(), blob.getSize())) { dir.deleteRecursively(); return false; }

    const auto r = audiocity::engine::nnxt::importFile(sxt);
    dir.deleteRecursively();
    return r.hasErrors() && !r.hasPlayableProgram();
}

bool runMalformedImporterCorpusTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_malformed_importer_corpus_test", "");
    if (!dir.createDirectory()) return false;

    auto writeBytes = [&dir](const juce::String& name, std::initializer_list<juce::uint8> bytes)
    {
        const auto file = dir.getChildFile(name);
        juce::MemoryBlock blob;
        for (const auto byte : bytes)
            blob.append(&byte, 1);
        return file.replaceWithData(blob.getData(), blob.getSize()) ? file : juce::File{};
    };

    const auto rejects = [](const auto& result)
    {
        return result.hasErrors() && !result.hasPlayableProgram();
    };

    bool ok = true;

    const auto sf2 = writeBytes("broken.sf2", { 'R', 'I', 'F', 'F', 0xff, 0xff, 0xff, 0x7f, 's', 'f', 'b', 'k' });
    ok = ok && sf2.existsAsFile() && rejects(audiocity::engine::sf2::importFile(sf2));

    const auto bitwig = writeBytes("broken.multisample", { 'P', 'K', 3, 4, 0, 0, 0 });
    ok = ok && bitwig.existsAsFile() && rejects(audiocity::engine::bitwig::importFile(bitwig));

    const auto korgMulti = writeBytes("broken.korgmultisample", { 'P', 'K', 3, 4, 0, 0, 0 });
    ok = ok && korgMulti.existsAsFile() && rejects(audiocity::engine::korgmulti::importFile(korgMulti));

    const auto ableton = writeBytes("broken.adv", { '<', 'A', 'b', 'l', 'e', 't', 'o', 'n', '>' });
    ok = ok && ableton.existsAsFile() && rejects(audiocity::engine::ableton::importFile(ableton));

    const auto kmp = writeBytes("broken.kmp", { 'M', 'S', 'P', '1', 0, 0, 0, 64, 1, 2, 3 });
    ok = ok && kmp.existsAsFile() && rejects(audiocity::engine::korgkmp::importFile(kmp));

    const auto exs = writeBytes("broken.exs", { 0, 1, 2, 3, 4, 5, 6, 7 });
    ok = ok && exs.existsAsFile() && rejects(audiocity::engine::exs24::importFile(exs));

    const auto sxt = writeBytes("broken.sxt", { 'C', 'A', 'T', ' ', 'N', 'N', '-', 'X', 'T', 0, 1, 2 });
    ok = ok && sxt.existsAsFile() && rejects(audiocity::engine::nnxt::importFile(sxt));

    dir.deleteRecursively();
    return ok;
}

bool runLegacyNkiImportNcwViaConverterTest()
{
    using namespace audiocity::engine::nki;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_nki_import_ncw_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleDirectory = tempDirectory.getChildFile("Samples");
    if (!sampleDirectory.createDirectory())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto sampleFile = sampleDirectory.getChildFile("Piano.ncw");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        auto sample = createTestSample(sampleLength);
        sample.applyGain(0.45f);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto nkiFile = tempDirectory.getChildFile("LegacyPiano.nki");
    juce::MemoryOutputStream payload;
    auto writeAscii = [&payload](const char* text)
    {
        payload.write(text, std::strlen(text));
        payload.writeByte(0);
    };

    payload.write("NKI\0", 4);
    payload.writeByte(0x44);
    payload.writeByte(0x02);
    writeAscii("group_name=Keys");
    writeAscii("zone_name=Piano");
    writeAscii("lowkey=60");
    writeAscii("highkey=72");
    writeAscii("rootkey=60");
    writeAscii("lowvel=1");
    writeAscii("highvel=127");
    writeAscii("Samples/Piano.ncw");
    if (!nkiFile.replaceWithData(payload.getData(), payload.getDataSize()))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    ScopedEnvironmentVariable converterCommand(
        "AUDIOCITY_NCW_CONVERTER_COMMAND",
        "cmd.exe /c copy /y {input} {output} >nul");
    if (!converterCommand.valid)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto result = importFile(nkiFile);
    if (result.hasErrors()
        || !result.hasPlayableProgram()
        || result.program.sampleAssets.size() != 1
        || result.program.zones.size() != 1
        || result.program.sampleAssets[0].displayName != "Piano.ncw"
        || !juce::String(result.program.sampleAssets[0].sourcePath).endsWithIgnoreCase(".wav")
        || result.program.zones[0].rootMidiNote != 60
        || result.program.zones[0].keyRange.low != 60
        || result.program.zones[0].keyRange.high != 72)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    using namespace audiocity::engine;
    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setProgram(result.program, result.sampleDataByAsset);

    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);
    float energy = 0.0f;
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
            energy += std::abs(block.getSample(channel, sample));

    tempDirectory.deleteRecursively();
    return energy > 0.01f;
}

bool runLegacyNkiProbeDetectsEncryptedPatchTest()
{
    namespace nki = audiocity::engine::nki;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_nki_encrypted_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto file = tempDirectory.getChildFile("Protected.nki");
    juce::MemoryBlock blob;
    blob.setSize(8192, true);
    juce::Random rng(0xC0FFEEu);
    auto* bytes = static_cast<juce::uint8*>(blob.getData());
    for (size_t i = 0; i < blob.getSize(); ++i)
        bytes[i] = static_cast<juce::uint8>(rng.nextInt(256));
    // Stamp a recognisable Kontakt monolithic-container tag near the header so the
    // probe's signature heuristic identifies this fixture as protected content.
    const char* tag = "NICnt";
    std::memcpy(bytes + 16, tag, std::strlen(tag));
    if (!file.replaceWithData(blob.getData(), blob.getSize()))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto result = nki::probeFile(file);
    tempDirectory.deleteRecursively();

    if (!result.likelyEncryptedOrProtected)
        return false;
    if (result.status != nki::ProbeStatus::unsupportedOrUnrecognized)
        return false;
    const auto summary = nki::buildProbeSummary(result);
    if (!summary.containsIgnoreCase("encrypted") && !summary.containsIgnoreCase("protected"))
        return false;
    return true;
}

bool runLegacyNkiProbeDetectsDiscreteSampleReferencesTest()
{
    using namespace audiocity::engine::nki;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_nki_probe_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleDirectory = tempDirectory.getChildFile("Samples");
    const auto layersDirectory = tempDirectory.getChildFile("Layers").getChildFile("Sub");
    if (!sampleDirectory.createDirectory()
        || !layersDirectory.createDirectory()
        || !sampleDirectory.getChildFile("Kick.WAV").replaceWithText("")
        || !layersDirectory.getChildFile("Room.AIFF").replaceWithText(""))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto nkiFile = tempDirectory.getChildFile("LegacyKit.nki");
    juce::MemoryOutputStream payload;
    auto writeAscii = [&payload](const char* text)
    {
        payload.write(text, std::strlen(text));
        payload.writeByte(0);
    };

    payload.write("NKI\0", 4);
    payload.writeByte(0x12);
    payload.writeByte(0x01);
    writeAscii("Samples\\Kick.WAV");
    writeAscii("Layers/Sub/Room.AIFF");
    if (!nkiFile.replaceWithData(payload.getData(), payload.getDataSize()))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto result = probeFile(nkiFile);
    const auto ok = result.status == ProbeStatus::legacyDiscreteSampleCandidate
        && result.sampleReferences.size() == 2
        && result.sampleReferences[0] == "Samples/Kick.WAV"
        && result.sampleReferences[1] == "Layers/Sub/Room.AIFF"
        && result.resolvedSampleFiles.size() == 2
        && result.missingSampleReferences.isEmpty()
        && result.containerReferences.isEmpty()
        && buildProbeSummary(result).contains("2 resolved");

    tempDirectory.deleteRecursively();
    return ok;
}

bool runLegacyNkiProbeResolvesParentSamplesFolderTest()
{
    using namespace audiocity::engine::nki;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_nki_probe_resolution_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto libraryRoot = tempDirectory.getChildFile("Library");
    const auto instrumentsDirectory = libraryRoot.getChildFile("Instruments");
    const auto samplesDirectory = libraryRoot.getChildFile("Samples");
    if (!libraryRoot.createDirectory()
        || !instrumentsDirectory.createDirectory()
        || !samplesDirectory.createDirectory()
        || !samplesDirectory.getChildFile("Kick.WAV").replaceWithText(""))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto nkiFile = instrumentsDirectory.getChildFile("LegacyKit.nki");
    juce::MemoryOutputStream payload;
    auto writeAscii = [&payload](const char* text)
    {
        payload.write(text, std::strlen(text));
        payload.writeByte(0);
    };

    payload.write("NKI\0", 4);
    payload.writeByte(0x21);
    payload.writeByte(0x04);
    writeAscii("Kick.WAV");
    writeAscii("MissingSnare.AIF");
    if (!nkiFile.replaceWithData(payload.getData(), payload.getDataSize()))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto result = probeFile(nkiFile);
    const auto expectedResolvedPath = samplesDirectory.getChildFile("Kick.WAV")
        .getFullPathName().replaceCharacter('\\', '/');
    const auto ok = result.status == ProbeStatus::legacyDiscreteSampleCandidate
        && result.sampleReferences.size() == 2
        && result.resolvedSampleFiles.size() == 1
        && result.resolvedSampleFiles[0].equalsIgnoreCase(expectedResolvedPath)
        && result.missingSampleReferences.size() == 1
        && result.missingSampleReferences[0] == "MissingSnare.AIF"
        && buildProbeSummary(result).contains("1 resolved")
        && buildProbeSummary(result).contains("1 unresolved");

    tempDirectory.deleteRecursively();
    return ok;
}

bool runLegacyNkiProbeEnumeratesZoneMetadataTest()
{
    using namespace audiocity::engine::nki;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_nki_probe_metadata_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleDirectory = tempDirectory.getChildFile("Samples");
    if (!sampleDirectory.createDirectory()
        || !sampleDirectory.getChildFile("Kick.WAV").replaceWithText("")
        || !sampleDirectory.getChildFile("Snare.WAV").replaceWithText(""))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto nkiFile = tempDirectory.getChildFile("LegacyDrums.nki");
    juce::MemoryOutputStream payload;
    auto writeAscii = [&payload](const char* text)
    {
        payload.write(text, std::strlen(text));
        payload.writeByte(0);
    };

    payload.write("NKI\0", 4);
    payload.writeByte(0x31);
    payload.writeByte(0x05);
    writeAscii("group_name=Drums");
    writeAscii("zone_name=Kick");
    writeAscii("lowkey=36");
    writeAscii("highkey=36");
    writeAscii("rootkey=36");
    writeAscii("lowvel=1");
    writeAscii("highvel=90");
    writeAscii("Samples/Kick.WAV");
    writeAscii("zone_name=Snare");
    writeAscii("lokey=38");
    writeAscii("hikey=38");
    writeAscii("root=38");
    writeAscii("lovel=91");
    writeAscii("hivel=127");
    writeAscii("Samples/Snare.WAV");
    if (!nkiFile.replaceWithData(payload.getData(), payload.getDataSize()))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto result = probeFile(nkiFile);
    const auto ok = result.status == ProbeStatus::legacyDiscreteSampleCandidate
        && result.groupNames.size() == 1
        && result.groupNames[0] == "Drums"
        && result.zoneMetadata.size() == 2
        && result.zoneMetadata[0].groupName == "Drums"
        && result.zoneMetadata[0].zoneName == "Kick"
        && result.zoneMetadata[0].sampleReference == "Samples/Kick.WAV"
        && result.zoneMetadata[0].lowKey == 36
        && result.zoneMetadata[0].highKey == 36
        && result.zoneMetadata[0].rootKey == 36
        && result.zoneMetadata[0].lowVelocity == 1
        && result.zoneMetadata[0].highVelocity == 90
        && result.zoneMetadata[1].groupName == "Drums"
        && result.zoneMetadata[1].zoneName == "Snare"
        && result.zoneMetadata[1].sampleReference == "Samples/Snare.WAV"
        && result.zoneMetadata[1].lowKey == 38
        && result.zoneMetadata[1].highKey == 38
        && result.zoneMetadata[1].rootKey == 38
        && result.zoneMetadata[1].lowVelocity == 91
        && result.zoneMetadata[1].highVelocity == 127
        && buildProbeSummary(result).contains("2 zone metadata blocks")
        && buildProbeSummary(result).contains("1 group");

    tempDirectory.deleteRecursively();
    return ok;
}

bool runLegacyNkiImportTranslatesLegacyZonesTest()
{
    using namespace audiocity::engine::nki;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_nki_import_translation_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleDirectory = tempDirectory.getChildFile("Samples");
    if (!sampleDirectory.createDirectory())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    auto writeSample = [&](const juce::File& outputFile, const float amplitude)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(outputFile.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();
        auto sample = createTestSample(sampleLength);
        sample.applyGain(amplitude);
        return writer->writeFromAudioSampleBuffer(sample, 0, sampleLength);
    };

    if (!writeSample(sampleDirectory.getChildFile("Kick.WAV"), 1.0f)
        || !writeSample(sampleDirectory.getChildFile("Snare.WAV"), 0.35f))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto nkiFile = tempDirectory.getChildFile("LegacyDrums.nki");
    juce::MemoryOutputStream payload;
    auto writeAscii = [&payload](const char* text)
    {
        payload.write(text, std::strlen(text));
        payload.writeByte(0);
    };

    payload.write("NKI\0", 4);
    payload.writeByte(0x44);
    payload.writeByte(0x02);
    writeAscii("group_name=Drums");
    writeAscii("zone_name=Kick");
    writeAscii("lowkey=36");
    writeAscii("highkey=36");
    writeAscii("rootkey=36");
    writeAscii("lowvel=1");
    writeAscii("highvel=127");
    writeAscii("Samples/Kick.WAV");
    writeAscii("zone_name=Snare");
    writeAscii("lowkey=38");
    writeAscii("highkey=38");
    writeAscii("rootkey=38");
    writeAscii("lowvel=1");
    writeAscii("highvel=127");
    writeAscii("Samples/Snare.WAV");
    if (!nkiFile.replaceWithData(payload.getData(), payload.getDataSize()))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto result = importFile(nkiFile);
    if (result.hasErrors()
        || !result.hasPlayableProgram()
        || result.program.groups.size() != 1
        || result.program.zones.size() != 2
        || result.program.sampleAssets.size() != 2
        || result.sampleDataByAsset.size() != 2
        || result.program.groups[0].name != "Drums"
        || result.program.zones[0].groupIndex != 0
        || result.program.zones[1].groupIndex != 0
        || result.program.zones[0].keyRange.low != 36
        || result.program.zones[1].keyRange.low != 38
        || result.program.zones[0].rootMidiNote != 36
        || result.program.zones[1].rootMidiNote != 38)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    using namespace audiocity::engine;
    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setProgram(result.program, result.sampleDataByAsset);

    auto renderNote = [&](const int midiNote)
    {
        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 1.0f), 0);
        engine.render(block, midi);

        auto selectedZone = -1;
        const auto states = engine.getVoicePlaybackStates();
        for (const auto& state : states)
        {
            if (state.active)
            {
                selectedZone = state.zoneIndex;
                break;
            }
        }

        auto energy = 0.0f;
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
        {
            const auto* data = block.getReadPointer(channel);
            for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); ++sampleIndex)
                energy += std::abs(data[sampleIndex]);
        }

        engine.panic();
        return std::pair<int, float>{ selectedZone, energy };
    };

    const auto [kickZone, kickEnergy] = renderNote(36);
    const auto [snareZone, snareEnergy] = renderNote(38);
    const auto [emptyZone, emptyEnergy] = renderNote(42);
    tempDirectory.deleteRecursively();

    return kickZone == 0
        && snareZone == 1
        && emptyZone == -1
        && kickEnergy > 0.01f
        && snareEnergy > 0.01f
        && emptyEnergy <= 1.0e-6f;
}

bool runLegacyNkiImportSampleWindowAndLoopTest()
{
    using namespace audiocity::engine::nki;

    constexpr double sampleRate = 48000.0;
    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr int sampleLength = 128;
    constexpr int sampleStart = 32;
    constexpr int sampleEnd = 95;
    constexpr int loopStart = 48;
    constexpr int loopEnd = 63;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_nki_import_window_loop_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleDirectory = tempDirectory.getChildFile("Samples");
    if (!sampleDirectory.createDirectory())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto sampleFile = sampleDirectory.getChildFile("Loop.WAV");
    {
        juce::AudioBuffer<float> sample(1, sampleLength);
        sample.clear();
        for (int index = loopStart; index <= loopEnd; ++index)
            sample.setSample(0, index, 0.8f);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto nkiFile = tempDirectory.getChildFile("LoopedTone.nki");
    juce::MemoryOutputStream payload;
    auto writeAscii = [&payload](const char* text)
    {
        payload.write(text, std::strlen(text));
        payload.writeByte(0);
    };

    payload.write("NKI\0", 4);
    payload.writeByte(0x51);
    payload.writeByte(0x09);
    writeAscii("zone_name=LoopedTone");
    writeAscii("lowkey=60");
    writeAscii("highkey=60");
    writeAscii("rootkey=60");
    writeAscii("offset=32");
    writeAscii("end=95");
    writeAscii("loop_start=48");
    writeAscii("loop_end=63");
    writeAscii("loop_mode=loop_continuous");
    writeAscii("Samples/Loop.WAV");
    if (!nkiFile.replaceWithData(payload.getData(), payload.getDataSize()))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto result = importFile(nkiFile);
    if (result.hasErrors()
        || !result.hasPlayableProgram()
        || result.program.zones.size() != 1
        || result.sampleDataByAsset.size() != 1)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto& zone = result.program.zones.front();
    if (zone.sampleStart != sampleStart
        || zone.sampleEndExclusive != sampleEnd + 1
        || zone.loopStart != loopStart
        || zone.loopEndExclusive != loopEnd + 1
        || zone.loopMode != audiocity::engine::ZoneLoopMode::continuous)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    using namespace audiocity::engine;
    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.5f;
    engine.setAmpEnvelope(amp);

    EngineCore::FilterSettings openFilter;
    openFilter.baseCutoffHz = 20000.0f;
    openFilter.envAmountHz = 0.0f;
    openFilter.resonance = 0.0f;
    engine.setFilterSettings(openFilter);

    EngineCore::DcFilterSettings dc;
    dc.enabled = false;
    engine.setDcFilterSettings(dc);

    engine.setProgram(result.program, result.sampleDataByAsset);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    midi.clear();
    engine.render(block, midi);

    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);
    midi.clear();

    engine.render(block, midi);

    auto energy = 0.0f;
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const auto* data = block.getReadPointer(channel);
        for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); ++sampleIndex)
            energy += std::abs(data[sampleIndex]);
    }

    tempDirectory.deleteRecursively();
    return energy > 50.0f;
}

bool runLegacyNkiImportGainPanTuneTest()
{
    using namespace audiocity::engine::nki;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 128;
    constexpr int sampleLength = 512;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_nki_import_gain_pan_tune_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleDirectory = tempDirectory.getChildFile("Samples");
    if (!sampleDirectory.createDirectory())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto sampleFile = sampleDirectory.getChildFile("Tone.WAV");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto nkiFile = tempDirectory.getChildFile("GainPanTune.nki");
    juce::MemoryOutputStream payload;
    auto writeAscii = [&payload](const char* text)
    {
        payload.write(text, std::strlen(text));
        payload.writeByte(0);
    };

    payload.write("NKI\0", 4);
    payload.writeByte(0x57);
    payload.writeByte(0x01);
    writeAscii("zone_name=GainPanTune");
    writeAscii("lowkey=60");
    writeAscii("highkey=60");
    writeAscii("rootkey=60");
    writeAscii("volume=-6");
    writeAscii("pan=75");
    writeAscii("tune=12.5");
    writeAscii("transpose=1");
    writeAscii("Samples/Tone.WAV");
    if (!nkiFile.replaceWithData(payload.getData(), payload.getDataSize()))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto result = importFile(nkiFile);
    if (result.hasErrors()
        || !result.hasPlayableProgram()
        || result.program.zones.size() != 1
        || result.sampleDataByAsset.size() != 1)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto& zone = result.program.zones.front();
    if (std::abs(zone.gainDb - (-6.0f)) > 1.0e-6f
        || std::abs(zone.pan - 0.75f) > 1.0e-6f
        || std::abs(zone.tuneCents - 112.5f) > 1.0e-6f)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    using namespace audiocity::engine;
    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setProgram(result.program, result.sampleDataByAsset);

    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    auto leftEnergy = 0.0f;
    auto rightEnergy = 0.0f;
    for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); ++sampleIndex)
    {
        leftEnergy += std::abs(block.getSample(0, sampleIndex));
        rightEnergy += std::abs(block.getSample(1, sampleIndex));
    }

    tempDirectory.deleteRecursively();
    return rightEnergy > 0.01f && rightEnergy > leftEnergy * 1.5f;
}

bool runLegacyNkiImportTriggerModesTest()
{
    using namespace audiocity::engine::nki;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 64;
    constexpr int sampleLength = 512;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_nki_import_trigger_modes_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleDirectory = tempDirectory.getChildFile("Samples");
    if (!sampleDirectory.createDirectory())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto sampleFile = sampleDirectory.getChildFile("Tone.WAV");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto nkiFile = tempDirectory.getChildFile("TriggerModes.nki");
    juce::MemoryOutputStream payload;
    auto writeAscii = [&payload](const char* text)
    {
        payload.write(text, std::strlen(text));
        payload.writeByte(0);
    };

    payload.write("NKI\0", 4);
    payload.writeByte(0x33);
    payload.writeByte(0x14);
    writeAscii("zone_name=OneShot");
    writeAscii("lowkey=60");
    writeAscii("highkey=60");
    writeAscii("rootkey=60");
    writeAscii("trigger=one_shot");
    writeAscii("Samples/Tone.WAV");
    writeAscii("zone_name=Release");
    writeAscii("lowkey=62");
    writeAscii("highkey=62");
    writeAscii("rootkey=62");
    writeAscii("trigger=release");
    writeAscii("Samples/Tone.WAV");
    if (!nkiFile.replaceWithData(payload.getData(), payload.getDataSize()))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto result = importFile(nkiFile);
    if (result.hasErrors()
        || !result.hasPlayableProgram()
        || result.program.zones.size() != 2
        || result.sampleDataByAsset.size() != 1
        || result.program.zones[0].triggerMode != audiocity::engine::ZoneTriggerMode::oneShot
        || result.program.zones[1].triggerMode != audiocity::engine::ZoneTriggerMode::release)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    using namespace audiocity::engine;
    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setProgram(result.program, result.sampleDataByAsset);

    auto renderNoteOnOnly = [&](const int midiNote)
    {
        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 1.0f), 0);
        engine.render(block, midi);
    };

    auto renderNoteOffOnly = [&](const int midiNote)
    {
        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, midiNote), 0);
        engine.render(block, midi);
    };

    renderNoteOnOnly(60);
    renderNoteOffOnly(60);
    if (engine.activeVoiceCount() != 1 || !engine.isNoteActive(60))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    engine.panic();

    renderNoteOnOnly(62);
    if (engine.activeVoiceCount() != 0)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    renderNoteOffOnly(62);
    const auto releaseTriggered = engine.activeVoiceCount() == 1 && engine.isNoteActive(62);
    tempDirectory.deleteRecursively();
    return releaseTriggered;
}

bool runLegacyNkiProbeRejectsContainerFormatsTest()
{
    using namespace audiocity::engine::nki;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_nki_probe_container_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto nkiFile = tempDirectory.getChildFile("Modernish.nki");
    juce::MemoryOutputStream payload;
    auto writeAscii = [&payload](const char* text)
    {
        payload.write(text, std::strlen(text));
        payload.writeByte(0);
    };

    payload.write("NKI\0", 4);
    payload.writeByte(0x02);
    payload.writeByte(0x03);
    writeAscii("Samples/Archive.nkx");
    writeAscii("Compressed/Layer.nks");
    if (!nkiFile.replaceWithData(payload.getData(), payload.getDataSize()))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto result = probeFile(nkiFile);
    const auto ok = result.status == ProbeStatus::unsupportedContainerReference
        && result.sampleReferences.isEmpty()
        && result.containerReferences.size() == 2
        && buildProbeSummary(result).contains("container-based or newer format");

    tempDirectory.deleteRecursively();
    return ok;
}

bool runVoiceStealingEdgeCaseTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.resetStealCount();

    juce::MidiBuffer midi;

    for (int index = 0; index <= static_cast<int>(audiocity::engine::VoicePool::maxVoices); ++index)
        midi.addEvent(juce::MidiMessage::noteOn(1, 36 + index, 1.0f), 0);

    juce::AudioBuffer<float> block(channels, blockSize);
    engine.render(block, midi);

    if (engine.activeVoiceCount() != static_cast<int>(audiocity::engine::VoicePool::maxVoices))
        return false;

    if (engine.stealCount() != 1)
        return false;

    return !engine.isNoteActive(36) && engine.isNoteActive(36 + static_cast<int>(audiocity::engine::VoicePool::maxVoices));
}

bool runPolyphonyLimitControlTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

    audiocity::engine::EngineCore::AdsrSettings sustain;
    sustain.attackSeconds = 0.0001f;
    sustain.decaySeconds = 0.0001f;
    sustain.sustainLevel = 1.0f;
    sustain.releaseSeconds = 0.5f;
    engine.setAmpEnvelope(sustain);

    engine.setPolyphonyLimit(1);

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 1);
        engine.render(block, midi);
    }

    if (engine.activeVoiceCount() != 1 || !engine.isNoteActive(64))
        return false;

    engine.panic();
    engine.setPolyphonyLimit(3);

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, 1.0f), 1);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 2);
        midi.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 3);
        engine.render(block, midi);
    }

    return engine.activeVoiceCount() == 3;
}

float blockEnergy(const juce::AudioBuffer<float>& block)
{
    float energy = 0.0f;

    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const auto* data = block.getReadPointer(channel);
        for (int i = 0; i < block.getNumSamples(); ++i)
            energy += std::abs(data[i]);
    }

    return energy;
}

float channelEnergy(const juce::AudioBuffer<float>& block, const int channel)
{
    if (channel < 0 || channel >= block.getNumChannels())
        return 0.0f;

    float energy = 0.0f;
    const auto* data = block.getReadPointer(channel);
    for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); ++sampleIndex)
        energy += std::abs(data[sampleIndex]);

    return energy;
}

bool runSameOffsetMidiEventOrderTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;

    auto renderEnergy = [](const bool noteOffFirst)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(4096), sampleRate, 60);

        audiocity::engine::EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.001f;
        engine.setAmpEnvelope(amp);

        if (noteOffFirst)
        {
            engine.noteOff(60, 0);
            engine.noteOn(60, 1.0f, 0);
        }
        else
        {
            engine.noteOn(60, 1.0f, 0);
            engine.noteOff(60, 0);
        }

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        engine.render(block, midi);
        return blockEnergy(block);
    };

    const auto releasedImmediately = renderEnergy(false);
    const auto startedAfterIgnoredRelease = renderEnergy(true);

    return releasedImmediately < 1.0e-6f && startedAfterIgnoredRelease > 0.01f;
}

bool runEngineProgramSnapshotZoneSelectionTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(8192), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    Program program;
    SampleAsset sample;
    sample.lengthSamples = 8192;
    sample.numChannels = 1;
    sample.sampleRateHz = sampleRate;
    sample.rootMidiNote = 72;
    program.sampleAssets.push_back(sample);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(72);
    zone.velocityRange = VelocityRange::full();
    zone.rootMidiNote = 72;
    program.zones.push_back(zone);
    engine.setProgram(program);

    if (!engine.hasProgram() || engine.getProgramZoneCount() != 1)
        return false;

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        if (engine.activeVoiceCount() != 0 || blockEnergy(block) > 1.0e-6f)
            return false;
    }

    int activeSampleIndex = -1;
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
        engine.render(block, midi);

        if (engine.activeVoiceCount() != 1 || blockEnergy(block) <= 0.01f)
            return false;

        const auto states = engine.getVoicePlaybackStates();
        for (const auto& state : states)
        {
            if (state.active)
            {
                activeSampleIndex = state.sampleIndex;
                break;
            }
        }
    }

    if (activeSampleIndex < 32 || activeSampleIndex > 96)
        return false;

    engine.panic();
    engine.clearProgram();
    if (engine.hasProgram())
        return false;

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    return engine.activeVoiceCount() == 1 && blockEnergy(block) > 0.01f;
}

bool runEngineProgramSampleAssetBindingTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    Program program;

    SampleAsset silentAsset;
    silentAsset.lengthSamples = 4096;
    silentAsset.numChannels = 1;
    silentAsset.sampleRateHz = sampleRate;
    silentAsset.rootMidiNote = 60;
    program.sampleAssets.push_back(silentAsset);

    SampleAsset toneAsset;
    toneAsset.lengthSamples = 4096;
    toneAsset.numChannels = 1;
    toneAsset.sampleRateHz = sampleRate;
    toneAsset.rootMidiNote = 72;
    program.sampleAssets.push_back(toneAsset);

    Zone silentZone;
    silentZone.sampleAssetIndex = 0;
    silentZone.keyRange = MidiRange::single(60);
    silentZone.velocityRange = VelocityRange::full();
    silentZone.rootMidiNote = 60;
    program.zones.push_back(silentZone);

    Zone toneZone;
    toneZone.sampleAssetIndex = 1;
    toneZone.keyRange = MidiRange::single(72);
    toneZone.velocityRange = VelocityRange::full();
    toneZone.rootMidiNote = 72;
    program.zones.push_back(toneZone);

    juce::AudioBuffer<float> silentSample(1, 4096);
    silentSample.clear();
    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(silentSample);
    samples.push_back(createTestSample(4096));
    engine.setProgram(program, samples);

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        if (engine.activeVoiceCount() != 1 || blockEnergy(block) > 1.0e-6f)
            return false;

        const auto states = engine.getVoicePlaybackStates();
        bool foundSilentZone = false;
        for (const auto& state : states)
            foundSilentZone = foundSilentZone || (state.active && state.zoneIndex == 0 && state.sampleAssetIndex == 0);

        if (!foundSilentZone)
            return false;
    }

    engine.panic();

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
        engine.render(block, midi);

        if (engine.activeVoiceCount() != 1 || blockEnergy(block) <= 0.01f)
            return false;

        const auto states = engine.getVoicePlaybackStates();
        bool foundToneZone = false;
        for (const auto& state : states)
            foundToneZone = foundToneZone || (state.active && state.zoneIndex == 1 && state.sampleAssetIndex == 1);

        if (!foundToneZone)
            return false;
    }

    return true;
}

bool runEngineProgramStereoAssetPlaybackTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    EngineCore::FilterSettings openFilter;
    openFilter.baseCutoffHz = 20000.0f;
    openFilter.envAmountHz = 0.0f;
    openFilter.resonance = 0.0f;
    engine.setFilterSettings(openFilter);

    EngineCore::DcFilterSettings dc;
    dc.enabled = false;
    engine.setDcFilterSettings(dc);

    Program program;
    SampleAsset asset;
    asset.lengthSamples = sampleLength;
    asset.numChannels = 2;
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(60);
    zone.velocityRange = VelocityRange::full();
    zone.rootMidiNote = 60;
    program.zones.push_back(zone);

    juce::AudioBuffer<float> stereoSample(2, sampleLength);
    stereoSample.clear();
    const auto leftTone = createTestSample(sampleLength);
    stereoSample.copyFrom(0, 0, leftTone, 0, 0, sampleLength);

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(stereoSample);
    engine.setProgram(program, samples);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    const auto leftEnergy = channelEnergy(block, 0);
    const auto rightEnergy = channelEnergy(block, 1);

    return leftEnergy > 0.01f && rightEnergy < leftEnergy * 0.05f;
}

bool runEngineProgramRoundRobinZoneSelectionTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    Program program;

    for (int index = 0; index < 3; ++index)
    {
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);
    }

    Group group;
    group.keyRange = MidiRange::single(60);
    group.velocityRange = VelocityRange::full();
    group.roundRobinGroup = 11;
    program.groups.push_back(group);

    Zone secondPosition;
    secondPosition.sampleAssetIndex = 0;
    secondPosition.groupIndex = 0;
    secondPosition.keyRange = MidiRange::single(60);
    secondPosition.velocityRange = VelocityRange::full();
    secondPosition.rootMidiNote = 60;
    secondPosition.roundRobinPosition = 2;
    program.zones.push_back(secondPosition);

    Zone firstPosition = secondPosition;
    firstPosition.sampleAssetIndex = 1;
    firstPosition.roundRobinPosition = 1;
    program.zones.push_back(firstPosition);

    Zone thirdPosition = secondPosition;
    thirdPosition.sampleAssetIndex = 2;
    thirdPosition.roundRobinPosition = 3;
    program.zones.push_back(thirdPosition);

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(createTestSample(sampleLength));
    samples.push_back(createTestSample(sampleLength));
    samples.push_back(createTestSample(sampleLength));
    engine.setProgram(program, samples);

    auto triggerAndReadZone = [&]()
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        const auto states = engine.getVoicePlaybackStates();
        auto zoneIndex = -1;
        for (const auto& state : states)
        {
            if (state.active)
            {
                zoneIndex = state.zoneIndex;
                break;
            }
        }

        engine.panic();
        return zoneIndex;
    };

    if (triggerAndReadZone() != 1)
        return false;

    if (triggerAndReadZone() != 0)
        return false;

    if (triggerAndReadZone() != 2)
        return false;

    if (triggerAndReadZone() != 1)
        return false;

    engine.setProgram(program, samples);
    return triggerAndReadZone() == 1;
}

bool runEngineProgramLayeredZonePlaybackTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    Program program;
    for (int index = 0; index < 2; ++index)
    {
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);
    }

    for (int zoneIndex = 0; zoneIndex < 2; ++zoneIndex)
    {
        Zone zone;
        zone.sampleAssetIndex = zoneIndex;
        zone.keyRange = MidiRange::single(60);
        zone.velocityRange = VelocityRange::full();
        zone.rootMidiNote = 60;
        program.zones.push_back(zone);
    }

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(createTestSample(sampleLength));
    samples.push_back(createTestSample(sampleLength));
    engine.setProgram(program, samples);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    if (engine.activeVoiceCount() != 2)
        return false;

    std::array<bool, 2> activeZones{};
    const auto states = engine.getVoicePlaybackStates();
    for (const auto& state : states)
    {
        if (state.active && state.zoneIndex >= 0 && state.zoneIndex < 2)
            activeZones[static_cast<std::size_t>(state.zoneIndex)] = true;
    }

    if (!activeZones[0] || !activeZones[1])
        return false;

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);

    midi.clear();
    for (int blockIndex = 0; blockIndex < 12; ++blockIndex)
        engine.render(block, midi);

    return engine.activeVoiceCount() == 0;
}

bool runEngineProgramVelocityFadeInTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    auto renderVelocity = [](const float velocity)
    {
        EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

        EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.001f;
        engine.setAmpEnvelope(amp);

        Program program;
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);

        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.keyRange = MidiRange::single(60);
        zone.velocityRange = VelocityRange::full();
        zone.velocityFadeIn = VelocityFadeRange::fromUnordered(32, 96);
        zone.rootMidiNote = 60;
        program.zones.push_back(zone);

        std::vector<juce::AudioBuffer<float>> samples;
        samples.push_back(createTestSample(sampleLength));
        engine.setProgram(program, samples);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, velocity), 0);
        engine.render(block, midi);
        return blockEnergy(block);
    };

    const auto lowVelocityEnergy = renderVelocity(0.25f);
    const auto midVelocityEnergy = renderVelocity(0.50f);
    const auto highVelocityEnergy = renderVelocity(1.0f);

    return lowVelocityEnergy < highVelocityEnergy * 0.05f
        && midVelocityEnergy > highVelocityEnergy * 0.25f
        && midVelocityEnergy < highVelocityEnergy * 0.75f;
}

bool runEngineProgramCycleRandomRoundRobinZoneSelectionTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    auto renderSequence = []()
    {
        EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

        EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.001f;
        engine.setAmpEnvelope(amp);

        Program program;
        for (int index = 0; index < 3; ++index)
        {
            SampleAsset asset;
            asset.lengthSamples = sampleLength;
            asset.numChannels = 1;
            asset.sampleRateHz = sampleRate;
            asset.rootMidiNote = 60;
            program.sampleAssets.push_back(asset);
        }

        Group group;
        group.keyRange = MidiRange::single(60);
        group.velocityRange = VelocityRange::full();
        group.roundRobinGroup = 17;
        group.roundRobinMode = RoundRobinMode::cycleRandom;
        program.groups.push_back(group);

        for (int zoneIndex = 0; zoneIndex < 3; ++zoneIndex)
        {
            Zone zone;
            zone.sampleAssetIndex = zoneIndex;
            zone.groupIndex = 0;
            zone.keyRange = MidiRange::single(60);
            zone.velocityRange = VelocityRange::full();
            zone.rootMidiNote = 60;
            zone.roundRobinPosition = zoneIndex + 1;
            program.zones.push_back(zone);
        }

        std::vector<juce::AudioBuffer<float>> samples;
        samples.push_back(createTestSample(sampleLength));
        samples.push_back(createTestSample(sampleLength));
        samples.push_back(createTestSample(sampleLength));
        engine.setProgram(program, samples);

        std::array<int, 6> sequence{};
        for (auto& selectedZone : sequence)
        {
            juce::AudioBuffer<float> block(channels, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            engine.render(block, midi);

            selectedZone = -1;
            const auto states = engine.getVoicePlaybackStates();
            for (const auto& state : states)
            {
                if (state.active)
                {
                    selectedZone = state.zoneIndex;
                    break;
                }
            }

            engine.panic();
        }

        return sequence;
    };

    const auto first = renderSequence();
    const auto second = renderSequence();
    if (first != second)
        return false;

    for (std::size_t index = 1; index < first.size(); ++index)
    {
        if (first[index] == first[index - 1])
            return false;
    }

    for (std::size_t cycleStart = 0; cycleStart < first.size(); cycleStart += 3)
    {
        std::array<bool, 3> seen{};
        for (std::size_t offset = 0; offset < 3; ++offset)
        {
            const auto zoneIndex = first[cycleStart + offset];
            if (zoneIndex < 0 || zoneIndex >= 3)
                return false;

            seen[static_cast<std::size_t>(zoneIndex)] = true;
        }

        if (!seen[0] || !seen[1] || !seen[2])
            return false;
    }

    return true;
}

bool runEngineProgramChokeGroupTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.5f;
    engine.setAmpEnvelope(amp);

    Program program;

    for (int index = 0; index < 2; ++index)
    {
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60 + (index * 2);
        program.sampleAssets.push_back(asset);
    }

    Group chokeGroup;
    chokeGroup.keyRange = MidiRange::fromUnordered(60, 62);
    chokeGroup.velocityRange = VelocityRange::full();
    chokeGroup.chokeGroup = 9;
    program.groups.push_back(chokeGroup);

    Zone firstZone;
    firstZone.sampleAssetIndex = 0;
    firstZone.groupIndex = 0;
    firstZone.keyRange = MidiRange::single(60);
    firstZone.velocityRange = VelocityRange::full();
    firstZone.rootMidiNote = 60;
    program.zones.push_back(firstZone);

    Zone secondZone = firstZone;
    secondZone.sampleAssetIndex = 1;
    secondZone.keyRange = MidiRange::single(62);
    secondZone.rootMidiNote = 62;
    program.zones.push_back(secondZone);

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(createTestSample(sampleLength));
    samples.push_back(createTestSample(sampleLength));
    engine.setProgram(program, samples);

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
    }

    if (engine.activeVoiceCount() != 1 || !engine.isNoteActive(60))
        return false;

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, 1.0f), 0);
        engine.render(block, midi);
    }

    return engine.activeVoiceCount() == 1
        && !engine.isNoteActive(60)
        && engine.isNoteActive(62);
}

bool runEngineProgramZoneAndGroupPanTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    EngineCore::FilterSettings openFilter;
    openFilter.baseCutoffHz = 20000.0f;
    openFilter.envAmountHz = 0.0f;
    openFilter.resonance = 0.0f;
    engine.setFilterSettings(openFilter);

    EngineCore::DcFilterSettings dc;
    dc.enabled = false;
    engine.setDcFilterSettings(dc);

    Program program;
    SampleAsset asset;
    asset.lengthSamples = sampleLength;
    asset.numChannels = 1;
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Group leftGroup;
    leftGroup.keyRange = MidiRange::single(60);
    leftGroup.velocityRange = VelocityRange::full();
    leftGroup.pan = -1.0f;
    program.groups.push_back(leftGroup);

    Group partialRightGroup;
    partialRightGroup.keyRange = MidiRange::single(62);
    partialRightGroup.velocityRange = VelocityRange::full();
    partialRightGroup.pan = 0.25f;
    program.groups.push_back(partialRightGroup);

    Zone leftZone;
    leftZone.sampleAssetIndex = 0;
    leftZone.groupIndex = 0;
    leftZone.keyRange = MidiRange::single(60);
    leftZone.velocityRange = VelocityRange::full();
    leftZone.rootMidiNote = 60;
    program.zones.push_back(leftZone);

    Zone rightZone = leftZone;
    rightZone.groupIndex = 1;
    rightZone.keyRange = MidiRange::single(62);
    rightZone.rootMidiNote = 62;
    rightZone.pan = 0.75f;
    program.zones.push_back(rightZone);

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(createTestSample(sampleLength));
    engine.setProgram(program, samples);

    auto renderNote = [&](const int note)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
        engine.render(block, midi);
        engine.panic();
        return std::pair<float, float>{ channelEnergy(block, 0), channelEnergy(block, 1) };
    };

    const auto [leftNoteLeftEnergy, leftNoteRightEnergy] = renderNote(60);
    if (leftNoteLeftEnergy <= 0.01f || leftNoteRightEnergy >= leftNoteLeftEnergy * 0.05f)
        return false;

    const auto [rightNoteLeftEnergy, rightNoteRightEnergy] = renderNote(62);
    return rightNoteRightEnergy > 0.01f && rightNoteLeftEnergy < rightNoteRightEnergy * 0.05f;
}

bool runEngineProgramZoneTriggerModeTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    auto configureEngine = [](EngineCore& engine,
                              const ZoneTriggerMode groupTriggerMode,
                              const ZoneTriggerMode zoneTriggerMode)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

        EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.005f;
        engine.setAmpEnvelope(amp);

        Program program;
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);

        Group group;
        group.keyRange = MidiRange::single(60);
        group.velocityRange = VelocityRange::full();
        group.triggerMode = groupTriggerMode;
        program.groups.push_back(group);

        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.groupIndex = 0;
        zone.keyRange = MidiRange::single(60);
        zone.velocityRange = VelocityRange::full();
        zone.rootMidiNote = 60;
        zone.triggerMode = zoneTriggerMode;
        program.zones.push_back(zone);

        std::vector<juce::AudioBuffer<float>> samples;
        samples.push_back(createTestSample(sampleLength));
        engine.setProgram(program, samples);
    };

    auto renderNoteOnAndOff = [](EngineCore& engine, const int releaseBlocks)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        midi.clear();
        for (int blockIndex = 0; blockIndex < releaseBlocks; ++blockIndex)
            engine.render(block, midi);
    };

    auto renderNoteOnOnly = [](EngineCore& engine)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
    };

    auto renderNoteOffOnly = [](EngineCore& engine)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);
    };

    EngineCore zoneOneShot;
    configureEngine(zoneOneShot, ZoneTriggerMode::gate, ZoneTriggerMode::oneShot);
    renderNoteOnAndOff(zoneOneShot, 1);
    if (zoneOneShot.activeVoiceCount() != 1 || !zoneOneShot.isNoteActive(60))
        return false;

    EngineCore groupOneShot;
    configureEngine(groupOneShot, ZoneTriggerMode::oneShot, ZoneTriggerMode::gate);
    renderNoteOnAndOff(groupOneShot, 1);
    if (groupOneShot.activeVoiceCount() != 1 || !groupOneShot.isNoteActive(60))
        return false;

    EngineCore gate;
    configureEngine(gate, ZoneTriggerMode::gate, ZoneTriggerMode::gate);
    renderNoteOnAndOff(gate, 32);

    if (gate.activeVoiceCount() != 0)
        return false;

    EngineCore zoneRelease;
    configureEngine(zoneRelease, ZoneTriggerMode::gate, ZoneTriggerMode::release);
    renderNoteOnOnly(zoneRelease);
    if (zoneRelease.activeVoiceCount() != 0)
        return false;
    renderNoteOffOnly(zoneRelease);
    if (zoneRelease.activeVoiceCount() != 1 || !zoneRelease.isNoteActive(60))
        return false;

    EngineCore groupRelease;
    configureEngine(groupRelease, ZoneTriggerMode::release, ZoneTriggerMode::gate);
    renderNoteOnOnly(groupRelease);
    if (groupRelease.activeVoiceCount() != 0)
        return false;
    renderNoteOffOnly(groupRelease);
    return groupRelease.activeVoiceCount() == 1 && groupRelease.isNoteActive(60);
}

bool runEngineProgramZoneGainAndTuneTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;

    struct RenderResult
    {
        float energy = 0.0f;
        int sampleIndex = -1;
    };

    auto renderZone = [](const float gainDb, const float tuneCents) -> RenderResult
    {
        EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(4096), sampleRate, 60);

        EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.001f;
        engine.setAmpEnvelope(amp);

        Program program;
        SampleAsset asset;
        asset.lengthSamples = 4096;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);

        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.keyRange = MidiRange::single(60);
        zone.velocityRange = VelocityRange::full();
        zone.rootMidiNote = 60;
        zone.gainDb = gainDb;
        zone.tuneCents = tuneCents;
        program.zones.push_back(zone);

        std::vector<juce::AudioBuffer<float>> samples;
        samples.push_back(createTestSample(4096));
        engine.setProgram(program, samples);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        RenderResult result;
        result.energy = blockEnergy(block);

        const auto states = engine.getVoicePlaybackStates();
        for (const auto& state : states)
        {
            if (state.active)
            {
                result.sampleIndex = state.sampleIndex;
                break;
            }
        }

        return result;
    };

    const auto neutral = renderZone(0.0f, 0.0f);
    const auto quiet = renderZone(-12.0f, 0.0f);
    const auto tunedUp = renderZone(0.0f, 1200.0f);

    if (!(neutral.energy > 0.01f))
        return false;

    if (!(quiet.energy < neutral.energy * 0.4f))
        return false;

    return tunedUp.sampleIndex > neutral.sampleIndex + 32;
}

bool runEngineProgramZoneSampleWindowTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int toneStart = 512;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    Program program;
    SampleAsset asset;
    asset.lengthSamples = 4096;
    asset.numChannels = 1;
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(60);
    zone.velocityRange = VelocityRange::full();
    zone.rootMidiNote = 60;
    zone.sampleStart = toneStart;
    zone.sampleEndExclusive = 4096;
    program.zones.push_back(zone);

    juce::AudioBuffer<float> sample(1, 4096);
    sample.clear();
    const auto tone = createTestSample(4096 - toneStart);
    sample.copyFrom(0, toneStart, tone, 0, 0, tone.getNumSamples());

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(sample);
    engine.setProgram(program, samples);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    if (blockEnergy(block) <= 0.01f)
        return false;

    const auto states = engine.getVoicePlaybackStates();
    for (const auto& state : states)
    {
        if (state.active)
            return state.sampleIndex >= toneStart;
    }

    return false;
}

bool runEngineProgramZoneLoopModeTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 128;
    constexpr int zoneStart = 32;
    constexpr int zoneEnd = 96;
    constexpr int loopStart = 48;
    constexpr int loopEnd = 64;

    auto renderTailEnergy = [](const ZoneLoopMode loopMode, const bool releaseBeforeTail)
    {
        EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

        EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.5f;
        engine.setAmpEnvelope(amp);

        EngineCore::FilterSettings openFilter;
        openFilter.baseCutoffHz = 20000.0f;
        openFilter.envAmountHz = 0.0f;
        openFilter.resonance = 0.0f;
        engine.setFilterSettings(openFilter);

        EngineCore::DcFilterSettings dc;
        dc.enabled = false;
        engine.setDcFilterSettings(dc);

        Program program;
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);

        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.keyRange = MidiRange::single(60);
        zone.velocityRange = VelocityRange::full();
        zone.rootMidiNote = 60;
        zone.sampleStart = zoneStart;
        zone.sampleEndExclusive = zoneEnd;
        zone.loopStart = loopStart;
        zone.loopEndExclusive = loopEnd;
        zone.loopMode = loopMode;
        program.zones.push_back(zone);

        juce::AudioBuffer<float> sample(1, sampleLength);
        sample.clear();
        for (int index = loopStart; index < loopEnd; ++index)
            sample.setSample(0, index, 0.8f);

        std::vector<juce::AudioBuffer<float>> samples;
        samples.push_back(sample);
        engine.setProgram(program, samples);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        engine.render(block, midi);

        if (releaseBeforeTail)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            engine.render(block, midi);
            midi.clear();
        }

        engine.render(block, midi);
        return blockEnergy(block);
    };

    const auto noLoopHeld = renderTailEnergy(ZoneLoopMode::noLoop, false);
    const auto sustainHeld = renderTailEnergy(ZoneLoopMode::sustain, false);
    const auto sustainReleased = renderTailEnergy(ZoneLoopMode::sustain, true);
    const auto continuousReleased = renderTailEnergy(ZoneLoopMode::continuous, true);

    if (noLoopHeld > 0.1f)
        return false;

    if (sustainHeld < 50.0f)
        return false;

    return continuousReleased > 50.0f && sustainReleased < continuousReleased * 0.5f;
}

juce::AudioBuffer<float> createOneCycleSine(const int sampleCount)
{
    juce::AudioBuffer<float> buffer(1, sampleCount);
    for (int i = 0; i < sampleCount; ++i)
    {
        const auto phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi
            * static_cast<double>(i) / static_cast<double>(juce::jmax(1, sampleCount)));
        buffer.setSample(0, i, std::sin(phase));
    }

    return buffer;
}

juce::AudioBuffer<float> renderHeldNote(audiocity::engine::EngineCore& engine,
                                        const int midiNote,
                                        const int blockSize,
                                        const int blocks,
                                        const int channels)
{
    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 1.0f), 0);

        engine.render(blockBuffer, midi);
        for (int ch = 0; ch < channels; ++ch)
            output.copyFrom(ch, block * blockSize, blockBuffer, ch, 0, blockSize);
    }

    return output;
}

float estimateFrequencyFromPositiveCrossings(const juce::AudioBuffer<float>& audio,
                                             const double sampleRate,
                                             const int skipSamples)
{
    if (audio.getNumChannels() <= 0)
        return 0.0f;

    const auto* data = audio.getReadPointer(0);
    const auto total = audio.getNumSamples();
    const auto start = juce::jlimit(1, juce::jmax(1, total - 1), skipSamples);
    int crossings = 0;

    for (int i = start; i < total; ++i)
    {
        const auto previous = data[i - 1];
        const auto current = data[i];
        if (previous <= 0.0f && current > 0.0f)
            ++crossings;
    }

    const auto measuredSamples = juce::jmax(1, total - start);
    const auto seconds = static_cast<float>(measuredSamples / sampleRate);
    return seconds > 0.0f ? static_cast<float>(crossings) / seconds : 0.0f;
}

bool runGeneratedCyclePitchInvariantAcrossSampleCountsTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr int blocks = 64;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 36;

    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);

    auto renderFrequency = [&](const int cycleSamples) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings fastSustain;
        fastSustain.attackSeconds = 0.0001f;
        fastSustain.decaySeconds = 0.0001f;
        fastSustain.sustainLevel = 1.0f;
        fastSustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(fastSustain);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);
        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);

        const auto rendered = renderHeldNote(engine, rootMidiNote, blockSize, blocks, channels);
        return estimateFrequencyFromPositiveCrossings(rendered, outputSampleRate, blockSize * 2);
    };

    const auto freq64 = renderFrequency(64);
    const auto freq1024 = renderFrequency(1024);
    const auto freq8192 = renderFrequency(8192);

    if (freq64 <= 0.0f || freq1024 <= 0.0f || freq8192 <= 0.0f)
        return false;

    const auto targetError64 = std::abs(freq64 - static_cast<float>(targetHz));
    const auto targetError1024 = std::abs(freq1024 - static_cast<float>(targetHz));
    const auto targetError8192 = std::abs(freq8192 - static_cast<float>(targetHz));
    const auto crossCountDelta = std::abs(freq64 - freq1024);
    const auto crossCountDeltaHigh = std::abs(freq1024 - freq8192);

    return targetError64 < 1.5f
        && targetError1024 < 1.5f
        && targetError8192 < 1.5f
        && crossCountDelta < 0.8f
        && crossCountDeltaHigh < 0.8f;
}

bool runDisplayMinMaxPreservesPolarityTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> shaped(1, 16);
    for (int i = 0; i < 8; ++i)
        shaped.setSample(0, i, -0.95f + static_cast<float>(i) * 0.05f);
    for (int i = 8; i < 16; ++i)
        shaped.setSample(0, i, 0.15f + static_cast<float>(i - 8) * 0.08f);

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(shaped, sampleRate, 60);

    const auto minMax = engine.buildDisplayMinMaxByChannel(4);
    if (minMax.size() != 1 || minMax.front().size() != 4)
        return false;

    const auto& buckets = minMax.front();
    const bool hasNegativeOnlyBucket = buckets[0].maxValue < 0.0f && buckets[1].maxValue < 0.0f;
    const bool hasPositiveOnlyBucket = buckets[2].minValue > 0.0f && buckets[3].minValue > 0.0f;

    return hasNegativeOnlyBucket && hasPositiveOnlyBucket;
}

bool runLoadedSampleMetadataForGeneratedDataTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    juce::AudioBuffer<float> generated(2, 512);
    generated.clear();
    for (int i = 0; i < generated.getNumSamples(); ++i)
    {
        generated.setSample(0, i, std::sin(static_cast<float>(i) * 0.05f));
        generated.setSample(1, i, std::cos(static_cast<float>(i) * 0.05f));
    }

    engine.setSampleData(generated, 32000.0, 60);

    return engine.getLoadedSampleLength() == 512
        && engine.getLoadedSampleChannels() == 2
        && std::abs(engine.getLoadedSampleRateHz() - 32000.0) < 0.01
        && engine.getLoadedSampleBitDepth() < 0;
}

bool runParameterIdSafetyTest()
{
    constexpr int kAaxSafeParamIdLength = 31;

    const auto processorFile = fixtureFile("src/plugin/PluginProcessor.cpp");
    if (!processorFile.existsAsFile())
        return false;

    juce::StringArray lines;
    lines.addLines(processorFile.loadFileAsString());

    std::set<juce::String> trimmedIds;
    int paramIdCount = 0;

    for (const auto& rawLine : lines)
    {
        const auto line = rawLine.trim();
        if (!line.startsWith("constexpr auto kParam"))
            continue;

        const auto firstQuote = line.indexOfChar('"');
        if (firstQuote < 0)
            return false;

        const auto secondQuote = line.indexOfChar(firstQuote + 1, '"');
        if (secondQuote <= firstQuote)
            return false;

        const auto paramId = line.substring(firstQuote + 1, secondQuote);
        ++paramIdCount;

        if (paramId.length() > kAaxSafeParamIdLength)
            return false;

        if (!trimmedIds.insert(paramId.substring(0, kAaxSafeParamIdLength)).second)
            return false;
    }

    return paramIdCount > 0;
}

bool runEditorFilterLfoPushPreservesAdvancedControlsTest()
{
    const auto editorFile = fixtureFile("src/plugin/PluginEditor.cpp");
    if (!editorFile.existsAsFile())
        return false;

    const auto editorSource = editorFile.loadFileAsString();
    const auto functionStart = editorSource.indexOf("void AudiocityAudioProcessorEditor::pushFilterSettings()");
    if (functionStart < 0)
        return false;

    const auto functionEnd = editorSource.indexOf(functionStart, "processor_.setFilterSettings(settings);");
    if (functionEnd <= functionStart)
        return false;

    const auto pushBlock = editorSource.substring(functionStart, functionEnd);

    return pushBlock.contains("filterLfoRateKeyDial_.getValue()")
        && pushBlock.contains("filterLfoAmtKeyDial_.getValue()")
        && pushBlock.contains("filterLfoStartPhaseDial_.getValue()")
        && pushBlock.contains("filterLfoStartRandDial_.getValue()")
        && pushBlock.contains("filterLfoFadeInDial_.getValue()")
        && pushBlock.contains("filterLfoRateKeySyncToggle_.getToggleState()")
        && pushBlock.contains("filterLfoKeytrackLinearToggle_.getToggleState()")
        && pushBlock.contains("filterLfoUnipolarToggle_.getToggleState()")
        && !pushBlock.contains("settings.lfoRateKeyTracking = 0.0f")
        && !pushBlock.contains("settings.lfoAmountKeyTracking = 0.0f")
        && !pushBlock.contains("settings.lfoStartPhaseDegrees = 0.0f")
        && !pushBlock.contains("settings.lfoStartPhaseRandomDegrees = 0.0f")
        && !pushBlock.contains("settings.lfoFadeInMs = 0.0f")
        && !pushBlock.contains("settings.lfoRateKeytrackInTempoSync = true")
        && !pushBlock.contains("settings.lfoKeytrackLinear = false")
        && !pushBlock.contains("settings.lfoUnipolar = false");
}

bool runEditorModulationPanelExtractionTest()
{
    const auto editorHeader = fixtureFile("src/plugin/PluginEditor.h");
    const auto editorSource = fixtureFile("src/plugin/PluginEditor.cpp");
    if (!editorHeader.existsAsFile() || !editorSource.existsAsFile())
        return false;

    const auto headerText = editorHeader.loadFileAsString();
    const auto sourceText = editorSource.loadFileAsString();
    const auto editorClassStart = headerText.indexOf("class AudiocityAudioProcessorEditor final");
    if (editorClassStart < 0)
        return false;

    const auto editorClassBody = headerText.substring(editorClassStart);
    return headerText.contains("class PlayerModulationPanel final")
        && headerText.contains("PlayerModulationPanel modulationPanel_;")
        && sourceText.contains("void PlayerModulationPanel::pushToProcessor()")
        && sourceText.contains("void PlayerModulationPanel::syncFromProcessor()")
        && sourceText.contains("void PlayerModulationPanel::forEachDial")
        && !editorClassBody.contains("void pushModulationControls()")
        && !editorClassBody.contains("void syncModulationControlsFromProcessor()");
}

bool runBackgroundImportWorkerPublishContractTest()
{
    const auto editorHeader = fixtureFile("src/plugin/PluginEditor.h");
    const auto editorSource = fixtureFile("src/plugin/PluginEditor.cpp");
    const auto processorHeader = fixtureFile("src/plugin/PluginProcessor.h");
    const auto processorSource = fixtureFile("src/plugin/PluginProcessor.cpp");
    if (!editorHeader.existsAsFile()
        || !editorSource.existsAsFile()
        || !processorHeader.existsAsFile()
        || !processorSource.existsAsFile())
    {
        return false;
    }

    const auto headerText = editorHeader.loadFileAsString();
    const auto sourceText = editorSource.loadFileAsString();
    const auto processorHeaderText = processorHeader.loadFileAsString();
    const auto processorSourceText = processorSource.loadFileAsString();

    const auto startFunction = sourceText.indexOf(
        "bool AudiocityAudioProcessorEditor::startBackgroundInstrumentLoad(");
    if (startFunction < 0)
        return false;

    const auto loadFunction = sourceText.indexOf(
        "bool AudiocityAudioProcessorEditor::loadFileAsInstrument(const juce::File& file,");
    if (loadFunction < 0)
        return false;

    const auto startFunctionBody = sourceText.substring(startFunction, loadFunction);
    const auto loadFunctionBody = sourceText.substring(loadFunction);

    return headerText.contains("std::atomic<int> backgroundImportGeneration_")
        && headerText.contains("std::atomic<bool> backgroundImportInProgress_")
        && headerText.contains("OwnedJobWorker backgroundImportWorker_")
        && headerText.contains("void cancelBackgroundInstrumentLoad()")
        && headerText.contains("const juce::File& searchFolder = {}")
        && sourceText.contains("return audiocity::plugin::supportsBackgroundImport(format);")
        && startFunctionBody.contains("canImportInstrumentInBackground(format)")
        && startFunctionBody.contains("backgroundImportWorker_.submit(")
        && startFunctionBody.contains("CancellationFlag& cancelled")
        && startFunctionBody.contains("cancelled.load(std::memory_order_acquire)")
        && startFunctionBody.contains("prepareBackgroundImportJob(")
        && startFunctionBody.contains("fallbackRootMidiNote")
        && startFunctionBody.contains("fallbackPlaybackMode")
        && startFunctionBody.contains("&cancelled)")
        && !startFunctionBody.contains("importProcessor")
        && startFunctionBody.contains("juce::MessageManager::callAsync")
        && startFunctionBody.contains("publishPreparedBackgroundImport(file, std::move(prepared))")
        && startFunctionBody.contains("Preparing ")
        && startFunctionBody.contains("Publishing ")
        && startFunctionBody.contains("generation != self->backgroundImportGeneration_.load")
        && loadFunctionBody.contains("startBackgroundInstrumentLoad(file, detectedFormat, -1, completion)")
        && sourceText.contains("startBackgroundInstrumentLoad(nkiFile,")
        && processorHeaderText.contains("bool publishPreparedImportedProgram(const juce::File& file")
        && processorHeaderText.contains("PreparedBackgroundImport prepareBackgroundImport(")
        && processorHeaderText.contains("static PreparedBackgroundImport prepareBackgroundImportJob(")
        && processorHeaderText.contains("bool publishPreparedBackgroundImport(const juce::File& file, PreparedBackgroundImport prepared)")
        && processorSourceText.contains("AudiocityAudioProcessor::PreparedBackgroundImport AudiocityAudioProcessor::prepareBackgroundImport(")
        && processorSourceText.contains("bool AudiocityAudioProcessor::publishPreparedBackgroundImport(")
        && processorSourceText.contains("bool AudiocityAudioProcessor::publishPreparedImportedProgram(")
        && processorSourceText.contains("setImportedProgramMetadata(file,")
        && !sourceText.contains(".detach()");
}

bool runAboutPageExtractionContractTest()
{
    const auto aboutHeader = fixtureFile("src/plugin/PluginAboutPage.h");
    const auto aboutSource = fixtureFile("src/plugin/PluginAboutPage.cpp");
    const auto editorHeader = fixtureFile("src/plugin/PluginEditor.h");
    const auto editorSource = fixtureFile("src/plugin/PluginEditor.cpp");
    const auto cmakeFile = fixtureFile("CMakeLists.txt");
    const auto testsCmakeFile = fixtureFile("tests/CMakeLists.txt");
    if (!aboutHeader.existsAsFile()
        || !aboutSource.existsAsFile()
        || !editorHeader.existsAsFile()
        || !editorSource.existsAsFile()
        || !cmakeFile.existsAsFile()
        || !testsCmakeFile.existsAsFile())
    {
        return false;
    }

    const auto aboutHeaderText = aboutHeader.loadFileAsString();
    const auto aboutSourceText = aboutSource.loadFileAsString();
    const auto editorHeaderText = editorHeader.loadFileAsString();
    const auto editorSourceText = editorSource.loadFileAsString();
    const auto cmakeText = cmakeFile.loadFileAsString();
    const auto testsCmakeText = testsCmakeFile.loadFileAsString();

    return aboutHeaderText.contains("class PluginAboutPage final")
        && aboutHeaderText.contains("juce::TextButton gitHubButton_{ \"GitHub\" };")
        && aboutHeaderText.contains("juce::TextButton coffeeButton_{ \"Buy Me a Coffee\" };")
        && aboutSourceText.contains("PluginAboutPage::PluginAboutPage()")
        && aboutSourceText.contains("void PluginAboutPage::paint(juce::Graphics& g)")
        && aboutSourceText.contains("void PluginAboutPage::resized()")
        && aboutSourceText.contains("juce::URL(\"https://github.com/thetheosopher/Audiocity\")")
        && aboutSourceText.contains("juce::URL(\"https://buymeacoffee.com/theosopher\")")
        && editorHeaderText.contains("#include \"PluginAboutPage.h\"")
        && editorHeaderText.contains("PluginAboutPage aboutPage_;")
        && !editorHeaderText.contains("aboutGitHubButton_")
        && !editorHeaderText.contains("aboutCoffeeButton_")
        && !editorHeaderText.contains("aboutIconImage_")
        && !editorHeaderText.contains("paintAboutPane(")
        && editorSourceText.contains("addAndMakeVisible(aboutPage_);")
        && editorSourceText.contains("aboutPage_.setVisible(showAboutTab);")
        && fixtureFile("src/plugin/PluginInstrumentWorkspace.cpp").loadFileAsString().contains("place(aboutPage_, area)")
        && !editorSourceText.contains("paintAboutPane(")
        && cmakeText.contains("src/plugin/PluginAboutPage.cpp")
        && testsCmakeText.contains("../src/plugin/PluginAboutPage.cpp");
}

bool runGeneratePageExtractionContractTest()
{
    const auto generateHeader = fixtureFile("src/plugin/PluginGeneratePage.h");
    const auto generateSource = fixtureFile("src/plugin/PluginGeneratePage.cpp");
    const auto editorHeader = fixtureFile("src/plugin/PluginEditor.h");
    const auto editorSource = fixtureFile("src/plugin/PluginEditor.cpp");
    const auto cmakeFile = fixtureFile("CMakeLists.txt");
    const auto testsCmakeFile = fixtureFile("tests/CMakeLists.txt");
    if (!generateHeader.existsAsFile()
        || !generateSource.existsAsFile()
        || !editorHeader.existsAsFile()
        || !editorSource.existsAsFile()
        || !cmakeFile.existsAsFile()
        || !testsCmakeFile.existsAsFile())
    {
        return false;
    }

    const auto generateHeaderText = generateHeader.loadFileAsString();
    const auto generateSourceText = generateSource.loadFileAsString();
    const auto editorHeaderText = editorHeader.loadFileAsString();
    const auto editorSourceText = editorSource.loadFileAsString();
    const auto cmakeText = cmakeFile.loadFileAsString();
    const auto testsCmakeText = testsCmakeFile.loadFileAsString();

    return generateHeaderText.contains("class PluginGeneratePage final")
        && generateSourceText.contains("PluginGeneratePage::PluginGeneratePage(")
        && generateSourceText.contains("void PluginGeneratePage::resized()")
        && generateSourceText.contains("addAndMakeVisible(waveformView_);")
        && editorHeaderText.contains("#include \"PluginGeneratePage.h\"")
        && editorHeaderText.contains("PluginGeneratePage generatePage_;")
        && editorSourceText.contains("addAndMakeVisible(generatePage_);")
        && editorSourceText.contains("generatePage_.setVisible(showGenerateTab);")
        && fixtureFile("src/plugin/PluginInstrumentWorkspace.cpp").loadFileAsString().contains("place(generatePage_, area)")
        && !editorSourceText.contains("generateWaveformView_.setVisible(showGenerateTab);")
        && !editorSourceText.contains("generateWaveformView_.setBounds(waveformArea);")
        && cmakeText.contains("src/plugin/PluginGeneratePage.cpp")
        && testsCmakeText.contains("../src/plugin/PluginGeneratePage.cpp");
}

bool runCapturePageExtractionContractTest()
{
    const auto captureHeader = fixtureFile("src/plugin/PluginCapturePage.h");
    const auto captureSource = fixtureFile("src/plugin/PluginCapturePage.cpp");
    const auto editorHeader = fixtureFile("src/plugin/PluginEditor.h");
    const auto editorSource = fixtureFile("src/plugin/PluginEditor.cpp");
    const auto cmakeFile = fixtureFile("CMakeLists.txt");
    const auto testsCmakeFile = fixtureFile("tests/CMakeLists.txt");
    if (!captureHeader.existsAsFile()
        || !captureSource.existsAsFile()
        || !editorHeader.existsAsFile()
        || !editorSource.existsAsFile()
        || !cmakeFile.existsAsFile()
        || !testsCmakeFile.existsAsFile())
    {
        return false;
    }

    const auto captureHeaderText = captureHeader.loadFileAsString();
    const auto captureSourceText = captureSource.loadFileAsString();
    const auto editorHeaderText = editorHeader.loadFileAsString();
    const auto editorSourceText = editorSource.loadFileAsString();
    const auto cmakeText = cmakeFile.loadFileAsString();
    const auto testsCmakeText = testsCmakeFile.loadFileAsString();

    return captureHeaderText.contains("class PluginCapturePage final")
        && captureSourceText.contains("PluginCapturePage::PluginCapturePage(")
        && captureSourceText.contains("void PluginCapturePage::resized()")
        && captureSourceText.contains("int PluginCapturePage::measureButtonWidth(")
        && editorHeaderText.contains("#include \"PluginCapturePage.h\"")
        && editorHeaderText.contains("PluginCapturePage capturePage_;")
        && editorSourceText.contains("addAndMakeVisible(capturePage_);")
        && editorSourceText.contains("capturePage_.setVisible(showCaptureTab);")
        && fixtureFile("src/plugin/PluginInstrumentWorkspace.cpp").loadFileAsString().contains("place(capturePage_, area)")
        && !editorSourceText.contains("captureWaveformView_.setVisible(showCaptureTab);")
        && !editorSourceText.contains("captureWaveformView_.setBounds(waveformArea);")
        && cmakeText.contains("src/plugin/PluginCapturePage.cpp")
        && testsCmakeText.contains("../src/plugin/PluginCapturePage.cpp");
}

bool runDecentSamplerSaveRoutingContractTest()
{
    const auto editorSource = fixtureFile("src/plugin/PluginEditor.cpp");
    const auto processorHeader = fixtureFile("src/plugin/PluginProcessor.h");
    const auto processorSource = fixtureFile("src/plugin/PluginProcessor.cpp");
    if (!editorSource.existsAsFile()
        || !processorHeader.existsAsFile()
        || !processorSource.existsAsFile())
    {
        return false;
    }

    const auto editorSourceText = editorSource.loadFileAsString();
    const auto processorHeaderText = processorHeader.loadFileAsString();
    const auto processorSourceText = processorSource.loadFileAsString();

    return editorSourceText.contains("Save Library as SFZ or DecentSampler")
        && editorSourceText.contains("*.sfz;*.dspreset")
        && editorSourceText.contains("saveImportedProgramAsDecentSampler(")
        && processorHeaderText.contains("bool saveImportedProgramAsDecentSampler(")
        && processorSourceText.contains("#include \"../engine/DecentSamplerExporter.h\"")
        && processorSourceText.contains("bool AudiocityAudioProcessor::saveImportedProgramAsDecentSampler(")
        && processorSourceText.contains("exportProgramToDecentSampler(")
        && processorSourceText.contains("ImportedProgramFormat::decentSampler");
}

bool runPlaybackModesTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore::AdsrSettings fastAdsr;
    fastAdsr.attackSeconds = 0.0001f;
    fastAdsr.decaySeconds = 0.001f;
    fastAdsr.sustainLevel = 1.0f;
    fastAdsr.releaseSeconds = 0.005f;

    {
        audiocity::engine::EngineCore gate;
        gate.prepare(sampleRate, blockSize, channels);
        gate.setAmpEnvelope(fastAdsr);
        gate.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);
        gate.setSampleData(createTestSample(4096), sampleRate, 60);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        gate.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        gate.render(block, midi);

        midi.clear();
        for (int i = 0; i < 12; ++i)
            gate.render(block, midi);

        if (gate.activeVoiceCount() != 0)
            return false;
    }

    {
        audiocity::engine::EngineCore oneShot;
        oneShot.prepare(sampleRate, blockSize, channels);
        oneShot.setAmpEnvelope(fastAdsr);
        oneShot.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        oneShot.setSampleData(createTestSample(4096), sampleRate, 60);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        oneShot.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        oneShot.render(block, midi);

        midi.clear();
        oneShot.render(block, midi);

        if (oneShot.activeVoiceCount() == 0)
            return false;
    }

    // Loop mode: without note off, playback should continue beyond sample length
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setAmpEnvelope(fastAdsr);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setSampleData(createTestSample(128), sampleRate, 60);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        engine.render(block, midi);
        engine.render(block, midi);

        if (blockEnergy(block) < 0.2f)
            return false;
    }

    return true;
}

bool runLoopMarkersTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> shaped(1, 64);
    shaped.clear();
    for (int i = 16; i < 32; ++i)
        shaped.setSample(0, i, 0.9f);

    audiocity::engine::EngineCore::AdsrSettings slowRelease;
    slowRelease.attackSeconds = 0.0001f;
    slowRelease.decaySeconds = 0.001f;
    slowRelease.sustainLevel = 1.0f;
    slowRelease.releaseSeconds = 0.5f;

    // Loop markers should keep playback in the [16, 31] region while note is held.
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setAmpEnvelope(slowRelease);
        engine.setSampleData(shaped, sampleRate, 60);
        engine.setLoopPoints(16, 31);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        engine.render(block, midi);
        engine.render(block, midi);

        if (blockEnergy(block) < 10.0f)
            return false;
    }

    // After note-off, loop should stop and voice should enter release.
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setAmpEnvelope(slowRelease);
        engine.setSampleData(shaped, sampleRate, 60);
        engine.setLoopPoints(16, 31);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        // After note-off the voice should eventually stop
        midi.clear();
        for (int i = 0; i < 200; ++i)
            engine.render(block, midi);

        if (engine.activeVoiceCount() != 0)
            return false;
    }

    return true;
}

float maxConsecutiveDelta(const juce::AudioBuffer<float>& block)
{
    float maxDelta = 0.0f;
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const auto* data = block.getReadPointer(channel);
        for (int i = 1; i < block.getNumSamples(); ++i)
            maxDelta = juce::jmax(maxDelta, std::abs(data[i] - data[i - 1]));
    }

    return maxDelta;
}

bool runLoopCrossfadeSmoothsBoundaryTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> stepped(1, 256);
    for (int i = 0; i < stepped.getNumSamples(); ++i)
        stepped.setSample(0, i, i < 128 ? -0.9f : 0.9f);

    auto renderBoundaryDelta = [&](const int crossfadeSamples)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(stepped, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setLoopPoints(32, 220);
        engine.setLoopCrossfadeSamples(crossfadeSamples);

        audiocity::engine::EngineCore::AdsrSettings holdAdsr;
        holdAdsr.attackSeconds = 0.0001f;
        holdAdsr.decaySeconds = 0.001f;
        holdAdsr.sustainLevel = 1.0f;
        holdAdsr.releaseSeconds = 0.5f;
        engine.setAmpEnvelope(holdAdsr);

        audiocity::engine::EngineCore::FilterSettings openFilter;
        openFilter.baseCutoffHz = 20000.0f;
        openFilter.envAmountHz = 0.0f;
        openFilter.resonance = 0.0f;
        engine.setFilterSettings(openFilter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 4; ++i)
            engine.render(block, midi);

        return maxConsecutiveDelta(block);
    };

    const auto noCrossfadeDelta = renderBoundaryDelta(0);
    const auto crossfadedDelta = renderBoundaryDelta(24);

    return crossfadedDelta < (noCrossfadeDelta * 0.85f);
}

bool runPanicSilencesAudioImmediatelyTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    if (blockEnergy(block) <= 0.001f)
        return false;

    engine.panic();

    midi.clear();
    engine.render(block, midi);
    const auto postPanicEnergy = blockEnergy(block);

    return postPanicEnergy <= 1.0e-6f;
}

bool runLoadSampleResetsPlaybackAndLoopRangesTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_load_reset_test", ".wav");

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 440.0 / sampleRate);
            buffer.setSample(0, i, 0.3f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
            return false;
    }

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setSampleWindow(64, 192);
    engine.setLoopPoints(80, 160);

    const auto loaded = engine.loadSampleFromFile(tempFile);
    tempFile.deleteFile();

    if (!loaded)
        return false;

    return engine.getSampleWindowStart() == 0
        && engine.getSampleWindowEnd() == sampleLength - 1
        && engine.getLoopStart() == 0
        && engine.getLoopEnd() == sampleLength - 1;
}

bool runLoadSampleClearsProgramSnapshotTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_load_clears_program_test", ".wav");

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 220.0 / sampleRate);
            buffer.setSample(0, i, 0.4f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
            return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    Program program;
    SampleAsset asset;
    asset.lengthSamples = sampleLength;
    asset.numChannels = 1;
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 72;
    program.sampleAssets.push_back(asset);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(72);
    zone.velocityRange = VelocityRange::full();
    zone.rootMidiNote = 72;
    program.zones.push_back(zone);

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(createTestSample(sampleLength));
    engine.setProgram(program, samples);
    if (!engine.hasProgram())
        return false;

    const auto loaded = engine.loadSampleFromFile(tempFile);
    tempFile.deleteFile();
    if (!loaded || engine.hasProgram())
        return false;

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    return blockEnergy(block) > 0.001f;
}

bool runLoadSampleResetsEnvelopeAndFilterDefaultsTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_load_defaults_test", ".wav");

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 330.0 / sampleRate);
            buffer.setSample(0, i, 0.3f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
            return false;
    }

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    audiocity::engine::EngineCore::AdsrSettings customAmp;
    customAmp.attackSeconds = 0.320f;
    customAmp.decaySeconds = 0.410f;
    customAmp.sustainLevel = 0.23f;
    customAmp.releaseSeconds = 0.580f;
    engine.setAmpEnvelope(customAmp);

    audiocity::engine::EngineCore::AdsrSettings customFilterEnv;
    customFilterEnv.attackSeconds = 0.420f;
    customFilterEnv.decaySeconds = 0.530f;
    customFilterEnv.sustainLevel = 0.64f;
    customFilterEnv.releaseSeconds = 0.710f;
    engine.setFilterEnvelope(customFilterEnv);

    auto customPitchLfo = engine.getPitchLfoSettings();
    customPitchLfo.rateHz = 7.5f;
    customPitchLfo.depthCents = 42.0f;
    engine.setPitchLfoSettings(customPitchLfo);

    auto customFilter = engine.getFilterSettings();
    customFilter.baseCutoffHz = 420.0f;
    customFilter.envAmountHz = 9500.0f;
    customFilter.resonance = 0.77f;
    customFilter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::highPass24;
    customFilter.lfoRateHz = 12.0f;
    customFilter.lfoAmountHz = 3200.0f;
    customFilter.lfoTempoSync = true;
    engine.setFilterSettings(customFilter);

    const auto loaded = engine.loadSampleFromFile(tempFile);
    tempFile.deleteFile();

    if (!loaded)
        return false;

    const auto amp = engine.getAmpEnvelope();
    if (std::abs(amp.attackSeconds - 0.005f) > 1.0e-6f
        || std::abs(amp.decaySeconds - 0.150f) > 1.0e-6f
        || std::abs(amp.sustainLevel - 0.85f) > 1.0e-6f
        || std::abs(amp.releaseSeconds - 0.150f) > 1.0e-6f)
    {
        return false;
    }

    const auto filterEnv = engine.getFilterEnvelope();
    if (std::abs(filterEnv.attackSeconds - 0.001f) > 1.0e-6f
        || std::abs(filterEnv.decaySeconds - 0.120f) > 1.0e-6f
        || std::abs(filterEnv.sustainLevel - 0.0f) > 1.0e-6f
        || std::abs(filterEnv.releaseSeconds - 0.100f) > 1.0e-6f)
    {
        return false;
    }

    const auto filter = engine.getFilterSettings();
    const auto pitchLfo = engine.getPitchLfoSettings();
    return std::abs(filter.baseCutoffHz - 18000.0f) <= 1.0e-6f
        && std::abs(filter.envAmountHz - 0.0f) <= 1.0e-6f
        && std::abs(filter.resonance - 0.0f) <= 1.0e-6f
        && filter.mode == audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12
        && std::abs(filter.lfoRateHz - 0.0f) <= 1.0e-6f
        && std::abs(filter.lfoAmountHz - 0.0f) <= 1.0e-6f
        && !filter.lfoTempoSync
        && std::abs(pitchLfo.rateHz - 0.0f) <= 1.0e-6f
        && std::abs(pitchLfo.depthCents - 0.0f) <= 1.0e-6f;
}

bool runLoadNcwSampleViaConverterTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_load_ncw_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto tempFile = tempDirectory.getChildFile("Converted.ncw");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 440.0 / sampleRate);
            buffer.setSample(0, i, 0.35f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    ScopedEnvironmentVariable converterCommand(
        "AUDIOCITY_NCW_CONVERTER_COMMAND",
        "cmd.exe /c copy /y {input} {output} >nul");
    if (!converterCommand.valid)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    const auto loaded = engine.loadSampleFromFile(tempFile);
    const auto ok = loaded
        && engine.getLoadedSampleLength() == sampleLength
        && engine.getSamplePath() == tempFile.getFullPathName();

    tempDirectory.deleteRecursively();
    return ok;
}

bool runLoadNcwSampleViaConverterQuotesShellMetacharactersTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 256;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_load_ncw_shell_metachar_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto tempFile = tempDirectory.getChildFile("Converted & Echo.ncw");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 330.0 / sampleRate);
            buffer.setSample(0, i, 0.25f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    ScopedEnvironmentVariable converterCommand(
        "AUDIOCITY_NCW_CONVERTER_COMMAND",
        "cmd.exe /c copy /y {input} {output} >nul");
    if (!converterCommand.valid)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    const auto loaded = engine.loadSampleFromFile(tempFile);
    const auto ok = loaded
        && engine.getLoadedSampleLength() == sampleLength
        && engine.getSamplePath() == tempFile.getFullPathName();

    tempDirectory.deleteRecursively();
    return ok;
}

bool runFilterModulationAmountsAreBipolarTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    auto filter = engine.getFilterSettings();
    filter.envAmountHz = -3000.0f;
    filter.velocityAmountHz = 4500.0f;
    engine.setFilterSettings(filter);

    auto applied = engine.getFilterSettings();
    if (std::abs(applied.envAmountHz - (-3000.0f)) > 1.0e-6f
        || std::abs(applied.velocityAmountHz - 4500.0f) > 1.0e-6f)
    {
        return false;
    }

    filter.envAmountHz = -50000.0f;
    filter.velocityAmountHz = 50000.0f;
    engine.setFilterSettings(filter);

    applied = engine.getFilterSettings();
    return std::abs(applied.envAmountHz - (-12000.0f)) <= 1.0e-6f
        && std::abs(applied.velocityAmountHz - 12000.0f) <= 1.0e-6f;
}

bool runEmbeddedLoopMetadataLoadsWithoutRootNoteTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;
    constexpr int loopStart = 96;
    constexpr int loopEnd = 320;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_embedded_loop_test", ".wav");

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
            return false;

        juce::StringPairArray metadata;
        metadata.set("loop0start", juce::String(loopStart));
        metadata.set("loop0end", juce::String(loopEnd));

        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(output.get(), sampleRate, 1, 16, metadata, 0));
        if (writer == nullptr)
            return false;

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 220.0 / sampleRate);
            buffer.setSample(0, i, 0.3f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
            return false;
    }

    auto getMetadataIntCaseInsensitive = [](const juce::StringPairArray& metadata, const juce::String& key) -> int
    {
        const auto keys = metadata.getAllKeys();
        for (int i = 0; i < keys.size(); ++i)
        {
            if (keys[i].equalsIgnoreCase(key))
            {
                const auto value = metadata.getValue(keys[i], {}).trim();
                if (value.containsOnly("-0123456789"))
                    return value.getIntValue();
            }
        }

        return -1;
    };

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(tempFile));
    if (reader == nullptr)
    {
        tempFile.deleteFile();
        return false;
    }

    const auto readBackStart = getMetadataIntCaseInsensitive(reader->metadataValues, "Loop0Start");
    const auto readBackEnd = getMetadataIntCaseInsensitive(reader->metadataValues, "Loop0End");
    if (readBackStart < 0 || readBackEnd <= readBackStart)
    {
        tempFile.deleteFile();
        return true;
    }

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    const auto loaded = engine.loadSampleFromFile(tempFile);
    tempFile.deleteFile();

    if (!loaded)
        return false;

    return engine.getSampleWindowStart() == 0
        && engine.getSampleWindowEnd() == sampleLength - 1
        && engine.getLoopStart() == readBackStart
        && engine.getLoopEnd() == readBackEnd
        && engine.getPlaybackMode() == audiocity::engine::EngineCore::PlaybackMode::loop;
}

bool runRexRuntimeFallbackSmokeTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto rexFixture = fixtureFile("third_party/REXSDK_Win_1.9.2/REX Test Protocol Files/120Mono.rx2");
    if (!rexFixture.existsAsFile())
        return true;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    const auto rexRuntimeAvailable = engine.isRexRuntimeAvailable();
    const auto loaded = engine.loadSampleFromFile(rexFixture);

    if (!rexRuntimeAvailable)
        return !loaded;

    if (!loaded || engine.getLoadedSampleLength() <= 0)
        return false;

    const auto fullEnd = juce::jmax(0, engine.getLoadedSampleLength() - 1);
    return engine.getSampleWindowStart() == 0
        && engine.getSampleWindowEnd() == fullEnd
        && engine.getLoopStart() == 0
        && engine.getLoopEnd() == fullEnd
        && engine.getPlaybackMode() == audiocity::engine::EngineCore::PlaybackMode::loop;
}

bool runRexSliceProgramBuildTest()
{
    const auto rexFixture = fixtureFile("third_party/REXSDK_Win_1.9.2/REX Test Protocol Files/120Mono.rx2");
    if (!rexFixture.existsAsFile())
        return true;

    const auto runtimeAvailable = audiocity::engine::rex::isRuntimeAvailable();
    audiocity::engine::rex::DecodedLoop decoded;
    const auto decodedOk = audiocity::engine::rex::decodeFile(rexFixture, decoded);

    if (!runtimeAvailable)
        return !decodedOk;

    if (!decodedOk
        || decoded.slices.size() <= 1
        || decoded.audio.getNumSamples() <= 0
        || decoded.sampleRateHz <= 0.0)
    {
        return false;
    }

    int accumulatedSliceSamples = 0;
    for (std::size_t sliceIndex = 0; sliceIndex < decoded.slices.size(); ++sliceIndex)
    {
        const auto& slice = decoded.slices[sliceIndex];
        if (slice.audio.getNumSamples() <= 0 || slice.audio.getNumChannels() <= 0)
            return false;

        if (slice.startSample != accumulatedSliceSamples)
            return false;

        accumulatedSliceSamples += slice.audio.getNumSamples();
    }

    if (accumulatedSliceSamples != decoded.audio.getNumSamples())
        return false;

    audiocity::engine::rex::ChromaticSliceProgram sliceProgram;
    if (!audiocity::engine::rex::buildChromaticSliceProgram(rexFixture, decoded, sliceProgram))
        return false;

    if (sliceProgram.baseMidiNote != audiocity::engine::rex::kDefaultSliceBaseMidiNote
        || sliceProgram.program.sampleAssets.size() != decoded.slices.size()
        || sliceProgram.program.zones.size() != decoded.slices.size()
        || sliceProgram.sampleDataByAsset.size() != decoded.slices.size())
    {
        return false;
    }

    for (std::size_t sliceIndex = 0; sliceIndex < sliceProgram.program.zones.size(); ++sliceIndex)
    {
        const auto& zone = sliceProgram.program.zones[sliceIndex];
        const auto& asset = sliceProgram.program.sampleAssets[sliceIndex];
        const auto expectedNote = audiocity::engine::rex::kDefaultSliceBaseMidiNote + static_cast<int>(sliceIndex);
        if (zone.sampleAssetIndex != static_cast<int>(sliceIndex)
            || zone.keyRange.low != expectedNote
            || zone.keyRange.high != expectedNote
            || zone.rootMidiNote != expectedNote
            || zone.triggerMode != audiocity::engine::ZoneTriggerMode::oneShot
            || zone.loopMode != audiocity::engine::ZoneLoopMode::noLoop
            || asset.rootMidiNote != expectedNote
            || asset.lengthSamples != decoded.slices[sliceIndex].audio.getNumSamples()
            || asset.numChannels != decoded.slices[sliceIndex].audio.getNumChannels())
        {
            return false;
        }
    }

    return sliceProgram.program.hasPlayableZones();
}

bool runTransientSliceProgramBuildTest()
{
    constexpr int totalSamples = 12000;
    constexpr int hitSpacing = 4000;

    juce::AudioBuffer<float> sample(1, totalSamples);
    sample.clear();
    for (int hit = 0; hit < 3; ++hit)
    {
        const auto hitStart = hit * hitSpacing;
        for (int offset = 0; offset < 480 && hitStart + offset < totalSamples; ++offset)
        {
            const auto decay = std::exp(-static_cast<float>(offset) / 70.0f);
            sample.setSample(0, hitStart + offset, 0.95f * decay);
        }
    }

    audiocity::engine::transient_slice::TransientSliceProgram sliceProgram;
    audiocity::engine::transient_slice::TransientSliceSettings settings;
    settings.maxSlices = 8;
    settings.minSliceSamples = 1500;

    if (!audiocity::engine::transient_slice::buildTransientSliceProgram(
            juce::File("C:/Library/Transient.wav"), sample, 48000.0, sliceProgram, settings))
    {
        return false;
    }

    if (sliceProgram.program.sampleAssets.size() != 1
        || sliceProgram.sampleDataByAsset.size() != 1
        || sliceProgram.program.zones.size() != 3
        || sliceProgram.sliceStartSamples.size() < 3)
    {
        return false;
    }

    if (sliceProgram.sliceStartSamples[0] != 0
        || sliceProgram.sliceStartSamples[1] < 3400
        || sliceProgram.sliceStartSamples[1] > 4100
        || sliceProgram.sliceStartSamples[2] < 7400
        || sliceProgram.sliceStartSamples[2] > 8100)
    {
        return false;
    }

    for (std::size_t sliceIndex = 0; sliceIndex < sliceProgram.program.zones.size(); ++sliceIndex)
    {
        const auto& zone = sliceProgram.program.zones[sliceIndex];
        const auto expectedNote = audiocity::engine::transient_slice::kDefaultSliceBaseMidiNote + static_cast<int>(sliceIndex);
        if (zone.sampleAssetIndex != 0
            || zone.keyRange.low != expectedNote
            || zone.keyRange.high != expectedNote
            || zone.rootMidiNote != expectedNote
            || zone.triggerMode != audiocity::engine::ZoneTriggerMode::oneShot
            || zone.loopMode != audiocity::engine::ZoneLoopMode::noLoop
            || zone.sampleEndExclusive <= zone.sampleStart)
        {
            return false;
        }
    }

    return sliceProgram.program.zones[0].sampleStart == 0
        && sliceProgram.program.zones[0].sampleEndExclusive <= sliceProgram.program.zones[1].sampleStart
        && sliceProgram.program.zones[1].sampleEndExclusive <= sliceProgram.program.zones[2].sampleStart;
}

bool runEditorSampleEditControlsTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> ascending(1, 64);
    for (int i = 0; i < ascending.getNumSamples(); ++i)
        ascending.setSample(0, i, static_cast<float>(i) / 63.0f);

    audiocity::engine::EngineCore::AdsrSettings flatAdsr;
    flatAdsr.attackSeconds = 0.0001f;
    flatAdsr.decaySeconds = 0.0001f;
    flatAdsr.sustainLevel = 1.0f;
    flatAdsr.releaseSeconds = 0.001f;

    audiocity::engine::EngineCore::FilterSettings openFilter;
    openFilter.baseCutoffHz = 18000.0f;
    openFilter.envAmountHz = 0.0f;

    auto configureEngine = [&](audiocity::engine::EngineCore& engine)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::cpu);
        engine.setAmpEnvelope(flatAdsr);
        engine.setFilterSettings(openFilter);
        engine.setSampleData(ascending, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setSampleWindow(8, 39);
    };

    auto renderWithEdits = [&](const bool reverse, const int fadeIn, const int fadeOut)
    {
        audiocity::engine::EngineCore engine;
        configureEngine(engine);
        engine.setReversePlayback(reverse);
        engine.setFadeSamples(fadeIn, fadeOut);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto forward = renderWithEdits(false, 0, 0);
    const auto reversed = renderWithEdits(true, 0, 0);

    float forwardEarly = 0.0f;
    float forwardLate = 0.0f;
    float reverseEarly = 0.0f;
    float reverseLate = 0.0f;

    for (int i = 4; i < 12; ++i)
    {
        forwardEarly += std::abs(forward.getSample(0, i));
        reverseEarly += std::abs(reversed.getSample(0, i));
    }

    for (int i = 20; i < 28; ++i)
    {
        forwardLate += std::abs(forward.getSample(0, i));
        reverseLate += std::abs(reversed.getSample(0, i));
    }

    if (!(forwardLate > forwardEarly * 1.25f))
        return false;

    if (!(reverseEarly > reverseLate * 1.25f))
        return false;

    const auto faded = renderWithEdits(false, 10, 10);

    float noFadeHead = 0.0f;
    float fadedHead = 0.0f;
    float noFadeTail = 0.0f;
    float fadedTail = 0.0f;

    for (int i = 1; i < 8; ++i)
    {
        noFadeHead += std::abs(forward.getSample(0, i));
        fadedHead += std::abs(faded.getSample(0, i));
    }

    for (int i = 24; i < 31; ++i)
    {
        noFadeTail += std::abs(forward.getSample(0, i));
        fadedTail += std::abs(faded.getSample(0, i));
    }

    if (!(fadedHead < noFadeHead * 0.65f))
        return false;

    if (!(fadedTail < noFadeTail * 0.75f))
        return false;

    return true;
}

bool runPolyphonicDifferentNotesLayerWhenMonoOffTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setMonoMode(false);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 16);

    juce::AudioBuffer<float> block(channels, blockSize);
    engine.render(block, midi);

    return engine.activeVoiceCount() >= 2 && engine.isNoteActive(60) && engine.isNoteActive(64);
}

bool runMonoLegatoUsesSingleVoiceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setMonoMode(true);
    engine.setLegatoMode(true);

    audiocity::engine::EngineCore::AdsrSettings fastAdsr;
    fastAdsr.attackSeconds = 0.0001f;
    fastAdsr.decaySeconds = 0.001f;
    fastAdsr.sustainLevel = 1.0f;
    fastAdsr.releaseSeconds = 0.004f;
    engine.setAmpEnvelope(fastAdsr);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;

    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
    engine.render(block, midi);

    if (engine.activeVoiceCount() != 1 || !engine.isNoteActive(72) || engine.isNoteActive(60))
        return false;

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 72), 0);
    engine.render(block, midi);

    midi.clear();
    for (int i = 0; i < 10; ++i)
        engine.render(block, midi);

    return engine.activeVoiceCount() == 0;
}

bool runPolyphonicSameNoteReleaseTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setMonoMode(false);
    engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

    audiocity::engine::EngineCore::AdsrSettings adsr;
    adsr.attackSeconds = 0.0001f;
    adsr.decaySeconds = 0.001f;
    adsr.sustainLevel = 1.0f;
    adsr.releaseSeconds = 0.02f;
    engine.setAmpEnvelope(adsr);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;

    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 32);
    engine.render(block, midi);

    if (engine.activeVoiceCount() != 1)
        return false;

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);

    midi.clear();
    for (int i = 0; i < 120; ++i)
        engine.render(block, midi);

    return engine.activeVoiceCount() == 0;
}

bool runDenseLoopModeOverflowDoesNotStickNotesTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int denseEventCount = 1600;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setMonoMode(false);
    engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

    audiocity::engine::EngineCore::AdsrSettings adsr;
    adsr.attackSeconds = 0.0001f;
    adsr.decaySeconds = 0.001f;
    adsr.sustainLevel = 1.0f;
    adsr.releaseSeconds = 0.01f;
    engine.setAmpEnvelope(adsr);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < denseEventCount; ++i)
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    for (int i = 0; i < denseEventCount; ++i)
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);

    engine.render(block, midi);

    midi.clear();
    for (int i = 0; i < 260; ++i)
        engine.render(block, midi);

    return engine.activeVoiceCount() == 0;
}

bool runQueueSaturatedByPitchBendStillReleasesNoteOffTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int saturationEvents = 1600;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

    audiocity::engine::EngineCore::AdsrSettings adsr;
    adsr.attackSeconds = 0.0001f;
    adsr.decaySeconds = 0.001f;
    adsr.sustainLevel = 1.0f;
    adsr.releaseSeconds = 0.01f;
    engine.setAmpEnvelope(adsr);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;

    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);
    if (engine.activeVoiceCount() == 0)
        return false;

    midi.clear();
    for (int i = 0; i < saturationEvents; ++i)
        midi.addEvent(juce::MidiMessage::pitchWheel(1, (i % 2 == 0) ? 16383 : 0), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);

    midi.clear();
    for (int i = 0; i < 260; ++i)
        engine.render(block, midi);

    return engine.activeVoiceCount() == 0;
}

juce::AudioBuffer<float> renderLegatoTransitionBuffer(const float glideSeconds)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 6;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setMonoMode(true);
    engine.setLegatoMode(true);
    engine.setGlideSeconds(glideSeconds);

    juce::AudioBuffer<float> rendered(channels, blockSize * blocks);

    for (int blockIndex = 0; blockIndex < blocks; ++blockIndex)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        if (blockIndex == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

        if (blockIndex == 1)
            midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);

        if (blockIndex == 4)
            midi.addEvent(juce::MidiMessage::noteOff(1, 72), 0);

        engine.render(block, midi);

        for (int channel = 0; channel < channels; ++channel)
            rendered.copyFrom(channel, blockIndex * blockSize, block, channel, 0, blockSize);
    }

    return rendered;
}

bool runGlideChangesLegatoTransitionTest()
{
    const auto immediate = renderLegatoTransitionBuffer(0.0f);
    const auto gliding = renderLegatoTransitionBuffer(0.05f);
    return !buffersAreEqual(immediate, gliding, 1.0e-6f);
}

bool runPreloadSegmentationDeterminismTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto sample = createTestSample(8192);

    audiocity::engine::EngineCore fullPreload;
    fullPreload.prepare(sampleRate, blockSize, channels);
    fullPreload.setPreloadSamples(16384);
    fullPreload.setSampleData(sample, sampleRate, 60);

    audiocity::engine::EngineCore segmented;
    segmented.prepare(sampleRate, blockSize, channels);
    segmented.setPreloadSamples(512);
    segmented.setSampleData(sample, sampleRate, 60);

    const auto a = renderSequence(fullPreload);
    const auto b = renderSequence(segmented);
    return buffersAreEqual(a, b, 1.0e-6f);
}

juce::AudioBuffer<float> renderSequenceWithOptionalPreloadChange(
    audiocity::engine::EngineCore& engine,
    const bool applyChange,
    const int changedPreloadSamples)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 8;

    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

        if (block == 5)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 32);

        if (applyChange && block == 2)
            engine.setPreloadSamples(changedPreloadSamples);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            output.copyFrom(channel, block * blockSize, blockBuffer, channel, 0, blockSize);
    }

    return output;
}

bool runRuntimePreloadChangeStabilityTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto sample = createTestSample(8192);

    audiocity::engine::EngineCore reference;
    reference.prepare(sampleRate, blockSize, channels);
    reference.setPreloadSamples(4096);
    reference.setSampleData(sample, sampleRate, 60);

    audiocity::engine::EngineCore changed;
    changed.prepare(sampleRate, blockSize, channels);
    changed.setPreloadSamples(4096);
    changed.setSampleData(sample, sampleRate, 60);

    const auto stable = renderSequenceWithOptionalPreloadChange(reference, false, 512);
    const auto withChange = renderSequenceWithOptionalPreloadChange(changed, true, 512);
    return buffersAreEqual(stable, withChange, 1.0e-6f);
}

juce::AudioBuffer<float> renderLoopSequenceWithOptionalPreloadChange(
    audiocity::engine::EngineCore& engine,
    const bool applyChange,
    const int changedPreloadSamples)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 10;

    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

        if (applyChange && block == 4)
            engine.setPreloadSamples(changedPreloadSamples);

        if (block == 8)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 64);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            output.copyFrom(channel, block * blockSize, blockBuffer, channel, 0, blockSize);
    }

    return output;
}

juce::AudioBuffer<float> renderSequenceWithOptionalSampleReload(
    audiocity::engine::EngineCore& engine,
    const juce::AudioBuffer<float>& sample,
    const double sampleRate,
    const bool applyReload)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 8;

    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

        if (block == 5)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 32);

        if (applyReload && block == 2)
            engine.setSampleData(sample, sampleRate, 60);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            output.copyFrom(channel, block * blockSize, blockBuffer, channel, 0, blockSize);
    }

    return output;
}

bool runRuntimeSampleReloadStabilityTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto sample = createTestSample(8192);

    audiocity::engine::EngineCore reference;
    reference.prepare(sampleRate, blockSize, channels);
    reference.setSampleData(sample, sampleRate, 60);

    audiocity::engine::EngineCore changed;
    changed.prepare(sampleRate, blockSize, channels);
    changed.setSampleData(sample, sampleRate, 60);

    const auto stable = renderSequenceWithOptionalSampleReload(reference, sample, sampleRate, false);
    const auto withReload = renderSequenceWithOptionalSampleReload(changed, sample, sampleRate, true);
    return buffersAreEqual(stable, withReload, 1.0e-6f);
}

bool runLoopModeRuntimePreloadChangeStabilityTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto shaped = juce::AudioBuffer<float>(1, 96);
    shaped.clear();
    for (int i = 20; i < 56; ++i)
        shaped.setSample(0, i, 0.7f * std::sin(static_cast<float>(i) * 0.3f));

    audiocity::engine::EngineCore::AdsrSettings slowRelease;
    slowRelease.attackSeconds = 0.0001f;
    slowRelease.decaySeconds = 0.001f;
    slowRelease.sustainLevel = 1.0f;
    slowRelease.releaseSeconds = 0.4f;

    audiocity::engine::EngineCore reference;
    reference.prepare(sampleRate, blockSize, channels);
    reference.setAmpEnvelope(slowRelease);
    reference.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
    reference.setPreloadSamples(2048);
    reference.setSampleData(shaped, sampleRate, 60);
    reference.setLoopPoints(20, 55);

    audiocity::engine::EngineCore changed;
    changed.prepare(sampleRate, blockSize, channels);
    changed.setAmpEnvelope(slowRelease);
    changed.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
    changed.setPreloadSamples(2048);
    changed.setSampleData(shaped, sampleRate, 60);
    changed.setLoopPoints(20, 55);

    const auto stable = renderLoopSequenceWithOptionalPreloadChange(reference, false, 256);
    const auto withChange = renderLoopSequenceWithOptionalPreloadChange(changed, true, 256);
    return buffersAreEqual(stable, withChange, 1.0e-6f);
}

bool runSegmentRebuildCounterTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    const auto baseCount = engine.getSegmentRebuildCount();

    const auto sample = createTestSample(4096);
    engine.setSampleData(sample, sampleRate, 60);
    const auto afterLoadCount = engine.getSegmentRebuildCount();

    if (afterLoadCount <= baseCount)
        return false;

    engine.setPreloadSamples(512);
    const auto afterPreloadChangeCount = engine.getSegmentRebuildCount();

    return afterPreloadChangeCount > afterLoadCount;
}

bool runProgramPreloadMetricsAndRebuildTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setPreloadSamples(2048);

    Program program;

    SampleAsset firstAsset;
    firstAsset.displayName = "layer_a.wav";
    firstAsset.sampleRateHz = sampleRate;
    firstAsset.rootMidiNote = 60;
    program.sampleAssets.push_back(firstAsset);

    SampleAsset secondAsset = firstAsset;
    secondAsset.displayName = "layer_b.wav";
    program.sampleAssets.push_back(secondAsset);

    Zone firstZone;
    firstZone.sampleAssetIndex = 0;
    firstZone.keyRange = MidiRange::single(60);
    firstZone.rootMidiNote = 60;
    program.zones.push_back(firstZone);

    Zone secondZone = firstZone;
    secondZone.sampleAssetIndex = 1;
    secondZone.keyRange = MidiRange::single(61);
    secondZone.rootMidiNote = 61;
    program.zones.push_back(secondZone);

    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    sampleDataByAsset.push_back(createTestSample(4096));
    sampleDataByAsset.push_back(createTestSample(1536));

    engine.setProgram(program, sampleDataByAsset);
    const auto afterLoadCount = engine.getSegmentRebuildCount();

    if (engine.getLoadedPreloadSamples() != 3584)
        return false;

    if (engine.getLoadedStreamSamples() != 2048)
        return false;

    engine.setPreloadSamples(512);
    const auto afterPreloadChangeCount = engine.getSegmentRebuildCount();

    return afterPreloadChangeCount > afterLoadCount
        && engine.getLoadedPreloadSamples() == 1024
        && engine.getLoadedStreamSamples() == 4608;
}

bool runSingleSampleFileStreamingPreloadMetricsTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_single_stream_metrics", ".wav");

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 220.0 / sampleRate);
            buffer.setSample(0, i, 0.35f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
            return false;
    }

    auto cleanup = [&]() { tempFile.deleteFile(); };

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setPreloadSamples(256);

    if (!engine.loadSampleFromFile(tempFile))
    {
        cleanup();
        return false;
    }

    auto renderBlock = [&](const bool noteOn) -> float
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        if (noteOn)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

        engine.render(block, midi);
        return blockEnergy(block);
    };

    if (engine.getLoadedPreloadSamples() != 256
        || engine.getLoadedStreamSamples() != (sampleLength - 256))
    {
        cleanup();
        return false;
    }

    const auto afterLoadCount = engine.getSegmentRebuildCount();
    if (renderBlock(true) <= 0.001f)
    {
        cleanup();
        return false;
    }

    renderBlock(false);
    renderBlock(false);

    if (engine.getStreamPrimeRequestCount() != 4
        || engine.getStreamPrimeCacheHitCount() != 3
        || engine.getStreamPrimeCacheMissCount() != 1
        || engine.getStreamPrimeServiceCount() != 0)
    {
        cleanup();
        return false;
    }

    engine.serviceStreamPriming();
    renderBlock(false);

    if (engine.getStreamPrimeRequestCount() != 5
        || engine.getStreamPrimeCacheHitCount() != 4
        || engine.getStreamPrimeCacheMissCount() != 1
        || engine.getStreamPrimeServiceCount() != 1)
    {
        cleanup();
        return false;
    }

    engine.panic();
    engine.setPreloadSamples(512);

    if (engine.getSegmentRebuildCount() <= afterLoadCount
        || engine.getLoadedPreloadSamples() != 512
        || engine.getLoadedStreamSamples() != (sampleLength - 512)
        || engine.getStreamPrimeRequestCount() != 0
        || engine.getStreamPrimeCacheHitCount() != 0
        || engine.getStreamPrimeCacheMissCount() != 0
        || engine.getStreamPrimeServiceCount() != 0)
    {
        cleanup();
        return false;
    }

    if (renderBlock(true) <= 0.001f)
    {
        cleanup();
        return false;
    }

    renderBlock(false);
    renderBlock(false);
    renderBlock(false);
    renderBlock(false);

    if (engine.getStreamPrimeRequestCount() != 6
        || engine.getStreamPrimeCacheHitCount() != 5
        || engine.getStreamPrimeCacheMissCount() != 1
        || engine.getStreamPrimeServiceCount() != 0)
    {
        cleanup();
        return false;
    }

    engine.serviceStreamPriming();
    renderBlock(false);

    const auto passed = engine.getStreamPrimeRequestCount() == 7
        && engine.getStreamPrimeCacheHitCount() == 6
        && engine.getStreamPrimeCacheMissCount() == 1
        && engine.getStreamPrimeServiceCount() == 1;

    cleanup();
    return passed;
}

bool runProgramStreamPrimingAndCacheMetricsTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_stream_prime_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("stream.wav");
    const auto sample = createTestSample(4096);

    auto writeWavFile = [&](const juce::File& fileToWrite) -> bool
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(fileToWrite.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();
        return writer->writeFromAudioSampleBuffer(sample, 0, sample.getNumSamples());
    };

    if (!writeWavFile(sampleFile))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setPreloadSamples(256);

    Program program;
    SampleAsset asset;
    asset.sourcePath = sampleFile.getFullPathName().toStdString();
    asset.displayName = "stream.wav";
    asset.lengthSamples = sample.getNumSamples();
    asset.numChannels = sample.getNumChannels();
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    const std::array<int, 5> zoneStarts{ 256, 768, 1280, 1792, 2304 };
    for (int zoneIndex = 0; zoneIndex < static_cast<int>(zoneStarts.size()); ++zoneIndex)
    {
        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.keyRange = MidiRange::single(60 + zoneIndex);
        zone.rootMidiNote = 60 + zoneIndex;
        zone.sampleStart = zoneStarts[static_cast<std::size_t>(zoneIndex)];
        zone.sampleEndExclusive = sample.getNumSamples();
        program.zones.push_back(zone);
    }

    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    sampleDataByAsset.push_back(sample);
    engine.setProgram(program, sampleDataByAsset);

    const auto baseRequests = engine.getStreamPrimeRequestCount();
    const auto baseHits = engine.getStreamPrimeCacheHitCount();
    const auto baseMisses = engine.getStreamPrimeCacheMissCount();
    const auto baseServices = engine.getStreamPrimeServiceCount();

    juce::AudioBuffer<float> firstBlock(channels, blockSize);
    juce::MidiBuffer firstMidi;
    firstMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    engine.render(firstBlock, firstMidi);

    if ((engine.getStreamPrimeRequestCount() - baseRequests) != 2
        || (engine.getStreamPrimeCacheHitCount() - baseHits) != 1
        || (engine.getStreamPrimeCacheMissCount() - baseMisses) != 1)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    engine.serviceStreamPriming();
    if ((engine.getStreamPrimeServiceCount() - baseServices) != 1)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    juce::AudioBuffer<float> secondBlock(channels, blockSize);
    juce::MidiBuffer secondMidi;
    secondMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    engine.render(secondBlock, secondMidi);

    const auto passed = (engine.getStreamPrimeRequestCount() - baseRequests) == 4
        && (engine.getStreamPrimeCacheHitCount() - baseHits) == 2
        && (engine.getStreamPrimeCacheMissCount() - baseMisses) == 2
        && (engine.getStreamPrimeServiceCount() - baseServices) == 1;

    tempDirectory.deleteRecursively();
    return passed;
}

bool runProgramStreamLookaheadPrimingTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_stream_lookahead_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("lookahead.wav");
    const auto sample = createTestSample(4096);

    auto writeWavFile = [&](const juce::File& fileToWrite) -> bool
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(fileToWrite.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();
        return writer->writeFromAudioSampleBuffer(sample, 0, sample.getNumSamples());
    };

    if (!writeWavFile(sampleFile))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setPreloadSamples(256);

    Program program;
    SampleAsset asset;
    asset.sourcePath = sampleFile.getFullPathName().toStdString();
    asset.displayName = "lookahead.wav";
    asset.lengthSamples = sample.getNumSamples();
    asset.numChannels = sample.getNumChannels();
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(60);
    zone.rootMidiNote = 60;
    zone.sampleStart = 256;
    zone.sampleEndExclusive = sample.getNumSamples();
    program.zones.push_back(zone);

    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    sampleDataByAsset.push_back(sample);
    engine.setProgram(program, sampleDataByAsset);

    const auto baseRequests = engine.getStreamPrimeRequestCount();
    const auto baseHits = engine.getStreamPrimeCacheHitCount();
    const auto baseMisses = engine.getStreamPrimeCacheMissCount();
    const auto baseServices = engine.getStreamPrimeServiceCount();

    for (int blockIndex = 0; blockIndex < 5; ++blockIndex)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        if (blockIndex == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

        if (blockIndex == 4)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 32);

        engine.render(block, midi);
        engine.serviceStreamPriming();
    }

    const auto passed = (engine.getStreamPrimeRequestCount() - baseRequests) >= 6
        && (engine.getStreamPrimeCacheHitCount() - baseHits) >= 4
        && (engine.getStreamPrimeCacheMissCount() - baseMisses) >= 2
        && (engine.getStreamPrimeServiceCount() - baseServices) >= 2;

    tempDirectory.deleteRecursively();
    return passed;
}

bool runDiskStreamCacheStaysResponsiveUnderContentionTest()
{
    // std::atomic<std::shared_ptr<T>>::load() takes an internal spinlock on every standard library
    // implementation (is_always_lock_free is always false) -- so a reader on the audio thread can be
    // made to wait behind a writer on another thread. A plain pointer atomic carries no such
    // obligation. This is the type-level fact EngineCore's disk-stream cache now depends on.
    static_assert(!std::atomic<std::shared_ptr<int>>::is_always_lock_free,
                 "This assumption is what made the old cache-state publish unsafe for the audio thread.");
    static_assert(std::atomic<const int*>::is_always_lock_free,
                 "The fix relies on a raw pointer atomic being lock-free, which is guaranteed by the standard "
                 "on every platform this project targets.");

    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 200000;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_stream_contention_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("contention.wav");
    const auto sample = createTestSample(sampleLength);

    auto writeWavFile = [&](const juce::File& fileToWrite) -> bool
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(fileToWrite.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();
        return writer->writeFromAudioSampleBuffer(sample, 0, sample.getNumSamples());
    };

    if (!writeWavFile(sampleFile))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setPreloadSamples(128);

    Program program;
    SampleAsset asset;
    asset.sourcePath = sampleFile.getFullPathName().toStdString();
    asset.displayName = "contention.wav";
    asset.lengthSamples = sample.getNumSamples();
    asset.numChannels = sample.getNumChannels();
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(60);
    zone.rootMidiNote = 60;
    zone.sampleStart = 0;
    zone.sampleEndExclusive = sample.getNumSamples();
    zone.loopMode = ZoneLoopMode::continuous;
    zone.loopStart = 0;
    zone.loopEndExclusive = sample.getNumSamples();
    program.zones.push_back(zone);

    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    sampleDataByAsset.push_back(sample);
    engine.setProgram(program, sampleDataByAsset);

    const auto baseServices = engine.getStreamPrimeServiceCount();

    // Hammer servicePendingPrime() (via serviceStreamPriming()) from a second thread with no
    // throttling at all -- far more aggressively than the real ~2ms stream-prime worker -- while the
    // "audio thread" renders continuously. This is a liveness/contention regression guard, not a
    // reliable reproduction of the original bug: the old lock's critical section was only a handful
    // of instructions, so tripping real priority inversion needs an unlucky OS preemption of the lock
    // holder, which a short-lived unit test cannot force. What this test does prove deterministically
    // is that the two threads can pound the same cache state concurrently without the render loop
    // ever stalling past a generous bound, and without producing corrupted (NaN/Inf) audio.
    std::atomic<bool> stopRequested{ false };
    std::thread primingThread([&engine, &stopRequested]
    {
        while (!stopRequested.load(std::memory_order_relaxed))
            engine.serviceStreamPriming();
    });

    auto worstBlockDuration = std::chrono::steady_clock::duration::zero();
    auto sawNonFiniteSample = false;

    const auto testStart = std::chrono::steady_clock::now();
    constexpr auto testDuration = std::chrono::milliseconds(150);
    auto firstBlock = true;

    // One continuous, looping note keeps the stream advancing (and, thanks to the loop wrapping
    // back over pages the small cache has since evicted, keeps missing) for the whole test window,
    // rather than settling into a steady state where the cache is never touched again.
    while (std::chrono::steady_clock::now() - testStart < testDuration)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        if (firstBlock)
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            firstBlock = false;
        }

        const auto blockStart = std::chrono::steady_clock::now();
        engine.render(block, midi);
        const auto blockDuration = std::chrono::steady_clock::now() - blockStart;
        worstBlockDuration = std::max(worstBlockDuration, blockDuration);

        for (int channel = 0; channel < channels && !sawNonFiniteSample; ++channel)
        {
            const auto* data = block.getReadPointer(channel);
            for (int sampleIndex = 0; sampleIndex < blockSize; ++sampleIndex)
            {
                if (!std::isfinite(data[sampleIndex]))
                {
                    sawNonFiniteSample = true;
                    break;
                }
            }
        }
    }

    stopRequested.store(true, std::memory_order_relaxed);
    primingThread.join();

    tempDirectory.deleteRecursively();

    if (sawNonFiniteSample)
        return false;

    if (engine.getStreamPrimeServiceCount() == baseServices)
        return false; // The contention scenario never actually exercised the cache-state publish.

    constexpr auto worstAcceptableBlockDuration = std::chrono::milliseconds(50);
    return worstBlockDuration < worstAcceptableBlockDuration;
}

bool runRtSnapshotCellProtectsHeldReaderAcrossPublishesTest()
{
    // EngineCore's top-level programSnapshot_/programAudioSnapshot_/sampleSegments_ differ from
    // DiskSampleStreamSource::cacheState: render() can hold a Reader guard for an entire
    // host-controlled block (which can be large), and the message thread can publish faster than
    // that block takes to render -- so a small fixed-size retirement ring is not automatically safe
    // here (see RtSnapshotCell.h). This test proves the actual property RtSnapshotCell relies on
    // instead: a generation held by a live Reader survives ANY number of subsequent publishes from
    // another thread, and is reclaimed only after the guard is released. It needs no EngineCore or
    // audio rendering at all -- just the cell in isolation.
    using audiocity::engine::RtReaderRole;
    using audiocity::engine::RtSnapshotCell;

    struct Payload
    {
        int id = 0;
        std::function<void(int)> onDestroy;

        // A real (non-aggregate) constructor is essential here: Payload has a user-declared
        // destructor, which suppresses the implicit move constructor, so
        // make_shared<const Payload>(Payload{id, cb}) would fall back to *copying* the temporary --
        // leaving the short-lived temporary with its own live `onDestroy`, which then fires
        // spuriously when that temporary is destroyed at the end of the full expression, regardless
        // of whether the real managed object was ever reclaimed. Constructing in place via this
        // constructor avoids creating any such temporary.
        Payload(const int id_, std::function<void(int)> cb) : id(id_), onDestroy(std::move(cb)) {}
        ~Payload()
        {
            if (onDestroy)
                onDestroy(id);
        }
    };

    std::mutex destroyedMutex;
    std::set<int> destroyedIds;
    const auto recordDestroyed = [&](const int id)
    {
        std::lock_guard<std::mutex> lock(destroyedMutex);
        destroyedIds.insert(id);
    };
    const auto wasDestroyed = [&](const int id)
    {
        std::lock_guard<std::mutex> lock(destroyedMutex);
        return destroyedIds.count(id) != 0;
    };

    RtSnapshotCell<Payload> cell;
    cell.publish(std::make_shared<const Payload>(0, recordDestroyed));

    {
        // Acquire and hold a Reader guard for generation 0, exactly like render() holds its
        // top-level snapshots for the whole block.
        const auto heldReader = cell.read(RtReaderRole::audio);
        if (!heldReader || heldReader->id != 0)
            return false;

        // Flood far more publishes through than any small fixed-size ring could survive, from a
        // different thread (the message-thread role), while the guard above is still alive.
        constexpr int floodCount = 5000;
        std::thread writer([&]
        {
            for (int index = 1; index <= floodCount; ++index)
                cell.publish(std::make_shared<const Payload>(index, recordDestroyed));
        });
        writer.join();

        // The held generation must still be exactly what we captured -- not freed, not reused.
        if (!heldReader || heldReader->id != 0)
            return false;

        if (wasDestroyed(0))
            return false; // Freed while still protected by a live Reader -- unsafe.
    }

    // Once the guard is released, one more publish must let generation 0 finally be reclaimed --
    // otherwise this is a leak (an unconditional "never touch anything a hazard once pointed at"
    // fix), not a correct one.
    cell.publish(std::make_shared<const Payload>(-1, recordDestroyed));

    return wasDestroyed(0);
}

bool runQualityTierDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> sample(1, 2048);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto value = static_cast<float>((i % 31) / 31.0);
        sample.setSample(0, i, value * 0.5f - 0.25f);
    }

    audiocity::engine::EngineCore cpu;
    cpu.prepare(sampleRate, blockSize, channels);
    cpu.setQualityTier(audiocity::engine::EngineCore::QualityTier::cpu);
    cpu.setSampleData(sample, sampleRate, 60);

    audiocity::engine::EngineCore fidelity;
    fidelity.prepare(sampleRate, blockSize, channels);
    fidelity.setQualityTier(audiocity::engine::EngineCore::QualityTier::fidelity);
    fidelity.setSampleData(sample, sampleRate, 60);

    juce::AudioBuffer<float> cpuBlock(channels, blockSize);
    juce::AudioBuffer<float> fidelityBlock(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 0);

    cpu.render(cpuBlock, midi);
    fidelity.render(fidelityBlock, midi);

    return !buffersAreEqual(cpuBlock, fidelityBlock, 1.0e-7f);
}

juce::AudioBuffer<float> renderQualityTierSequence(audiocity::engine::EngineCore& engine)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 6;

    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.95f), 0);

        if (block == 3)
            midi.addEvent(juce::MidiMessage::noteOff(1, 67), 64);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            output.copyFrom(channel, block * blockSize, blockBuffer, channel, 0, blockSize);
    }

    return output;
}

bool runQualityTierDeterminismTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> sample(1, 2048);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto value = static_cast<float>((i % 29) / 29.0);
        sample.setSample(0, i, value * 0.7f - 0.35f);
    }

    auto renderPairForTier = [&](const audiocity::engine::EngineCore::QualityTier tier)
    {
        audiocity::engine::EngineCore first;
        first.prepare(sampleRate, blockSize, channels);
        first.setQualityTier(tier);
        first.setSampleData(sample, sampleRate, 60);

        audiocity::engine::EngineCore second;
        second.prepare(sampleRate, blockSize, channels);
        second.setQualityTier(tier);
        second.setSampleData(sample, sampleRate, 60);

        const auto a = renderQualityTierSequence(first);
        const auto b = renderQualityTierSequence(second);
        return buffersAreEqual(a, b, 1.0e-7f);
    };

    return renderPairForTier(audiocity::engine::EngineCore::QualityTier::cpu)
        && renderPairForTier(audiocity::engine::EngineCore::QualityTier::fidelity)
        && renderPairForTier(audiocity::engine::EngineCore::QualityTier::ultra);
}

double computeAverage(const std::vector<float>& values, const int startIndex, const int endIndexExclusive)
{
    if (values.empty())
        return 0.0;

    const auto start = juce::jlimit(0, static_cast<int>(values.size()), startIndex);
    const auto endExclusive = juce::jlimit(start, static_cast<int>(values.size()), endIndexExclusive);

    double sum = 0.0;
    int count = 0;
    for (int i = start; i < endExclusive; ++i)
    {
        sum += static_cast<double>(values[static_cast<std::size_t>(i)]);
        ++count;
    }

    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

bool runCpuQualityEnergyDriftSmokeTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 128;
    constexpr double sampleRate = 48000.0;
    constexpr double maxCpuVsFidelityDriftDelta = 0.15;
    constexpr double hardMaxNormalizedDrift = 1.0;

    juce::AudioBuffer<float> shaped(1, 96);
    shaped.clear();
    for (int i = 20; i < 56; ++i)
        shaped.setSample(0, i, 0.75f * std::sin(static_cast<float>(i) * 0.33f));

    audiocity::engine::EngineCore::AdsrSettings stableAdsr;
    stableAdsr.attackSeconds = 0.0001f;
    stableAdsr.decaySeconds = 0.001f;
    stableAdsr.sustainLevel = 1.0f;
    stableAdsr.releaseSeconds = 0.2f;

    auto computeDrift = [&](const audiocity::engine::EngineCore::QualityTier tier)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setQualityTier(tier);
        engine.setAmpEnvelope(stableAdsr);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setSampleData(shaped, sampleRate, 60);
        engine.setLoopPoints(20, 55);

        std::vector<float> energies;
        energies.reserve(static_cast<std::size_t>(blocks));

        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

            engine.render(blockBuffer, midi);
            energies.push_back(blockEnergy(blockBuffer));
        }

        const auto earlyMean = computeAverage(energies, 16, 48);
        const auto lateMean = computeAverage(energies, blocks - 32, blocks);

        if (!(earlyMean > 1.0e-6) || !std::isfinite(earlyMean) || !std::isfinite(lateMean))
            return std::numeric_limits<double>::infinity();

        return std::abs(lateMean - earlyMean) / earlyMean;
    };

    const auto cpuDrift = computeDrift(audiocity::engine::EngineCore::QualityTier::cpu);
    const auto fidelityDrift = computeDrift(audiocity::engine::EngineCore::QualityTier::fidelity);

    if (!std::isfinite(cpuDrift) || !std::isfinite(fidelityDrift))
        return false;

    if (cpuDrift > hardMaxNormalizedDrift)
        return false;

    return cpuDrift <= fidelityDrift + maxCpuVsFidelityDriftDelta;
}

bool runRuntimeQualitySwitchSmokeTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 14;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> sample(1, 3072);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 330.0 / sampleRate);
        sample.setSample(0, i, 0.35f * std::sin(phase));
    }

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::fidelity);
    engine.setSampleData(sample, sampleRate, 60);

    double totalEnergy = 0.0;
    for (int block = 0; block < blocks; ++block)
    {
        if (block == 4)
            engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::ultra);
        if (block == 8)
            engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::cpu);
        if (block == 11)
            engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::fidelity);

        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.9f), 0);
        if (block == 12)
            midi.addEvent(juce::MidiMessage::noteOff(1, 67), 32);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < blockBuffer.getNumChannels(); ++channel)
        {
            const auto* data = blockBuffer.getReadPointer(channel);
            for (int i = 0; i < blockBuffer.getNumSamples(); ++i)
            {
                const auto sampleValue = data[i];
                if (!std::isfinite(sampleValue))
                    return false;

                totalEnergy += std::abs(static_cast<double>(sampleValue));
            }
        }
    }

    return totalEnergy > 0.01 && totalEnergy < 20000.0;
}

bool runEditorUndoHistoryMixedOrderTest()
{
    using namespace audiocity::engine;

    Program initialProgram;
    SampleAsset sample;
    sample.displayName = "undo.wav";
    sample.lengthSamples = 512;
    initialProgram.sampleAssets.push_back(sample);

    Zone first;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::single(60);
    first.rootMidiNote = 60;
    initialProgram.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::single(61);
    second.rootMidiNote = 61;
    initialProgram.zones.push_back(second);

    const auto initialState = audiocity::plugin::createProgramZoneMappingState(initialProgram);

    auto editedProgram = initialProgram;
    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = 48;
    edit.keyHigh = 52;
    edit.velocityLow = 8;
    edit.velocityHigh = 96;
    edit.rootMidiNote = 50;
    if (!audiocity::plugin::applyProgramZoneEdit(editedProgram, edit))
        return false;

    const auto editedState = audiocity::plugin::createProgramZoneMappingState(editedProgram);

    auto finalProgram = editedProgram;
    if (!audiocity::plugin::deleteProgramZone(finalProgram, 1))
        return false;

    const auto finalState = audiocity::plugin::createProgramZoneMappingState(finalProgram);

    audiocity::engine::SettingsSnapshot initialSettings;
    initialSettings.preloadSamples = 32768;
    initialSettings.sampleWindowStart = 0;

    auto changedSettings = initialSettings;
    changedSettings.preloadSamples = 4096;
    changedSettings.sampleWindowStart = 12;

    audiocity::plugin::ProgramMappingStateSnapshot initialMapping;
    initialMapping.hasImportedProgram = true;
    initialMapping.programPath = "C:/Library/undo.sfz";
    initialMapping.mappingState = initialState;

    auto editedMapping = initialMapping;
    editedMapping.mappingState = editedState;

    auto finalMapping = initialMapping;
    finalMapping.mappingState = finalState;

    audiocity::plugin::EditorUndoHistory history;
    history.recordSettingsChange(initialSettings, changedSettings, 1, "Edit Settings");
    history.recordMappingChange(editedMapping, finalMapping, "Delete Mapping Zone");

    if (!history.canUndo() || history.canRedo() || history.undoLabel() != "Delete Mapping Zone")
        return false;

    auto currentSettings = changedSettings;
    auto currentMapping = finalMapping;
    const auto undo1 = history.undo(currentSettings, currentMapping);
    if (!undo1.has_value() || undo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (undo1->mappingSnapshot != editedMapping)
        return false;

    currentMapping = undo1->mappingSnapshot;
    const auto undo2 = history.undo(currentSettings, currentMapping);
    if (!undo2.has_value() || undo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::settings)
        return false;

    if (!(undo2->settingsSnapshot == initialSettings))
        return false;

    if (history.canUndo() || !history.canRedo())
        return false;

    currentSettings = undo2->settingsSnapshot;
    const auto redo1 = history.redo(currentSettings, currentMapping);
    if (!redo1.has_value() || redo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::settings)
        return false;

    if (!(redo1->settingsSnapshot == changedSettings))
        return false;

    currentSettings = redo1->settingsSnapshot;
    const auto redo2 = history.redo(currentSettings, currentMapping);
    if (!redo2.has_value() || redo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (redo2->mappingSnapshot != finalMapping)
        return false;

    return !history.canRedo() && history.canUndo();
}

bool runEditorUndoHistoryCoalesceAndLabelsTest()
{
    audiocity::engine::SettingsSnapshot a;
    a.preloadSamples = 32768;

    auto b = a;
    b.preloadSamples = 24000;

    auto c = b;
    c.preloadSamples = 16000;

    audiocity::plugin::EditorUndoHistory history;
    history.recordSettingsChange(a, b, 1, "Edit Settings");
    history.recordSettingsChange(b, c, 1, "Edit Settings");

    if (history.undoLabel() != "Edit Settings")
        return false;

    auto currentMapping = audiocity::plugin::ProgramMappingStateSnapshot{};
    auto currentSettings = c;
    const auto undo = history.undo(currentSettings, currentMapping);
    if (!undo.has_value() || undo->kind != audiocity::plugin::EditorUndoHistory::EntryKind::settings)
        return false;

    if (!(undo->settingsSnapshot == a))
        return false;

    if (!history.undoLabel().empty())
        return false;

    return history.canRedo();
}

bool runEditorUndoHistoryCreateZoneTest()
{
    using namespace audiocity::engine;

    Program initialProgram;
    SampleAsset sample;
    sample.displayName = "create.wav";
    sample.lengthSamples = 512;
    sample.rootMidiNote = 60;
    initialProgram.sampleAssets.push_back(sample);

    audiocity::plugin::ProgramMappingStateSnapshot initialMapping;
    initialMapping.hasImportedProgram = true;
    initialMapping.programPath = "C:/Library/create.sfz";
    initialMapping.mappingState = audiocity::plugin::createProgramZoneMappingState(initialProgram);

    auto createdProgram = initialProgram;
    if (audiocity::plugin::createProgramZone(createdProgram) != 0)
        return false;

    auto createdMapping = initialMapping;
    createdMapping.mappingState = audiocity::plugin::createProgramZoneMappingState(createdProgram);

    audiocity::engine::SettingsSnapshot initialSettings;
    initialSettings.preloadSamples = 32768;

    auto changedSettings = initialSettings;
    changedSettings.preloadSamples = 8192;

    audiocity::plugin::EditorUndoHistory history;
    history.recordSettingsChange(initialSettings, changedSettings, 1, "Edit Settings");
    history.recordMappingChange(initialMapping, createdMapping, "Create Mapping Zone");

    if (!history.canUndo() || history.undoLabel() != "Create Mapping Zone")
        return false;

    auto currentSettings = changedSettings;
    auto currentMapping = createdMapping;
    const auto undo1 = history.undo(currentSettings, currentMapping);
    if (!undo1.has_value() || undo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (undo1->mappingSnapshot != initialMapping || history.undoLabel() != "Edit Settings")
        return false;

    currentMapping = undo1->mappingSnapshot;
    const auto undo2 = history.undo(currentSettings, currentMapping);
    if (!undo2.has_value() || undo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::settings)
        return false;

    if (!(undo2->settingsSnapshot == initialSettings))
        return false;

    currentSettings = undo2->settingsSnapshot;
    const auto redo1 = history.redo(currentSettings, currentMapping);
    if (!redo1.has_value() || redo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::settings)
        return false;

    if (!(redo1->settingsSnapshot == changedSettings))
        return false;

    currentSettings = redo1->settingsSnapshot;
    const auto redo2 = history.redo(currentSettings, currentMapping);
    if (!redo2.has_value() || redo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    return redo2->mappingSnapshot == createdMapping && !history.canRedo() && history.canUndo();
}

bool runEditorUndoHistoryDuplicateAndSplitTest()
{
    using namespace audiocity::engine;

    Program initialProgram;
    SampleAsset sample;
    sample.displayName = "undo.wav";
    sample.lengthSamples = 512;
    initialProgram.sampleAssets.push_back(sample);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(36, 47);
    zone.rootMidiNote = 42;
    initialProgram.zones.push_back(zone);

    audiocity::plugin::ProgramMappingStateSnapshot initialMapping;
    initialMapping.hasImportedProgram = true;
    initialMapping.programPath = "C:/Library/undo.sfz";
    initialMapping.mappingState = audiocity::plugin::createProgramZoneMappingState(initialProgram);

    auto duplicatedProgram = initialProgram;
    const auto duplicatedIndex = audiocity::plugin::duplicateProgramZone(duplicatedProgram, 0);
    if (duplicatedIndex != 1)
        return false;

    auto duplicatedMapping = initialMapping;
    duplicatedMapping.mappingState = audiocity::plugin::createProgramZoneMappingState(duplicatedProgram);

    auto splitProgram = duplicatedProgram;
    const auto splitIndex = audiocity::plugin::splitProgramZoneByKey(splitProgram, duplicatedIndex);
    if (splitIndex != 2)
        return false;

    auto splitMapping = initialMapping;
    splitMapping.mappingState = audiocity::plugin::createProgramZoneMappingState(splitProgram);

    audiocity::plugin::EditorUndoHistory history;
    history.recordMappingChange(initialMapping, duplicatedMapping, "Duplicate Mapping Zone");
    history.recordMappingChange(duplicatedMapping, splitMapping, "Split Mapping Zone");

    if (!history.canUndo() || history.canRedo() || history.undoLabel() != "Split Mapping Zone")
        return false;

    auto currentSettings = audiocity::engine::SettingsSnapshot{};
    auto currentMapping = splitMapping;
    const auto undo1 = history.undo(currentSettings, currentMapping);
    if (!undo1.has_value() || undo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (undo1->mappingSnapshot != duplicatedMapping || history.undoLabel() != "Duplicate Mapping Zone")
        return false;

    currentMapping = undo1->mappingSnapshot;
    const auto undo2 = history.undo(currentSettings, currentMapping);
    if (!undo2.has_value() || undo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (undo2->mappingSnapshot != initialMapping)
        return false;

    currentMapping = undo2->mappingSnapshot;
    const auto redo1 = history.redo(currentSettings, currentMapping);
    if (!redo1.has_value() || redo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (redo1->mappingSnapshot != duplicatedMapping)
        return false;

    currentMapping = redo1->mappingSnapshot;
    const auto redo2 = history.redo(currentSettings, currentMapping);
    if (!redo2.has_value() || redo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (redo2->mappingSnapshot != splitMapping)
        return false;

    return !history.canRedo() && history.canUndo();
}

bool runSettingsUndoHistoryTest()
{
    audiocity::engine::SettingsUndoHistory history;
    audiocity::engine::SettingsSnapshot initial;
    initial.preloadSamples = 32768;
    initial.qualityTierIndex = 1;
    initial.playbackModeIndex = 0;
    initial.pitchBendRangeSemitones = 2.0f;
    initial.monoEnabled = false;
    initial.legatoEnabled = false;
    initial.glideSeconds = 0.0f;
    initial.polyphonyLimit = 64;
    initial.sampleWindowStart = 0;

    auto changedPreload = initial;
    changedPreload.preloadSamples = 4096;

    auto changedTierAndMapping = changedPreload;
    changedTierAndMapping.qualityTierIndex = 0;
    changedTierAndMapping.playbackModeIndex = 1;
    changedTierAndMapping.pitchBendRangeSemitones = 12.0f;
    changedTierAndMapping.monoEnabled = true;
    changedTierAndMapping.legatoEnabled = true;
    changedTierAndMapping.glideSeconds = 0.04f;
    changedTierAndMapping.polyphonyLimit = 8;
    changedTierAndMapping.sampleWindowStart = 2;

    history.recordChange(initial, changedPreload);
    history.recordChange(changedPreload, changedTierAndMapping);

    if (!history.canUndo() || history.canRedo())
        return false;

    auto current = changedTierAndMapping;
    const auto firstUndo = history.undo(current);
    if (!firstUndo.has_value() || *firstUndo != changedPreload)
        return false;

    current = *firstUndo;
    const auto secondUndo = history.undo(current);
    if (!secondUndo.has_value() || *secondUndo != initial)
        return false;

    if (history.canUndo() || !history.canRedo())
        return false;

    current = *secondUndo;
    const auto firstRedo = history.redo(current);
    if (!firstRedo.has_value() || *firstRedo != changedPreload)
        return false;

    current = *firstRedo;
    const auto secondRedo = history.redo(current);
    if (!secondRedo.has_value() || *secondRedo != changedTierAndMapping)
        return false;

    return !history.canRedo() && history.canUndo();
}

bool runSettingsUndoHistoryCapacityTest()
{
    audiocity::engine::SettingsUndoHistory history(2);

    audiocity::engine::SettingsSnapshot s0;
    s0.preloadSamples = 32768;
    s0.qualityTierIndex = 1;
    s0.playbackModeIndex = 0;
    s0.monoEnabled = false;
    s0.legatoEnabled = false;
    s0.glideSeconds = 0.0f;
    s0.sampleWindowStart = 0;

    auto s1 = s0;
    s1.preloadSamples = 16384;

    auto s2 = s1;
    s2.preloadSamples = 8192;
    s2.playbackModeIndex = 1;

    auto s3 = s2;
    s3.preloadSamples = 4096;
    s3.qualityTierIndex = 0;
    s3.playbackModeIndex = 2;
    s3.monoEnabled = true;
    s3.glideSeconds = 0.01f;
    s3.sampleWindowStart = 1;

    history.recordChange(s0, s1);
    history.recordChange(s1, s2);
    history.recordChange(s2, s3);

    auto current = s3;

    const auto undo1 = history.undo(current);
    if (!undo1.has_value() || *undo1 != s2)
        return false;

    current = *undo1;
    const auto undo2 = history.undo(current);
    if (!undo2.has_value() || *undo2 != s1)
        return false;

    current = *undo2;
    const auto undo3 = history.undo(current);
    if (undo3.has_value())
        return false;

    return !history.canUndo() && history.canRedo();
}

bool runSettingsUndoHistoryCoalesceTest()
{
    audiocity::engine::SettingsUndoHistory history;

    audiocity::engine::SettingsSnapshot a;
    a.preloadSamples = 32768;

    auto b = a;
    b.preloadSamples = 30000;

    auto c = b;
    c.preloadSamples = 25000;

    auto d = c;
    d.preloadSamples = 22000;

    history.recordChange(a, b, 1);
    history.recordChange(b, c, 1);
    history.recordChange(c, d, 1);

    auto current = d;
    const auto firstUndo = history.undo(current);
    if (!firstUndo.has_value() || *firstUndo != a)
        return false;

    current = *firstUndo;
    const auto secondUndo = history.undo(current);
    if (secondUndo.has_value())
        return false;

    return history.canRedo();
}

bool runSettingsUndoHistoryLabelsTest()
{
    audiocity::engine::SettingsUndoHistory history;

    audiocity::engine::SettingsSnapshot a;
    a.preloadSamples = 32768;

    auto b = a;
    b.preloadSamples = 4096;

    history.recordChange(a, b, -1, "Change Preload Samples");

    if (history.undoLabel() != "Change Preload Samples")
        return false;

    auto current = b;
    const auto undo = history.undo(current);
    if (!undo.has_value() || *undo != a)
        return false;

    if (!history.undoLabel().empty())
        return false;

    return history.canRedo();
}

bool runSettingsUndoHistoryEditorStateTest()
{
    audiocity::engine::SettingsUndoHistory history;

    audiocity::engine::SettingsSnapshot base;
    base.sampleWindowStart = 4;
    base.sampleWindowEnd = 120;
    base.loopStart = 16;
    base.loopEnd = 96;
    base.fadeInSamples = 2;
    base.fadeOutSamples = 2;
    base.reversePlayback = false;

    auto edited = base;
    edited.sampleWindowStart = 20;
    edited.sampleWindowEnd = 80;
    edited.loopStart = 24;
    edited.loopEnd = 72;
    edited.fadeInSamples = 8;
    edited.fadeOutSamples = 10;
    edited.reversePlayback = true;

    history.recordChange(base, edited, -1, "Edit Sample");

    auto current = edited;
    const auto undo = history.undo(current);
    if (!undo.has_value() || *undo != base)
        return false;

    current = *undo;
    const auto redo = history.redo(current);
    if (!redo.has_value() || *redo != edited)
        return false;

    return true;
}

bool runPlayerPadStateUtilityTest()
{
    auto pads = audiocity::plugin::defaultPlayerPadAssignments();
    if (pads[0].noteNumber != 36 || pads[0].velocity != 100)
        return false;
    if (pads[3].noteNumber != 39 || pads[3].velocity != 100)
        return false;
    if (pads[7].noteNumber != 43 || pads[7].velocity != 100)
        return false;

    const auto sanitized = audiocity::plugin::sanitizePlayerPadAssignment({ -12, 999 });
    if (sanitized.noteNumber != 0 || sanitized.velocity != 127)
        return false;

    return true;
}

bool runSampleBrowserTooltipFormattingTest()
{
    audiocity::plugin::SampleBrowserTooltipData tooltipData;
    tooltipData.fileName = "Kick_01.wav";
    tooltipData.relativePath = "Drums/Acoustic/Kick_01.wav";
    tooltipData.metadataLine = "SR: 48000 Hz  Ch: 2  Bit Depth: 24  Duration: 00:01.000  Samples: 48000";
    tooltipData.loopFormatBadge = "Apple Loop";
    tooltipData.loopMetadataLine = "Root: C3  |  Loop: 1024-4096";
    tooltipData.tags.add("kick");
    tooltipData.tags.add("acoustic");
    tooltipData.isFavorite = true;
    tooltipData.isRecent = true;
    tooltipData.previewSupported = true;
    tooltipData.mappingDragSupported = true;

    const auto tooltipText = audiocity::plugin::buildSampleBrowserTooltipText(tooltipData);
    if (!tooltipText.contains("Kick_01.wav"))
        return false;
    if (!tooltipText.contains("Path: Drums/Acoustic/Kick_01.wav"))
        return false;
    if (!tooltipText.contains(tooltipData.metadataLine))
        return false;
    if (!tooltipText.contains("Format: Apple Loop  |  Root: C3  |  Loop: 1024-4096"))
        return false;
    if (!tooltipText.contains("Tags: kick, acoustic"))
        return false;
    if (!tooltipText.contains("Status: Favorite, Recent"))
        return false;
    if (tooltipText.contains("Actions:"))
        return false;

    if (audiocity::plugin::buildSampleBrowserActionText(true, false, true) != "Previewing  |  Dbl-click load")
        return false;
    if (audiocity::plugin::buildSampleBrowserActionText(false, false, false) != "Dbl-click load")
        return false;

    tooltipData.fileName = "Pad.sfz";
    tooltipData.relativePath = "Pads/Pad.sfz";
    tooltipData.metadataLine = audiocity::plugin::importedProgramFormatDescription("Pads/Pad.sfz");
    tooltipData.loopFormatBadge = "SFZ";
    tooltipData.loopMetadataLine = {};
    tooltipData.tags.clear();
    tooltipData.isFavorite = false;
    tooltipData.isRecent = false;
    tooltipData.previewSupported = false;
    tooltipData.mappingDragSupported = false;
    tooltipData.previewing = false;

    const auto sfzTooltipText = audiocity::plugin::buildSampleBrowserTooltipText(tooltipData);
    if (!sfzTooltipText.contains("Pad.sfz"))
        return false;
    if (!sfzTooltipText.contains("Format: SFZ"))
        return false;
    if (!sfzTooltipText.contains("SFZ instrument"))
        return false;
    if (sfzTooltipText.contains("Actions:"))
        return false;

    if (audiocity::plugin::importedProgramFormatDescription("C:/Library/Keys/Layered.multisample")
            != "Bitwig multisample")
    {
        return false;
    }

    if (audiocity::plugin::importedProgramFormatDescription(audiocity::plugin::ImportedProgramFormat::nki)
            != "NKI instrument (legacy subset)")
    {
        return false;
    }

    return true;
}

bool runFilterModeDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> sample(1, 4096);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 120.0 / sampleRate);
        const auto high = 0.35 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 6200.0 / sampleRate);
        sample.setSample(0, i, static_cast<float>(0.4 * low + high));
    }

    auto renderWithMode = [&](const audiocity::engine::EngineCore::FilterSettings::Mode mode)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setAmpEnvelope({ 0.0001f, 0.001f, 1.0f, 0.1f });
        engine.setFilterEnvelope({ 0.0001f, 0.001f, 0.0f, 0.1f });
        engine.setSampleData(sample, sampleRate, 60);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = mode;
        filter.baseCutoffHz = 1200.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.35f;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto lp12 = renderWithMode(audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12);
    const auto lp24 = renderWithMode(audiocity::engine::EngineCore::FilterSettings::Mode::lowPass24);
    const auto hp12 = renderWithMode(audiocity::engine::EngineCore::FilterSettings::Mode::highPass12);

    if (buffersAreEqual(lp12, hp12, 1.0e-5f))
        return false;

    if (buffersAreEqual(lp12, lp24, 1.0e-6f))
        return false;

    return true;
}

bool runFilterModulationDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(4096);

    auto renderWithSettings = [&](const int note, const float velocity, const float keyTracking, const float velocityHz)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 800.0f;
        filter.envAmountHz = 0.0f;
        filter.keyTracking = keyTracking;
        filter.velocityAmountHz = velocityHz;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, note, velocity), 0);
        engine.render(block, midi);
        return block;
    };

    const auto base = renderWithSettings(60, 0.5f, 0.0f, 0.0f);
    const auto keyTracked = renderWithSettings(84, 0.5f, 1.0f, 0.0f);
    const auto velocityTracked = renderWithSettings(60, 1.0f, 0.0f, 6000.0f);

    if (buffersAreEqual(base, keyTracked, 1.0e-5f))
        return false;

    if (buffersAreEqual(base, velocityTracked, 1.0e-5f))
        return false;

    return true;
}

bool runFilterKeytrackPolarityTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 220.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5400.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.25 * low + high));
    }

    auto renderEnergyForKeytrack = [&](const float keytrack)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 900.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.0f;
        filter.keyTracking = keytrack;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);
        return blockEnergy(block);
    };

    const auto inverseEnergy = renderEnergyForKeytrack(-1.0f);
    const auto neutralEnergy = renderEnergyForKeytrack(0.0f);
    const auto positiveEnergy = renderEnergyForKeytrack(1.0f);
    const auto overTrackedEnergy = renderEnergyForKeytrack(2.0f);

    if (!(inverseEnergy < neutralEnergy))
        return false;

    if (!(positiveEnergy > neutralEnergy))
        return false;

    return overTrackedEnergy > positiveEnergy;
}

bool runFilterLfoDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 180.0 / sampleRate);
        const auto high = 0.7 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5200.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.25 * low + high));
    }

    auto renderWithLfoAmount = [&](const float lfoAmountHz)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1100.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.15f;
        filter.lfoRateHz = 4.0f;
        filter.lfoAmountHz = lfoAmountHz;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 7; ++i)
            engine.render(block, midi);

        return block;
    };

    const auto noLfo = renderWithLfoAmount(0.0f);
    const auto withLfo = renderWithLfoAmount(3000.0f);
    return !buffersAreEqual(noLfo, withLfo, 1.0e-6f);
}

bool runPitchLfoVibratoSettingsTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 48;
    constexpr double sampleRate = 48000.0;

    auto renderWithPitchLfo = [&](const float rateHz, const float depthCents)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createOneCycleSine(128), sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        audiocity::engine::EngineCore::AdsrSettings heldAdsr;
        heldAdsr.attackSeconds = 0.0001f;
        heldAdsr.decaySeconds = 0.0001f;
        heldAdsr.sustainLevel = 1.0f;
        heldAdsr.releaseSeconds = 0.5f;
        engine.setAmpEnvelope(heldAdsr);

        auto pitchLfo = engine.getPitchLfoSettings();
        pitchLfo.rateHz = rateHz;
        pitchLfo.depthCents = depthCents;
        engine.setPitchLfoSettings(pitchLfo);

        return renderHeldNote(engine, 60, blockSize, blocks, channels);
    };

    const auto dry = renderWithPitchLfo(0.0f, 0.0f);
    const auto vibrato = renderWithPitchLfo(5.0f, 40.0f);
    if (buffersAreEqual(dry, vibrato, 1.0e-6f))
        return false;

    const auto slowRate = renderWithPitchLfo(1.5f, 35.0f);
    const auto fastRate = renderWithPitchLfo(8.0f, 35.0f);
    if (buffersAreEqual(slowRate, fastRate, 1.0e-6f))
        return false;

    const auto shallowDepth = renderWithPitchLfo(5.0f, 10.0f);
    const auto deepDepth = renderWithPitchLfo(5.0f, 70.0f);
    return !buffersAreEqual(shallowDepth, deepDepth, 1.0e-6f);
}

bool runAmpLfoTremoloSettingsTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 48;
    constexpr double sampleRate = 48000.0;

    auto renderWithAmpLfo = [&](const float rateHz,
                                const float depth,
                                const audiocity::engine::EngineCore::FilterSettings::LfoShape shape)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createOneCycleSine(128), sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        audiocity::engine::EngineCore::AdsrSettings heldAdsr;
        heldAdsr.attackSeconds = 0.0001f;
        heldAdsr.decaySeconds = 0.0001f;
        heldAdsr.sustainLevel = 1.0f;
        heldAdsr.releaseSeconds = 0.5f;
        engine.setAmpEnvelope(heldAdsr);

        auto ampLfo = engine.getAmpLfoSettings();
        ampLfo.rateHz = rateHz;
        ampLfo.depth = depth;
        ampLfo.shape = shape;
        engine.setAmpLfoSettings(ampLfo);

        return renderHeldNote(engine, 60, blockSize, blocks, channels);
    };

    const auto dry = renderWithAmpLfo(0.0f, 0.0f, audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    const auto tremolo = renderWithAmpLfo(5.0f, 1.0f, audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    if (buffersAreEqual(dry, tremolo, 1.0e-6f))
        return false;

    const auto sineShape = renderWithAmpLfo(4.0f, 0.8f, audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    const auto squareShape = renderWithAmpLfo(4.0f, 0.8f, audiocity::engine::EngineCore::FilterSettings::LfoShape::square);
    if (buffersAreEqual(sineShape, squareShape, 1.0e-6f))
        return false;

    const auto slowRate = renderWithAmpLfo(1.5f, 0.8f, audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    const auto fastRate = renderWithAmpLfo(8.0f, 0.8f, audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    return !buffersAreEqual(slowRate, fastRate, 1.0e-6f);
}

bool runFilterLfoShapeDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 160.0 / sampleRate);
        const auto high = 0.75 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 4800.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.25 * low + high));
    }

    auto renderWithShape = [&](const audiocity::engine::EngineCore::FilterSettings::LfoShape shape)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1000.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.15f;
        filter.lfoRateHz = 5.0f;
        filter.lfoAmountHz = 2800.0f;
        filter.lfoShape = shape;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        return block;
    };

    const auto sine = renderWithShape(audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    const auto square = renderWithShape(audiocity::engine::EngineCore::FilterSettings::LfoShape::square);
    return !buffersAreEqual(sine, square, 1.0e-6f);
}

bool runFilterLfoTempoSyncSettingsTest()
{
    audiocity::engine::EngineCore engine;
    engine.prepare(48000.0, 128, 2);

    auto filter = engine.getFilterSettings();
    filter.lfoTempoSync = true;
    filter.lfoSyncDivision = 11;
    filter.lfoRateHz = 7.5f;
    filter.lfoAmountHz = 1200.0f;
    engine.setFilterSettings(filter);

    const auto applied = engine.getFilterSettings();
    if (!applied.lfoTempoSync)
        return false;

    if (applied.lfoSyncDivision != 11)
        return false;

    return std::abs(applied.lfoRateHz - 7.5f) < 1.0e-6f;
}

bool runFilterLfoRetriggerDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 170.0 / sampleRate);
        const auto high = 0.75 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5000.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.25 * low + high));
    }

    auto renderSecondAttack = [&](const bool retrigger)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 950.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 4.0f;
        filter.lfoAmountHz = 2800.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = retrigger;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto retrigSecondAttack = renderSecondAttack(true);
    const auto freeRunSecondAttack = renderSecondAttack(false);
    return !buffersAreEqual(retrigSecondAttack, freeRunSecondAttack, 1.0e-6f);
}

bool runFilterLfoStartPhaseDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 150.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5400.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderSecondAttack = [&](const float startPhaseDegrees)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 900.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 4.0f;
        filter.lfoAmountHz = 3000.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoStartPhaseDegrees = startPhaseDegrees;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto phaseZero = renderSecondAttack(0.0f);
    const auto phaseQuarter = renderSecondAttack(90.0f);
    return !buffersAreEqual(phaseZero, phaseQuarter, 1.0e-6f);
}

bool runFilterLfoFadeInDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 140.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5600.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderSecondAttack = [&](const float fadeInMs)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 900.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 6.0f;
        filter.lfoAmountHz = 3200.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoStartPhaseDegrees = 90.0f;
        filter.lfoFadeInMs = fadeInMs;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto instant = renderSecondAttack(0.0f);
    const auto faded = renderSecondAttack(250.0f);
    return !buffersAreEqual(instant, faded, 1.0e-6f);
}

bool runFilterLfoStartRandomDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 145.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5450.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderSecondAttack = [&](const float randomDegrees)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 900.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 5.0f;
        filter.lfoAmountHz = 3200.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoStartPhaseDegrees = 45.0f;
        filter.lfoStartPhaseRandomDegrees = randomDegrees;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto fixed = renderSecondAttack(0.0f);
    const auto randomised = renderSecondAttack(120.0f);
    return !buffersAreEqual(fixed, randomised, 1.0e-6f);
}

bool runFilterLfoAmountKeytrackingDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 155.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5300.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderHighNoteSecondAttack = [&](const float lfoAmountKeytrack)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1000.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 5.0f;
        filter.lfoAmountHz = 2400.0f;
        filter.lfoAmountKeyTracking = lfoAmountKeytrack;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 84), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto neutral = renderHighNoteSecondAttack(0.0f);
    const auto positiveTracked = renderHighNoteSecondAttack(1.0f);
    return !buffersAreEqual(neutral, positiveTracked, 1.0e-6f);
}

bool runFilterLfoRateKeytrackingDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 150.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5200.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderHighNoteSecondAttack = [&](const float lfoRateKeytrack)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1050.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 2.0f;
        filter.lfoRateKeyTracking = lfoRateKeytrack;
        filter.lfoAmountHz = 2600.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 84), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto neutral = renderHighNoteSecondAttack(0.0f);
    const auto positiveTracked = renderHighNoteSecondAttack(1.0f);
    return !buffersAreEqual(neutral, positiveTracked, 1.0e-6f);
}

bool runFilterLfoRateKeytrackInTempoSyncToggleDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 150.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5100.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderHighNoteSecondAttack = [&](const bool keytrackInSync)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1000.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 3.0f;
        filter.lfoRateKeyTracking = 1.0f;
        filter.lfoAmountHz = 2600.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoTempoSync = true;
        filter.lfoRateKeytrackInTempoSync = keytrackInSync;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 84), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto disabled = renderHighNoteSecondAttack(false);
    const auto enabled = renderHighNoteSecondAttack(true);
    return !buffersAreEqual(disabled, enabled, 1.0e-6f);
}

bool runFilterLfoKeytrackCurveDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 170.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5000.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderHighNoteSecondAttack = [&](const bool linearCurve)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 980.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 2.0f;
        filter.lfoRateKeyTracking = 1.0f;
        filter.lfoAmountHz = 2600.0f;
        filter.lfoAmountKeyTracking = 1.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoKeytrackLinear = linearCurve;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 84), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto exponential = renderHighNoteSecondAttack(false);
    const auto linear = renderHighNoteSecondAttack(true);
    return !buffersAreEqual(exponential, linear, 1.0e-6f);
}

bool runFilterLfoUnipolarDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 165.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5150.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderSecondAttack = [&](const bool unipolar)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1020.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 3.0f;
        filter.lfoAmountHz = 2800.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoUnipolar = unipolar;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 72), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto bipolar = renderSecondAttack(false);
    const auto uni = renderSecondAttack(true);
    return !buffersAreEqual(bipolar, uni, 1.0e-6f);
}

bool runUltraQualityDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> sample(1, 8192);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto phase = static_cast<float>(i % 32) / 31.0f;
        const auto stair = std::floor(phase * 8.0f) / 8.0f;
        sample.setSample(0, i, stair * 2.0f - 1.0f);
    }

    auto renderTier = [&](const audiocity::engine::EngineCore::QualityTier tier)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setRootMidiNote(48);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setQualityTier(tier);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 79, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto fidelity = renderTier(audiocity::engine::EngineCore::QualityTier::fidelity);
    const auto ultra = renderTier(audiocity::engine::EngineCore::QualityTier::ultra);
    return !buffersAreEqual(fidelity, ultra, 1.0e-7f);
}

struct UltraSpectralMetric
{
    double mainBandEnergy = 0.0;
    double artifactEnergy = 0.0;
    double artifactRatio = std::numeric_limits<double>::infinity();
    int targetBin = 0;
    int peakBin = 0;
};

UltraSpectralMetric measureUltraSpectralMetric(const audiocity::engine::EngineCore::QualityTier tier)
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr int fftOrder = 12;
    constexpr int fftSize = 1 << fftOrder;
    constexpr int warmupBlocks = 4;
    constexpr int captureBlocks = fftSize / blockSize;
    constexpr int sampleLength = 32768;
    constexpr double sampleRate = 48000.0;
    constexpr int sourceRootNote = 60;
    constexpr int playedNote = 79;
    constexpr int targetBin = 1450;
    constexpr int mainBandRadius = 2;

    const auto pitchRatio = std::pow(2.0, static_cast<double>(playedNote - sourceRootNote) / 12.0);
    const auto targetFrequencyHz = static_cast<double>(targetBin) * sampleRate / static_cast<double>(fftSize);
    const auto sourceToneFrequencyHz = targetFrequencyHz / pitchRatio;

    juce::AudioBuffer<float> sample(1, sampleLength);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto phase = 2.0 * juce::MathConstants<double>::pi * sourceToneFrequencyHz * static_cast<double>(i) / sampleRate;
        sample.setSample(0, i, static_cast<float>(0.9 * std::sin(phase)));
    }

    audiocity::engine::EngineCore::AdsrSettings adsr;
    adsr.attackSeconds = 0.0001f;
    adsr.decaySeconds = 0.0001f;
    adsr.sustainLevel = 1.0f;
    adsr.releaseSeconds = 0.1f;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(sample, sampleRate, sourceRootNote);
    engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);
    engine.setAmpEnvelope(adsr);
    engine.setQualityTier(tier);

    juce::AudioBuffer<float> renderedBlock(channels, blockSize);
    juce::AudioBuffer<float> captured(1, fftSize);

    for (int blockIndex = 0; blockIndex < warmupBlocks + captureBlocks; ++blockIndex)
    {
        juce::MidiBuffer midi;
        if (blockIndex == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, playedNote, 1.0f), 0);

        engine.render(renderedBlock, midi);

        if (blockIndex >= warmupBlocks)
        {
            const auto capturedOffset = (blockIndex - warmupBlocks) * blockSize;
            captured.copyFrom(0, capturedOffset, renderedBlock, 0, 0, blockSize);
        }
    }

    std::array<float, fftSize * 2> fftData {};
    std::copy(captured.getReadPointer(0), captured.getReadPointer(0) + fftSize, fftData.begin());

    juce::dsp::WindowingFunction<float> window(fftSize,
                                               juce::dsp::WindowingFunction<float>::hann,
                                               true);
    window.multiplyWithWindowingTable(fftData.data(), fftSize);

    juce::dsp::FFT fft(fftOrder);
    fft.performRealOnlyForwardTransform(fftData.data());

    auto magnitudeSquaredAtBin = [&](const int bin)
    {
        const auto real = static_cast<double>(fftData[static_cast<std::size_t>(bin * 2)]);
        const auto imag = static_cast<double>(fftData[static_cast<std::size_t>(bin * 2 + 1)]);
        return real * real + imag * imag;
    };

    UltraSpectralMetric metric;
    metric.targetBin = targetBin;

    double peakMagnitude = 0.0;
    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const auto magnitudeSquared = magnitudeSquaredAtBin(bin);
        if (magnitudeSquared > peakMagnitude)
        {
            peakMagnitude = magnitudeSquared;
            metric.peakBin = bin;
        }
    }

    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const auto magnitudeSquared = magnitudeSquaredAtBin(bin);
        if (std::abs(bin - targetBin) <= mainBandRadius)
            metric.mainBandEnergy += magnitudeSquared;
        else
            metric.artifactEnergy += magnitudeSquared;
    }

    const auto denominator = juce::jmax(metric.mainBandEnergy, 1.0e-12);
    metric.artifactRatio = metric.artifactEnergy / denominator;
    return metric;
}

bool runUltraQualitySpectralTonePreservationTest()
{
    const auto fidelity = measureUltraSpectralMetric(audiocity::engine::EngineCore::QualityTier::fidelity);
    const auto ultra = measureUltraSpectralMetric(audiocity::engine::EngineCore::QualityTier::ultra);

    return std::isfinite(fidelity.mainBandEnergy)
        && std::isfinite(fidelity.artifactRatio)
        && std::isfinite(ultra.mainBandEnergy)
        && std::isfinite(ultra.artifactRatio)
        && std::abs(fidelity.peakBin - fidelity.targetBin) <= 1
        && std::abs(ultra.peakBin - ultra.targetBin) <= 1
        && fidelity.mainBandEnergy > 1.0
        && ultra.mainBandEnergy > 1.0
        && ultra.mainBandEnergy > fidelity.mainBandEnergy * 1.02
        && ultra.artifactEnergy < fidelity.artifactEnergy
        && ultra.artifactRatio < fidelity.artifactRatio * 0.90;
}

bool runReverbMixTailTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(2048);

    auto renderTailEnergy = [&](const float mix)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(mix);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        float tailEnergy = 0.0f;
        for (int i = 0; i < 10; ++i)
        {
            engine.render(block, midi);
            if (i >= 6)
                tailEnergy += blockEnergy(block);
        }

        return tailEnergy;
    };

    const auto dryTail = renderTailEnergy(0.0f);
    const auto wetTail = renderTailEnergy(0.35f);
    return wetTail > dryTail * 1.05f;
}

bool runDelayMixTailTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> impulse(1, 2048);
    impulse.clear();
    impulse.setSample(0, 0, 1.0f);

    auto renderTailEnergy = [&](const float mix)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(impulse, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(0.0f);

        audiocity::engine::EngineCore::DelaySettings delay;
        delay.timeMs = 30.0f;
        delay.feedback = 0.45f;
        delay.mix = mix;
        delay.tempoSync = false;
        engine.setDelaySettings(delay);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        float tailEnergy = 0.0f;
        for (int i = 0; i < 28; ++i)
        {
            engine.render(block, midi);
            if (i >= 5)
                tailEnergy += blockEnergy(block);
        }

        return tailEnergy;
    };

    const auto dryTail = renderTailEnergy(0.0f);
    const auto wetTail = renderTailEnergy(0.35f);
    return wetTail > dryTail * 1.15f;
}

bool runDelayTempoSyncRespondsToTempoTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> impulse(1, 4096);
    impulse.clear();
    impulse.setSample(0, 0, 1.0f);

    auto renderLeftChannel = [&](const float bpm)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(impulse, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(0.0f);
        engine.setHostTempoBpm(bpm);

        audiocity::engine::EngineCore::DelaySettings delay;
        delay.timeMs = 600.0f;
        delay.feedback = 0.0f;
        delay.mix = 1.0f;
        delay.tempoSync = true;
        engine.setDelaySettings(delay);

        constexpr int totalBlocks = 420;
        std::vector<float> rendered;
        rendered.resize(static_cast<std::size_t>(totalBlocks * blockSize), 0.0f);

        juce::AudioBuffer<float> block(channels, blockSize);
        for (int blockIndex = 0; blockIndex < totalBlocks; ++blockIndex)
        {
            juce::MidiBuffer midi;
            if (blockIndex == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

            engine.render(block, midi);
            for (int sampleIndex = 0; sampleIndex < blockSize; ++sampleIndex)
            {
                rendered[static_cast<std::size_t>(blockIndex * blockSize + sampleIndex)] = block.getSample(0, sampleIndex);
            }
        }

        return rendered;
    };

    auto windowEnergy = [](const std::vector<float>& signal, const int center, const int radius)
    {
        const auto start = juce::jlimit(0, static_cast<int>(signal.size()) - 1, center - radius);
        const auto end = juce::jlimit(start, static_cast<int>(signal.size()) - 1, center + radius);
        float energy = 0.0f;
        for (int i = start; i <= end; ++i)
            energy += std::abs(signal[static_cast<std::size_t>(i)]);
        return energy;
    };

    const auto rendered120 = renderLeftChannel(120.0f);
    const auto rendered90 = renderLeftChannel(90.0f);

    const auto sampleAt500ms = static_cast<int>(std::round(sampleRate * 0.500));
    const auto sampleAt667ms = static_cast<int>(std::round(sampleRate * (2.0 / 3.0)));
    constexpr int kWindowRadius = 256;

    const auto e120at500 = windowEnergy(rendered120, sampleAt500ms, kWindowRadius);
    const auto e120at667 = windowEnergy(rendered120, sampleAt667ms, kWindowRadius);
    const auto e90at500 = windowEnergy(rendered90, sampleAt500ms, kWindowRadius);
    const auto e90at667 = windowEnergy(rendered90, sampleAt667ms, kWindowRadius);

    if (e120at500 <= 1.0e-5f || e90at667 <= 1.0e-5f)
        return false;

    return e120at500 > e120at667 * 2.0f
        && e90at667 > e90at500 * 2.0f;
}

bool runDcOffsetFilterRemovesBiasTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> dcSample(1, 4096);
    dcSample.clear();
    for (int i = 0; i < dcSample.getNumSamples(); ++i)
        dcSample.setSample(0, i, 0.5f);

    auto renderMeanWithSettings = [&](const bool enabled, const float cutoffHz, const int channel)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(dcSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(0.0f);
        engine.setMasterVolume(1.0f);

        audiocity::engine::EngineCore::DcFilterSettings dc;
        dc.enabled = enabled;
        dc.cutoffHz = cutoffHz;
        engine.setDcFilterSettings(dc);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

        std::vector<float> captured;
        captured.reserve(static_cast<std::size_t>(blockSize * 16));
        for (int blockIndex = 0; blockIndex < 16; ++blockIndex)
        {
            engine.render(block, midi);
            midi.clear();
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
                captured.push_back(block.getSample(channel, sample));
        }

        constexpr int kSkip = 384;
        double sum = 0.0;
        int count = 0;
        for (int i = kSkip; i < static_cast<int>(captured.size()); ++i)
        {
            sum += static_cast<double>(captured[static_cast<std::size_t>(i)]);
            ++count;
        }

        return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 0.0f;
    };

    const auto meanBypassedLeft = renderMeanWithSettings(false, 10.0f, 0);
    const auto meanFilteredLeft = renderMeanWithSettings(true, 10.0f, 0);
    const auto meanBypassedRight = renderMeanWithSettings(false, 10.0f, 1);
    const auto meanFilteredRight = renderMeanWithSettings(true, 10.0f, 1);

    return std::abs(meanBypassedLeft) > 0.10f
        && std::abs(meanFilteredLeft) < 0.02f
        && std::abs(meanBypassedRight) > 0.10f
        && std::abs(meanFilteredRight) < 0.02f;
}

bool runMasterVolumeGainTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(2048);

    auto renderPeakForVolume = [&](const float volume)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(0.0f);
        engine.setMasterVolume(volume);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        float peak = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            peak = juce::jmax(peak, block.getMagnitude(channel, 0, block.getNumSamples()));

        return peak;
    };

    const auto peakUnity = renderPeakForVolume(1.0f);
    const auto peakHalf = renderPeakForVolume(0.5f);
    const auto peakMute = renderPeakForVolume(0.0f);

    if (peakUnity <= 1.0e-6f)
        return false;

    const auto ratio = peakHalf / peakUnity;
    return ratio > 0.45f && ratio < 0.55f && peakMute < 1.0e-7f;
}

bool runPanBalanceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(2048);

    auto renderChannelPeaks = [&](const float pan)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(0.0f);
        engine.setMasterVolume(1.0f);
        engine.setPan(pan);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        const auto leftPeak = block.getMagnitude(0, 0, block.getNumSamples());
        const auto rightPeak = block.getMagnitude(1, 0, block.getNumSamples());
        return std::pair<float, float>{ leftPeak, rightPeak };
    };

    const auto [centerLeft, centerRight] = renderChannelPeaks(0.0f);
    const auto [fullLeft, fullLeftRight] = renderChannelPeaks(-1.0f);
    const auto [fullRightLeft, fullRight] = renderChannelPeaks(1.0f);

    if (centerLeft <= 1.0e-6f || centerRight <= 1.0e-6f)
        return false;

    return fullLeft > centerLeft * 0.85f
        && fullLeftRight < centerRight * 0.05f
        && fullRight > centerRight * 0.85f
        && fullRightLeft < centerLeft * 0.05f;
}

bool runAutopanStereoMotionTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 60;
    constexpr int cycleSamples = 512;
    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);
    const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);

    auto renderDiffStats = [&](const float depth)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);
        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setReverbMix(0.0f);
        engine.setPan(0.0f);
        engine.setMasterVolume(1.0f);

        audiocity::engine::EngineCore::DelaySettings delay;
        delay.mix = 0.0f;
        engine.setDelaySettings(delay);

        audiocity::engine::EngineCore::AutopanSettings autopan;
        autopan.rateHz = 2.0f;
        autopan.depth = juce::jlimit(0.0f, 1.0f, depth);
        engine.setAutopanSettings(autopan);

        juce::AudioBuffer<float> block(channels, blockSize);
        double absDiffSum = 0.0;
        int counted = 0;
        float maxDiff = -std::numeric_limits<float>::infinity();
        float minDiff = std::numeric_limits<float>::infinity();

        for (int blockIndex = 0; blockIndex < 90; ++blockIndex)
        {
            juce::MidiBuffer midi;
            if (blockIndex == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            engine.render(block, midi);

            if (blockIndex < 8)
                continue;

            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto diff = block.getSample(0, sample) - block.getSample(1, sample);
                absDiffSum += std::abs(diff);
                ++counted;
                maxDiff = juce::jmax(maxDiff, diff);
                minDiff = juce::jmin(minDiff, diff);
            }
        }

        struct Stats
        {
            float meanAbsDiff = 0.0f;
            float minDiff = 0.0f;
            float maxDiff = 0.0f;
        };

        Stats stats;
        if (counted > 0)
            stats.meanAbsDiff = static_cast<float>(absDiffSum / static_cast<double>(counted));
        stats.minDiff = minDiff;
        stats.maxDiff = maxDiff;
        return stats;
    };

    const auto dry = renderDiffStats(0.0f);
    const auto mod = renderDiffStats(0.85f);

    return dry.meanAbsDiff < 0.003f
        && mod.meanAbsDiff > 0.05f
        && mod.maxDiff > 0.05f
        && mod.minDiff < -0.05f;
}

bool runSaturationDriveAndModeTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 60;
    constexpr int cycleSamples = 512;
    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);
    const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);

    auto renderSignal = [&](const float drive,
                            const audiocity::engine::EngineCore::SaturationSettings::Mode mode)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);
        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setReverbMix(0.0f);

        audiocity::engine::EngineCore::DelaySettings delay;
        delay.mix = 0.0f;
        engine.setDelaySettings(delay);

        audiocity::engine::EngineCore::AutopanSettings autopan;
        autopan.depth = 0.0f;
        engine.setAutopanSettings(autopan);

        engine.setPan(0.0f);
        engine.setMasterVolume(1.0f);

        audiocity::engine::EngineCore::SaturationSettings sat;
        sat.drive = drive;
        sat.mode = mode;
        engine.setSaturationSettings(sat);

        std::vector<float> captured;
        captured.reserve(static_cast<std::size_t>(blockSize * 80));
        for (int blockIndex = 0; blockIndex < 80; ++blockIndex)
        {
            juce::AudioBuffer<float> block(channels, blockSize);
            juce::MidiBuffer midi;
            if (blockIndex == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            engine.render(block, midi);

            if (blockIndex < 8)
                continue;

            for (int sample = 0; sample < blockSize; ++sample)
                captured.push_back(block.getSample(0, sample));
        }

        return captured;
    };

    const auto base = renderSignal(0.0f, audiocity::engine::EngineCore::SaturationSettings::Mode::softClip);
    const auto drivenSoft = renderSignal(0.8f, audiocity::engine::EngineCore::SaturationSettings::Mode::softClip);
    const auto drivenHard = renderSignal(0.8f, audiocity::engine::EngineCore::SaturationSettings::Mode::hardClip);
    const auto drivenTape = renderSignal(0.8f, audiocity::engine::EngineCore::SaturationSettings::Mode::tape);
    const auto drivenTube = renderSignal(0.8f, audiocity::engine::EngineCore::SaturationSettings::Mode::tube);

    if (base.empty() || drivenSoft.empty() || drivenHard.empty() || drivenTape.empty() || drivenTube.empty())
        return false;

    auto meanAbsDifference = [](const std::vector<float>& a, const std::vector<float>& b)
    {
        const auto count = juce::jmin(static_cast<int>(a.size()), static_cast<int>(b.size()));
        if (count <= 0)
            return 0.0f;

        double sum = 0.0;
        for (int i = 0; i < count; ++i)
            sum += std::abs(a[static_cast<std::size_t>(i)] - b[static_cast<std::size_t>(i)]);

        return static_cast<float>(sum / static_cast<double>(count));
    };

    const auto diffDrive = meanAbsDifference(base, drivenSoft);
    const auto diffHard = meanAbsDifference(drivenSoft, drivenHard);
    const auto diffTape = meanAbsDifference(drivenSoft, drivenTape);
    const auto diffTube = meanAbsDifference(drivenSoft, drivenTube);

    return diffDrive > 0.01f
        && diffHard > 0.002f
        && diffTape > 0.002f
        && diffTube > 0.002f;
}

bool runTuneCoarseFinePitchShiftTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr int blocks = 56;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 60;
    constexpr int cycleSamples = 512;

    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);
    const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);

    auto renderFrequency = [&](const float coarseSemitones, const float fineCents) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setCoarseTuneSemitones(coarseSemitones);
        engine.setFineTuneCents(fineCents);

        const auto rendered = renderHeldNote(engine, rootMidiNote, blockSize, blocks, channels);
        return estimateFrequencyFromPositiveCrossings(rendered, outputSampleRate, blockSize * 3);
    };

    const auto baseHz = renderFrequency(0.0f, 0.0f);
    const auto coarseUpHz = renderFrequency(12.0f, 0.0f);
    const auto coarseDownHz = renderFrequency(-12.0f, 0.0f);
    const auto fineUpHz = renderFrequency(0.0f, 100.0f);

    if (baseHz <= 0.0f || coarseUpHz <= 0.0f || coarseDownHz <= 0.0f || fineUpHz <= 0.0f)
        return false;

    const auto coarseUpRatio = coarseUpHz / baseHz;
    const auto coarseDownRatio = coarseDownHz / baseHz;
    const auto fineUpRatio = fineUpHz / baseHz;
    const auto expectedFineRatio = std::pow(2.0f, 1.0f / 12.0f);

    return std::abs(coarseUpRatio - 2.0f) < 0.12f
        && std::abs(coarseDownRatio - 0.5f) < 0.06f
        && std::abs(fineUpRatio - expectedFineRatio) < 0.04f;
}

bool runPitchBendRangeAndRealtimeModulationTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr int blocks = 56;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 60;
    constexpr int cycleSamples = 512;

    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);
    const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);

    auto renderFrequency = [&](const float bendRangeSemitones, const bool bendUp) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setPitchBendRangeSemitones(bendRangeSemitones);

        juce::AudioBuffer<float> output(channels, blockSize * blocks);
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            if (bendUp && block == 8)
                midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);

            engine.render(blockBuffer, midi);

            for (int ch = 0; ch < channels; ++ch)
                output.copyFrom(ch, block * blockSize, blockBuffer, ch, 0, blockSize);
        }

        return estimateFrequencyFromPositiveCrossings(output, outputSampleRate, blockSize * 20);
    };

    const auto baseHz = renderFrequency(2.0f, false);
    const auto bend2Hz = renderFrequency(2.0f, true);
    const auto bend12Hz = renderFrequency(12.0f, true);

    if (baseHz <= 0.0f || bend2Hz <= 0.0f || bend12Hz <= 0.0f)
        return false;

    const auto ratio2 = bend2Hz / baseHz;
    const auto ratio12 = bend12Hz / baseHz;
    const auto expected2 = std::pow(2.0f, 2.0f / 12.0f);
    const auto expected12 = 2.0f;

    return std::abs(ratio2 - expected2) < 0.05f
        && std::abs(ratio12 - expected12) < 0.12f
        && ratio12 > ratio2 * 1.5f;
}

bool runModulationRoutingRealtimeTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr int blocks = 56;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 60;
    constexpr int cycleSamples = 512;

    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);
    const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);

    auto renderPitchFrequency = [&](const float modWheelToPitchCents, const bool applyWheel) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        auto routing = engine.getModulationRoutingSettings();
        routing.modWheel.toPitchCents = modWheelToPitchCents;
        engine.setModulationRoutingSettings(routing);

        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        juce::AudioBuffer<float> output(channels, blockSize * blocks);
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            if (applyWheel && block == 8)
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);

            engine.render(blockBuffer, midi);

            for (int ch = 0; ch < channels; ++ch)
                output.copyFrom(ch, block * blockSize, blockBuffer, ch, 0, blockSize);
        }

        return estimateFrequencyFromPositiveCrossings(output, outputSampleRate, blockSize * 20);
    };

    auto renderAftertouchPitchFrequency = [&](const float aftertouchToPitchCents, const bool applyAftertouch) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        auto routing = engine.getModulationRoutingSettings();
        routing.aftertouch.toPitchCents = aftertouchToPitchCents;
        engine.setModulationRoutingSettings(routing);

        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        juce::AudioBuffer<float> output(channels, blockSize * blocks);
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            if (applyAftertouch && block == 8)
                engine.channelPressure(127, 0);

            engine.render(blockBuffer, midi);

            for (int ch = 0; ch < channels; ++ch)
                output.copyFrom(ch, block * blockSize, blockBuffer, ch, 0, blockSize);
        }

        return estimateFrequencyFromPositiveCrossings(output, outputSampleRate, blockSize * 20);
    };

    auto renderVelocityPitchFrequency = [&](const float velocityToPitchCents, const float noteVelocity) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        auto routing = engine.getModulationRoutingSettings();
        routing.velocity.toPitchCents = velocityToPitchCents;
        engine.setModulationRoutingSettings(routing);

        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        juce::AudioBuffer<float> output(channels, blockSize * blocks);
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, noteVelocity), 0);

            engine.render(blockBuffer, midi);

            for (int ch = 0; ch < channels; ++ch)
                output.copyFrom(ch, block * blockSize, blockBuffer, ch, 0, blockSize);
        }

        return estimateFrequencyFromPositiveCrossings(output, outputSampleRate, blockSize * 20);
    };

    auto renderAmpEnergy = [&](const float modWheelToAmp, const bool applyWheel) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        auto routing = engine.getModulationRoutingSettings();
        routing.modWheel.toAmp = modWheelToAmp;
        engine.setModulationRoutingSettings(routing);

        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        float accumulatedEnergy = 0.0f;
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            if (applyWheel && block == 8)
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);

            engine.render(blockBuffer, midi);
            if (block >= 16)
                accumulatedEnergy += blockEnergy(blockBuffer);
        }

        return accumulatedEnergy;
    };

    auto renderMacroAmpEnergy = [&](const int macroIndex, const float macroToAmp, const float macroValue) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        auto routing = engine.getModulationRoutingSettings();
        routing.macros[static_cast<std::size_t>(macroIndex)].toAmp = macroToAmp;
        engine.setModulationRoutingSettings(routing);

        auto macroValues = engine.getMacroControlValues();
        macroValues[static_cast<std::size_t>(macroIndex)] = macroValue;
        engine.setMacroControlValues(macroValues);

        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        float accumulatedEnergy = 0.0f;
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            engine.render(blockBuffer, midi);
            if (block >= 16)
                accumulatedEnergy += blockEnergy(blockBuffer);
        }

        return accumulatedEnergy;
    };

    auto renderFilterEnergy = [&](const float modWheelToFilterHz, const bool applyWheel) -> float
    {
        juce::AudioBuffer<float> brightSample(1, 4096);
        for (int i = 0; i < brightSample.getNumSamples(); ++i)
        {
            const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 180.0 / outputSampleRate);
            const auto high = 0.7 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5200.0 / outputSampleRate);
            brightSample.setSample(0, i, static_cast<float>(0.25 * low + high));
        }

        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 900.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        engine.setFilterSettings(filter);

        auto routing = engine.getModulationRoutingSettings();
        routing.modWheel.toFilterHz = modWheelToFilterHz;
        engine.setModulationRoutingSettings(routing);

        engine.setSampleData(brightSample, outputSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        float accumulatedEnergy = 0.0f;
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            if (applyWheel && block == 8)
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);

            engine.render(blockBuffer, midi);
            if (block >= 16)
                accumulatedEnergy += blockEnergy(blockBuffer);
        }

        return accumulatedEnergy;
    };

    auto renderMacroFilterEnergy = [&](const int macroIndex, const float macroToFilterHz, const float macroValue) -> float
    {
        juce::AudioBuffer<float> brightSample(1, 4096);
        for (int i = 0; i < brightSample.getNumSamples(); ++i)
        {
            const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 180.0 / outputSampleRate);
            const auto high = 0.7 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5200.0 / outputSampleRate);
            brightSample.setSample(0, i, static_cast<float>(0.25 * low + high));
        }

        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 900.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        engine.setFilterSettings(filter);

        auto routing = engine.getModulationRoutingSettings();
        routing.macros[static_cast<std::size_t>(macroIndex)].toFilterHz = macroToFilterHz;
        engine.setModulationRoutingSettings(routing);

        auto macroValues = engine.getMacroControlValues();
        macroValues[static_cast<std::size_t>(macroIndex)] = macroValue;
        engine.setMacroControlValues(macroValues);

        engine.setSampleData(brightSample, outputSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        float accumulatedEnergy = 0.0f;
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            engine.render(blockBuffer, midi);
            if (block >= 16)
                accumulatedEnergy += blockEnergy(blockBuffer);
        }

        return accumulatedEnergy;
    };

    const auto basePitchHz = renderPitchFrequency(200.0f, false);
    const auto wheelPitchHz = renderPitchFrequency(200.0f, true);
    if (basePitchHz <= 0.0f || wheelPitchHz <= 0.0f)
        return false;

    const auto ampBaseEnergy = renderAmpEnergy(-1.0f, false);
    const auto ampWheelEnergy = renderAmpEnergy(-1.0f, true);
    const auto filterBaseEnergy = renderFilterEnergy(6000.0f, false);
    const auto filterWheelEnergy = renderFilterEnergy(6000.0f, true);
    const auto aftertouchBasePitchHz = renderAftertouchPitchFrequency(300.0f, false);
    const auto aftertouchPitchHz = renderAftertouchPitchFrequency(300.0f, true);
    const auto lowVelocityPitchHz = renderVelocityPitchFrequency(400.0f, 0.25f);
    const auto highVelocityPitchHz = renderVelocityPitchFrequency(400.0f, 1.0f);
    const auto macro1FilterBaseEnergy = renderMacroFilterEnergy(0, 6000.0f, 0.0f);
    const auto macro1FilterEnergy = renderMacroFilterEnergy(0, 6000.0f, 1.0f);
    const auto macro2AmpBaseEnergy = renderMacroAmpEnergy(1, -1.0f, 0.0f);
    const auto macro2AmpEnergy = renderMacroAmpEnergy(1, -1.0f, 1.0f);

    return wheelPitchHz > basePitchHz * 1.08f
        && ampBaseEnergy > 0.1f
        && ampWheelEnergy < ampBaseEnergy * 0.2f
        && filterWheelEnergy > filterBaseEnergy * 1.15f
        && aftertouchBasePitchHz > 0.0f
        && aftertouchPitchHz > aftertouchBasePitchHz * 1.08f
        && lowVelocityPitchHz > 0.0f
        && highVelocityPitchHz > lowVelocityPitchHz * 1.12f
        && macro1FilterEnergy > macro1FilterBaseEnergy * 1.15f
        && macro2AmpBaseEnergy > 0.1f
        && macro2AmpEnergy < macro2AmpBaseEnergy * 0.2f;
}

bool runVoicePlaybackStateSnapshotTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 32);
        engine.render(block, midi);
    }

    const auto states = engine.getVoicePlaybackStates();
    int activeCount = 0;
    int positivePositions = 0;
    for (const auto& state : states)
    {
        if (!state.active)
        {
            if (state.sampleIndex >= 0)
                return false;
            continue;
        }

        ++activeCount;
        if (state.sampleIndex > 0)
            ++positivePositions;
    }

    if (activeCount != 2 || positivePositions == 0)
        return false;

    engine.panic();
    const auto afterPanic = engine.getVoicePlaybackStates();
    for (const auto& state : afterPanic)
    {
        if (state.active || state.sampleIndex >= 0)
            return false;
    }

    return true;
}

bool runCcLearnDialUserClearCallbackTest()
{
    CcLearnDial dial("Test", 0.0, 1.0, 0.01, {}, 0.5);
    bool callbackInvoked = false;
    dial.onCcClearedByUser = [&callbackInvoked]
    {
        callbackInvoked = true;
    };

    dial.assignCc(74);
    dial.clearCcByUser();

    return callbackInvoked && dial.getAssignedCc() == -1 && !dial.isCcLearnArmed();
}

bool runSettingsSnapshotCaptureFieldsAffectEqualityTest()
{
    audiocity::engine::SettingsSnapshot baseline;
    baseline.captureTargetSampleRate = 0;
    baseline.captureChannelMode = 0;
    baseline.captureBitDepth = 16;
    baseline.captureInputGain = 1.0f;

    auto changed = baseline;
    changed.captureTargetSampleRate = 48000;
    if (changed == baseline)
        return false;

    changed = baseline;
    changed.captureChannelMode = 3;
    if (changed == baseline)
        return false;

    changed = baseline;
    changed.captureBitDepth = 24;
    if (changed == baseline)
        return false;

    changed = baseline;
    changed.captureInputGain = 1.25f;
    if (changed == baseline)
        return false;

    return true;
}

bool runSettingsUndoHistoryTracksCaptureSettingsTest()
{
    audiocity::engine::SettingsUndoHistory history;
    audiocity::engine::SettingsSnapshot before;
    audiocity::engine::SettingsSnapshot after = before;
    after.captureTargetSampleRate = 44100;
    after.captureChannelMode = 1;
    after.captureBitDepth = 24;
    after.captureInputGain = 1.4f;

    history.recordChange(before, after, -1, "Capture Settings");
    if (!history.canUndo())
        return false;

    const auto undoSnapshot = history.undo(after);
    if (!undoSnapshot.has_value())
        return false;

    if (undoSnapshot->captureTargetSampleRate != before.captureTargetSampleRate
        || undoSnapshot->captureChannelMode != before.captureChannelMode
        || undoSnapshot->captureBitDepth != before.captureBitDepth
        || undoSnapshot->captureInputGain != before.captureInputGain)
    {
        return false;
    }

    const auto redoSnapshot = history.redo(before);
    if (!redoSnapshot.has_value())
        return false;

    return redoSnapshot->captureTargetSampleRate == after.captureTargetSampleRate
        && redoSnapshot->captureChannelMode == after.captureChannelMode
        && redoSnapshot->captureBitDepth == after.captureBitDepth
        && redoSnapshot->captureInputGain == after.captureInputGain;
}

bool runPresetXmlRoundTripWithEmbeddedSampleDataTest()
{
    constexpr auto kPatchRoot = "AudiocityPatch";

    juce::ValueTree state(kPatchRoot);
    state.setProperty("samplePath", juce::String("C:/Samples/Kick.wav"), nullptr);
    state.setProperty("rootMidiNote", 60, nullptr);
    state.setProperty("playbackMode", 2, nullptr);

    std::vector<float> generatedWave{ 0.0f, 0.25f, -0.25f, 0.75f, -0.5f };
    juce::MemoryBlock waveBytes(generatedWave.size() * sizeof(float));
    std::memcpy(waveBytes.getData(), generatedWave.data(), waveBytes.getSize());
    state.setProperty("generatedWaveformData", juce::var(waveBytes), nullptr);

    const auto xml = audiocity::plugin::encodePresetXml(state);
    if (xml.isEmpty())
        return false;

    juce::ValueTree decoded;
    juce::String error;
    if (!audiocity::plugin::decodePresetXml(xml, decoded, error))
        return false;

    if (!decoded.isValid() || !decoded.hasType(kPatchRoot))
        return false;

    if (decoded.getProperty("samplePath").toString() != "C:/Samples/Kick.wav")
        return false;

    if (static_cast<int>(decoded.getProperty("rootMidiNote", -1)) != 60)
        return false;

    const auto* decodedBinary = decoded.getProperty("generatedWaveformData").getBinaryData();
    if (decodedBinary == nullptr || decodedBinary->getSize() != waveBytes.getSize())
        return false;

    return std::memcmp(decodedBinary->getData(), waveBytes.getData(), waveBytes.getSize()) == 0;
}

bool runPresetXmlRejectsInvalidPayloadTest()
{
    juce::ValueTree decoded;
    juce::String error;
    const auto ok = audiocity::plugin::decodePresetXml("this is not xml", decoded, error);
    return !ok && error.isNotEmpty();
}

bool runLibraryMetadataFavoritesAndRecentsTest()
{
    audiocity::plugin::LibraryMetadata metadata;
    const juce::String kick = "C:/Library/Drums/Kick.wav";
    const juce::String kickWindows = "C:\\Library\\Drums\\Kick.wav";
    const juce::String snare = "C:/Library/Drums/Snare.wav";

    metadata.setFavorite(kick, true);
    metadata.setFavorite(kickWindows, true);
    if (!metadata.isFavorite(kickWindows) || metadata.getFavoritePaths().size() != 1)
        return false;

    metadata.setFavorite(kickWindows, false);
    if (metadata.isFavorite(kick) || !metadata.getFavoritePaths().isEmpty())
        return false;

    metadata.markRecent(kick);
    metadata.markRecent(snare);
    metadata.markRecent(kickWindows);
    if (metadata.getRecentPaths().size() != 2
        || metadata.recentRank(kick) != 0
        || metadata.recentRank(snare) != 1)
    {
        return false;
    }

    juce::StringArray tags;
    tags.add("Drums");
    tags.add("#OneShot");
    tags.add("drums");
    metadata.setTags(kick, tags);
    if (!metadata.hasTag(kickWindows, "oneshot")
        || metadata.getTags(kick).size() != 2)
    {
        return false;
    }

    const auto allTags = metadata.getAllTags();
    if (allTags.size() != 2
        || !allTags[0].equalsIgnoreCase("Drums")
        || !allTags[1].equalsIgnoreCase("OneShot"))
    {
        return false;
    }

    juce::StringArray emptyTags;
    metadata.setTags(kick, emptyTags);
    if (!metadata.getTags(kick).isEmpty())
        return false;

    for (int i = 0; i < audiocity::plugin::LibraryMetadata::maxRecentItems + 4; ++i)
        metadata.markRecent("C:/Library/Generated/Item" + juce::String(i) + ".wav");

    return metadata.getRecentPaths().size() == audiocity::plugin::LibraryMetadata::maxRecentItems
        && !metadata.isRecent(snare);
}

bool runLibraryMetadataValueTreeRoundTripTest()
{
    audiocity::plugin::LibraryMetadata metadata;
    const juce::String favorite = "C:/Library/Bass/Sub.wav";
    const juce::String newest = "C:/Library/Keys/EPiano.wav";
    const juce::String older = "C:/Library/Keys/Clav.wav";

    metadata.setFavorite(favorite, true);
    metadata.markRecent(older);
    metadata.markRecent(newest);
    juce::StringArray tags;
    tags.add("Keys");
    tags.add("Warm");
    metadata.setTags(newest, tags);

    const auto restored = audiocity::plugin::LibraryMetadata::fromValueTree(metadata.toValueTree());
    if (!restored.isFavorite(favorite)
        || restored.recentRank(newest) != 0
        || restored.recentRank(older) != 1
        || !restored.hasTag(newest, "warm"))
    {
        return false;
    }

    const auto empty = audiocity::plugin::LibraryMetadata::fromValueTree(juce::ValueTree("notLibraryMetadata"));
    return empty.getFavoritePaths().isEmpty() && empty.getRecentPaths().isEmpty();
}

bool runLibraryMetadataBookmarksTest()
{
    audiocity::plugin::LibraryMetadata metadata;
    const juce::String drums = "C:\\Library\\Drums";
    const juce::String drumsNormalized = "C:/Library/Drums";
    const juce::String synths = "C:/Library/Synths";

    metadata.addBookmark(drums);
    metadata.addBookmark(synths);
    metadata.addBookmark(drumsNormalized);

    auto bookmarks = metadata.getBookmarkPaths();
    if (bookmarks.size() != 2
        || !metadata.isBookmark(drumsNormalized)
        || !bookmarks[0].equalsIgnoreCase(drumsNormalized)
        || !bookmarks[1].equalsIgnoreCase(synths))
    {
        return false;
    }

    metadata.removeBookmark("c:/library/drums");
    if (metadata.isBookmark(drums) || metadata.getBookmarkPaths().size() != 1)
        return false;

    metadata.addBookmark(drums);
    for (int i = 0; i < audiocity::plugin::LibraryMetadata::maxBookmarkItems + 4; ++i)
        metadata.addBookmark("C:/Library/Generated/Root" + juce::String(i));

    bookmarks = metadata.getBookmarkPaths();
    if (bookmarks.size() != audiocity::plugin::LibraryMetadata::maxBookmarkItems
        || metadata.isBookmark(synths)
        || metadata.isBookmark(drums))
    {
        return false;
    }

    const auto restored = audiocity::plugin::LibraryMetadata::fromValueTree(metadata.toValueTree());
    const auto restoredBookmarks = restored.getBookmarkPaths();
    if (restoredBookmarks.size() != bookmarks.size())
        return false;

    for (int index = 0; index < bookmarks.size(); ++index)
    {
        if (!restoredBookmarks[index].equalsIgnoreCase(bookmarks[index]))
            return false;
    }

    return true;
}

bool runImportFormatRegistryConsistencyTest()
{
    using namespace audiocity::plugin;

    const auto descriptors = importFormatDescriptors();
    if (descriptors.size() != 18)
        return false;

    const ImportFormatCapabilities unavailableCapabilities{ false, false };
    const ImportFormatCapabilities availableCapabilities{ true, true };
    juce::StringArray allPatterns;
    allPatterns.addTokens(buildImportChooserWildcard(unavailableCapabilities, true), ";", {});
    juce::StringArray availablePatterns;
    availablePatterns.addTokens(buildImportChooserWildcard(unavailableCapabilities, false), ";", {});

    std::set<int> instrumentFormats;
    std::set<std::string> stateTokens;
    for (const auto& descriptor : descriptors)
    {
        if (descriptor.badge.empty()
            || descriptor.description.empty()
            || descriptor.extensionCount == 0
            || descriptor.extensionCount > descriptor.extensions.size())
        {
            return false;
        }

        const auto representativePath = descriptor.requiredFileName.empty()
            ? (juce::String("Fixture") + descriptor.extensions[0].data())
            : juce::String(descriptor.requiredFileName.data());
        const auto* matched = findImportFormatDescriptorForPath(representativePath);
        if (matched != &descriptor
            || !isKnownImportPath(representativePath)
            || isMappingImportPath(representativePath) != descriptor.supportsMappingDrag)
        {
            return false;
        }

        const auto available = isImportFormatAvailable(descriptor, unavailableCapabilities);
        const auto expectedAvailable = descriptor.availability == ImportFormatAvailability::always;
        const auto expectedAvailableWithCapabilities =
            descriptor.availability != ImportFormatAvailability::diagnosticOnly;
        if (descriptor.availability == ImportFormatAvailability::diagnosticOnly
            && (descriptor.unavailableMessage.empty()
                || descriptor.supportsMappingDrag
                || descriptor.supportsBackgroundImport))
        {
            return false;
        }
        if (available != expectedAvailable
            || isImportPathAvailable(representativePath, unavailableCapabilities) != expectedAvailable
            || isImportPathAvailable(representativePath, availableCapabilities)
                != expectedAvailableWithCapabilities)
        {
            return false;
        }

        const auto unavailableMessage = importPathUnavailableMessage(
            representativePath, unavailableCapabilities);
        if (expectedAvailable != unavailableMessage.isEmpty())
            return false;

        if (descriptor.isInstrument)
        {
            if (descriptor.stateToken.empty()
                || !instrumentFormats.insert(static_cast<int>(descriptor.format)).second
                || !stateTokens.insert(std::string(descriptor.stateToken)).second
                || findImportFormatDescriptor(descriptor.format) != &descriptor
                || parseImportedProgramFormatToken(descriptor.stateToken.data()) != descriptor.format
                || importedProgramFormatToken(descriptor.format) != descriptor.stateToken.data()
                || supportsBackgroundImport(descriptor.format) != descriptor.supportsBackgroundImport)
            {
                return false;
            }
        }

        if (!descriptor.requiredFileName.empty())
        {
            if (!allPatterns.contains(descriptor.requiredFileName.data())
                || availablePatterns.contains(descriptor.requiredFileName.data()) != expectedAvailable)
            {
                return false;
            }
            continue;
        }

        for (std::size_t extensionIndex = 0;
             extensionIndex < descriptor.extensionCount;
             ++extensionIndex)
        {
            const auto pattern = "*" + juce::String(descriptor.extensions[extensionIndex].data());
            if (!allPatterns.contains(pattern)
                || availablePatterns.contains(pattern) != expectedAvailable)
            {
                return false;
            }
        }
    }

    const auto* bento = findImportFormatDescriptorForPath("preset.xml");
    const auto diagnosticBrowserRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_diagnostic_format_browser", "");
    const auto diagnosticBrowserFile = diagnosticBrowserRoot.getChildFile("Patch.sxt");
    const auto createdDiagnosticFixture = diagnosticBrowserRoot.createDirectory()
        && diagnosticBrowserFile.replaceWithText("CAT NN-XT");
    const auto browserIncludesDiagnostic = createdDiagnosticFixture
        && LibraryFileIndex::isSupportedFile(
            diagnosticBrowserFile, availableCapabilities, true);
    const auto browserExcludesUnavailable = createdDiagnosticFixture
        && !LibraryFileIndex::isSupportedFile(
            diagnosticBrowserFile, availableCapabilities, false);
    diagnosticBrowserRoot.deleteRecursively();

    return instrumentFormats.size() == 16
        && stateTokens.size() == 16
        && bento != nullptr
        && bento->format == ImportedProgramFormat::bento1010
        && findImportFormatDescriptorForPath("NotPreset.xml") == nullptr
        && allPatterns.contains("preset.xml")
        && !allPatterns.contains("*.xml")
        && !availablePatterns.contains("*.rex")
        && !availablePatterns.contains("*.rx2")
        && !availablePatterns.contains("*.ncw")
        && !availablePatterns.contains("*.sxt")
        && isKnownImportPath("Patch.sxt")
        && !isMappingImportPath("Patch.sxt")
        && !supportsBackgroundImport(ImportedProgramFormat::nnxt)
        && browserIncludesDiagnostic
        && browserExcludesUnavailable
        && buildImportChooserTitle(unavailableCapabilities).contains("REX")
        && buildImportChooserTitle(unavailableCapabilities).contains("NCW")
        && buildImportChooserTitle(unavailableCapabilities).contains("NNXT")
        && importPathUnavailableMessage("Patch.sxt", availableCapabilities).contains("not yet supported");
}

bool runLibraryFileIndexScanTest()
{
    using audiocity::plugin::LibraryFileIndex;

    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_library_index_test", "");
    if (!tempRoot.createDirectory())
        return false;

    const auto nested = tempRoot.getChildFile("Nested");
    if (!nested.createDirectory()
        || !tempRoot.getChildFile("Kick.WAV").replaceWithText("")
        || !tempRoot.getChildFile("Layer.ncw").replaceWithText("")
        || !tempRoot.getChildFile("Patch.sfz").replaceWithText("<region> sample=Kick.WAV\n")
        || !tempRoot.getChildFile("LegacyKit.nki").replaceWithText("probe-only")
        || !tempRoot.getChildFile("Loop.rx2").replaceWithText("")
        || !tempRoot.getChildFile("Ignore.txt").replaceWithText("")
        || !nested.getChildFile("Snare.aif").replaceWithText(""))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    if (!LibraryFileIndex::isSupportedExtension(".RX2", true)
        || LibraryFileIndex::isSupportedExtension(".RX2", false)
        || !LibraryFileIndex::isSupportedExtension(".NKI", false)
        || !LibraryFileIndex::isSupportedExtension(".NCW", false)
        || LibraryFileIndex::isSupportedExtension(".txt", true))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    const auto withoutRex = LibraryFileIndex::scanRoot(tempRoot, false);
    const auto withRex = LibraryFileIndex::scanRoot(tempRoot, true);

    auto hasRelativePath = [](const std::vector<audiocity::plugin::LibraryFileIndexEntry>& entries,
                              const juce::String& relativePath)
    {
        for (const auto& entry : entries)
        {
            if (entry.relativePath == relativePath)
                return true;
        }

        return false;
    };

    auto hasInstrument = [](const std::vector<audiocity::plugin::LibraryFileIndexEntry>& entries)
    {
        auto sawSfz = false;
        auto sawNki = false;
        for (const auto& entry : entries)
        {
            if (entry.isInstrument && entry.extensionLower == ".sfz")
                sawSfz = true;

            if (entry.isInstrument && entry.extensionLower == ".nki")
                sawNki = true;
        }

        return sawSfz && sawNki;
    };

    const auto ok = withoutRex.size() == 5
        && withRex.size() == 6
        && hasRelativePath(withoutRex, "Layer.ncw")
        && hasRelativePath(withoutRex, "Nested/Snare.aif")
        && hasRelativePath(withoutRex, "LegacyKit.nki")
        && !hasRelativePath(withoutRex, "Loop.rx2")
        && hasRelativePath(withRex, "Loop.rx2")
        && hasInstrument(withoutRex);

    tempRoot.deleteRecursively();
    return ok;
}

bool runLibraryFileIndexPersistentStoreTest()
{
    using audiocity::plugin::LibraryFileIndexData;
    using audiocity::plugin::LibraryFileIndexEntry;
    using audiocity::plugin::LibraryFileIndexStore;

    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_library_index_store", "");
    const auto libraryA = tempRoot.getChildFile("LibraryA");
    const auto libraryB = tempRoot.getChildFile("LibraryB");
    if (!libraryA.createDirectory() || !libraryB.createDirectory())
        return false;

    if (LibraryFileIndexStore::getCacheFileForRoot(libraryA)
        == LibraryFileIndexStore::getCacheFileForRoot(libraryB))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    const auto cacheFile = tempRoot.getChildFile("index.acli");
    LibraryFileIndexStore store(cacheFile);
    LibraryFileIndexData source;
    source.libraryRootPath = libraryA.getFullPathName();
    LibraryFileIndexEntry kick;
    kick.relativePath = "Drums/Kick.wav";
    kick.sizeBytes = 4096;
    kick.modificationTimeMs = 123456;
    source.entries.push_back(kick);
    LibraryFileIndexEntry patch;
    patch.relativePath = "Patches/Kit.sfz";
    patch.sizeBytes = 8192;
    patch.modificationTimeMs = 234567;
    patch.isInstrument = true;
    source.entries.push_back(patch);

    if (!store.save(source))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    const auto restored = store.load();
    if (!restored.has_value()
        || restored->libraryRootPath != libraryA.getFullPathName()
        || restored->entries.size() != 2
        || restored->entries[0].relativePath != kick.relativePath
        || restored->entries[0].file != libraryA.getChildFile(kick.relativePath)
        || restored->entries[0].sizeBytes != kick.sizeBytes
        || restored->entries[1].relativePath != patch.relativePath
        || !restored->entries[1].isInstrument)
    {
        tempRoot.deleteRecursively();
        return false;
    }

    auto cancelledReplacement = source;
    cancelledReplacement.entries[0].sizeBytes = 999999;
    auto cancellationChecks = 0;
    if (store.save(cancelledReplacement, [&cancellationChecks]
        {
            return ++cancellationChecks >= 3;
        }))
    {
        tempRoot.deleteRecursively();
        return false;
    }
    const auto preservedAfterCancelledSave = store.load();
    if (!preservedAfterCancelledSave.has_value()
        || preservedAfterCancelledSave->entries.size() != source.entries.size()
        || preservedAfterCancelledSave->entries[0].sizeBytes != kick.sizeBytes)
    {
        tempRoot.deleteRecursively();
        return false;
    }

    juce::MemoryBlock encoded;
    if (!cacheFile.loadFileAsData(encoded) || encoded.getSize() < 40)
    {
        tempRoot.deleteRecursively();
        return false;
    }
    auto* encodedBytes = static_cast<juce::uint8*>(encoded.getData());
    encodedBytes[encoded.getSize() - 1] ^= 0x5a;
    if (!cacheFile.replaceWithData(encoded.getData(), encoded.getSize()))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    LibraryFileIndexData destination;
    destination.libraryRootPath = "sentinel";
    destination.entries.push_back(kick);
    if (store.load(destination)
        || destination.libraryRootPath != "sentinel"
        || destination.entries.size() != 1)
    {
        tempRoot.deleteRecursively();
        return false;
    }

    source.entries[0].relativePath = "../escape.wav";
    const auto rejectedUnsafePath = !store.save(source);
    juce::String overDeepPath;
    for (auto component = 0; component < 257; ++component)
        overDeepPath += "d/";
    source.entries[0].relativePath = overDeepPath + "sample.wav";
    const auto rejectedOverDeepPath = !store.save(source);
    tempRoot.deleteRecursively();
    return rejectedUnsafePath && rejectedOverDeepPath;
}

bool runLibraryFileIndex50kScanSearchIntegrationTest()
{
    using audiocity::plugin::LibraryFileIndex;

   #if defined(NDEBUG)
    constexpr auto kEntryCount = std::size_t{ 50000 };
   #else
    // Debug exercises the same real traversal, batching, search, and cancellation
    // paths without turning every local test run into a filesystem benchmark.
    constexpr auto kEntryCount = std::size_t{ 4096 };
   #endif
    constexpr auto kFilesPerDirectory = std::size_t{ 500 };
    constexpr auto kMaximumBatchSize = std::size_t{ 256 };
    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_library_scan_50k", "");
    const auto libraryRoot = tempRoot.getChildFile("Library");
    struct DirectoryCleanup final
    {
        juce::File directory;
        ~DirectoryCleanup() { static_cast<void>(directory.deleteRecursively()); }
    } cleanup{ tempRoot };

    if (!libraryRoot.createDirectory())
        return false;

    const auto setupStart = juce::Time::getMillisecondCounterHiRes();
    juce::File currentDirectory;
    for (std::size_t index = 0; index < kEntryCount; ++index)
    {
        if (index % kFilesPerDirectory == 0)
        {
            currentDirectory = libraryRoot.getChildFile(
                "Bank_" + juce::String(static_cast<juce::int64>(index / kFilesPerDirectory)));
            if (!currentDirectory.createDirectory())
                return false;
        }

        const auto file = currentDirectory.getChildFile(
            "Sample_" + juce::String(static_cast<juce::int64>(index)).paddedLeft('0', 5) + ".wav");
        std::ofstream output(file.getFullPathName().toStdString(), std::ios::binary | std::ios::trunc);
        output.put(static_cast<char>(index & 0xffu));
        if (!output.good())
            return false;
    }
    const auto setupDurationMs = juce::Time::getMillisecondCounterHiRes() - setupStart;

    auto firstDeliveryMs = -1.0;
    auto deliveredEntries = std::size_t{ 0 };
    auto largestBatch = std::size_t{ 0 };
    const auto scanStart = juce::Time::getMillisecondCounterHiRes();
    auto scan = LibraryFileIndex::scanRootIncrementally(
        libraryRoot,
        audiocity::plugin::ImportFormatCapabilities{ false, true },
        true,
        {},
        [&](const std::span<const audiocity::plugin::LibraryFileIndexEntry> batch)
        {
            if (firstDeliveryMs < 0.0)
                firstDeliveryMs = juce::Time::getMillisecondCounterHiRes() - scanStart;
            deliveredEntries += batch.size();
            largestBatch = juce::jmax(largestBatch, batch.size());
        });
    const auto fullScanDurationMs = juce::Time::getMillisecondCounterHiRes() - scanStart;
    if (scan.cancelled
        || scan.entryLimitReached
        || scan.entries.size() != kEntryCount
        || deliveredEntries != kEntryCount
        || firstDeliveryMs < 0.0
        || largestBatch == 0
        || largestBatch > kMaximumBatchSize)
    {
        return false;
    }

    const auto targetName = "Sample_"
        + juce::String(static_cast<juce::int64>(kEntryCount - 1)).paddedLeft('0', 5)
        + ".wav";
    const auto searchStart = juce::Time::getMillisecondCounterHiRes();
    const auto matches = LibraryFileIndex::search(scan.entries, targetName);
    const auto searchDurationMs = juce::Time::getMillisecondCounterHiRes() - searchStart;
    if (matches.size() != 1
        || scan.entries[matches.front()].fileName != targetName)
    {
        return false;
    }

    // A root replacement cancels the old traversal through the same callback used
    // by the editor worker. No partial result is suitable for persistence after it.
    auto cancelRequested = false;
    auto cancellationDeliveries = std::size_t{ 0 };
    const auto cancelledScan = LibraryFileIndex::scanRootIncrementally(
        libraryRoot,
        audiocity::plugin::ImportFormatCapabilities{ false, true },
        true,
        [&cancelRequested] { return cancelRequested; },
        [&](const std::span<const audiocity::plugin::LibraryFileIndexEntry>)
        {
            ++cancellationDeliveries;
            cancelRequested = true;
        });
    if (!cancelledScan.cancelled
        || cancelledScan.incompleteReason
            != audiocity::plugin::LibraryFileIndexScanResult::IncompleteReason::cancelled
        || cancelledScan.entryLimitReached
        || cancellationDeliveries != 1
        || cancelledScan.entries.empty()
        || cancelledScan.entries.size() >= kEntryCount)
    {
        return false;
    }

    const auto emptyTreeRoot = tempRoot.getChildFile("DirectoryOnly");
    if (!emptyTreeRoot.createDirectory())
        return false;
    for (auto index = 0; index < 64; ++index)
        if (!emptyTreeRoot.getChildFile("Empty_" + juce::String(index)).createDirectory())
            return false;

    auto directoryCancellationChecks = 0;
    const auto directoryOnlyCancelled = LibraryFileIndex::scanRootIncrementally(
        emptyTreeRoot,
        audiocity::plugin::ImportFormatCapabilities{ false, true },
        true,
        [&directoryCancellationChecks]
        {
            return ++directoryCancellationChecks >= 4;
        });
    if (!directoryOnlyCancelled.cancelled
        || directoryOnlyCancelled.incompleteReason
            != audiocity::plugin::LibraryFileIndexScanResult::IncompleteReason::cancelled
        || directoryCancellationChecks < 4
        || !directoryOnlyCancelled.entries.empty())
    {
        return false;
    }

    audiocity::plugin::LibraryFileIndexScanLimits tinyLimits;
    tinyLimits.maximumEntries = 2;
    tinyLimits.maximumDirectories = 1024;
    tinyLimits.maximumDepth = 8;
    auto limitedScan = LibraryFileIndex::scanRootIncrementally(
        libraryRoot,
        audiocity::plugin::ImportFormatCapabilities{ false, true },
        true,
        {},
        {},
        tinyLimits);
    if (limitedScan.complete()
        || !limitedScan.entryLimitReached
        || limitedScan.incompleteReason
            != audiocity::plugin::LibraryFileIndexScanResult::IncompleteReason::entryLimitReached
        || limitedScan.entries.size() != tinyLimits.maximumEntries)
    {
        return false;
    }

    const auto preservationCache = tempRoot.getChildFile("limited-preservation.acli");
    audiocity::plugin::LibraryFileIndexStore preservationStore(preservationCache);
    audiocity::plugin::LibraryFileIndexData preservedIndex;
    preservedIndex.libraryRootPath = libraryRoot.getFullPathName();
    audiocity::plugin::LibraryFileIndexEntry preservedEntry;
    preservedEntry.relativePath = "Preserved.wav";
    preservedEntry.sizeBytes = 17;
    preservedEntry.modificationTimeMs = 23;
    preservedIndex.entries.push_back(preservedEntry);
    if (!preservationStore.save(preservedIndex)
        || preservationStore.saveCompletedScan(libraryRoot, std::move(limitedScan)))
    {
        return false;
    }
    const auto preservedAfterLimit = preservationStore.load();
    if (!preservedAfterLimit.has_value()
        || preservedAfterLimit->entries.size() != 1
        || preservedAfterLimit->entries.front().relativePath != preservedEntry.relativePath)
    {
        return false;
    }

    const auto replacementRoot = tempRoot.getChildFile("ReplaceDuringScan");
    if (!replacementRoot.createDirectory())
        return false;
    for (auto index = 0; index < 300; ++index)
    {
        const auto file = replacementRoot.getChildFile(
            "Replace_" + juce::String(index).paddedLeft('0', 3) + ".wav");
        std::ofstream output(file.getFullPathName().toStdString(), std::ios::binary | std::ios::trunc);
        output.put(static_cast<char>(index & 0xff));
        if (!output.good())
            return false;
    }

    auto completedBeforeRootReplacement = LibraryFileIndex::scanRootIncrementally(
        replacementRoot,
        audiocity::plugin::ImportFormatCapabilities{ false, true },
        true);
    if (!completedBeforeRootReplacement.complete()
        || completedBeforeRootReplacement.entries.size() != 300)
    {
        return false;
    }

    const auto movedReplacementRoot = tempRoot.getChildFile("OriginalRootMoved");
    auto replacementAttempted = false;
    auto replacementSucceeded = false;
    const auto rootReplacedScan = LibraryFileIndex::scanRootIncrementally(
        replacementRoot,
        audiocity::plugin::ImportFormatCapabilities{ false, true },
        true,
        {},
        [&](const std::span<const audiocity::plugin::LibraryFileIndexEntry>)
        {
            if (replacementAttempted)
                return;
            replacementAttempted = true;
            replacementSucceeded = replacementRoot.moveFileTo(movedReplacementRoot)
                && replacementRoot.createDirectory();
        });
    if (!replacementAttempted
        || !replacementSucceeded
        || rootReplacedScan.complete()
        || rootReplacedScan.incompleteReason
            != audiocity::plugin::LibraryFileIndexScanResult::IncompleteReason::rootChanged)
    {
        return false;
    }
    if (preservationStore.saveCompletedScan(
            replacementRoot, std::move(completedBeforeRootReplacement)))
    {
        return false;
    }
    const auto preservedAfterRootReplacement = preservationStore.load();
    if (!preservedAfterRootReplacement.has_value()
        || preservedAfterRootReplacement->entries.size() != 1
        || preservedAfterRootReplacement->entries.front().relativePath
            != preservedEntry.relativePath)
    {
        return false;
    }

    std::printf("Library index filesystem fixture: setup %.2f ms (not budgeted), "
                "first batch %.2f ms, full scan %.2f ms, search %.2f ms, entries %zu\n",
        setupDurationMs,
        firstDeliveryMs,
        fullScanDurationMs,
        searchDurationMs,
        kEntryCount);

   #if defined(NDEBUG)
    constexpr auto kMaximumFirstDeliveryMs = 500.0;
    constexpr auto kMaximumSearchMs = 50.0;
    return firstDeliveryMs < kMaximumFirstDeliveryMs
        && searchDurationMs < kMaximumSearchMs;
   #else
    return true;
   #endif
}

bool runLibraryFileIndexExactCaseIdentityTest()
{
    using audiocity::plugin::LibraryFileIndexData;
    using audiocity::plugin::LibraryFileIndexEntry;
    using audiocity::plugin::LibraryFileIndexStore;

    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_library_index_case_identity", "");
    const auto libraryRoot = tempRoot.getChildFile("Library");
    if (!libraryRoot.createDirectory())
        return false;

    const auto upperPartition = LibraryFileIndexStore::getCacheFileForRoot(
        tempRoot.getChildFile("CaseRoot")).getFileName();
    const auto lowerPartition = LibraryFileIndexStore::getCacheFileForRoot(
        tempRoot.getChildFile("caseroot")).getFileName();

    LibraryFileIndexData source;
    source.libraryRootPath = libraryRoot.getFullPathName();
    LibraryFileIndexEntry entry;
    entry.relativePath = "Bank/Sample.wav";
    entry.sizeBytes = 64;
    entry.modificationTimeMs = 1000;
    source.entries.push_back(entry);
    entry.relativePath = "bank/Sample.wav";
    source.entries.push_back(entry);

    const LibraryFileIndexStore store(tempRoot.getChildFile("case-index.acli"));
    const auto saved = store.save(source);
    const auto restored = store.load();
    const auto ok = upperPartition != lowerPartition
        && saved
        && restored.has_value()
        && restored->entries.size() == source.entries.size()
        && restored->entries[0].relativePath == "Bank/Sample.wav"
        && restored->entries[1].relativePath == "bank/Sample.wav";
    tempRoot.deleteRecursively();
    return ok;
}

bool runPeakPreviewCacheRoundTripTest()
{
    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("audiocity_peak_cache_tests");
    tempRoot.createDirectory();

    const auto cacheFile = tempRoot.getChildFile("peak_preview_cache.xml");
    audiocity::plugin::PeakPreviewCacheStore store(cacheFile);
    store.reset();

    audiocity::plugin::PeakPreviewCacheData saveData;
    saveData.libraryRootPath = "C:/Library/Samples";
    saveData.entries["c:/library/samples/kick.wav"] = {
        2048,
        123456,
        234567,
        { 0.1f, 0.5f, 0.9f },
        "SR: 48000 Hz  Ch: 1",
        "Acidized",
        "Loop: 100-800"
    };

    if (!store.save(saveData))
        return false;

    const auto loaded = store.load();
    if (!loaded.libraryRootPath.equalsIgnoreCase(saveData.libraryRootPath))
        return false;

    const auto it = loaded.entries.find("c:/library/samples/kick.wav");
    if (it == loaded.entries.end())
        return false;

    const auto& entry = it->second;
    if (entry.fileSizeBytes != 2048)
        return false;
    if (entry.fileModificationTimeMs != 123456 || entry.lastAccessTimeMs != 234567)
        return false;
    if (entry.metadataLine != "SR: 48000 Hz  Ch: 1")
        return false;
    if (entry.loopFormatBadge != "Acidized")
        return false;
    if (entry.loopMetadataLine != "Loop: 100-800")
        return false;
    if (entry.peaks.size() != 3)
        return false;

    return std::abs(entry.peaks[0] - 0.1f) < 1.0e-5f
        && std::abs(entry.peaks[1] - 0.5f) < 1.0e-5f
        && std::abs(entry.peaks[2] - 0.9f) < 1.0e-5f;
}

bool runPeakPreviewCacheResetClearsFileTest()
{
    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("audiocity_peak_cache_tests");
    tempRoot.createDirectory();

    const auto cacheFile = tempRoot.getChildFile("peak_preview_cache_reset.xml");
    audiocity::plugin::PeakPreviewCacheStore store(cacheFile);
    store.reset();

    audiocity::plugin::PeakPreviewCacheData saveData;
    saveData.libraryRootPath = "C:/Library/A";
    saveData.entries["c:/library/a/snare.wav"] = {
        100,
        123,
        456,
        { 0.2f },
        "meta",
        {},
        {}
    };

    if (!store.save(saveData))
        return false;
    if (!cacheFile.existsAsFile())
        return false;

    if (!store.reset())
        return false;

    const auto loaded = store.load();
    return loaded.libraryRootPath.isEmpty() && loaded.entries.empty();
}

bool runPeakPreviewCacheFreshnessPartitionAndCorruptionTest()
{
    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_peak_cache_validity", "");
    const auto firstRoot = tempRoot.getChildFile("LibraryA");
    const auto secondRoot = tempRoot.getChildFile("LibraryB");
    if (!firstRoot.createDirectory() || !secondRoot.createDirectory())
        return false;

    const auto sampleFile = firstRoot.getChildFile("same-size.wav");
    if (!sampleFile.replaceWithText("AAAA"))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    const auto oldTimestamp = juce::Time::getCurrentTime().toMilliseconds() - 10000;
    if (!sampleFile.setLastModificationTime(juce::Time(oldTimestamp)))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    audiocity::plugin::PeakPreviewCacheEntry entry;
    entry.fileSizeBytes = sampleFile.getSize();
    entry.fileModificationTimeMs = sampleFile.getLastModificationTime().toMilliseconds();
    if (!audiocity::plugin::isPeakPreviewCacheEntryCurrent(entry, sampleFile))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    if (!sampleFile.replaceWithText("BBBB")
        || !sampleFile.setLastModificationTime(juce::Time(oldTimestamp + 4000))
        || sampleFile.getSize() != entry.fileSizeBytes
        || sampleFile.getLastModificationTime().toMilliseconds() == entry.fileModificationTimeMs
        || audiocity::plugin::isPeakPreviewCacheEntryCurrent(entry, sampleFile))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    const auto firstPartition = audiocity::plugin::PeakPreviewCacheStore::getCacheFileForRoot(firstRoot);
    const auto secondPartition = audiocity::plugin::PeakPreviewCacheStore::getCacheFileForRoot(secondRoot);
    if (firstPartition == secondPartition)
    {
        tempRoot.deleteRecursively();
        return false;
    }

    const auto corruptCache = tempRoot.getChildFile("corrupt.xml");
    if (!corruptCache.replaceWithText("<peakPreviewCache version=\"2\"><entry"))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    const auto corruptData = audiocity::plugin::PeakPreviewCacheStore(corruptCache).load();
    const auto invalidPeakCache = tempRoot.getChildFile("invalid-peaks.xml");
    if (!invalidPeakCache.replaceWithText(
            "<peakPreviewCache version=\"2\" libraryRoot=\"LibraryA\">"
            "<entry path=\"bad.wav\" size=\"4\" mtime=\"1\" accessed=\"2\">"
            "<peaks>1.2.3</peaks></entry>"
            "<entry path=\"missing.wav\" size=\"4\" mtime=\"1\" accessed=\"2\"/>"
            "</peakPreviewCache>"))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    const auto invalidPeakData = audiocity::plugin::PeakPreviewCacheStore(invalidPeakCache).load();
    const auto ok = corruptData.libraryRootPath.isEmpty() && corruptData.entries.empty()
        && invalidPeakData.entries.empty();
    tempRoot.deleteRecursively();
    return ok;
}

bool runPeakPreviewCacheLruAndPeakBoundsTest()
{
    using audiocity::plugin::PeakPreviewCacheStore;

    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_peak_cache_bounds", "");
    if (!tempRoot.createDirectory())
        return false;

    const auto cacheFile = tempRoot.getChildFile("bounded.xml");
    PeakPreviewCacheStore store(cacheFile);
    audiocity::plugin::PeakPreviewCacheData saveData;
    saveData.libraryRootPath = tempRoot.getFullPathName();

    for (std::size_t index = 0; index <= PeakPreviewCacheStore::maxEntries; ++index)
    {
        audiocity::plugin::PeakPreviewCacheEntry entry;
        entry.fileSizeBytes = static_cast<juce::int64>(index + 1);
        entry.fileModificationTimeMs = 1000;
        entry.lastAccessTimeMs = static_cast<juce::int64>(index + 1);
        entry.peaks.assign(PeakPreviewCacheStore::maxPeaksPerEntry + 17, 0.5f);
        saveData.entries.emplace("entry-" + std::to_string(index), std::move(entry));
    }

    if (!store.save(saveData))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    const auto loaded = store.load();
    const auto newestKey = "entry-" + std::to_string(PeakPreviewCacheStore::maxEntries);
    const auto newest = loaded.entries.find(newestKey);
    const auto ok = !loaded.entries.empty()
        && loaded.entries.size() < PeakPreviewCacheStore::maxEntries
        && loaded.entries.find("entry-0") == loaded.entries.end()
        && newest != loaded.entries.end()
        && newest->second.peaks.size() == PeakPreviewCacheStore::maxPeaksPerEntry
        && cacheFile.getSize() > 0
        && cacheFile.getSize() < 16 * 1024 * 1024;
    tempRoot.deleteRecursively();
    return ok;
}

// --- Factory / embedded-sample preset tests --------------------------------

namespace factory
{
constexpr auto kPatchRoot = "AudiocityPatch";
constexpr auto kEmbeddedSampleData = "embeddedSampleData";
constexpr auto kEmbeddedSampleRate = "embeddedSampleRate";
constexpr auto kEmbeddedSampleRootMidiNote = "embeddedSampleRootMidiNote";
constexpr auto kEmbeddedSampleName = "embeddedSampleName";
constexpr auto kEmbeddedSampleChannels = "embeddedSampleChannels";
constexpr auto kPlaybackMode = "playbackMode";
constexpr auto kLoopStart = "loopStart";
constexpr auto kLoopEnd = "loopEnd";
constexpr auto kLoopCrossfadeSamples = "loopCrossfadeSamples";
constexpr auto kFilterMode = "filterMode";
constexpr auto kFilterLfoRate = "filterLfoRate";
constexpr auto kFilterLfoAmount = "filterLfoAmount";
constexpr auto kFilterLfoTempoSync = "filterLfoTempoSync";
constexpr auto kAmpLfoRate = "ampLfoRate";
constexpr auto kAmpLfoDepth = "ampLfoDepth";
constexpr auto kPitchLfoRate = "pitchLfoRate";
constexpr auto kPitchLfoDepth = "pitchLfoDepth";
constexpr auto kDelayTempoSync = "delayTempoSync";
constexpr auto kAutopanDepth = "autopanDepth";
constexpr auto kSaturationDrive = "saturationDrive";
constexpr auto kSaturationMode = "saturationMode";
constexpr auto kQualityTier = "qualityTier";
constexpr auto kMacro1ToFilter = "macro1ToFilter";
constexpr auto kMacro2ToPitch = "macro2ToPitch";
constexpr auto kMacro2ToFilter = "macro2ToFilter";
constexpr auto kMacro2ToAmp = "macro2ToAmp";
constexpr auto kAftertouchToPitch = "aftertouchToPitch";
constexpr auto kAftertouchToFilter = "aftertouchToFilter";
constexpr auto kAftertouchToAmp = "aftertouchToAmp";
constexpr auto kVelocityToFilter = "velocityToFilter";
constexpr auto kVelocityToAmp = "velocityToAmp";
}

bool runEmbeddedSamplePresetRoundTripTest()
{
    constexpr int sampleCount = 256;
    std::vector<float> leftWaveform(sampleCount);
    std::vector<float> rightWaveform(sampleCount);
    for (int i = 0; i < sampleCount; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleCount);
        leftWaveform[static_cast<std::size_t>(i)] = std::sin(juce::MathConstants<float>::twoPi * t);
        rightWaveform[static_cast<std::size_t>(i)] = 0.5f * std::cos(juce::MathConstants<float>::twoPi * t * 2.0f);
    }

    juce::ValueTree state(factory::kPatchRoot);
    juce::MemoryBlock bytes(static_cast<std::size_t>(sampleCount) * 2 * sizeof(float));
    auto* floatBytes = static_cast<float*>(bytes.getData());
    std::memcpy(floatBytes, leftWaveform.data(), static_cast<std::size_t>(sampleCount) * sizeof(float));
    std::memcpy(floatBytes + sampleCount, rightWaveform.data(), static_cast<std::size_t>(sampleCount) * sizeof(float));
    state.setProperty(factory::kEmbeddedSampleData, juce::var(bytes), nullptr);
    state.setProperty(factory::kEmbeddedSampleRate, 44100.0, nullptr);
    state.setProperty(factory::kEmbeddedSampleRootMidiNote, 60, nullptr);
    state.setProperty(factory::kEmbeddedSampleChannels, 2, nullptr);
    state.setProperty(factory::kEmbeddedSampleName, juce::String("Round Trip"), nullptr);

    const auto xml = audiocity::plugin::encodePresetXml(state);
    if (xml.isEmpty())
        return false;

    juce::ValueTree decoded;
    juce::String error;
    if (!audiocity::plugin::decodePresetXml(xml, decoded, error))
        return false;

    if (!decoded.hasType(factory::kPatchRoot))
        return false;

    const auto* decodedBytes = decoded.getProperty(factory::kEmbeddedSampleData).getBinaryData();
    if (decodedBytes == nullptr || decodedBytes->getSize() != bytes.getSize())
        return false;

    if (std::memcmp(decodedBytes->getData(), bytes.getData(), bytes.getSize()) != 0)
        return false;

    if (static_cast<int>(decoded.getProperty(factory::kEmbeddedSampleChannels, -1)) != 2)
        return false;

    if (static_cast<int>(decoded.getProperty(factory::kEmbeddedSampleRootMidiNote, -1)) != 60)
        return false;

    return decoded.getProperty(factory::kEmbeddedSampleName).toString() == "Round Trip";
}

bool runFactoryPresetBankDiscoveryTest()
{
    const auto factoryDir = juce::File(AUDIOCITY_SOURCE_DIR)
        .getChildFile("assets")
        .getChildFile("factory_presets");

    if (!factoryDir.isDirectory())
    {
        std::fprintf(stderr, "Factory preset directory missing: %s\n",
            factoryDir.getFullPathName().toRawUTF8());
        return false;
    }

    juce::Array<juce::File> presetFiles;
    factoryDir.findChildFiles(presetFiles, juce::File::TypesOfFileToFind::findFiles, false, "*.acp");

    if (presetFiles.size() < 64)
    {
        std::fprintf(stderr, "Factory bank has %d presets; expected >= 64\n", presetFiles.size());
        return false;
    }

    int withEmbedded = 0;
    int sustainedFamilyPresets = 0;
    int longLoopingPresets = 0;
    int longEvolvingSources = 0;
    int filterLfoPresets = 0;
    int tempoSyncedFilterLfoPresets = 0;
    int ampLfoPresets = 0;
    int pitchLfoPresets = 0;
    int tempoSyncedDelayPresets = 0;
    int autopanPresets = 0;
    int saturatedPresets = 0;
    int nonDefaultSaturationPresets = 0;
    int ultraQualityPresets = 0;
    int expressiveMacroPresets = 0;
    int aftertouchPresets = 0;
    int velocitySensitivePresets = 0;
    std::set<int> filterModes;
    juce::int64 totalBytes = 0;
    for (const auto& file : presetFiles)
    {
        totalBytes += file.getSize();
        const auto xml = file.loadFileAsString();
        if (xml.isEmpty())
            continue;
        juce::ValueTree state;
        juce::String err;
        if (!audiocity::plugin::decodePresetXml(xml, state, err))
            continue;
        if (!state.hasType(factory::kPatchRoot))
            continue;
        const auto* embeddedData = state.getProperty(factory::kEmbeddedSampleData).getBinaryData();
        if (embeddedData != nullptr && embeddedData->getSize() >= sizeof(float))
        {
            ++withEmbedded;

            const auto embeddedChannels = juce::jmax(1,
                static_cast<int>(state.getProperty(factory::kEmbeddedSampleChannels, 1)));
            const auto embeddedSampleFrames = static_cast<int>(embeddedData->getSize() / sizeof(float)) / embeddedChannels;
            const auto playbackMode = static_cast<int>(state.getProperty(factory::kPlaybackMode, 0));

            filterModes.insert(static_cast<int>(state.getProperty(factory::kFilterMode, 0)));
            const auto filterLfoRate = static_cast<float>(state.getProperty(factory::kFilterLfoRate, 0.0f));
            const auto filterLfoAmount = static_cast<float>(state.getProperty(factory::kFilterLfoAmount, 0.0f));
            if (filterLfoRate > 0.0f && std::abs(filterLfoAmount) > 1.0f)
                ++filterLfoPresets;
            if (filterLfoRate > 0.0f && static_cast<int>(state.getProperty(factory::kFilterLfoTempoSync, 0)) == 1)
                ++tempoSyncedFilterLfoPresets;

            const auto ampLfoRate = static_cast<float>(state.getProperty(factory::kAmpLfoRate, 0.0f));
            const auto ampLfoDepth = static_cast<float>(state.getProperty(factory::kAmpLfoDepth, 0.0f));
            if (ampLfoRate > 0.0f && ampLfoDepth > 0.0f)
                ++ampLfoPresets;

            const auto pitchLfoRate = static_cast<float>(state.getProperty(factory::kPitchLfoRate, 0.0f));
            const auto pitchLfoDepth = static_cast<float>(state.getProperty(factory::kPitchLfoDepth, 0.0f));
            if (pitchLfoRate > 0.0f && pitchLfoDepth > 0.0f)
                ++pitchLfoPresets;

            if (static_cast<int>(state.getProperty(factory::kDelayTempoSync, 0)) == 1)
                ++tempoSyncedDelayPresets;
            if (static_cast<float>(state.getProperty(factory::kAutopanDepth, 0.0f)) > 0.0f)
                ++autopanPresets;
            if (static_cast<float>(state.getProperty(factory::kSaturationDrive, 0.0f)) > 0.15f)
                ++saturatedPresets;
            if (static_cast<int>(state.getProperty(factory::kSaturationMode, 0)) != 0)
                ++nonDefaultSaturationPresets;
            if (static_cast<int>(state.getProperty(factory::kQualityTier, 1)) == 2)
                ++ultraQualityPresets;

            const auto macro2Motion = std::abs(static_cast<float>(state.getProperty(factory::kMacro2ToPitch, 0.0f)))
                + std::abs(static_cast<float>(state.getProperty(factory::kMacro2ToFilter, 0.0f)))
                + std::abs(static_cast<float>(state.getProperty(factory::kMacro2ToAmp, 0.0f)));
            if (std::abs(static_cast<float>(state.getProperty(factory::kMacro1ToFilter, 0.0f))) > 1.0f
                && macro2Motion > 1.0f)
                ++expressiveMacroPresets;

            const auto aftertouchMotion = std::abs(static_cast<float>(state.getProperty(factory::kAftertouchToPitch, 0.0f)))
                + std::abs(static_cast<float>(state.getProperty(factory::kAftertouchToFilter, 0.0f)))
                + std::abs(static_cast<float>(state.getProperty(factory::kAftertouchToAmp, 0.0f)));
            if (aftertouchMotion > 0.0f)
                ++aftertouchPresets;

            if (std::abs(static_cast<float>(state.getProperty(factory::kVelocityToFilter, 0.0f))) > 1.0f
                && std::abs(static_cast<float>(state.getProperty(factory::kVelocityToAmp, 0.0f))) > 0.0f)
                ++velocitySensitivePresets;

            if (embeddedSampleFrames >= 2 * 44100)
                ++longEvolvingSources;

            const auto fileName = file.getFileName();
            const auto expectsSustainLoop = fileName.contains(" - Bass - ")
                || fileName.contains(" - Lead - ")
                || fileName.contains(" - Pad - ")
                || fileName.contains(" - Ensemble - ")
                || fileName.contains(" - FX - ");

            if (expectsSustainLoop)
            {
                ++sustainedFamilyPresets;

                if (playbackMode != 2)
                {
                    std::fprintf(stderr,
                        "Factory preset is not authored in loop mode: %s (playbackMode=%d)\n",
                        fileName.toRawUTF8(),
                        playbackMode);
                    return false;
                }
            }

            if (playbackMode == 2 && embeddedSampleFrames > 4096)
            {
                ++longLoopingPresets;

                const auto loopStart = static_cast<int>(state.getProperty(factory::kLoopStart, -1));
                const auto loopEnd = static_cast<int>(state.getProperty(factory::kLoopEnd, -1));
                const auto loopCrossfadeSamples = static_cast<int>(state.getProperty(factory::kLoopCrossfadeSamples, -1));
                const auto loopLength = loopEnd - loopStart + 1;

                if (loopStart <= 0
                    || loopEnd >= embeddedSampleFrames - 1
                    || loopEnd <= loopStart
                    || loopCrossfadeSamples <= 0
                    || loopCrossfadeSamples > loopLength / 2)
                {
                    std::fprintf(stderr,
                        "Factory preset has invalid sustain loop window: %s (frames=%d, loop=%d-%d, xfade=%d)\n",
                        file.getFileName().toRawUTF8(),
                        embeddedSampleFrames,
                        loopStart,
                        loopEnd,
                        loopCrossfadeSamples);
                    return false;
                }
            }
        }
    }

    if (withEmbedded < 64)
    {
        std::fprintf(stderr, "Factory bank: only %d/%d presets carry embedded audio\n",
            withEmbedded, presetFiles.size());
        return false;
    }

    constexpr juce::int64 kBudgetBytes = 64 * 1024 * 1024; // 64 MB
    if (totalBytes > kBudgetBytes)
    {
        std::fprintf(stderr, "Factory bank exceeds size budget: %lld bytes (limit %lld)\n",
            static_cast<long long>(totalBytes), static_cast<long long>(kBudgetBytes));
        return false;
    }

    if (longLoopingPresets == 0)
    {
        std::fprintf(stderr, "Factory bank does not contain any long loop-mode embedded presets\n");
        return false;
    }

    if (sustainedFamilyPresets == 0)
    {
        std::fprintf(stderr, "Factory bank does not contain any sustained-family presets\n");
        return false;
    }

    if (filterModes.size() < 2
        || filterLfoPresets < 10
        || autopanPresets < 10
        || saturatedPresets < 6
        || expressiveMacroPresets < 32
        || velocitySensitivePresets < 32
        || longEvolvingSources < 10)
    {
        std::fprintf(stderr,
            "Factory bank underuses engine design surface: modes=%zu filterLfo=%d ampLfo=%d pitchLfo=%d syncedFilterLfo=%d syncedDelay=%d autopan=%d saturated=%d satModes=%d ultra=%d macros=%d aftertouch=%d velocity=%d longSources=%d\n",
            filterModes.size(),
            filterLfoPresets,
            ampLfoPresets,
            pitchLfoPresets,
            tempoSyncedFilterLfoPresets,
            tempoSyncedDelayPresets,
            autopanPresets,
            saturatedPresets,
            nonDefaultSaturationPresets,
            ultraQualityPresets,
            expressiveMacroPresets,
            aftertouchPresets,
            velocitySensitivePresets,
            longEvolvingSources);
        return false;
    }

    return true;
}

bool runSfzExporterRoundTripTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_export_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    auto cleanupAndFail = [&]()
    {
        tempDirectory.deleteRecursively();
        return false;
    };

    // Build a small in-memory program: one sample asset, two zones (different
    // key ranges, second zone with a sustaining loop).
    Program program;
    program.name = "Round Trip Library";

    SampleAsset asset;
    asset.sourcePath = ""; // exporter will write from buffer
    asset.displayName = "RoundTripTone";
    asset.lengthSamples = sampleLength;
    asset.numChannels = 1;
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Zone zoneLow{};
    zoneLow.sampleAssetIndex = 0;
    zoneLow.keyRange = { 48, 59 };
    zoneLow.velocityRange = { 1, 100 };
    zoneLow.rootMidiNote = 55;
    zoneLow.gainDb = -3.0f;
    zoneLow.pan = -0.5f;
    zoneLow.tuneCents = 12.0f;
    zoneLow.sampleEndExclusive = sampleLength;
    zoneLow.loopMode = ZoneLoopMode::noLoop;
    program.zones.push_back(zoneLow);

    Zone zoneHigh{};
    zoneHigh.sampleAssetIndex = 0;
    zoneHigh.keyRange = { 60, 72 };
    zoneHigh.velocityRange = { 64, 127 };
    zoneHigh.rootMidiNote = 64;
    zoneHigh.gainDb = 2.0f;
    zoneHigh.pan = 0.25f;
    zoneHigh.tuneCents = -7.0f;
    zoneHigh.sampleEndExclusive = sampleLength;
    zoneHigh.loopMode = ZoneLoopMode::continuous;
    zoneHigh.loopStart = 64;
    zoneHigh.loopEndExclusive = 256;
    zoneHigh.chokeGroup = 3;
    program.zones.push_back(zoneHigh);

    std::vector<juce::AudioBuffer<float>> sampleData;
    sampleData.push_back(createTestSample(sampleLength));

    const auto destSfz = tempDirectory.getChildFile("RoundTrip.sfz");

    sfz_export::ExportOptions options;
    options.copySamples = true;
    options.libraryDisplayName = "Round Trip Library";
    const auto exportResult = sfz_export::exportProgramToSfz(destSfz, program, sampleData, options);

    if (exportResult.hasErrors() || exportResult.writtenRegionCount != 2)
        return cleanupAndFail();
    if (!destSfz.existsAsFile())
        return cleanupAndFail();
    if (exportResult.copiedSampleCount != 1
        || !tempDirectory.getChildFile("Samples").isDirectory())
    {
        return cleanupAndFail();
    }

    // Re-import the exported file and verify the model survived the round trip.
    SfzImporter importer;
    const auto importResult = importer.importFile(destSfz);
    if (importResult.hasErrors() || importResult.program.zones.size() != 2u)
        return cleanupAndFail();

    const auto& importedZones = importResult.program.zones;
    const auto findZone = [&](const int rootNote) -> const Zone*
    {
        for (const auto& z : importedZones)
            if (z.rootMidiNote == rootNote)
                return &z;
        return nullptr;
    };

    const auto* lowImported = findZone(55);
    const auto* highImported = findZone(64);
    if (lowImported == nullptr || highImported == nullptr)
        return cleanupAndFail();

    if (lowImported->keyRange.low != 48 || lowImported->keyRange.high != 59)
        return cleanupAndFail();
    if (lowImported->velocityRange.low != 1 || lowImported->velocityRange.high != 100)
        return cleanupAndFail();
    if (std::abs(lowImported->gainDb - (-3.0f)) > 0.05f)
        return cleanupAndFail();
    if (std::abs(lowImported->pan - (-0.5f)) > 0.05f)
        return cleanupAndFail();
    if (std::abs(lowImported->tuneCents - 12.0f) > 1.0f)
        return cleanupAndFail();
    if (lowImported->loopMode != ZoneLoopMode::noLoop)
        return cleanupAndFail();

    if (highImported->keyRange.low != 60 || highImported->keyRange.high != 72)
        return cleanupAndFail();
    if (highImported->velocityRange.low != 64 || highImported->velocityRange.high != 127)
        return cleanupAndFail();
    if (highImported->loopMode != ZoneLoopMode::continuous)
        return cleanupAndFail();
    if (highImported->loopStart != 64 || highImported->loopEndExclusive != 256)
        return cleanupAndFail();
    if (highImported->chokeGroup != 3)
        return cleanupAndFail();
    if (std::abs(highImported->gainDb - 2.0f) > 0.05f)
        return cleanupAndFail();
    if (std::abs(highImported->pan - 0.25f) > 0.05f)
        return cleanupAndFail();

    tempDirectory.deleteRecursively();
    return true;
}

bool runDecentSamplerExporterRoundTripTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_dspreset_export_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    auto cleanupAndFail = [&]()
    {
        tempDirectory.deleteRecursively();
        return false;
    };

    Program program;
    program.name = "Decent Round Trip";

    SampleAsset asset;
    asset.displayName = "RoundTripTone";
    asset.lengthSamples = sampleLength;
    asset.numChannels = 1;
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Group releaseGroup;
    releaseGroup.name = "Release Group";
    releaseGroup.gainDb = -2.0f;
    releaseGroup.pan = 0.25f;
    releaseGroup.roundRobinGroup = 7;
    releaseGroup.roundRobinMode = RoundRobinMode::ordered;
    releaseGroup.triggerMode = ZoneTriggerMode::release;
    releaseGroup.chokeGroup = 9;
    program.groups.push_back(releaseGroup);

    Zone groupedZoneA{};
    groupedZoneA.sampleAssetIndex = 0;
    groupedZoneA.groupIndex = 0;
    groupedZoneA.keyRange = { 48, 67 };
    groupedZoneA.velocityRange = { 1, 100 };
    groupedZoneA.rootMidiNote = 60;
    groupedZoneA.sampleStart = 16;
    groupedZoneA.sampleEndExclusive = 400;
    groupedZoneA.loopMode = ZoneLoopMode::continuous;
    groupedZoneA.loopStart = 64;
    groupedZoneA.loopEndExclusive = 300;
    groupedZoneA.gainDb = 1.5f;
    groupedZoneA.pan = -0.5f;
    groupedZoneA.tuneCents = 12.0f;
    groupedZoneA.roundRobinGroup = 7;
    groupedZoneA.roundRobinPosition = 1;
    groupedZoneA.roundRobinLength = 2;
    program.zones.push_back(groupedZoneA);

    Zone groupedZoneB = groupedZoneA;
    groupedZoneB.sampleStart = 24;
    groupedZoneB.sampleEndExclusive = 420;
    groupedZoneB.loopStart = 80;
    groupedZoneB.loopEndExclusive = 320;
    groupedZoneB.gainDb = 0.5f;
    groupedZoneB.pan = 0.15f;
    groupedZoneB.tuneCents = -4.0f;
    groupedZoneB.roundRobinPosition = 2;
    program.zones.push_back(groupedZoneB);

    Zone directZone{};
    directZone.sampleAssetIndex = 0;
    directZone.keyRange = { 68, 84 };
    directZone.velocityRange = { 64, 127 };
    directZone.rootMidiNote = 72;
    directZone.sampleEndExclusive = sampleLength;
    directZone.gainDb = 3.0f;
    directZone.pan = 0.4f;
    directZone.tuneCents = -7.0f;
    directZone.chokeGroup = 11;
    program.zones.push_back(directZone);

    std::vector<juce::AudioBuffer<float>> sampleData;
    sampleData.push_back(createTestSample(sampleLength));

    const auto destPreset = tempDirectory.getChildFile("RoundTrip.dspreset");

    dspreset_export::ExportOptions options;
    options.copySamples = true;
    options.libraryDisplayName = program.name;
    const auto exportResult = dspreset_export::exportProgramToDecentSampler(destPreset,
                                                                             program,
                                                                             sampleData,
                                                                             options);
    if (exportResult.hasErrors() || exportResult.writtenSampleCount != 3)
        return cleanupAndFail();
    if (!destPreset.existsAsFile())
        return cleanupAndFail();
    if (exportResult.copiedSampleCount != 1
        || !tempDirectory.getChildFile("Samples").isDirectory())
    {
        return cleanupAndFail();
    }
    for (const auto& diagnostic : exportResult.diagnostics)
    {
        if (diagnostic.message.find("round-robin") != std::string::npos)
            return cleanupAndFail();
        if (diagnostic.message.find("choke") != std::string::npos)
            return cleanupAndFail();
    }

    const auto importResult = dspreset::importFile(destPreset);
    if (importResult.hasErrors() || importResult.program.zones.size() != 3u || importResult.program.groups.size() != 1u)
        return cleanupAndFail();

    const auto& importedZones = importResult.program.zones;
    const auto findZoneIndexBySampleStart = [&](const int sampleStart) -> int
    {
        for (int index = 0; index < static_cast<int>(importedZones.size()); ++index)
        {
            if (importedZones[static_cast<std::size_t>(index)].sampleStart == sampleStart)
                return index;
        }
        return -1;
    };

    const auto groupedImportedAIndex = findZoneIndexBySampleStart(16);
    const auto groupedImportedBIndex = findZoneIndexBySampleStart(24);
    const auto directImportedIndex = findZoneIndexBySampleStart(0);
    if (groupedImportedAIndex < 0 || groupedImportedBIndex < 0 || directImportedIndex < 0)
        return cleanupAndFail();

    const auto& groupedImportedA = importedZones[static_cast<std::size_t>(groupedImportedAIndex)];
    const auto& groupedImportedB = importedZones[static_cast<std::size_t>(groupedImportedBIndex)];
    const auto& directImported = importedZones[static_cast<std::size_t>(directImportedIndex)];
    const auto& importedGroup = importResult.program.groups[0];

    if (groupedImportedA.keyRange.low != 48 || groupedImportedA.keyRange.high != 67)
        return cleanupAndFail();
    if (groupedImportedA.velocityRange.low != 1 || groupedImportedA.velocityRange.high != 100)
        return cleanupAndFail();
    if (groupedImportedA.groupIndex != 0 || groupedImportedB.groupIndex != 0)
        return cleanupAndFail();
    if (groupedImportedA.sampleStart != 16 || groupedImportedA.sampleEndExclusive != 400)
        return cleanupAndFail();
    if (groupedImportedA.loopMode != ZoneLoopMode::continuous
        || groupedImportedA.loopStart != 64
        || groupedImportedA.loopEndExclusive != 300)
    {
        return cleanupAndFail();
    }
    if (groupedImportedA.triggerMode != ZoneTriggerMode::gate)
        return cleanupAndFail();
    if (std::abs(groupedImportedA.gainDb - 1.5f) > 0.1f)
        return cleanupAndFail();
    if (std::abs(groupedImportedA.pan - (-0.5f)) > 0.05f)
        return cleanupAndFail();
    if (std::abs(groupedImportedA.tuneCents - 12.0f) > 1.0f)
        return cleanupAndFail();
    if (groupedImportedA.roundRobinPosition != 1 || groupedImportedA.roundRobinLength != 2)
        return cleanupAndFail();
    if (groupedImportedB.sampleEndExclusive != 420
        || groupedImportedB.loopStart != 80
        || groupedImportedB.loopEndExclusive != 320)
    {
        return cleanupAndFail();
    }
    if (std::abs(groupedImportedB.gainDb - 0.5f) > 0.1f)
        return cleanupAndFail();
    if (std::abs(groupedImportedB.pan - 0.15f) > 0.05f)
        return cleanupAndFail();
    if (std::abs(groupedImportedB.tuneCents - (-4.0f)) > 1.0f)
        return cleanupAndFail();
    if (groupedImportedB.roundRobinPosition != 2 || groupedImportedB.roundRobinLength != 2)
        return cleanupAndFail();
    if (std::abs(importedGroup.gainDb - (-2.0f)) > 0.1f)
        return cleanupAndFail();
    if (std::abs(importedGroup.pan - 0.25f) > 0.05f)
        return cleanupAndFail();
    if (importedGroup.triggerMode != ZoneTriggerMode::release)
        return cleanupAndFail();
    if (importedGroup.roundRobinGroup <= 0 || importedGroup.roundRobinMode != RoundRobinMode::ordered)
        return cleanupAndFail();
    if (importedGroup.chokeGroup != 9)
        return cleanupAndFail();
    if (groupedImportedA.chokeGroup != 0 || groupedImportedB.chokeGroup != 0)
        return cleanupAndFail();

    if (directImported.keyRange.low != 68 || directImported.keyRange.high != 84)
        return cleanupAndFail();
    if (directImported.velocityRange.low != 64 || directImported.velocityRange.high != 127)
        return cleanupAndFail();
    if (directImported.groupIndex != -1)
        return cleanupAndFail();
    if (directImported.triggerMode != ZoneTriggerMode::gate)
        return cleanupAndFail();
    if (directImported.loopMode != ZoneLoopMode::noLoop)
        return cleanupAndFail();
    if (directImported.sampleEndExclusive != sampleLength)
        return cleanupAndFail();
    if (std::abs(directImported.gainDb - 3.0f) > 0.1f)
        return cleanupAndFail();
    if (std::abs(directImported.pan - 0.4f) > 0.05f)
        return cleanupAndFail();
    if (std::abs(directImported.tuneCents - (-7.0f)) > 1.0f)
        return cleanupAndFail();
    if (directImported.chokeGroup != 11)
        return cleanupAndFail();

    const auto snapshot = ProgramSnapshot::fromProgram(importResult.program);
    const auto& groupedSnapshotZoneA = snapshot.zones[static_cast<std::size_t>(groupedImportedAIndex)];
    const auto& groupedSnapshotZoneB = snapshot.zones[static_cast<std::size_t>(groupedImportedBIndex)];
    const auto& directSnapshotZone = snapshot.zones[static_cast<std::size_t>(directImportedIndex)];
    if (snapshot.getZoneTriggerMode(groupedSnapshotZoneA) != ZoneTriggerMode::release)
        return cleanupAndFail();
    if (std::abs(snapshot.getZoneGainDb(groupedSnapshotZoneA) - (-0.5f)) > 0.1f)
        return cleanupAndFail();
    if (std::abs(snapshot.getZonePan(groupedSnapshotZoneA) - (-0.25f)) > 0.05f)
        return cleanupAndFail();
    if (snapshot.getZoneRoundRobinGroup(groupedSnapshotZoneA) <= 0
        || snapshot.getZoneRoundRobinGroup(groupedSnapshotZoneA)
            != snapshot.getZoneRoundRobinGroup(groupedSnapshotZoneB)
        || snapshot.getZoneRoundRobinMode(groupedSnapshotZoneA) != RoundRobinMode::ordered)
        return cleanupAndFail();
    if (snapshot.getZoneChokeGroup(groupedSnapshotZoneA) != 9
        || snapshot.getZoneChokeGroup(groupedSnapshotZoneB) != 9
        || snapshot.getZoneChokeGroup(directSnapshotZone) != 11)
        return cleanupAndFail();

    tempDirectory.deleteRecursively();
    return true;
}

bool runSfzExporterCreateFromScratchTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 256;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_from_scratch_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    auto cleanupAndFail = [&]()
    {
        tempDirectory.deleteRecursively();
        return false;
    };

    // Simulate the create-from-scratch + add-sample flow that the processor seam
    // performs: start with an empty program, then push two sample assets and one
    // zone per asset using the same helper the processor uses.
    Program program;
    program.name = "From Scratch";

    std::vector<juce::AudioBuffer<float>> sampleData;

    for (int i = 0; i < 2; ++i)
    {
        SampleAsset asset;
        asset.displayName = "Voice_" + std::to_string(i + 1);
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60 + i * 12;
        program.sampleAssets.push_back(asset);
        sampleData.push_back(createTestSample(sampleLength));

        const auto newZoneIndex = audiocity::plugin::createProgramZoneForSampleAsset(
            program, static_cast<int>(program.sampleAssets.size()) - 1, -1);
        if (newZoneIndex < 0)
            return cleanupAndFail();
    }

    if (program.zones.size() != 2u)
        return cleanupAndFail();

    const auto destSfz = tempDirectory.getChildFile("FromScratch.sfz");

    sfz_export::ExportOptions options;
    options.copySamples = true;
    options.libraryDisplayName = program.name;

    const auto exportResult = sfz_export::exportProgramToSfz(destSfz, program, sampleData, options);
    if (exportResult.hasErrors() || exportResult.writtenRegionCount != 2 || exportResult.copiedSampleCount != 2)
        return cleanupAndFail();

    if (!destSfz.existsAsFile())
        return cleanupAndFail();

    // Re-import to confirm playability.
    SfzImporter importer;
    const auto importResult = importer.importFile(destSfz);
    if (importResult.hasErrors() || importResult.program.zones.size() != 2u)
        return cleanupAndFail();

    tempDirectory.deleteRecursively();
    return true;
}

// ---------------------------------------------------------------------------
// ImportedProgramStore
// ---------------------------------------------------------------------------

class RecordingProgramSink final : public audiocity::plugin::ProgramSink
{
public:
    void republishProgram(const audiocity::engine::Program& program,
                          const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset) override
    {
        ++publishCount;
        lastProgram = program;
        lastSampleAssetCount = static_cast<int>(sampleDataByAsset.size());
    }

    bool republishProgramChecked(
        const audiocity::engine::Program& program,
        const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset,
        juce::String& diagnostic) override
    {
        ++checkedPublishCount;
        if (!acceptFullPublish)
        {
            diagnostic = "Injected engine publication failure";
            return false;
        }

        diagnostic.clear();
        republishProgram(program, sampleDataByAsset);
        return true;
    }

    bool republishProgramMetadata(const audiocity::engine::Program& program) override
    {
        ++metadataPublishCount;
        if (!acceptMetadataPublish)
            return false;

        ++publishCount;
        lastProgram = program;
        return true;
    }

    bool acceptMetadataPublish = true;
    bool acceptFullPublish = true;
    int publishCount = 0;
    int checkedPublishCount = 0;
    int metadataPublishCount = 0;
    int lastSampleAssetCount = 0;
    audiocity::engine::Program lastProgram;
};

audiocity::engine::Program createStoreTestProgram(const int zoneCount)
{
    audiocity::engine::Program program;
    program.name = "StoreTest";

    audiocity::engine::SampleAsset asset;
    asset.displayName = "Asset";
    asset.lengthSamples = 512;
    asset.numChannels = 1;
    asset.sampleRateHz = 48000.0;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    for (int index = 0; index < zoneCount; ++index)
    {
        audiocity::engine::Zone zone;
        zone.sampleAssetIndex = 0;
        zone.rootMidiNote = 60 + index;
        zone.keyRange = audiocity::engine::MidiRange::single(60 + index);
        zone.velocityRange = audiocity::engine::VelocityRange::full();
        zone.sampleStart = 0;
        zone.sampleEndExclusive = asset.lengthSamples;
        program.zones.push_back(zone);
    }

    return program;
}

void loadStoreTestProgram(audiocity::plugin::ImportedProgramStore& store, const int zoneCount)
{
    const auto program = createStoreTestProgram(zoneCount);
    std::vector<juce::AudioBuffer<float>> sampleData;
    sampleData.push_back(createTestSample(512));

    store.loadProgram(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("StoreTest.sfz"),
                      audiocity::plugin::ImportedProgramFormat::sfz,
                      program,
                      sampleData,
                      "Loaded",
                      static_cast<int>(program.zones.size()),
                      -1);
}

bool runImportedProgramStoreEditPublishesCommittedProgramTest()
{
    RecordingProgramSink sink;
    audiocity::plugin::ImportedProgramStore store(sink);
    loadStoreTestProgram(store, 2);

    if (!store.isLoaded() || store.getZoneCount() != 2 || sink.publishCount != 0)
        return false;

    const auto outcome = store.edit([](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome result;
        edit.program.zones[0].keyRange = audiocity::engine::MidiRange::fromUnordered(24, 36);
        result.ok = true;
        result.resultIndex = 0;
        result.label = "Mapping updated: zone 1";
        return result;
    });

    if (!outcome.ok || outcome.resultIndex != 0)
        return false;

    // Publishing happens once, with the committed program.
    if (sink.publishCount != 1)
        return false;

    if (sink.lastProgram.zones[0].keyRange.low != 24 || sink.lastProgram.zones[0].keyRange.high != 36)
        return false;

    // Derived state follows the commit without the caller asking for it.
    if (store.getLastDiagnosticSummary() != "Mapping updated: zone 1")
        return false;

    const auto rows = store.getZoneRows();
    if (rows.size() != 2u)
        return false;

    audiocity::engine::Program stored;
    std::vector<juce::AudioBuffer<float>> storedSamples;
    store.captureSnapshot(stored, storedSamples);
    return stored.zones[0].keyRange.low == 24 && storedSamples.size() == 1u;
}

bool runImportedProgramStoreRejectedEditLeavesStateUntouchedTest()
{
    RecordingProgramSink sink;
    audiocity::plugin::ImportedProgramStore store(sink);
    loadStoreTestProgram(store, 2);

    const auto outcome = store.edit([](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome result;
        // Mutate the working copy, then fail: neither change may survive.
        edit.program.zones.clear();
        result.appendedSampleData.push_back(juce::AudioBuffer<float>(1, 64));
        result.ok = false;
        result.resultIndex = 7;
        result.label = "Rejected";
        return result;
    });

    if (outcome.ok || outcome.resultIndex != -1 || !outcome.appendedSampleData.empty())
        return false;

    if (sink.publishCount != 0 || store.getZoneCount() != 2)
        return false;

    if (store.getLastDiagnosticSummary() != "Loaded")
        return false;

    audiocity::engine::Program stored;
    std::vector<juce::AudioBuffer<float>> storedSamples;
    store.captureSnapshot(stored, storedSamples);
    return stored.zones.size() == 2u && storedSamples.size() == 1u;
}

bool runImportedProgramStoreAppendsSampleDataOnCommitTest()
{
    RecordingProgramSink sink;
    audiocity::plugin::ImportedProgramStore store(sink);
    loadStoreTestProgram(store, 1);

    const auto outcome = store.edit([](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome result;
        if (edit.sampleDataByAsset.empty())
            return result;

        audiocity::engine::SampleAsset asset;
        asset.displayName = "Added";
        asset.lengthSamples = 128;
        asset.numChannels = 1;
        asset.sampleRateHz = 48000.0;
        asset.rootMidiNote = 60;
        edit.program.sampleAssets.push_back(asset);

        result.appendedSampleData.push_back(juce::AudioBuffer<float>(1, 128));
        result.ok = true;
        result.resultIndex = static_cast<int>(edit.program.sampleAssets.size()) - 1;
        result.label = "Sample added";
        return result;
    });

    if (!outcome.ok || outcome.resultIndex != 1)
        return false;

    if (sink.publishCount != 1 || sink.lastSampleAssetCount != 2)
        return false;

    audiocity::engine::Program stored;
    std::vector<juce::AudioBuffer<float>> storedSamples;
    store.captureSnapshot(stored, storedSamples);
    return stored.sampleAssets.size() == 2u && storedSamples.size() == 2u;
}

bool runImportedProgramStoreRejectsEditWithoutLoadedProgramTest()
{
    RecordingProgramSink sink;
    audiocity::plugin::ImportedProgramStore store(sink);

    auto mutate = [](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome result;
        edit.program.zones.clear();
        result.ok = true;
        result.label = "Should not run";
        return result;
    };

    if (store.edit(mutate).ok || sink.publishCount != 0)
        return false;

    loadStoreTestProgram(store, 1);
    if (!store.edit(mutate).ok || sink.publishCount != 1)
        return false;

    store.clear();
    if (store.isLoaded() || store.getZoneCount() != 0 || store.getZoneRows().size() != 0u)
        return false;

    if (store.getProgramPath().isNotEmpty()
        || store.getFormat() != audiocity::plugin::ImportedProgramFormat::unknown)
    {
        return false;
    }

    return !store.edit(mutate).ok && sink.publishCount == 1;
}

bool runImportedProgramStoreLoadProgramPublishesNothingTest()
{
    RecordingProgramSink sink;
    audiocity::plugin::ImportedProgramStore store(sink);
    loadStoreTestProgram(store, 3);

    if (sink.publishCount != 0)
        return false;

    if (store.getFormat() != audiocity::plugin::ImportedProgramFormat::sfz
        || store.getSelectionIndex() != -1
        || store.getProgramName() != "StoreTest"
        || store.getLastDiagnosticSummary() != "Loaded")
    {
        return false;
    }

    if (store.getMapSummary().isEmpty() || store.getZoneRows().size() != 3u)
        return false;

    int observedZones = 0;
    int observedAssets = 0;
    store.read([&observedZones, &observedAssets](const audiocity::engine::Program& program,
                                                 const std::vector<juce::AudioBuffer<float>>& sampleData)
    {
        observedZones = static_cast<int>(program.zones.size());
        observedAssets = static_cast<int>(sampleData.size());
    });

    return observedZones == 3 && observedAssets == 1;
}

bool runImportedProgramStoreZoneEditSkipsSampleDataTest()
{
    RecordingProgramSink sink;
    audiocity::plugin::ImportedProgramStore store(sink);
    loadStoreTestProgram(store, 2);

    const auto outcome = store.edit([](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome result;
        if (edit.program.zones.empty())
            return result;

        edit.program.zones.front().keyRange = audiocity::engine::MidiRange::fromUnordered(48, 52);
        result.ok = true;
        result.label = "Mapping updated";
        return result;
    });

    if (!outcome.ok)
        return false;

    // A zone edit leaves the audio alone, so the sink is asked to adopt the program on its own.
    if (sink.metadataPublishCount != 1 || sink.publishCount != 1)
        return false;

    if (sink.lastProgram.zones.empty() || sink.lastProgram.zones.front().keyRange.low != 48)
        return false;

    return sink.lastSampleAssetCount == 0;
}

bool runImportedProgramStoreFallsBackWhenMetadataPublishRefusedTest()
{
    RecordingProgramSink sink;
    sink.acceptMetadataPublish = false;

    audiocity::plugin::ImportedProgramStore store(sink);
    loadStoreTestProgram(store, 2);

    const auto outcome = store.edit([](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome result;
        if (edit.program.zones.empty())
            return result;

        edit.program.zones.front().rootMidiNote = 55;
        result.ok = true;
        result.label = "Mapping updated";
        return result;
    });

    if (!outcome.ok)
        return false;

    if (sink.metadataPublishCount != 1 || sink.publishCount != 1)
        return false;

    if (sink.lastSampleAssetCount != 1)
        return false;

    return !sink.lastProgram.zones.empty() && sink.lastProgram.zones.front().rootMidiNote == 55;
}

bool runImportedProgramStoreFailedPublishLeavesStateUntouchedTest()
{
    RecordingProgramSink sink;
    sink.acceptMetadataPublish = false;
    sink.acceptFullPublish = false;

    audiocity::plugin::ImportedProgramStore store(sink);
    loadStoreTestProgram(store, 2);

    const auto outcome = store.edit([](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome result;
        edit.program.zones.front().rootMidiNote = 47;
        result.ok = true;
        result.resultIndex = 0;
        result.label = "Mapping updated";
        return result;
    });

    if (outcome.ok
        || outcome.resultIndex != -1
        || !outcome.label.contains("Injected engine publication failure")
        || sink.metadataPublishCount != 1
        || sink.checkedPublishCount != 1
        || sink.publishCount != 0
        || store.getLastDiagnosticSummary() != outcome.label)
    {
        return false;
    }

    audiocity::engine::Program stored;
    std::vector<juce::AudioBuffer<float>> storedSamples;
    store.captureSnapshot(stored, storedSamples);
    return stored.zones.size() == 2u
        && stored.zones.front().rootMidiNote == 60
        && storedSamples.size() == 1u;
}

bool runProgramMetadataUpdateReusesLoadedAudioTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setPreloadSamples(1024);

    Program program;

    SampleAsset asset;
    asset.displayName = "metadata.wav";
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(60);
    zone.rootMidiNote = 60;
    program.zones.push_back(zone);

    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    sampleDataByAsset.push_back(createTestSample(4096));

    engine.setProgram(program, sampleDataByAsset);

    const auto preloadAfterLoad = engine.getLoadedPreloadSamples();
    const auto streamAfterLoad = engine.getLoadedStreamSamples();
    if (preloadAfterLoad <= 0 || streamAfterLoad <= 0)
        return false;

    // Move the zone to another key. Only the mapping changed, so the loaded audio must be reused.
    auto remapped = program;
    remapped.zones[0].keyRange = MidiRange::single(65);
    remapped.zones[0].rootMidiNote = 65;

    if (!engine.setProgramMetadata(remapped))
        return false;

    if (engine.getLoadedPreloadSamples() != preloadAfterLoad
        || engine.getLoadedStreamSamples() != streamAfterLoad)
    {
        return false;
    }

    if (!engine.hasProgram() || engine.getProgramZoneCount() != 1)
        return false;

    // The remapped zone still sounds, which is what proves the audio survived the publish.
    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 65, 0.9f), 0);
    engine.render(block, midi);

    if (block.getMagnitude(0, blockSize) <= 0.0f)
        return false;

    // Adding an asset changes the audio, so a metadata-only publish has to refuse.
    auto withExtraAsset = remapped;
    withExtraAsset.sampleAssets.push_back(asset);
    return !engine.setProgramMetadata(withExtraAsset);
}

bool runConcurrentProgramSnapshotReclamationTest()
{
    using namespace audiocity::engine;
    constexpr int zoneCount = 600;
    EngineCore engine;
    engine.prepare(48000.0, 64, 2);

    Program program;
    SampleAsset asset;
    asset.sampleRateHz = 48000.0;
    program.sampleAssets.push_back(asset);
    for (auto index = 0; index < zoneCount; ++index)
    {
        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.keyRange = MidiRange::single(index % 128);
        program.zones.push_back(zone);
    }
    std::vector<juce::AudioBuffer<float>> audio{ createTestSample(4096) };
    if (!engine.setProgram(program, audio))
        return false;

    std::atomic<bool> stop{ false };
    std::atomic<int> renderCount{ 0 };
    std::thread renderThread([&]
    {
        juce::AudioBuffer<float> block(2, 64);
        juce::MidiBuffer midi;
        while (!stop.load(std::memory_order_acquire))
        {
            block.clear();
            engine.render(block, midi);
            renderCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    for (auto edit = 0; edit < 256; ++edit)
    {
        program.zones[0].rootMidiNote = 48 + (edit % 24);
        if (!engine.setProgramMetadata(program))
        {
            stop.store(true, std::memory_order_release);
            renderThread.join();
            return false;
        }
    }
    stop.store(true, std::memory_order_release);
    renderThread.join();

    const auto retained = engine.getProgramSnapshotRetentionStats();
    const auto oneSnapshotUpperBound = sizeof(ProgramSnapshot)
        + program.sampleAssets.size() * sizeof(ProgramSnapshot::SampleAssetRef)
        + program.zones.size() * sizeof(ProgramSnapshot::ZoneRef)
        + program.zones.size() * sizeof(std::size_t);
    return renderCount.load(std::memory_order_relaxed) > 0
        && retained.ownerCount == 1
        && retained.metadataBytes <= oneSnapshotUpperBound;
}

bool runNestedSnapshotReaderReclamationTest()
{
    struct Payload
    {
        int generation = 0;
    };

    audiocity::engine::RtSnapshotCell<Payload> cell;
    {
        const auto emptyOuter = cell.read(audiocity::engine::RtReaderRole::audio);
        const auto emptyInner = cell.read(audiocity::engine::RtReaderRole::audio);
        cell.publish(std::make_shared<const Payload>(Payload{ 0 }));
        if (emptyOuter || emptyInner || cell.retainedOwnerCountForWriter() != 1)
            return false;
    }
    cell.publish(std::make_shared<const Payload>(Payload{ 1 }));

    {
        const auto outer = cell.read(audiocity::engine::RtReaderRole::audio);
        const auto inner = cell.read(audiocity::engine::RtReaderRole::audio);
        if (!outer || !inner || outer->generation != 1 || inner->generation != 1)
            return false;

        std::thread publisher([&]
        {
            for (auto generation = 2; generation <= 257; ++generation)
                cell.publish(std::make_shared<const Payload>(Payload{ generation }));
        });
        publisher.join();

        // A nested same-role read must remain pinned to the outer guard's generation even while
        // the writer publishes and reclaims hundreds of intervening owners.
        if (outer->generation != 1 || inner->generation != 1
            || cell.retainedOwnerCountForWriter() != 2)
            return false;
    }

    return cell.retainedOwnerCountForWriter() == 1;
}

bool runProgramPublishRejectsIncompleteReferencedAudioTest()
{
    using namespace audiocity::engine;

    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);

    Program baseline;
    SampleAsset asset;
    asset.displayName = "active.wav";
    asset.sampleRateHz = sampleRate;
    baseline.sampleAssets.push_back(asset);
    asset.displayName = "intentionally-unused.wav";
    baseline.sampleAssets.push_back(asset);

    Zone activeZone;
    activeZone.sampleAssetIndex = 0;
    activeZone.keyRange = MidiRange::single(60);
    activeZone.rootMidiNote = 60;
    baseline.zones.push_back(activeZone);

    std::vector<juce::AudioBuffer<float>> onlyActiveAudio;
    onlyActiveAudio.push_back(createTestSample(1024));
    if (!engine.setProgram(baseline, onlyActiveAudio))
        return false;

    // An unreferenced asset may be absent, but metadata-only edits may not make
    // that unloaded asset playable.
    auto invalidMetadataEdit = baseline;
    invalidMetadataEdit.zones.front().sampleAssetIndex = 1;
    invalidMetadataEdit.zones.front().keyRange = MidiRange::single(65);
    if (engine.setProgramMetadata(invalidMetadataEdit))
        return false;

    auto missingProgram = baseline;
    Zone missingZone = activeZone;
    missingZone.sampleAssetIndex = 1;
    missingZone.keyRange = MidiRange::single(65);
    missingProgram.zones.push_back(missingZone);
    const auto missingResult = engine.setProgram(missingProgram, onlyActiveAudio);
    if (missingResult
        || missingResult.error != EngineCore::ProgramPublishResult::Error::missingReferencedAudio
        || missingResult.referencedZoneIndex != 1
        || missingResult.referencedSampleAssetIndex != 1
        || !missingResult.diagnostic.contains("only 1 audio buffers")
        || !missingResult.diagnostic.contains("previous program preserved"))
    {
        return false;
    }

    auto audioWithEmptyReference = onlyActiveAudio;
    audioWithEmptyReference.emplace_back();
    const auto emptyResult = engine.setProgram(missingProgram, audioWithEmptyReference);
    if (emptyResult
        || emptyResult.error != EngineCore::ProgramPublishResult::Error::emptyReferencedAudio
        || emptyResult.referencedZoneIndex != 1
        || emptyResult.referencedSampleAssetIndex != 1
        || !emptyResult.diagnostic.contains("0 channels")
        || !emptyResult.diagnostic.contains("0 samples")
        || !emptyResult.diagnostic.contains("previous program preserved"))
    {
        return false;
    }

    if (!engine.hasProgram() || engine.getProgramZoneCount() != 1)
        return false;

    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);
    return engine.activeVoiceCount() == 1 && blockEnergy(block) > 0.01f;
}

bool runProgramCapacityBoundaryAndRejectionTest()
{
    using namespace audiocity::engine;

    EngineCore engine;
    engine.prepare(48000.0, 64, 2);

    Program baseline;
    SampleAsset baselineAsset;
    baselineAsset.lengthSamples = 256;
    baselineAsset.numChannels = 1;
    baselineAsset.sampleRateHz = 48000.0;
    baseline.sampleAssets.push_back(baselineAsset);
    Zone baselineZone;
    baselineZone.sampleAssetIndex = 0;
    baseline.zones.push_back(baselineZone);

    auto restoreBaseline = [&]()
    {
        return static_cast<bool>(engine.setProgram(baseline))
            && engine.getProgramZoneCount() == 1;
    };

    auto verifyRejected = [&](const Program& program, const juce::String& dimension)
    {
        if (!restoreBaseline())
            return false;

        const auto result = engine.setProgram(program);
        return !result
            && result.error == EngineCore::ProgramPublishResult::Error::capacityExceeded
            && result.diagnostic.contains(dimension)
            && result.diagnostic.contains("Counts/limits")
            && engine.hasProgram()
            && engine.getProgramZoneCount() == 1;
    };

    Program assetsAtLimit;
    assetsAtLimit.sampleAssets.resize(ProgramSnapshot::maxSampleAssets);
    if (!engine.setProgram(assetsAtLimit))
        return false;
    auto assetsOverLimit = assetsAtLimit;
    assetsOverLimit.sampleAssets.emplace_back();
    if (!verifyRejected(assetsOverLimit, "sample assets"))
        return false;

    Program groupsAtLimit;
    groupsAtLimit.groups.resize(ProgramSnapshot::maxGroups);
    if (!engine.setProgram(groupsAtLimit))
        return false;
    auto groupsOverLimit = groupsAtLimit;
    groupsOverLimit.groups.emplace_back();
    if (!verifyRejected(groupsOverLimit, "groups"))
        return false;

    Program zonesAtLimit;
    zonesAtLimit.zones.resize(ProgramSnapshot::maxZones);
    if (!engine.setProgram(zonesAtLimit))
        return false;
    auto zonesOverLimit = zonesAtLimit;
    zonesOverLimit.zones.emplace_back();
    return verifyRejected(zonesOverLimit, "zones");
}

bool runScalableProgramSnapshotAboveLegacyZoneLimitTest()
{
    using namespace audiocity::engine;

    constexpr int zoneCount = 600;
    constexpr int sampleLength = 2048;
    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_large_sfz_fixture", "");
    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    const auto sfzFile = tempDirectory.getChildFile("Large.sfz");
    if (!tempDirectory.createDirectory()
        || !writeMonoToneWav(sampleFile, 48000, sampleLength))
        return false;

    juce::String sfz;
    for (auto zoneIndex = 0; zoneIndex < zoneCount; ++zoneIndex)
    {
        const auto note = zoneIndex % 128;
        const auto velocity = 1 + (zoneIndex / 128);
        sfz << "<region> sample=Tone.wav key=" << note
            << " lovel=" << velocity << " hivel=" << velocity
            << " offset=" << zoneIndex << "\n";
    }
    if (!sfzFile.replaceWithText(sfz))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto imported = importer.importFile(sfzFile);
    if (imported.hasErrors()
        || imported.program.sampleAssets.size() != 1
        || imported.sampleDataByAsset.size() != 1
        || imported.program.zones.size() != zoneCount)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto snapshot = ProgramSnapshot::fromProgram(imported.program);
    if (snapshot.truncated || snapshot.zoneCount != zoneCount || snapshot.zones.size() != zoneCount)
    {
        tempDirectory.deleteRecursively();
        return false;
    }
    for (std::size_t zoneIndex = 0; zoneIndex < snapshot.zoneCount; ++zoneIndex)
    {
        const auto expectedNote = static_cast<int>(zoneIndex) % 128;
        const auto expectedVelocity = 1 + (static_cast<int>(zoneIndex) / 128);
        const auto& zone = imported.program.zones[zoneIndex];
        if (!snapshot.isZonePlayable(zoneIndex)
            || zone.sampleAssetIndex != 0
            || zone.keyRange.low != expectedNote
            || zone.keyRange.high != expectedNote
            || zone.velocityRange.low != expectedVelocity
            || zone.velocityRange.high != expectedVelocity
            || zone.sampleStart != static_cast<int>(zoneIndex))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    EngineCore engine;
    engine.prepare(48000.0, 64, 2);
    if (!engine.setProgram(imported.program, imported.sampleDataByAsset)
        || engine.getProgramZoneCount() != zoneCount)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    for (auto zoneIndex = 0; zoneIndex < zoneCount; ++zoneIndex)
    {
        engine.panic();
        juce::AudioBuffer<float> block(2, 64);
        block.clear();
        juce::MidiBuffer midi;
        const auto note = zoneIndex % 128;
        const auto velocity = static_cast<juce::uint8>(1 + (zoneIndex / 128));
        midi.addEvent(juce::MidiMessage::noteOn(1, note, velocity), 0);
        engine.render(block, midi);

        auto reachedZone = false;
        for (const auto& voice : engine.getVoicePlaybackStates())
            reachedZone = reachedZone || (voice.active && voice.zoneIndex == zoneIndex);
        if (!reachedZone || blockEnergy(block) <= 0.0f)
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    tempDirectory.deleteRecursively();
    return true;
}

bool runPreloadNoOpRebuildGuardTest()
{
    using namespace audiocity::engine;

    EngineCore engine;
    engine.prepare(48000.0, 64, 2);
    engine.setSampleData(createTestSample(8192), 48000.0, 60);

    const auto initialPreload = engine.getPreloadSamples();
    const auto beforeNoOp = engine.getSegmentRebuildCount();
    engine.setPreloadSamples(initialPreload);
    if (engine.getSegmentRebuildCount() != beforeNoOp)
        return false;

    const auto changedPreload = initialPreload == 2048 ? 4096 : 2048;
    engine.setPreloadSamples(changedPreload);
    const auto afterChange = engine.getSegmentRebuildCount();
    if (afterChange != beforeNoOp + 1)
        return false;

    engine.setPreloadSamples(changedPreload);
    return engine.getSegmentRebuildCount() == afterChange;
}

bool runPendingEventLinearDispatchTest()
{
    using namespace audiocity::engine;

    constexpr int blockSize = EngineCore::pendingEventCapacity;
    constexpr std::array eventCounts{ 64, 256, EngineCore::pendingEventCapacity };
    std::array<int, eventCounts.size()> traversalCounts{};
    auto previousEventCount = 0;
    auto previousTraversalCount = 0;

    for (auto countIndex = std::size_t{ 0 }; countIndex < eventCounts.size(); ++countIndex)
    {
        const auto eventCount = eventCounts[countIndex];
        EngineCore engine;
        engine.prepare(48000.0, blockSize, 2);
        engine.setSampleData(createTestSample(4096), 48000.0, 60);
        engine.resetPendingEventDropCounts();

        for (auto eventIndex = 0; eventIndex < eventCount; ++eventIndex)
        {
            const auto offset = eventIndex * blockSize / eventCount;
            engine.noteOff(1, offset);
        }

        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        engine.render(block, midi);

        const auto drops = engine.getPendingEventDropCounts();
        const auto traversalCount = engine.getLastPendingEventTraversalCount();
        if (engine.getLastPendingEventDispatchCount() != eventCount
            || traversalCount < eventCount
            || traversalCount > eventCount * 3
            || drops.noteOn != 0
            || drops.noteOff != 0
            || drops.allNotesOff != 0
            || drops.continuousControl != 0
            || drops.panicRecoveries != 0)
        {
            return false;
        }

        // The observed cursor work may have a small constant edge effect, but
        // quadrupling input must not grow it by more than a linear 4x plus that edge.
        if (previousEventCount > 0
            && traversalCount > previousTraversalCount * (eventCount / previousEventCount) + 8)
        {
            return false;
        }
        previousEventCount = eventCount;
        previousTraversalCount = traversalCount;
        traversalCounts[countIndex] = traversalCount;
    }

    std::printf("Pending-event traversal counts: 64=%d, 256=%d, 1024=%d (linear bound <= 3n).\n",
        traversalCounts[0], traversalCounts[1], traversalCounts[2]);
    return true;
}

bool runPendingEventSafetyOverflowRecoveryTest()
{
    using namespace audiocity::engine;

    EngineCore engine;
    engine.prepare(48000.0, 64, 2);
    engine.setSampleData(createTestSample(4096), 48000.0, 60);

    {
        juce::AudioBuffer<float> block(2, 64);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
    }
    if (!engine.isNoteActive(60))
        return false;

    engine.resetPendingEventDropCounts();
    for (auto eventIndex = 0; eventIndex < EngineCore::pendingEventCapacity; ++eventIndex)
        engine.noteOff(1, 0);

    // A queue containing only safety events cannot displace one. The bounded panic latch must
    // recover the held note on the next audio render and make that exceptional path observable.
    engine.noteOff(60, 0);

    juce::AudioBuffer<float> block(2, 64);
    juce::MidiBuffer midi;
    engine.render(block, midi);

    const auto drops = engine.getPendingEventDropCounts();
    return !engine.isNoteActive(60)
        && engine.activeVoiceCount() == 0
        && drops.noteOff == 1
        && drops.panicRecoveries == 1;
}

bool runPendingEventSafetyDisplacesContinuousControlTest()
{
    using namespace audiocity::engine;

    EngineCore engine;
    engine.prepare(48000.0, 64, 2);
    engine.setSampleData(createTestSample(4096), 48000.0, 60);
    auto amp = engine.getAmpEnvelope();
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    {
        juce::AudioBuffer<float> block(2, 64);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
    }
    if (!engine.isNoteActive(60))
        return false;

    engine.resetPendingEventDropCounts();
    const auto nonSafetyCapacity = EngineCore::pendingEventCapacity
        - EngineCore::pendingSafetyEventReserve;
    for (auto eventIndex = 0; eventIndex < nonSafetyCapacity; ++eventIndex)
        engine.pitchBend(8192 + (eventIndex % 128), 0);

    // Fill the reserved tail with harmless release events, then submit the release
    // that matters. The scheduler must evict a continuous-control event, not lose
    // the note-off or fall back to panic recovery.
    for (auto eventIndex = 0; eventIndex < EngineCore::pendingSafetyEventReserve; ++eventIndex)
        engine.noteOff(1, 0);
    engine.noteOff(60, 0);

    juce::AudioBuffer<float> block(2, 64);
    juce::MidiBuffer midi;
    engine.render(block, midi);

    const auto drops = engine.getPendingEventDropCounts();
    return !engine.isNoteActive(60)
        && engine.getLastPendingEventDispatchCount() == EngineCore::pendingEventCapacity
        && drops.noteOn == 0
        && drops.noteOff == 0
        && drops.allNotesOff == 0
        && drops.continuousControl == 1
        && drops.panicRecoveries == 0;
}

bool runMidiAllNotesOffSafetyEventTest()
{
    using namespace audiocity::engine;

    EngineCore engine;
    engine.prepare(48000.0, 64, 2);
    engine.setSampleData(createTestSample(4096), 48000.0, 60);

    juce::AudioBuffer<float> block(2, 64);
    juce::MidiBuffer start;
    start.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, start);
    if (!engine.isNoteActive(60))
        return false;

    juce::MidiBuffer stop;
    stop.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    engine.render(block, stop);
    return engine.activeVoiceCount() == 0;
}

bool runAudioStateCodecRoundTripTest()
{
    juce::AudioBuffer<float> source(2, 4096);
    for (auto sample = 0; sample < source.getNumSamples(); ++sample)
    {
        source.setSample(0, sample, static_cast<float>(std::sin(sample * 0.01)));
        source.setSample(1, sample, sample % 64 == 0 ? 0.5f : 0.0f);
    }

    juce::String error;
    const auto encoded = audiocity::plugin::encodeAudioStateAsset(source, &error);
    if (encoded.isEmpty() || error.isNotEmpty()
        || encoded.getSize() >= static_cast<std::size_t>(source.getNumChannels()
            * source.getNumSamples() * static_cast<int>(sizeof(float))))
        return false;

    juce::AudioBuffer<float> restored;
    if (!audiocity::plugin::decodeAudioStateAsset(encoded, restored, &error)
        || restored.getNumChannels() != source.getNumChannels()
        || restored.getNumSamples() != source.getNumSamples())
        return false;

    for (auto channel = 0; channel < source.getNumChannels(); ++channel)
        if (std::memcmp(source.getReadPointer(channel),
                        restored.getReadPointer(channel),
                        static_cast<std::size_t>(source.getNumSamples()) * sizeof(float)) != 0)
            return false;

    return true;
}

bool runAudioStateCodecRejectsCorruptionAtomicallyTest()
{
    std::vector<float> source(2048, 0.125f);
    auto encoded = audiocity::plugin::encodeMonoAudioStateAsset(source);
    if (encoded.getSize() < 40)
        return false;

    auto* bytes = static_cast<std::uint8_t*>(encoded.getData());
    bytes[encoded.getSize() - 1] ^= 0x5au;

    juce::AudioBuffer<float> destination(1, 2);
    destination.setSample(0, 0, 0.25f);
    destination.setSample(0, 1, -0.5f);
    juce::String error;
    if (audiocity::plugin::decodeAudioStateAsset(encoded, destination, &error)
        || error.isEmpty()
        || destination.getNumChannels() != 1
        || destination.getNumSamples() != 2
        || destination.getSample(0, 0) != 0.25f
        || destination.getSample(0, 1) != -0.5f)
        return false;

    encoded.setSize(encoded.getSize() - 3, true);
    return !audiocity::plugin::decodeAudioStateAsset(encoded, destination, &error)
        && error.isNotEmpty()
        && destination.getNumSamples() == 2;
}

bool runAudioStateCodecEnforcesLimitsTest()
{
    juce::AudioBuffer<float> tooManyChannels(
        audiocity::plugin::AudioStateCodecLimits::maximumChannels + 1, 1);
    juce::String error;
    if (!audiocity::plugin::encodeAudioStateAsset(tooManyChannels, &error).isEmpty()
        || error.isEmpty())
        return false;

    juce::AudioBuffer<float> nonFinite(1, 2);
    nonFinite.setSample(0, 0, std::numeric_limits<float>::infinity());
    return audiocity::plugin::encodeAudioStateAsset(nonFinite, &error).isEmpty()
        && error.isNotEmpty();
}

juce::MemoryBlock gzipAudioStateTestPayload(const void* const data, const std::size_t size)
{
    juce::MemoryOutputStream compressed;
    {
        juce::GZIPCompressorOutputStream zipper(
            compressed,
            6,
            juce::GZIPCompressorOutputStream::windowBitsGZIP);
        if (!zipper.write(data, size))
            return {};
    }
    return compressed.getMemoryBlock();
}

juce::MemoryBlock forgeAudioStateTestAsset(const int channels,
                                           const int samples,
                                           const int decodedBytes,
                                           const std::uint32_t crc,
                                           const juce::MemoryBlock& compressed)
{
    constexpr std::array<char, 8> magic{ 'A', 'C', 'T', 'Y', 'A', 'U', 'D', '1' };
    juce::MemoryOutputStream output;
    output.write(magic.data(), magic.size());
    output.writeInt(1);
    output.writeInt(channels);
    output.writeInt(samples);
    output.writeInt(decodedBytes);
    output.writeInt(static_cast<int>(crc));
    output.writeInt(static_cast<int>(compressed.getSize()));
    output.write(compressed.getData(), compressed.getSize());
    return output.getMemoryBlock();
}

bool runAudioStateCodecRejectsForgedSizesAndTrailingDataTest()
{
    constexpr auto headerPrefixBytes = std::size_t{ 28 };
    const std::vector<float> source(32, 0.125f);
    const auto valid = audiocity::plugin::encodeMonoAudioStateAsset(source);
    if (valid.getSize() <= headerPrefixBytes)
        return false;

    std::vector<float> validDestination;
    juce::String error("stale error");
    if (!audiocity::plugin::decodeMonoAudioStateAsset(valid, validDestination, &error)
        || validDestination != source
        || error.isNotEmpty())
        return false;

    std::vector<float> decodedWithTrailingSample(source);
    decodedWithTrailingSample.push_back(-0.75f);
    const auto compressedWithTrailingSample = gzipAudioStateTestPayload(
        decodedWithTrailingSample.data(), decodedWithTrailingSample.size() * sizeof(float));
    if (compressedWithTrailingSample.isEmpty())
        return false;

    juce::MemoryOutputStream trailingAssetStream;
    trailingAssetStream.write(valid.getData(), headerPrefixBytes);
    trailingAssetStream.writeInt(static_cast<int>(compressedWithTrailingSample.getSize()));
    trailingAssetStream.write(compressedWithTrailingSample.getData(), compressedWithTrailingSample.getSize());
    const auto trailingAsset = trailingAssetStream.getMemoryBlock();

    std::vector<float> monoDestination{ 0.625f, -0.375f };
    if (audiocity::plugin::decodeMonoAudioStateAsset(trailingAsset, monoDestination, &error)
        || !error.containsIgnoreCase("trailing decoded data")
        || monoDestination != std::vector<float>({ 0.625f, -0.375f }))
        return false;

    const float tinyDecodedPayload = 0.0f;
    const auto tinyCompressedPayload = gzipAudioStateTestPayload(
        &tinyDecodedPayload, sizeof(tinyDecodedPayload));
    if (tinyCompressedPayload.isEmpty())
        return false;

    const auto maximumDecodedBytes = audiocity::plugin::AudioStateCodecLimits::maximumDecodedBytes;
    const auto maximumMonoSamples = static_cast<int>(maximumDecodedBytes / sizeof(float));
    const auto oversized = forgeAudioStateTestAsset(
        1,
        maximumMonoSamples + 1,
        static_cast<int>(maximumDecodedBytes + sizeof(float)),
        0,
        tinyCompressedPayload);

    juce::AudioBuffer<float> destination(1, 2);
    destination.setSample(0, 0, 0.25f);
    destination.setSample(0, 1, -0.5f);
    if (audiocity::plugin::decodeAudioStateAsset(oversized, destination, &error)
        || !error.containsIgnoreCase("header")
        || destination.getNumSamples() != 2
        || destination.getSample(0, 0) != 0.25f
        || destination.getSample(0, 1) != -0.5f)
        return false;

    // The exact 64 MiB boundary is admitted by the header checks, but a tiny forged
    // payload must still fail atomically instead of being treated as zero-filled data.
    const auto maximumBoundary = forgeAudioStateTestAsset(
        1,
        maximumMonoSamples,
        static_cast<int>(maximumDecodedBytes),
        0,
        tinyCompressedPayload);
    return !audiocity::plugin::decodeAudioStateAsset(maximumBoundary, destination, &error)
        && error.containsIgnoreCase("decoded fewer bytes")
        && destination.getNumSamples() == 2
        && destination.getSample(0, 0) == 0.25f
        && destination.getSample(0, 1) == -0.5f;
}

bool writeResolverFixtureFile(const juce::File& file, const char* const bytes)
{
    return file.getParentDirectory().createDirectory().wasOk()
        && file.replaceWithData(bytes, std::strlen(bytes));
}

bool runImportedAssetResolverMovedFolderAndSafetyTest()
{
    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_asset_resolver", "");
    const auto originalLibrary = tempDirectory.getChildFile("Original").getChildFile("Library");
    const auto originalProgram = originalLibrary.getChildFile("Instruments").getChildFile("Piano.sfz");
    const auto originalSampleA = originalLibrary.getChildFile("Samples").getChildFile("A.wav");
    const auto originalSampleB = originalLibrary.getChildFile("Samples").getChildFile("B.wav");
    if (!writeResolverFixtureFile(originalProgram, "<region> sample=../Samples/A.wav")
        || !writeResolverFixtureFile(originalSampleA, "RIFF-audio-A")
        || !writeResolverFixtureFile(originalSampleB, "RIFF-audio-B"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    audiocity::engine::Program program;
    program.sampleAssets.resize(2);
    program.sampleAssets[0].sourcePath = originalSampleA.getFullPathName().toStdString();
    program.sampleAssets[1].sourcePath = originalSampleB.getFullPathName().toStdString();
    const auto manifest = audiocity::plugin::createImportedAssetManifest(originalProgram, program);
    if (!manifest.isValid() || manifest.entries.size() != 3)
    {
        tempDirectory.deleteRecursively();
        return false;
    }
    for (const auto& entry : manifest.entries)
    {
        if (!entry.hasFastHash || entry.sizeBytes <= 0 || entry.modificationTimeMs <= 0
            || entry.relativePath.contains(".."))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    juce::ValueTree patch("Patch");
    audiocity::plugin::appendImportedProgramState(
        patch,
        originalProgram.getFullPathName(),
        {},
        audiocity::plugin::ImportedProgramFormat::sfz,
        -1,
        manifest);
    const auto restoredManifest = audiocity::plugin::readImportedAssetManifestState(patch);
    if (!restoredManifest.isValid() || restoredManifest.entries.size() != manifest.entries.size())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto collaboratorRoot = tempDirectory.getChildFile("Collaborator");
    const auto movedLibrary = collaboratorRoot.getChildFile("Library");
    collaboratorRoot.createDirectory();
    if (!originalLibrary.copyDirectoryTo(movedLibrary)
        || !tempDirectory.getChildFile("Original").deleteRecursively())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto moved = audiocity::plugin::resolveImportedAssetManifest(
        restoredManifest, { collaboratorRoot });
    if (!moved.complete || moved.ambiguous || moved.resolvedFiles.size() != 3
        || moved.resolvedProgramFile() != movedLibrary.getChildFile("Instruments").getChildFile("Piano.sfz"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    // A same-size replacement must not be accepted solely because its name and size match.
    const auto movedSampleA = movedLibrary.getChildFile("Samples").getChildFile("A.wav");
    if (!movedSampleA.replaceWithData("RIFF-audio-X", std::strlen("RIFF-audio-X")))
    {
        tempDirectory.deleteRecursively();
        return false;
    }
    const auto altered = audiocity::plugin::resolveImportedAssetManifest(
        restoredManifest, { collaboratorRoot });
    if (altered.complete || !altered.resolvedFiles.empty())
    {
        tempDirectory.deleteRecursively();
        return false;
    }
    movedSampleA.replaceWithData("RIFF-audio-A", std::strlen("RIFF-audio-A"));

    const auto duplicateRoot = tempDirectory.getChildFile("Duplicate");
    const auto duplicateLibrary = duplicateRoot.getChildFile("Library");
    duplicateRoot.createDirectory();
    if (!movedLibrary.copyDirectoryTo(duplicateLibrary))
    {
        tempDirectory.deleteRecursively();
        return false;
    }
    const auto ambiguous = audiocity::plugin::resolveImportedAssetManifest(
        restoredManifest, { tempDirectory });
    if (ambiguous.complete || !ambiguous.ambiguous || !ambiguous.resolvedFiles.empty())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    duplicateRoot.deleteRecursively();
    movedLibrary.getChildFile("Samples").getChildFile("B.wav").deleteFile();
    const auto missingSubset = audiocity::plugin::resolveImportedAssetManifest(
        restoredManifest, { collaboratorRoot });
    const auto ok = !missingSubset.complete && !missingSubset.ambiguous
        && missingSubset.resolvedFiles.empty() && missingSubset.diagnostic.isNotEmpty();
    tempDirectory.deleteRecursively();
    return ok;
}

bool runImportedAssetResolverBoundedUniquenessTest()
{
    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_asset_resolver_limit", "");
    const auto originalRoot = tempDirectory.getChildFile("Original");
    const auto originalProgram = originalRoot.getChildFile("Patch.sfz");
    const auto originalSample = originalRoot.getChildFile("Tone.wav");
    if (!writeResolverFixtureFile(originalProgram, "<region> sample=Tone.wav")
        || !writeResolverFixtureFile(originalSample, "RIFF-tone"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    audiocity::engine::Program program;
    program.sampleAssets.resize(1);
    program.sampleAssets.front().sourcePath = originalSample.getFullPathName().toStdString();
    auto manifest = audiocity::plugin::createImportedAssetManifest(originalProgram, program);

    const auto searchRoot = tempDirectory.getChildFile("Search");
    const auto firstVisited = searchRoot.getChildFile("ZFirst");
    const auto hiddenDuplicate = searchRoot.getChildFile("AHidden");
    if (!searchRoot.createDirectory()
        || !originalRoot.copyDirectoryTo(firstVisited)
        || !originalRoot.copyDirectoryTo(hiddenDuplicate)
        || !originalRoot.deleteRecursively())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    manifest.originalRoot.clear();
    audiocity::plugin::ImportedAssetSearchLimits limits;
    limits.maximumFiles = 1;
    limits.maximumDirectories = 32;
    limits.maximumDepth = 4;
    const auto resolution = audiocity::plugin::resolveImportedAssetManifest(
        manifest, { searchRoot }, limits);
    const auto ok = !resolution.complete
        && !resolution.ambiguous
        && resolution.limitReached
        && resolution.resolvedFiles.empty()
        && resolution.diagnostic.containsIgnoreCase("uniqueness")
        && firstVisited.getChildFile("Patch.sfz").existsAsFile()
        && hiddenDuplicate.getChildFile("Patch.sfz").existsAsFile();
    tempDirectory.deleteRecursively();
    return ok;
}

bool runImportedAssetResolverLegacyStateTest()
{
    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_legacy_asset_resolver", "");
    const auto oldFile = tempDirectory.getChildFile("Old").getChildFile("Legacy.sfz");
    const auto movedRoot = tempDirectory.getChildFile("Moved");
    const auto movedFile = movedRoot.getChildFile("Legacy.sfz");
    if (!writeResolverFixtureFile(movedFile, "<region> sample=tone.wav"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    auto legacyManifest = audiocity::plugin::createLegacyImportedProgramManifest(oldFile);
    legacyManifest.originalRoot.clear();
    const auto resolution = audiocity::plugin::resolveImportedAssetManifest(
        legacyManifest, { movedRoot });
    const auto ok = resolution.complete && resolution.resolvedFiles.size() == 1
        && resolution.resolvedProgramFile() == movedFile;
    tempDirectory.deleteRecursively();
    return ok;
}

bool runImportedAssetResolverHashlessMtimeTest()
{
    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_hashless_asset_resolver", "");
    const auto programFile = tempDirectory.getChildFile("Patch.sfz");
    if (!writeResolverFixtureFile(programFile, "<region> sample=Tone.wav"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    audiocity::engine::Program program;
    auto manifest = audiocity::plugin::createImportedAssetManifest(programFile, program);
    if (!manifest.isValid() || manifest.entries.size() != 1)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    auto& entry = manifest.entries.front();
    entry.hasFastHash = false;
    entry.fastHash = 0;
    if (!manifest.isValid())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto originalMtime = entry.modificationTimeMs;
    const auto unchanged = audiocity::plugin::resolveImportedAssetManifest(manifest, { tempDirectory });
    const auto changedTime = juce::Time(originalMtime + 5000);
    if (!unchanged.complete || !programFile.setLastModificationTime(changedTime))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto changed = audiocity::plugin::resolveImportedAssetManifest(manifest, { tempDirectory });
    entry.modificationTimeMs = 0;
    const auto rejectsMissingStoredMtime = !manifest.isValid();
    const auto ok = !changed.complete
        && changed.resolvedFiles.empty()
        && programFile.getSize() == entry.sizeBytes
        && rejectsMissingStoredMtime;
    tempDirectory.deleteRecursively();
    return ok;
}

bool runLibraryFileIndexAndResolverLinkTraversalRejectionTest()
{
    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_link_traversal", "");
    const auto libraryRoot = tempDirectory.getChildFile("Library");
    const auto outsideRoot = tempDirectory.getChildFile("Outside");
    const auto outsideProgram = outsideRoot.getChildFile("Patch.sfz");
    const auto outsideSample = outsideRoot.getChildFile("Tone.wav");
    const auto linkedFolder = libraryRoot.getChildFile("Linked");
    const auto originalLibraryProgram = linkedFolder.getChildFile("Patch.sfz");
    const auto originalLibrarySample = linkedFolder.getChildFile("Tone.wav");
    if (!libraryRoot.createDirectory()
        || !writeResolverFixtureFile(outsideProgram, "<region> sample=Tone.wav")
        || !writeResolverFixtureFile(outsideSample, "RIFF-link-tone")
        || !writeResolverFixtureFile(originalLibraryProgram, "<region> sample=Tone.wav")
        || !writeResolverFixtureFile(originalLibrarySample, "RIFF-link-tone"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    audiocity::plugin::LibraryFileIndexData indexData;
    indexData.libraryRootPath = libraryRoot.getFullPathName();
    audiocity::plugin::LibraryFileIndexEntry linkedEntry;
    linkedEntry.relativePath = "Linked/Tone.wav";
    linkedEntry.sizeBytes = originalLibrarySample.getSize();
    linkedEntry.modificationTimeMs = originalLibrarySample.getLastModificationTime().toMilliseconds();
    indexData.entries.push_back(linkedEntry);
    linkedEntry.relativePath = "Linked/Patch.sfz";
    linkedEntry.sizeBytes = originalLibraryProgram.getSize();
    linkedEntry.modificationTimeMs = originalLibraryProgram.getLastModificationTime().toMilliseconds();
    linkedEntry.isInstrument = true;
    indexData.entries.push_back(linkedEntry);
    const auto cacheFile = tempDirectory.getChildFile("linked-index.acli");
    audiocity::plugin::LibraryFileIndexStore indexStore(cacheFile);
    if (!indexStore.save(indexData))
    {
        tempDirectory.deleteRecursively();
        return false;
    }
    const auto safeRestored = indexStore.load();
    if (!safeRestored.has_value() || safeRestored->entries.size() != indexData.entries.size())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    bool replacementAttempted = false;
    bool originalDirectoryRemoved = false;
    bool replacementLinkCreated = false;
    audiocity::plugin::LibraryFileIndexStore replacingIndexStore(cacheFile);
    replacingIndexStore.setPathValidationObserverForTesting([&](const std::size_t validatedEntries)
    {
        if (validatedEntries != 1 || replacementAttempted)
            return;

        replacementAttempted = true;
        originalDirectoryRemoved = linkedFolder.deleteRecursively();
        if (originalDirectoryRemoved)
            replacementLinkCreated = outsideRoot.createSymbolicLink(linkedFolder, true)
                && linkedFolder.isSymbolicLink();
    });
    const auto replacedDuringLoad = replacingIndexStore.load();
    if (!replacementAttempted || !originalDirectoryRemoved)
    {
        tempDirectory.deleteRecursively();
        return false;
    }
    if (!replacementLinkCreated)
    {
        std::printf("Symbolic-link traversal test skipped: link creation is unavailable.\n");
        tempDirectory.deleteRecursively();
        return true;
    }
    if (replacedDuringLoad.has_value() || !linkedFolder.isSymbolicLink())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto scanned = audiocity::plugin::LibraryFileIndex::scanRoot(libraryRoot, false);
    const auto indexLoadRejected = !indexStore.load().has_value();
    const auto indexSaveRejected = !indexStore.save(indexData);

    audiocity::engine::Program program;
    program.sampleAssets.resize(1);
    program.sampleAssets.front().sourcePath = outsideSample.getFullPathName().toStdString();
    auto manifest = audiocity::plugin::createImportedAssetManifest(outsideProgram, program);
    manifest.originalRoot.clear();
    for (auto& entry : manifest.entries)
        entry.relativePath = "Linked/" + entry.relativePath;

    const auto resolution = audiocity::plugin::resolveImportedAssetManifest(
        manifest, { libraryRoot });
    auto caseSensitiveCacheRejected = true;
    const auto caseLibraryRoot = tempDirectory.getChildFile("CaseLibrary");
    const auto upperFolder = caseLibraryRoot.getChildFile("Bank");
    const auto lowerFolder = caseLibraryRoot.getChildFile("bank");
    const auto upperSample = upperFolder.getChildFile("Safe.wav");
    const auto caseOutsideRoot = tempDirectory.getChildFile("CaseOutside");
    if (!writeResolverFixtureFile(upperSample, "RIFF-safe")
        || !writeResolverFixtureFile(caseOutsideRoot.getChildFile("Linked.wav"), "RIFF-linked"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }
    if (caseOutsideRoot.createSymbolicLink(lowerFolder, false) && lowerFolder.isSymbolicLink())
    {
        audiocity::plugin::LibraryFileIndexData caseData;
        caseData.libraryRootPath = caseLibraryRoot.getFullPathName();
        audiocity::plugin::LibraryFileIndexEntry caseEntry;
        caseEntry.relativePath = "Bank/Safe.wav";
        caseEntry.sizeBytes = upperSample.getSize();
        caseEntry.modificationTimeMs = upperSample.getLastModificationTime().toMilliseconds();
        caseData.entries.push_back(caseEntry);
        caseEntry.relativePath = "bank/Linked.wav";
        caseData.entries.push_back(caseEntry);
        const audiocity::plugin::LibraryFileIndexStore caseStore(
            tempDirectory.getChildFile("case-sensitive-index.acli"));
        caseSensitiveCacheRejected = !caseStore.save(caseData);
    }

    const auto ok = scanned.empty()
        && indexLoadRejected
        && indexSaveRejected
        && !resolution.complete
        && resolution.resolvedFiles.empty()
        && caseSensitiveCacheRejected;
    tempDirectory.deleteRecursively();
    return ok;
}

enum class OfflineTestSuite
{
    engine,
    import,
    state,
    preset,
    count
};

struct OfflineTestCase
{
    const char* name = nullptr;
    bool (*run)() = nullptr;
    int failureCode = 1;
    OfflineTestSuite suite = OfflineTestSuite::engine;
};

const char* offlineTestSuiteName(const OfflineTestSuite suite)
{
    switch (suite)
    {
        case OfflineTestSuite::import: return "import";
        case OfflineTestSuite::state: return "state";
        case OfflineTestSuite::preset: return "preset";
        case OfflineTestSuite::engine:
        default: return "engine";
    }
}

template <std::size_t TestCount>
bool validateOfflineTestRegistry(const OfflineTestCase (&tests)[TestCount])
{
    std::set<std::string> names;
    std::array<std::size_t, static_cast<std::size_t>(OfflineTestSuite::count)> suiteCounts{};
    for (const auto& test : tests)
    {
        const auto suiteIndex = static_cast<std::size_t>(test.suite);
        if (test.name == nullptr
            || test.name[0] == '\0'
            || test.run == nullptr
            || suiteIndex >= suiteCounts.size()
            || !names.emplace(test.name).second)
        {
            return false;
        }
        ++suiteCounts[suiteIndex];
    }

    return std::all_of(suiteCounts.begin(), suiteCounts.end(), [](const auto count)
    {
        return count > 0;
    });
}

template <std::size_t TestCount>
int runOfflineTests(const OfflineTestCase (&tests)[TestCount],
                    const juce::String& requestedSuite,
                    const juce::String& requestedFilter,
                    const bool listOnly)
{
    auto selectedTests = 0;
    for (const auto& test : tests)
    {
        const auto suite = test.suite;
        if (requestedSuite.isNotEmpty() && requestedSuite != offlineTestSuiteName(suite))
            continue;
        if (requestedFilter.isNotEmpty()
            && !juce::String(test.name).containsIgnoreCase(requestedFilter))
            continue;

        ++selectedTests;
        if (listOnly)
        {
            std::printf("%s\t%s\n", offlineTestSuiteName(suite), test.name);
            continue;
        }
        if (test.run != nullptr && test.run())
            continue;

        std::fprintf(stderr, "Offline test failed: %s (exit %d)\n", test.name, test.failureCode);
        return test.failureCode;
    }

    if (selectedTests == 0)
    {
        std::fprintf(stderr, "No offline tests matched suite '%s' and filter '%s'.\n",
                     requestedSuite.toRawUTF8(), requestedFilter.toRawUTF8());
        return 64;
    }

    return 0;
}
}

int main(const int argc, char** argv)
{
#define AUDIOCITY_TEST(suite, fn, code) { #fn, fn, code, OfflineTestSuite::suite }
    const OfflineTestCase tests[] = {
        AUDIOCITY_TEST(engine, runDeterminismTest, 1),
        AUDIOCITY_TEST(engine, runRenderSegmentationMatchesSubBlockSequenceTest, 225),
        AUDIOCITY_TEST(engine, runStaticFilterSegmentationMatchesSubBlockSequenceTest, 226),
        AUDIOCITY_TEST(engine, runEditedSampleSegmentationMatchesSubBlockSequenceTest, 227),
        AUDIOCITY_TEST(engine, runStereoFilterChannelIsolationTest, 228),
        AUDIOCITY_TEST(engine, runDynamic24dBFilterSegmentationMatchesSubBlockSequenceTest, 229),
        AUDIOCITY_TEST(engine, runStereoFidelitySegmentationMatchesSubBlockSequenceTest, 230),
        AUDIOCITY_TEST(engine, runLowDepth24dBFilterSegmentationMatchesSubBlockSequenceTest, 231),
        AUDIOCITY_TEST(engine, runFilterCutoffHysteresisMatchesReferenceWithinToleranceTest, 232),
        AUDIOCITY_TEST(engine, runMonoFidelityLoopCrossfadeSegmentationMatchesSubBlockSequenceTest, 233),
        AUDIOCITY_TEST(engine, runProgramModelRangeAndZoneMatchingTest, 73),
        AUDIOCITY_TEST(engine, runProgramSnapshotBuildAndMatchTest, 75),
        AUDIOCITY_TEST(import, runProgramMappingRowsTest, 97),
        AUDIOCITY_TEST(import, runProgramMappingEditTest, 98),
        AUDIOCITY_TEST(import, runProgramMappingOverviewEditTest, 99),
        AUDIOCITY_TEST(import, runProgramMappingSampleWindowEditTest, 100),
        AUDIOCITY_TEST(import, runProgramMappingZoneOperationsTest, 102),
        AUDIOCITY_TEST(import, runProgramMappingChromaticRemapTest, 115),
        AUDIOCITY_TEST(import, runProgramMappingKeyRangeSpreadTest, 117),
        AUDIOCITY_TEST(import, runProgramSliceSplitAtSampleTest, 118),
        AUDIOCITY_TEST(import, runProgramSliceMergeAtBoundaryTest, 120),
        AUDIOCITY_TEST(import, runProgramMappingDeriveRootNotesTest, 119),
        AUDIOCITY_TEST(import, runProgramMappingMapToRootNotesTest, 121),
        AUDIOCITY_TEST(import, runProgramMappingAtomicBatchEditRollbackTest, 111),
        AUDIOCITY_TEST(import, runProgramMappingAtomicBatchDeleteRollbackTest, 112),
        AUDIOCITY_TEST(state, runProgramMappingStateRoundTripTest, 101),
        AUDIOCITY_TEST(state, runProgramMappingStructuralStateRoundTripTest, 103),
        AUDIOCITY_TEST(state, runImportedProgramStateSubtreeRoundTripTest, 106),
        AUDIOCITY_TEST(state, runImportedProgramStateLegacyReplayFallbackTest, 107),
        AUDIOCITY_TEST(import, runImportedProgramRestoreResultSuccessTest, 108),
        AUDIOCITY_TEST(import, runImportedProgramRestoreResultAtomicFailureTest, 109),
        AUDIOCITY_TEST(state, runImportedProgramDerivedStateSummaryTest, 110),
        AUDIOCITY_TEST(import, runSfzImportIncludeDefineDefaultPathTest, 87),
        AUDIOCITY_TEST(import, runSfzImportRoundRobinPlaybackTest, 88),
        AUDIOCITY_TEST(import, runSfzImportSeqModeRandomTest, 125),
        AUDIOCITY_TEST(import, runSfzImportSeqLengthPlaybackTest, 104),
        AUDIOCITY_TEST(import, runSfzImportReleaseTriggerPlaybackTest, 105),
        AUDIOCITY_TEST(import, runSfzImportLoopContinuousPlaybackTest, 129),
        AUDIOCITY_TEST(import, runSfzImportOneShotPlaybackTest, 131),
        AUDIOCITY_TEST(import, runSfzImportGainPanTunePlaybackTest, 133),
        AUDIOCITY_TEST(import, runSfzImportChokeGroupPlaybackTest, 135),
        AUDIOCITY_TEST(import, runSfzImportVelocityCrossfadePlaybackTest, 127),
        AUDIOCITY_TEST(import, runSfzImporterDiagnosticsTest, 89),
        AUDIOCITY_TEST(import, runSfzExporterRoundTripTest, 234),
        AUDIOCITY_TEST(import, runSfzExporterCreateFromScratchTest, 235),
        AUDIOCITY_TEST(import, runDecentSamplerImporterTest, 136),
        AUDIOCITY_TEST(import, runDecentSamplerExporterRoundTripTest, 249),
        AUDIOCITY_TEST(import, runSf2ImporterMinimalTest, 137),
        AUDIOCITY_TEST(import, runSf2ImporterPresetSelectionTest, 224),
        AUDIOCITY_TEST(import, runSf2ImporterRejectsShortListChunkTest, 243),
        AUDIOCITY_TEST(import, runSf2ImporterRejectsEmptyRequiredTablesTest, 244),
        AUDIOCITY_TEST(import, runBitwigMultisampleImporterTest, 138),
        AUDIOCITY_TEST(import, runArchiveRelativePathSafetyTest, 236),
        AUDIOCITY_TEST(import, runBitwigMultisampleRejectsUnsafeArchivePathTest, 237),
        AUDIOCITY_TEST(import, runArchiveImportersRejectOversizedManifestTest, 245),
        AUDIOCITY_TEST(import, runMpcKeygroupImporterTest, 140),
        AUDIOCITY_TEST(import, run1010MusicPresetImporterTest, 141),
        AUDIOCITY_TEST(import, runTalSamplerImporterTest, 142),
        AUDIOCITY_TEST(import, runTx16WxImporterTest, 143),
        AUDIOCITY_TEST(import, runKorgMultisampleImporterTest, 144),
        AUDIOCITY_TEST(import, runKorgMultisampleRejectsUnsafeArchivePathTest, 238),
        AUDIOCITY_TEST(import, runAbletonAdvImporterTest, 145),
        AUDIOCITY_TEST(import, runAbletonAdvRejectsOversizedXmlTest, 246),
        AUDIOCITY_TEST(import, runDistingExPresetImporterTest, 146),
        AUDIOCITY_TEST(import, runKorgKmpImporterTest, 147),
        AUDIOCITY_TEST(import, runKorgKmpImporterCapsRlp1EntriesTest, 241),
        AUDIOCITY_TEST(import, runLogicExs24ImporterRejectsOversizedChunkTest, 242),
        AUDIOCITY_TEST(import, runLogicExs24ImporterTest, 148),
        AUDIOCITY_TEST(import, runNnxtImporterDiagnosticTest, 149),
        AUDIOCITY_TEST(import, runMalformedImporterCorpusTest, 240),
        AUDIOCITY_TEST(import, runLegacyNkiImportNcwViaConverterTest, 222),
        AUDIOCITY_TEST(import, runLegacyNkiProbeDetectsEncryptedPatchTest, 139),
        AUDIOCITY_TEST(import, runLegacyNkiProbeDetectsDiscreteSampleReferencesTest, 122),
        AUDIOCITY_TEST(import, runLegacyNkiProbeRejectsContainerFormatsTest, 123),
        AUDIOCITY_TEST(import, runLegacyNkiProbeResolvesParentSamplesFolderTest, 124),
        AUDIOCITY_TEST(state, runLegacyNkiProbeEnumeratesZoneMetadataTest, 126),
        AUDIOCITY_TEST(import, runLegacyNkiImportTranslatesLegacyZonesTest, 128),
        AUDIOCITY_TEST(import, runLegacyNkiImportSampleWindowAndLoopTest, 130),
        AUDIOCITY_TEST(import, runLegacyNkiImportGainPanTuneTest, 132),
        AUDIOCITY_TEST(import, runLegacyNkiImportTriggerModesTest, 134),
        AUDIOCITY_TEST(engine, runSameOffsetMidiEventOrderTest, 74),
        AUDIOCITY_TEST(engine, runEngineProgramSnapshotZoneSelectionTest, 76),
        AUDIOCITY_TEST(engine, runEngineProgramSampleAssetBindingTest, 77),
        AUDIOCITY_TEST(engine, runEngineProgramStereoAssetPlaybackTest, 81),
        AUDIOCITY_TEST(engine, runEngineProgramRoundRobinZoneSelectionTest, 82),
        AUDIOCITY_TEST(engine, runEngineProgramLayeredZonePlaybackTest, 90),
        AUDIOCITY_TEST(engine, runEngineProgramVelocityFadeInTest, 91),
        AUDIOCITY_TEST(engine, runEngineProgramCycleRandomRoundRobinZoneSelectionTest, 85),
        AUDIOCITY_TEST(engine, runEngineProgramChokeGroupTest, 83),
        AUDIOCITY_TEST(engine, runEngineProgramZoneAndGroupPanTest, 84),
        AUDIOCITY_TEST(engine, runEngineProgramZoneTriggerModeTest, 86),
        AUDIOCITY_TEST(engine, runEngineProgramZoneGainAndTuneTest, 78),
        AUDIOCITY_TEST(engine, runEngineProgramZoneSampleWindowTest, 79),
        AUDIOCITY_TEST(engine, runEngineProgramZoneLoopModeTest, 80),
        AUDIOCITY_TEST(engine, runVoiceStealingEdgeCaseTest, 2),
        AUDIOCITY_TEST(engine, runPolyphonyLimitControlTest, 52),
        AUDIOCITY_TEST(engine, runPlaybackModesTest, 3),
        AUDIOCITY_TEST(engine, runLoopMarkersTest, 4),
        AUDIOCITY_TEST(engine, runLoadSampleResetsPlaybackAndLoopRangesTest, 44),
        AUDIOCITY_TEST(engine, runLoadSampleClearsProgramSnapshotTest, 92),
        AUDIOCITY_TEST(engine, runLoadSampleResetsEnvelopeAndFilterDefaultsTest, 45),
        AUDIOCITY_TEST(engine, runLoadNcwSampleViaConverterTest, 223),
        AUDIOCITY_TEST(engine, runLoadNcwSampleViaConverterQuotesShellMetacharactersTest, 239),
        AUDIOCITY_TEST(engine, runFilterModulationAmountsAreBipolarTest, 70),
        AUDIOCITY_TEST(state, runEmbeddedLoopMetadataLoadsWithoutRootNoteTest, 64),
        AUDIOCITY_TEST(engine, runRexRuntimeFallbackSmokeTest, 62),
        AUDIOCITY_TEST(import, runRexSliceProgramBuildTest, 114),
        AUDIOCITY_TEST(import, runTransientSliceProgramBuildTest, 116),
        AUDIOCITY_TEST(state, runCcLearnDialUserClearCallbackTest, 63),
        AUDIOCITY_TEST(engine, runGeneratedCyclePitchInvariantAcrossSampleCountsTest, 46),
        AUDIOCITY_TEST(engine, runDisplayMinMaxPreservesPolarityTest, 48),
        AUDIOCITY_TEST(state, runLoadedSampleMetadataForGeneratedDataTest, 69),
        AUDIOCITY_TEST(engine, runParameterIdSafetyTest, 72),
        AUDIOCITY_TEST(state, runEditorFilterLfoPushPreservesAdvancedControlsTest, 180),
        AUDIOCITY_TEST(state, runEditorModulationPanelExtractionTest, 181),
        AUDIOCITY_TEST(state, runBackgroundImportWorkerPublishContractTest, 247),
        AUDIOCITY_TEST(state, runOwnedJobWorkerReplacementIsSerialTest, 255),
        AUDIOCITY_TEST(state, runOwnedJobWorkerDestructorCancelsAndJoinsTest, 256),
        AUDIOCITY_TEST(engine, runCancellableAudioReadStopsAtChunkBoundaryTest, 257),
        AUDIOCITY_TEST(state, runAboutPageExtractionContractTest, 248),
        AUDIOCITY_TEST(state, runGeneratePageExtractionContractTest, 251),
        AUDIOCITY_TEST(state, runCapturePageExtractionContractTest, 252),
        AUDIOCITY_TEST(import, runDecentSamplerSaveRoutingContractTest, 250),
        AUDIOCITY_TEST(state, runEditorSampleEditControlsTest, 5),
        AUDIOCITY_TEST(engine, runPolyphonicDifferentNotesLayerWhenMonoOffTest, 43),
        AUDIOCITY_TEST(engine, runMonoLegatoUsesSingleVoiceTest, 7),
        AUDIOCITY_TEST(engine, runPolyphonicSameNoteReleaseTest, 24),
        AUDIOCITY_TEST(engine, runDenseLoopModeOverflowDoesNotStickNotesTest, 55),
        AUDIOCITY_TEST(engine, runQueueSaturatedByPitchBendStillReleasesNoteOffTest, 56),
        AUDIOCITY_TEST(engine, runGlideChangesLegatoTransitionTest, 8),
        AUDIOCITY_TEST(engine, runPreloadSegmentationDeterminismTest, 9),
        AUDIOCITY_TEST(engine, runRuntimePreloadChangeStabilityTest, 10),
        AUDIOCITY_TEST(engine, runRuntimeSampleReloadStabilityTest, 11),
        AUDIOCITY_TEST(engine, runLoopModeRuntimePreloadChangeStabilityTest, 12),
        AUDIOCITY_TEST(engine, runSegmentRebuildCounterTest, 13),
        AUDIOCITY_TEST(engine, runProgramPreloadMetricsAndRebuildTest, 14),
        AUDIOCITY_TEST(engine, runSingleSampleFileStreamingPreloadMetricsTest, 170),
        AUDIOCITY_TEST(state, runProgramStreamPrimingAndCacheMetricsTest, 171),
        AUDIOCITY_TEST(engine, runProgramStreamLookaheadPrimingTest, 172),
        AUDIOCITY_TEST(state, runDiskStreamCacheStaysResponsiveUnderContentionTest, 253),
        AUDIOCITY_TEST(engine, runRtSnapshotCellProtectsHeldReaderAcrossPublishesTest, 254),
        AUDIOCITY_TEST(import, runProgramMappingCreateZoneTest, 176),
        AUDIOCITY_TEST(engine, runQualityTierDifferenceTest, 15),
        AUDIOCITY_TEST(engine, runQualityTierDeterminismTest, 16),
        AUDIOCITY_TEST(engine, runCpuQualityEnergyDriftSmokeTest, 17),
        AUDIOCITY_TEST(engine, runRuntimeQualitySwitchSmokeTest, 18),
        AUDIOCITY_TEST(state, runEditorUndoHistoryMixedOrderTest, 173),
        AUDIOCITY_TEST(state, runEditorUndoHistoryCoalesceAndLabelsTest, 174),
        AUDIOCITY_TEST(state, runEditorUndoHistoryDuplicateAndSplitTest, 175),
        AUDIOCITY_TEST(state, runEditorUndoHistoryCreateZoneTest, 177),
        AUDIOCITY_TEST(state, runSettingsUndoHistoryTest, 18),
        AUDIOCITY_TEST(state, runSettingsUndoHistoryCapacityTest, 19),
        AUDIOCITY_TEST(state, runSettingsUndoHistoryCoalesceTest, 20),
        AUDIOCITY_TEST(state, runSettingsUndoHistoryLabelsTest, 21),
        AUDIOCITY_TEST(state, runSettingsUndoHistoryEditorStateTest, 22),
        AUDIOCITY_TEST(engine, runSettingsSnapshotCaptureFieldsAffectEqualityTest, 65),
        AUDIOCITY_TEST(state, runSettingsUndoHistoryTracksCaptureSettingsTest, 66),
        AUDIOCITY_TEST(preset, runPresetXmlRoundTripWithEmbeddedSampleDataTest, 67),
        AUDIOCITY_TEST(preset, runPresetXmlRejectsInvalidPayloadTest, 68),
        AUDIOCITY_TEST(preset, runEmbeddedSamplePresetRoundTripTest, 220),
        AUDIOCITY_TEST(preset, runFactoryPresetBankDiscoveryTest, 221),
        AUDIOCITY_TEST(state, runLibraryMetadataFavoritesAndRecentsTest, 93),
        AUDIOCITY_TEST(state, runLibraryMetadataValueTreeRoundTripTest, 94),
        AUDIOCITY_TEST(state, runLibraryMetadataBookmarksTest, 95),
        AUDIOCITY_TEST(import, runImportFormatRegistryConsistencyTest, 275),
        AUDIOCITY_TEST(state, runLibraryFileIndexScanTest, 96),
        AUDIOCITY_TEST(state, runLibraryFileIndexPersistentStoreTest, 271),
        AUDIOCITY_TEST(state, runLibraryFileIndex50kScanSearchIntegrationTest, 272),
        AUDIOCITY_TEST(state, runLibraryFileIndexExactCaseIdentityTest, 279),
        AUDIOCITY_TEST(state, runPeakPreviewCacheRoundTripTest, 70),
        AUDIOCITY_TEST(state, runPeakPreviewCacheResetClearsFileTest, 71),
        AUDIOCITY_TEST(state, runPeakPreviewCacheFreshnessPartitionAndCorruptionTest, 269),
        AUDIOCITY_TEST(state, runPeakPreviewCacheLruAndPeakBoundsTest, 270),
        AUDIOCITY_TEST(state, runPlayerPadStateUtilityTest, 23),
        AUDIOCITY_TEST(state, runSampleBrowserTooltipFormattingTest, 236),
        AUDIOCITY_TEST(engine, runFilterModeDifferenceTest, 25),
        AUDIOCITY_TEST(engine, runFilterModulationDifferenceTest, 26),
        AUDIOCITY_TEST(engine, runFilterKeytrackPolarityTest, 30),
        AUDIOCITY_TEST(engine, runFilterLfoDifferenceTest, 31),
        AUDIOCITY_TEST(engine, runPitchLfoVibratoSettingsTest, 53),
        AUDIOCITY_TEST(engine, runFilterLfoShapeDifferenceTest, 32),
        AUDIOCITY_TEST(engine, runAmpLfoTremoloSettingsTest, 52),
        AUDIOCITY_TEST(engine, runFilterLfoTempoSyncSettingsTest, 33),
        AUDIOCITY_TEST(engine, runFilterLfoRetriggerDifferenceTest, 34),
        AUDIOCITY_TEST(engine, runFilterLfoStartPhaseDifferenceTest, 35),
        AUDIOCITY_TEST(engine, runFilterLfoFadeInDifferenceTest, 36),
        AUDIOCITY_TEST(engine, runFilterLfoStartRandomDifferenceTest, 37),
        AUDIOCITY_TEST(engine, runFilterLfoAmountKeytrackingDifferenceTest, 38),
        AUDIOCITY_TEST(engine, runFilterLfoRateKeytrackingDifferenceTest, 39),
        AUDIOCITY_TEST(engine, runFilterLfoRateKeytrackInTempoSyncToggleDifferenceTest, 40),
        AUDIOCITY_TEST(engine, runFilterLfoKeytrackCurveDifferenceTest, 41),
        AUDIOCITY_TEST(engine, runFilterLfoUnipolarDifferenceTest, 42),
        AUDIOCITY_TEST(engine, runUltraQualityDifferenceTest, 27),
        AUDIOCITY_TEST(engine, runUltraQualitySpectralTonePreservationTest, 178),
        AUDIOCITY_TEST(engine, runReverbMixTailTest, 28),
        AUDIOCITY_TEST(engine, runDelayMixTailTest, 57),
        AUDIOCITY_TEST(engine, runDelayTempoSyncRespondsToTempoTest, 58),
        AUDIOCITY_TEST(engine, runDcOffsetFilterRemovesBiasTest, 59),
        AUDIOCITY_TEST(engine, runMasterVolumeGainTest, 47),
        AUDIOCITY_TEST(engine, runPanBalanceTest, 51),
        AUDIOCITY_TEST(engine, runAutopanStereoMotionTest, 60),
        AUDIOCITY_TEST(engine, runSaturationDriveAndModeTest, 61),
        AUDIOCITY_TEST(engine, runTuneCoarseFinePitchShiftTest, 49),
        AUDIOCITY_TEST(engine, runPitchBendRangeAndRealtimeModulationTest, 50),
        AUDIOCITY_TEST(engine, runModulationRoutingRealtimeTest, 179),
        AUDIOCITY_TEST(state, runVoicePlaybackStateSnapshotTest, 54),
        AUDIOCITY_TEST(engine, runLoopCrossfadeSmoothsBoundaryTest, 29),
        AUDIOCITY_TEST(import, runImportedProgramStoreEditPublishesCommittedProgramTest, 241),
        AUDIOCITY_TEST(state, runImportedProgramStoreRejectedEditLeavesStateUntouchedTest, 242),
        AUDIOCITY_TEST(import, runImportedProgramStoreAppendsSampleDataOnCommitTest, 243),
        AUDIOCITY_TEST(import, runImportedProgramStoreRejectsEditWithoutLoadedProgramTest, 244),
        AUDIOCITY_TEST(import, runImportedProgramStoreLoadProgramPublishesNothingTest, 245),
        AUDIOCITY_TEST(import, runImportedProgramStoreZoneEditSkipsSampleDataTest, 246),
        AUDIOCITY_TEST(state, runImportedProgramStoreFallsBackWhenMetadataPublishRefusedTest, 247),
        AUDIOCITY_TEST(state, runImportedProgramStoreFailedPublishLeavesStateUntouchedTest, 274),
        AUDIOCITY_TEST(state, runProgramMetadataUpdateReusesLoadedAudioTest, 248),
        AUDIOCITY_TEST(engine, runConcurrentProgramSnapshotReclamationTest, 282),
        AUDIOCITY_TEST(engine, runNestedSnapshotReaderReclamationTest, 283),
        AUDIOCITY_TEST(engine, runProgramPublishRejectsIncompleteReferencedAudioTest, 280),
        AUDIOCITY_TEST(engine, runProgramCapacityBoundaryAndRejectionTest, 258),
        AUDIOCITY_TEST(engine, runScalableProgramSnapshotAboveLegacyZoneLimitTest, 259),
        AUDIOCITY_TEST(engine, runPreloadNoOpRebuildGuardTest, 260),
        AUDIOCITY_TEST(engine, runPendingEventLinearDispatchTest, 261),
        AUDIOCITY_TEST(engine, runPendingEventSafetyOverflowRecoveryTest, 262),
        AUDIOCITY_TEST(engine, runPendingEventSafetyDisplacesContinuousControlTest, 276),
        AUDIOCITY_TEST(engine, runMidiAllNotesOffSafetyEventTest, 263),
        AUDIOCITY_TEST(engine, runPanicSilencesAudioImmediatelyTest, 284),
        AUDIOCITY_TEST(state, runAudioStateCodecRoundTripTest, 264),
        AUDIOCITY_TEST(state, runAudioStateCodecRejectsCorruptionAtomicallyTest, 265),
        AUDIOCITY_TEST(state, runAudioStateCodecEnforcesLimitsTest, 266),
        AUDIOCITY_TEST(state, runAudioStateCodecRejectsForgedSizesAndTrailingDataTest, 273),
        AUDIOCITY_TEST(state, runImportedAssetResolverMovedFolderAndSafetyTest, 267),
        AUDIOCITY_TEST(state, runImportedAssetResolverBoundedUniquenessTest, 277),
        AUDIOCITY_TEST(state, runImportedAssetResolverLegacyStateTest, 268),
        AUDIOCITY_TEST(state, runImportedAssetResolverHashlessMtimeTest, 281),
        AUDIOCITY_TEST(state, runLibraryFileIndexAndResolverLinkTraversalRejectionTest, 278),
    };
#undef AUDIOCITY_TEST

    if (!validateOfflineTestRegistry(tests))
    {
        std::fprintf(stderr, "Offline test registry has an invalid or duplicate suite assignment.\n");
        return 65;
    }

    juce::String requestedSuite;
    juce::String requestedFilter;
    auto listOnly = false;
    for (auto argument = 1; argument < argc; ++argument)
    {
        const juce::String token(argv[argument]);
        if (token == "--list")
        {
            listOnly = true;
        }
        else if (token == "--suite" && argument + 1 < argc)
        {
            requestedSuite = juce::String(argv[++argument]).toLowerCase();
        }
        else if (token == "--filter" && argument + 1 < argc)
        {
            requestedFilter = argv[++argument];
        }
        else
        {
            std::fprintf(stderr, "Usage: audiocity_offline_tests [--suite engine|import|state|preset] [--filter text] [--list]\n");
            return 64;
        }
    }

    if (requestedSuite.isNotEmpty()
        && requestedSuite != "engine"
        && requestedSuite != "import"
        && requestedSuite != "state"
        && requestedSuite != "preset")
    {
        std::fprintf(stderr, "Unknown offline test suite: %s\n", requestedSuite.toRawUTF8());
        return 64;
    }

    return runOfflineTests(tests, requestedSuite, requestedFilter, listOnly);
}
