#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

class PluginGeneratePage final : public juce::Component
{
public:
    PluginGeneratePage(juce::Component& waveformView,
                       juce::TextButton& sineButton,
                       juce::TextButton& rampButton,
                       juce::TextButton& squareButton,
                       juce::TextButton& sawtoothButton,
                       juce::TextButton& triangleButton,
                       juce::TextButton& pulseButton,
                       juce::TextButton& randomButton,
                       juce::Label& samplesLabel,
                       juce::ComboBox& samplesCombo,
                       juce::Label& bitDepthLabel,
                       juce::ComboBox& bitDepthCombo,
                       juce::Label& sketchSmoothingLabel,
                       juce::ComboBox& sketchSmoothingCombo,
                       juce::Label& pulseWidthLabel,
                       juce::Slider& pulseWidthSlider,
                       juce::TextButton& previewButton,
                       juce::Label& frequencyLabel,
                       juce::ComboBox& frequencyCombo,
                       juce::TextButton& loadAsSampleButton);

    void resized() override;

private:
    juce::Component& waveformView_;
    std::array<juce::TextButton*, 7> waveButtons_;
    juce::Label& samplesLabel_;
    juce::ComboBox& samplesCombo_;
    juce::Label& bitDepthLabel_;
    juce::ComboBox& bitDepthCombo_;
    juce::Label& sketchSmoothingLabel_;
    juce::ComboBox& sketchSmoothingCombo_;
    juce::Label& pulseWidthLabel_;
    juce::Slider& pulseWidthSlider_;
    juce::TextButton& previewButton_;
    juce::Label& frequencyLabel_;
    juce::ComboBox& frequencyCombo_;
    juce::TextButton& loadAsSampleButton_;
};