#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "../src/engine/EngineCore.h"
#include "../src/engine/ZoneSelector.h"
#include "../src/engine/sfz/SfzImport.h"

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

bool runSfzNestedIncludeAndMappingTest()
{
    audiocity::engine::sfz::Importer importer;
    const auto program = importer.importFromFile(fixtureFile("tests/fixtures/sfz/main_nested.sfz"));

    if (program.zones.size() != 3)
        return false;

    const auto& z0 = program.zones[0];
    const auto& z1 = program.zones[1];
    const auto& z2 = program.zones[2];

    if (z0.sourceSample != "loop.wav" || z0.loopMode != "loop_sustain" || z0.loopStart != 8 || z0.loopEnd != 88)
        return false;

    if (z1.sourceSample != "kick.wav" || z1.lowVelocity != 0 || z1.highVelocity != 63 || z1.rrGroup != 1)
        return false;

    if (z2.sourceSample != "snare.wav" || z2.lowVelocity != 64 || z2.highVelocity != 127 || z2.rrGroup != 2)
        return false;

    if (!z2.resolvedSamplePath.contains("samples\\B\\snare.wav")
        && !z2.resolvedSamplePath.contains("samples/B/snare.wav"))
        return false;

    return true;
}

bool runSfzDiagnosticsTest()
{
    audiocity::engine::sfz::Importer importer;

    const auto cycleProgram = importer.importFromFile(fixtureFile("tests/fixtures/sfz/cycle_a.sfz"));
    const auto missingProgram = importer.importFromFile(fixtureFile("tests/fixtures/sfz/missing_and_unsupported.sfz"));

    bool foundCycle = false;
    bool foundMissing = false;
    bool foundUnsupported = false;

    for (const auto& diagnostic : cycleProgram.diagnostics)
    {
        if (diagnostic.message.containsIgnoreCase("cycle"))
            foundCycle = true;
    }

    for (const auto& diagnostic : missingProgram.diagnostics)
    {
        if (diagnostic.message.containsIgnoreCase("Missing sample file"))
            foundMissing = true;

        if (diagnostic.message.containsIgnoreCase("Unsupported opcode"))
            foundUnsupported = true;
    }

    return foundCycle && foundMissing && foundUnsupported;
}

bool runZoneSelectorOrderedModeTest()
{
    audiocity::engine::ZoneSelector selector;

    std::vector<audiocity::engine::sfz::Zone> zones;
    zones.push_back({ "z0.wav", "", 36, 36, -1, 36, 0, 63, 0, 0, 0, 0, 0, "no_loop", 1 });
    zones.push_back({ "z1.wav", "", 36, 36, -1, 36, 0, 63, 0, 0, 0, 0, 0, "no_loop", 2 });
    zones.push_back({ "z2.wav", "", 36, 36, -1, 36, 64, 127, 0, 0, 0, 0, 0, "no_loop", 1 });

    selector.setZones(zones);
    selector.setRoundRobinMode(audiocity::engine::ZoneSelector::RoundRobinMode::ordered);

    if (selector.selectZoneIndex(36, 20) != 0)
        return false;

    if (selector.selectZoneIndex(36, 20) != 1)
        return false;

    if (selector.selectZoneIndex(36, 90) != 2)
        return false;

    if (selector.selectZoneIndex(50, 90) != -1)
        return false;

    return true;
}

bool runZoneSelectorRandomModeTest()
{
    audiocity::engine::ZoneSelector selectorA;
    audiocity::engine::ZoneSelector selectorB;

    std::vector<audiocity::engine::sfz::Zone> zones;
    zones.push_back({ "a.wav", "", 60, 60, -1, 60, 0, 127, 0, 0, 0, 0, 0, "no_loop", 1 });
    zones.push_back({ "b.wav", "", 60, 60, -1, 60, 0, 127, 0, 0, 0, 0, 0, "no_loop", 2 });
    zones.push_back({ "c.wav", "", 60, 60, -1, 60, 0, 127, 0, 0, 0, 0, 0, "no_loop", 3 });

    selectorA.setZones(zones);
    selectorB.setZones(zones);

    selectorA.setRoundRobinMode(audiocity::engine::ZoneSelector::RoundRobinMode::random);
    selectorB.setRoundRobinMode(audiocity::engine::ZoneSelector::RoundRobinMode::random);

    for (int i = 0; i < 8; ++i)
    {
        const auto a = selectorA.selectZoneIndex(60, 100);
        const auto b = selectorB.selectZoneIndex(60, 100);

        if (a != b)
            return false;

        if (a < 0 || a > 2)
            return false;
    }

    return true;
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
        engine.setSfzLoopMode(audiocity::engine::EngineCore::SfzLoopMode::loopSustain);
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

bool runLoopMarkersAndModesTest()
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

    // Loop markers should keep playback in the [16, 31] region when looping continuously.
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setAmpEnvelope(slowRelease);
        engine.setSampleData(shaped, sampleRate, 60);
        engine.setLoopPoints(16, 31);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setSfzLoopMode(audiocity::engine::EngineCore::SfzLoopMode::loopContinuous);

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

    // loop_sustain should react to note-off and release; loop_continuous should ignore note-off.
    {
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> block(channels, blockSize);

        audiocity::engine::EngineCore sustain;
        sustain.prepare(sampleRate, blockSize, channels);
        sustain.setAmpEnvelope(slowRelease);
        sustain.setSampleData(shaped, sampleRate, 60);
        sustain.setLoopPoints(16, 31);
        sustain.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        sustain.setSfzLoopMode(audiocity::engine::EngineCore::SfzLoopMode::loopSustain);

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        sustain.render(block, midi);
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        sustain.render(block, midi);
        midi.clear();
        sustain.render(block, midi);
        const auto sustainEnergy = blockEnergy(block);

        audiocity::engine::EngineCore continuous;
        continuous.prepare(sampleRate, blockSize, channels);
        continuous.setAmpEnvelope(slowRelease);
        continuous.setSampleData(shaped, sampleRate, 60);
        continuous.setLoopPoints(16, 31);
        continuous.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        continuous.setSfzLoopMode(audiocity::engine::EngineCore::SfzLoopMode::loopContinuous);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        continuous.render(block, midi);
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        continuous.render(block, midi);
        midi.clear();
        continuous.render(block, midi);
        const auto continuousEnergy = blockEnergy(block);

        if (!(continuousEnergy > sustainEnergy * 1.5f))
            return false;
    }

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
    reference.setSfzLoopMode(audiocity::engine::EngineCore::SfzLoopMode::loopContinuous);
    reference.setPreloadSamples(2048);
    reference.setSampleData(shaped, sampleRate, 60);
    reference.setLoopPoints(20, 55);

    audiocity::engine::EngineCore changed;
    changed.prepare(sampleRate, blockSize, channels);
    changed.setAmpEnvelope(slowRelease);
    changed.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
    changed.setSfzLoopMode(audiocity::engine::EngineCore::SfzLoopMode::loopContinuous);
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
        engine.setSfzLoopMode(audiocity::engine::EngineCore::SfzLoopMode::loopContinuous);
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
}

int main()
{
    if (!runDeterminismTest())
        return 1;

    if (!runVoiceStealingEdgeCaseTest())
        return 2;

    if (!runSfzNestedIncludeAndMappingTest())
        return 3;

    if (!runSfzDiagnosticsTest())
        return 4;

    if (!runZoneSelectorOrderedModeTest())
        return 5;

    if (!runZoneSelectorRandomModeTest())
        return 6;

    if (!runPlaybackModesTest())
        return 7;

    if (!runLoopMarkersAndModesTest())
        return 8;

    if (!runChokeGroupStopsPreviousVoiceTest())
        return 9;

    if (!runMonoLegatoUsesSingleVoiceTest())
        return 10;

    if (!runGlideChangesLegatoTransitionTest())
        return 11;

    if (!runPreloadSegmentationDeterminismTest())
        return 12;

    if (!runRuntimePreloadChangeStabilityTest())
        return 13;

    if (!runLoopModeRuntimePreloadChangeStabilityTest())
        return 14;

    if (!runSegmentRebuildCounterTest())
        return 15;

    if (!runQualityTierDifferenceTest())
        return 16;

    if (!runQualityTierDeterminismTest())
        return 17;

    if (!runCpuQualityEnergyDriftSmokeTest())
        return 18;

    return 0;
}
