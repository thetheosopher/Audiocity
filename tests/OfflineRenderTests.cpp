#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "../src/engine/EngineCore.h"
#include "../src/engine/sfz/SfzImport.h"

#include <cmath>

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

    return 0;
}
