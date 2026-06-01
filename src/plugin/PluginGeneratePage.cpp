#include "PluginGeneratePage.h"

namespace
{
constexpr int kGenerateButtonWidth = 64;
constexpr int kGenerateButtonGap = 6;
} // namespace

PluginGeneratePage::PluginGeneratePage(juce::Component& waveformView,
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
                                       juce::TextButton& loadAsSampleButton)
    : waveformView_(waveformView),
      waveButtons_{ { &sineButton, &rampButton, &squareButton, &sawtoothButton, &triangleButton, &pulseButton, &randomButton } },
      samplesLabel_(samplesLabel),
      samplesCombo_(samplesCombo),
      bitDepthLabel_(bitDepthLabel),
      bitDepthCombo_(bitDepthCombo),
      sketchSmoothingLabel_(sketchSmoothingLabel),
      sketchSmoothingCombo_(sketchSmoothingCombo),
      pulseWidthLabel_(pulseWidthLabel),
      pulseWidthSlider_(pulseWidthSlider),
      previewButton_(previewButton),
      frequencyLabel_(frequencyLabel),
      frequencyCombo_(frequencyCombo),
      loadAsSampleButton_(loadAsSampleButton)
{
    addAndMakeVisible(waveformView_);
    for (auto* button : waveButtons_)
        addAndMakeVisible(*button);

    addAndMakeVisible(samplesLabel_);
    addAndMakeVisible(samplesCombo_);
    addAndMakeVisible(bitDepthLabel_);
    addAndMakeVisible(bitDepthCombo_);
    addAndMakeVisible(sketchSmoothingLabel_);
    addAndMakeVisible(sketchSmoothingCombo_);
    addAndMakeVisible(pulseWidthLabel_);
    addAndMakeVisible(pulseWidthSlider_);
    addAndMakeVisible(previewButton_);
    addAndMakeVisible(frequencyLabel_);
    addAndMakeVisible(frequencyCombo_);
    addAndMakeVisible(loadAsSampleButton_);
}

void PluginGeneratePage::resized()
{
    auto area = getLocalBounds();

    auto waveformArea = area.removeFromTop(juce::jmax(200, area.getHeight() / 2));
    waveformView_.setBounds(waveformArea);
    area.removeFromTop(12);

    auto waveButtons = area.removeFromTop(32).reduced(12, 0);
    loadAsSampleButton_.setBounds(waveButtons.removeFromRight(140));
    waveButtons.removeFromRight(kGenerateButtonGap);
    for (auto* button : waveButtons_)
    {
        button->setBounds(waveButtons.removeFromLeft(kGenerateButtonWidth));
        waveButtons.removeFromLeft(kGenerateButtonGap);
    }

    area.removeFromTop(10);
    auto settingsRow = area.removeFromTop(32).reduced(12, 0);
    samplesLabel_.setBounds(settingsRow.removeFromLeft(58));
    samplesCombo_.setBounds(settingsRow.removeFromLeft(98));
    settingsRow.removeFromLeft(14);
    bitDepthLabel_.setBounds(settingsRow.removeFromLeft(34));
    bitDepthCombo_.setBounds(settingsRow.removeFromLeft(96));
    settingsRow.removeFromLeft(14);
    sketchSmoothingLabel_.setBounds(settingsRow.removeFromLeft(54));
    sketchSmoothingCombo_.setBounds(settingsRow.removeFromLeft(96));
    settingsRow.removeFromLeft(14);
    pulseWidthLabel_.setBounds(settingsRow.removeFromLeft(36));
    pulseWidthSlider_.setBounds(settingsRow);

    area.removeFromTop(10);
    auto actionsRow = area.removeFromTop(32).reduced(12, 0);
    previewButton_.setBounds(actionsRow.removeFromLeft(96));
    actionsRow.removeFromLeft(12);
    frequencyLabel_.setBounds(actionsRow.removeFromLeft(72));
    frequencyCombo_.setBounds(actionsRow.removeFromLeft(190));
}