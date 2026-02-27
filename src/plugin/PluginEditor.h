#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>

class AudiocityAudioProcessor;

class AudiocityAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudiocityAudioProcessorEditor(AudiocityAudioProcessor& processor);
    ~AudiocityAudioProcessorEditor() override = default;

    void resized() override;

private:
    class PlaceholderPanel final : public juce::Component
    {
    public:
        explicit PlaceholderPanel(juce::String title) : title_(std::move(title)) {}

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black.withAlpha(0.9f));
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.drawRect(getLocalBounds().reduced(8), 1);
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.setFont(18.0f);
            g.drawText(title_ + " panel (stub)", getLocalBounds(), juce::Justification::centred);
        }

    private:
        juce::String title_;
    };

    AudiocityAudioProcessor& processor_;
    juce::TabbedComponent tabs_{ juce::TabbedButtonBar::TabsAtTop };

    PlaceholderPanel browserPanel_{ "Browser" };
    PlaceholderPanel mappingPanel_{ "Mapping" };
    PlaceholderPanel editorPanel_{ "Editor" };
    PlaceholderPanel settingsPanel_{ "Settings" };
    PlaceholderPanel diagnosticsPanel_{ "Diagnostics" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudiocityAudioProcessorEditor)
};
