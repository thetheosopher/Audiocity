#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "../src/engine/EngineCore.h"
#include "../src/engine/SettingsUndoHistory.h"
#include "../src/plugin/PlayerPadState.h"

#include <cmath>
#include <limits>
#include <utility>

namespace
{
juce::File fixtureFile(const juce::String& relativePath)
{
    return juce::File(AUDIOCITY_SOURCE_DIR).getChildFile(relativePath);
}

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

    if (freq64 <= 0.0f || freq1024 <= 0.0f)
        return false;

    const auto targetError64 = std::abs(freq64 - static_cast<float>(targetHz));
    const auto targetError1024 = std::abs(freq1024 - static_cast<float>(targetHz));
    const auto crossCountDelta = std::abs(freq64 - freq1024);

    return targetError64 < 1.5f && targetError1024 < 1.5f && crossCountDelta < 0.8f;
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
        && renderPairForTier(audiocity::engine::EngineCore::QualityTier::fidelity);
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
    constexpr int blocks = 12;
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
            engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::cpu);
        if (block == 8)
            engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::fidelity);

        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.9f), 0);
        if (block == 10)
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

    auto renderMeanWithSettings = [&](const bool enabled, const float cutoffHz)
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
                captured.push_back(block.getSample(0, sample));
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

    const auto meanBypassed = renderMeanWithSettings(false, 10.0f);
    const auto meanFiltered = renderMeanWithSettings(true, 10.0f);

    return std::abs(meanBypassed) > 0.10f
        && std::abs(meanFiltered) < 0.02f;
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
}

int main()
{
    if (!runDeterminismTest())
        return 1;

    if (!runVoiceStealingEdgeCaseTest())
        return 2;

    if (!runPolyphonyLimitControlTest())
        return 52;

    if (!runPlaybackModesTest())
        return 3;

    if (!runLoopMarkersTest())
        return 4;

    if (!runLoadSampleResetsPlaybackAndLoopRangesTest())
        return 44;

    if (!runLoadSampleResetsEnvelopeAndFilterDefaultsTest())
        return 45;

    if (!runGeneratedCyclePitchInvariantAcrossSampleCountsTest())
        return 46;

    if (!runDisplayMinMaxPreservesPolarityTest())
        return 48;

    if (!runEditorSampleEditControlsTest())
        return 5;

    if (!runPolyphonicDifferentNotesLayerWhenMonoOffTest())
        return 43;

    if (!runMonoLegatoUsesSingleVoiceTest())
        return 7;

    if (!runPolyphonicSameNoteReleaseTest())
        return 24;

    if (!runDenseLoopModeOverflowDoesNotStickNotesTest())
        return 55;

    if (!runQueueSaturatedByPitchBendStillReleasesNoteOffTest())
        return 56;

    if (!runGlideChangesLegatoTransitionTest())
        return 8;

    if (!runPreloadSegmentationDeterminismTest())
        return 9;

    if (!runRuntimePreloadChangeStabilityTest())
        return 10;

    if (!runRuntimeSampleReloadStabilityTest())
        return 11;

    if (!runLoopModeRuntimePreloadChangeStabilityTest())
        return 12;

    if (!runSegmentRebuildCounterTest())
        return 13;

    if (!runQualityTierDifferenceTest())
        return 14;

    if (!runQualityTierDeterminismTest())
        return 15;

    if (!runCpuQualityEnergyDriftSmokeTest())
        return 16;

    if (!runRuntimeQualitySwitchSmokeTest())
        return 17;

    if (!runSettingsUndoHistoryTest())
        return 18;

    if (!runSettingsUndoHistoryCapacityTest())
        return 19;

    if (!runSettingsUndoHistoryCoalesceTest())
        return 20;

    if (!runSettingsUndoHistoryLabelsTest())
        return 21;

    if (!runSettingsUndoHistoryEditorStateTest())
        return 22;

    if (!runPlayerPadStateUtilityTest())
        return 23;

    if (!runFilterModeDifferenceTest())
        return 25;

    if (!runFilterModulationDifferenceTest())
        return 26;

    if (!runFilterKeytrackPolarityTest())
        return 30;

    if (!runFilterLfoDifferenceTest())
        return 31;

    if (!runPitchLfoVibratoSettingsTest())
        return 53;

    if (!runFilterLfoShapeDifferenceTest())
        return 32;

    if (!runAmpLfoTremoloSettingsTest())
        return 52;

    if (!runFilterLfoTempoSyncSettingsTest())
        return 33;

    if (!runFilterLfoRetriggerDifferenceTest())
        return 34;

    if (!runFilterLfoStartPhaseDifferenceTest())
        return 35;

    if (!runFilterLfoFadeInDifferenceTest())
        return 36;

    if (!runFilterLfoStartRandomDifferenceTest())
        return 37;

    if (!runFilterLfoAmountKeytrackingDifferenceTest())
        return 38;

    if (!runFilterLfoRateKeytrackingDifferenceTest())
        return 39;

    if (!runFilterLfoRateKeytrackInTempoSyncToggleDifferenceTest())
        return 40;

    if (!runFilterLfoKeytrackCurveDifferenceTest())
        return 41;

    if (!runFilterLfoUnipolarDifferenceTest())
        return 42;

    if (!runUltraQualityDifferenceTest())
        return 27;

    if (!runReverbMixTailTest())
        return 28;

    if (!runDelayMixTailTest())
        return 57;

    if (!runDelayTempoSyncRespondsToTempoTest())
        return 58;

    if (!runDcOffsetFilterRemovesBiasTest())
        return 59;

    if (!runMasterVolumeGainTest())
        return 47;

    if (!runPanBalanceTest())
        return 51;

    if (!runAutopanStereoMotionTest())
        return 60;

    if (!runSaturationDriveAndModeTest())
        return 61;

    if (!runTuneCoarseFinePitchShiftTest())
        return 49;

    if (!runPitchBendRangeAndRealtimeModulationTest())
        return 50;

    if (!runVoicePlaybackStateSnapshotTest())
        return 54;

    if (!runLoopCrossfadeSmoothsBoundaryTest())
        return 29;

    return 0;
}
