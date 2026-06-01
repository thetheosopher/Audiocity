#include "PluginCapturePage.h"

#include "DialLookAndFeel.h"

#include <cmath>

namespace
{
constexpr int kCaptureButtonGap = 8;
} // namespace

PluginCapturePage::PluginCapturePage(juce::Component& waveformView,
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
                                     DialLookAndFeel& buttonLookAndFeel)
    : waveformView_(waveformView),
      actionButtons_{ { &recordButton, &clearButton, &cutButton, &trimButton, &playButton, &normalizeButton } },
      loadAsSampleButton_(loadAsSampleButton),
      sourceLabel_(sourceLabel),
      sampleRateLabel_(sampleRateLabel),
      sampleRateCombo_(sampleRateCombo),
      channelLabel_(channelLabel),
      channelCombo_(channelCombo),
      bitDepthLabel_(bitDepthLabel),
      bitDepthCombo_(bitDepthCombo),
      rootNoteLabel_(rootNoteLabel),
      rootNoteCombo_(rootNoteCombo),
      inputLevelLabel_(inputLevelLabel),
      inputLevelSlider_(inputLevelSlider),
      inputVuMeter_(inputVuMeter),
      statusLabel_(statusLabel),
      buttonLookAndFeel_(buttonLookAndFeel)
{
    addAndMakeVisible(waveformView_);
    for (auto* button : actionButtons_)
        addAndMakeVisible(*button);

    addAndMakeVisible(loadAsSampleButton_);
    addAndMakeVisible(sourceLabel_);
    addAndMakeVisible(sampleRateLabel_);
    addAndMakeVisible(sampleRateCombo_);
    addAndMakeVisible(channelLabel_);
    addAndMakeVisible(channelCombo_);
    addAndMakeVisible(bitDepthLabel_);
    addAndMakeVisible(bitDepthCombo_);
    addAndMakeVisible(rootNoteLabel_);
    addAndMakeVisible(rootNoteCombo_);
    addAndMakeVisible(inputLevelLabel_);
    addAndMakeVisible(inputLevelSlider_);
    addAndMakeVisible(inputVuMeter_);
    addAndMakeVisible(statusLabel_);
}

int PluginCapturePage::measureButtonWidth(juce::TextButton& button,
                                          const juce::StringArray& labels,
                                          const int minWidth,
                                          const int maxWidth,
                                          const int buttonHeight) const
{
    const auto font = buttonLookAndFeel_.getTextButtonFont(button, buttonHeight);
    const auto measureTextWidth = [&font](const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, text, 0.0f, 0.0f);
        return static_cast<int>(std::ceil(glyphs.getBoundingBox(0, glyphs.getNumGlyphs(), true).getWidth()));
    };

    auto textWidth = measureTextWidth(button.getButtonText());
    for (const auto& label : labels)
        textWidth = juce::jmax(textWidth, measureTextWidth(label));

    return juce::jlimit(minWidth, maxWidth, textWidth + 22);
}

void PluginCapturePage::resized()
{
    auto area = getLocalBounds();
    waveformView_.setBounds(area.removeFromTop(juce::jmax(220, area.getHeight() / 2)));

    area.removeFromTop(10);
    auto controlsRow = area.removeFromTop(32).reduced(10, 0);
    const int loadAsSampleWidth = measureButtonWidth(loadAsSampleButton_, {}, 112, 134, controlsRow.getHeight());
    const int recordWidth = measureButtonWidth(*actionButtons_[0], { "Stop" }, 72, 96, controlsRow.getHeight());
    const int clearWidth = measureButtonWidth(*actionButtons_[1], {}, 62, 84, controlsRow.getHeight());
    const int cutWidth = measureButtonWidth(*actionButtons_[2], {}, 92, 116, controlsRow.getHeight());
    const int trimWidth = measureButtonWidth(*actionButtons_[3], {}, 100, 124, controlsRow.getHeight());
    const int playWidth = measureButtonWidth(*actionButtons_[4], { "Stop" }, 76, 92, controlsRow.getHeight());
    const int normalizeWidth = measureButtonWidth(*actionButtons_[5], {}, 98, 116, controlsRow.getHeight());

    loadAsSampleButton_.setBounds(controlsRow.removeFromRight(loadAsSampleWidth));
    controlsRow.removeFromRight(kCaptureButtonGap);
    actionButtons_[0]->setBounds(controlsRow.removeFromLeft(recordWidth));
    controlsRow.removeFromLeft(kCaptureButtonGap);
    actionButtons_[1]->setBounds(controlsRow.removeFromLeft(clearWidth));
    controlsRow.removeFromLeft(kCaptureButtonGap);
    actionButtons_[2]->setBounds(controlsRow.removeFromLeft(cutWidth));
    controlsRow.removeFromLeft(kCaptureButtonGap);
    actionButtons_[3]->setBounds(controlsRow.removeFromLeft(trimWidth));
    controlsRow.removeFromLeft(kCaptureButtonGap);
    actionButtons_[4]->setBounds(controlsRow.removeFromLeft(playWidth));
    controlsRow.removeFromLeft(kCaptureButtonGap);
    actionButtons_[5]->setBounds(controlsRow.removeFromLeft(normalizeWidth));

    area.removeFromTop(10);
    sourceLabel_.setBounds(area.removeFromTop(20));

    area.removeFromTop(6);
    auto settingsRow = area.removeFromTop(30);
    sampleRateLabel_.setBounds(settingsRow.removeFromLeft(28));
    sampleRateCombo_.setBounds(settingsRow.removeFromLeft(96));
    settingsRow.removeFromLeft(10);
    channelLabel_.setBounds(settingsRow.removeFromLeft(28));
    channelCombo_.setBounds(settingsRow.removeFromLeft(118));
    settingsRow.removeFromLeft(10);
    bitDepthLabel_.setBounds(settingsRow.removeFromLeft(34));
    bitDepthCombo_.setBounds(settingsRow.removeFromLeft(96));

    area.removeFromTop(8);
    auto levelRow = area.removeFromTop(40);
    rootNoteLabel_.setBounds(levelRow.removeFromLeft(72));
    rootNoteCombo_.setBounds(levelRow.removeFromLeft(160));
    levelRow.removeFromLeft(12);
    inputLevelLabel_.setBounds(levelRow.removeFromLeft(80));
    inputLevelSlider_.setBounds(levelRow.removeFromLeft(190).withSizeKeepingCentre(190, 28));
    levelRow.removeFromLeft(12);
    inputVuMeter_.setBounds(levelRow.removeFromLeft(180));

    area.removeFromTop(8);
    statusLabel_.setBounds(area.removeFromTop(22));
}