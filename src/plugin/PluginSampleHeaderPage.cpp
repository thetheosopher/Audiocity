#include "PluginSampleHeaderPage.h"

namespace
{
constexpr int kSamplePresetBarHeight = 32;
constexpr int kSamplePresetControlHeight = 28;
} // namespace

PluginSampleHeaderPage::PluginSampleHeaderPage(juce::TextButton& loadButton,
                                               juce::TextEditor& presetFilterEditor,
                                               juce::Label& presetCountLabel,
                                               juce::ComboBox& presetCombo,
                                               juce::TextButton& presetSaveButton,
                                               juce::TextButton& presetRenameButton,
                                               juce::TextButton& presetDeleteButton,
                                               juce::TextButton& sampleBrowserRailToggleButton,
                                               juce::TextButton& sampleInspectorRailToggleButton,
                                               juce::TextButton& diagnosticsToggleButton)
    : loadButton_(loadButton),
      presetFilterEditor_(presetFilterEditor),
      presetCountLabel_(presetCountLabel),
      presetCombo_(presetCombo),
      presetSaveButton_(presetSaveButton),
      presetRenameButton_(presetRenameButton),
      presetDeleteButton_(presetDeleteButton),
      sampleBrowserRailToggleButton_(sampleBrowserRailToggleButton),
      sampleInspectorRailToggleButton_(sampleInspectorRailToggleButton),
      diagnosticsToggleButton_(diagnosticsToggleButton)
{
}

void PluginSampleHeaderPage::layout(juce::Rectangle<int>& area) const
{
    auto topRow = area.removeFromTop(kSamplePresetBarHeight);
    auto controlRow = topRow.withSizeKeepingCentre(topRow.getWidth(), kSamplePresetControlHeight);
    loadButton_.setBounds(controlRow.removeFromLeft(74));
    controlRow.removeFromLeft(10);

    diagnosticsToggleButton_.setBounds(controlRow.removeFromRight(58));
    controlRow.removeFromRight(6);
    sampleInspectorRailToggleButton_.setBounds(controlRow.removeFromRight(60));
    controlRow.removeFromRight(6);
    sampleBrowserRailToggleButton_.setBounds(controlRow.removeFromRight(60));
    controlRow.removeFromRight(8);
    presetDeleteButton_.setBounds(controlRow.removeFromRight(68));
    controlRow.removeFromRight(6);
    presetRenameButton_.setBounds(controlRow.removeFromRight(76));
    controlRow.removeFromRight(6);
    presetSaveButton_.setBounds(controlRow.removeFromRight(60));
    controlRow.removeFromRight(10);
    const auto presetComboWidth = juce::jmin(190, juce::jmax(160, controlRow.getWidth() / 3));
    presetCombo_.setBounds(controlRow.removeFromRight(presetComboWidth));
    controlRow.removeFromRight(8);
    presetCountLabel_.setBounds(controlRow.removeFromRight(96));
    controlRow.removeFromRight(8);
    presetFilterEditor_.setBounds(controlRow);
}
