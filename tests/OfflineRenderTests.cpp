#include <juce_audio_basics/juce_audio_basics.h>

#include "../src/engine/EngineCore.h"

namespace
{
bool buffersAreEqual(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;

    for (int channel = 0; channel < a.getNumChannels(); ++channel)
    {
        const auto* aData = a.getReadPointer(channel);
        const auto* bData = b.getReadPointer(channel);

        for (int sample = 0; sample < a.getNumSamples(); ++sample)
        {
            if (aData[sample] != bData[sample])
                return false;
        }
    }

    return true;
}
}

int main()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    juce::AudioBuffer<float> first(channels, blockSize);
    juce::AudioBuffer<float> second(channels, blockSize);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 96);

    engine.render(first, midi);
    engine.render(second, midi);

    if (!buffersAreEqual(first, second))
        return 1;

    if (engine.activeVoiceCount() != 0)
        return 2;

    return 0;
}
