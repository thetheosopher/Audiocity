#include "PluginProcessor.h"

#include "PluginEditor.h"

AudiocityAudioProcessor::AudiocityAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void AudiocityAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    engine_.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
}

void AudiocityAudioProcessor::releaseResources()
{
    engine_.release();
}

bool AudiocityAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void AudiocityAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputChannels = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();

    for (auto channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    engine_.render(buffer, midiMessages);
}

juce::AudioProcessorEditor* AudiocityAudioProcessor::createEditor()
{
    return new AudiocityAudioProcessorEditor(*this);
}

void AudiocityAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream(destData, false);
    stream.writeInt(1);
}

void AudiocityAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    juce::ignoreUnused(data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudiocityAudioProcessor();
}
