#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

class DialLookAndFeel;

class PluginCapturePage final : public juce::Component
{
public:
    PluginCapturePage(juce::Component& waveformView,
                      juce::TextButton& recordButton,
                      juce::TextButton& clearButton,
                      juce::TextButton& cutButton,
                      juce::TextButton& trimButton,
                      juce::TextButton& playButton,
                      juce::TextButton& normalizeButton,
                      juce::TextButton& loadAsSampleButton,
                      juce::Label& sourceLabel,
                      juce::Label& sampleRateLabel,
                      juce::ComboBox& sampleRateCombo,
                      juce::Label& channelLabel,
                      juce::ComboBox& channelCombo,
                      juce::Label& bitDepthLabel,
                      juce::ComboBox& bitDepthCombo,
                      juce::Label& rootNoteLabel,
                      juce::ComboBox& rootNoteCombo,
                      juce::Label& inputLevelLabel,
                      juce::Slider& inputLevelSlider,
                      juce::Component& inputVuMeter,
                      juce::Label& statusLabel,
                      DialLookAndFeel& buttonLookAndFeel);

    void resized() override;

private:
    [[nodiscard]] int measureButtonWidth(juce::TextButton& button,
                                         const juce::StringArray& labels,
                                         int minWidth,
                                         int maxWidth,
                                         int buttonHeight) const;

    juce::Component& waveformView_;
    std::array<juce::TextButton*, 6> actionButtons_;
    juce::TextButton& loadAsSampleButton_;
    juce::Label& sourceLabel_;
    juce::Label& sampleRateLabel_;
    juce::ComboBox& sampleRateCombo_;
    juce::Label& channelLabel_;
    juce::ComboBox& channelCombo_;
    juce::Label& bitDepthLabel_;
    juce::ComboBox& bitDepthCombo_;
    juce::Label& rootNoteLabel_;
    juce::ComboBox& rootNoteCombo_;
    juce::Label& inputLevelLabel_;
    juce::Slider& inputLevelSlider_;
    juce::Component& inputVuMeter_;
    juce::Label& statusLabel_;
    DialLookAndFeel& buttonLookAndFeel_;
};