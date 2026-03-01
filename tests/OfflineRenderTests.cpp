#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "../src/engine/EngineCore.h"
#include "../src/engine/SettingsUndoHistory.h"
#include "../src/plugin/PlayerPadState.h"

#include <cmath>
#include <limits>

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

bool runChokeGroupStopsPreviousVoiceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setChokeGroup(1);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 16);

    juce::AudioBuffer<float> block(channels, blockSize);
    engine.render(block, midi);

    return engine.activeVoiceCount() == 1 && !engine.isNoteActive(60) && engine.isNoteActive(64);
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

    if (engine.activeVoiceCount() < 2)
        return false;

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);

    midi.clear();
    for (int i = 0; i < 40; ++i)
        engine.render(block, midi);

    if (engine.activeVoiceCount() != 1)
        return false;

    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);

    midi.clear();
    for (int i = 0; i < 120; ++i)
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
    const audiocity::engine::SettingsSnapshot initial{ 32768, 1, 0, false, false, 0.0f, 0 };
    const audiocity::engine::SettingsSnapshot changedPreload{ 4096, 1, 0, false, false, 0.0f, 0 };
    const audiocity::engine::SettingsSnapshot changedTierAndMapping{ 4096, 0, 1, true, true, 0.04f, 2 };

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

    const audiocity::engine::SettingsSnapshot s0{ 32768, 1, 0, false, false, 0.0f, 0 };
    const audiocity::engine::SettingsSnapshot s1{ 16384, 1, 0, false, false, 0.0f, 0 };
    const audiocity::engine::SettingsSnapshot s2{ 8192, 1, 1, false, false, 0.0f, 0 };
    const audiocity::engine::SettingsSnapshot s3{ 4096, 0, 2, true, false, 0.01f, 1 };

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

    const audiocity::engine::SettingsSnapshot a{ 32768, 1, 0, false, false, 0.00f, 0 };
    const audiocity::engine::SettingsSnapshot b{ 30000, 1, 0, false, false, 0.00f, 0 };
    const audiocity::engine::SettingsSnapshot c{ 25000, 1, 0, false, false, 0.00f, 0 };
    const audiocity::engine::SettingsSnapshot d{ 22000, 1, 0, false, false, 0.00f, 0 };

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

    const audiocity::engine::SettingsSnapshot a{ 32768, 1, 0, false, false, 0.00f, 0 };
    const audiocity::engine::SettingsSnapshot b{ 4096, 1, 0, false, false, 0.00f, 0 };

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
}

int main()
{
    if (!runDeterminismTest())
        return 1;

    if (!runVoiceStealingEdgeCaseTest())
        return 2;

    if (!runPlaybackModesTest())
        return 3;

    if (!runLoopMarkersTest())
        return 4;

    if (!runEditorSampleEditControlsTest())
        return 5;

    if (!runChokeGroupStopsPreviousVoiceTest())
        return 6;

    if (!runMonoLegatoUsesSingleVoiceTest())
        return 7;

    if (!runPolyphonicSameNoteReleaseTest())
        return 24;

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

    if (!runFilterLfoShapeDifferenceTest())
        return 32;

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

    if (!runLoopCrossfadeSmoothsBoundaryTest())
        return 29;

    return 0;
}
