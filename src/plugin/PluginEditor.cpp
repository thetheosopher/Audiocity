#include "PluginEditor.h"

#include "PluginProcessor.h"

AudiocityAudioProcessorEditor::AudiocityAudioProcessorEditor(AudiocityAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor)
{
    juce::ignoreUnused(processor_);

    setName("Audiocity");
    setSize(1200, 760);

    addAndMakeVisible(tabs_);

    tabs_.addTab("Browser", juce::Colours::transparentBlack, &browserPanel_, false);
    tabs_.addTab("Mapping", juce::Colours::transparentBlack, &mappingPanel_, false);
    tabs_.addTab("Editor", juce::Colours::transparentBlack, &editorPanel_, false);
    tabs_.addTab("Settings", juce::Colours::transparentBlack, &settingsPanel_, false);
    tabs_.addTab("Diagnostics", juce::Colours::transparentBlack, &diagnosticsPanel_, false);
}

void AudiocityAudioProcessorEditor::resized()
{
    tabs_.setBounds(getLocalBounds().reduced(8));
}
