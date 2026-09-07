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
    addAndMakeVisible(guidance_);
    guidance_.setText("Generate a source  |  Choose a shape or draw the waveform, preview it, then Use sample.", juce::dontSendNotification);
    guidance_.setFont(juce::Font(juce::FontOptions(15.0f)));
    addAndMakeVisible(advancedButton_);
    advancedButton_.onClick = [this] { advanced_ = !advanced_; resized(); };
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
    auto area = getLocalBounds().reduced(8);
    guidance_.setBounds(area.removeFromTop(32)); area.removeFromTop(8);
    waveformView_.setBounds(area.removeFromTop(juce::jlimit(160, 280, area.getHeight() / 2)));
    area.removeFromTop(12);
    auto shapes = area.removeFromTop(32);
    for (auto* button : waveButtons_)
    { button->setBounds(shapes.removeFromLeft(kGenerateButtonWidth)); shapes.removeFromLeft(kGenerateButtonGap); }
    area.removeFromTop(12);
    auto actions = area.removeFromTop(32);
    loadAsSampleButton_.setBounds(actions.removeFromRight(140));
    previewButton_.setBounds(actions.removeFromLeft(120)); actions.removeFromLeft(12);
    frequencyLabel_.setBounds(actions.removeFromLeft(72)); frequencyCombo_.setBounds(actions.removeFromLeft(180));
    area.removeFromTop(12);
    auto drawing = area.removeFromTop(32);
    sketchSmoothingLabel_.setBounds(drawing.removeFromLeft(72)); sketchSmoothingCombo_.setBounds(drawing.removeFromLeft(120));
    drawing.removeFromLeft(20); pulseWidthLabel_.setBounds(drawing.removeFromLeft(70)); pulseWidthSlider_.setBounds(drawing.removeFromLeft(200));
    area.removeFromTop(12);
    auto format = area.removeFromTop(32);
    advancedButton_.setBounds(format.removeFromLeft(130)); format.removeFromLeft(12);
    samplesLabel_.setBounds(format.removeFromLeft(66)); samplesCombo_.setBounds(format.removeFromLeft(100)); format.removeFromLeft(12);
    bitDepthLabel_.setBounds(format.removeFromLeft(48)); bitDepthCombo_.setBounds(format.removeFromLeft(100));
    advancedButton_.setToggleState(advanced_, juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*>{ &samplesLabel_, &samplesCombo_, &bitDepthLabel_, &bitDepthCombo_ }) c->setVisible(advanced_);
}
