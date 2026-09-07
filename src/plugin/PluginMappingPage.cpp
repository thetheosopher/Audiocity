#include "PluginMappingPage.h"

PluginMappingPage::PluginMappingPage(juce::Label& summaryLabel,
                                     juce::TextButton& newLibraryButton,
                                     juce::TextButton& addSampleButton,
                                     juce::TextButton& saveLibraryButton,
                                     juce::TextButton& refreshButton,
                                     juce::TextButton& createZoneButton,
                                     juce::TextButton& duplicateZoneButton,
                                     juce::TextButton& splitZoneButton,
                                     juce::TextButton& deleteZoneButton,
                                     juce::Component& overview,
                                     juce::ListBox& zoneListBox,
                                     juce::TextEditor& detailsText,
                                     juce::Label& editKeyLowLabel,
                                     juce::Slider& editKeyLowSlider,
                                     juce::Label& editKeyHighLabel,
                                     juce::Slider& editKeyHighSlider,
                                     juce::Label& editVelocityLowLabel,
                                     juce::Slider& editVelocityLowSlider,
                                     juce::Label& editVelocityHighLabel,
                                     juce::Slider& editVelocityHighSlider,
                                     juce::Label& editVelocityFadeInLabel,
                                     juce::Slider& editVelocityFadeInLowSlider,
                                     juce::Slider& editVelocityFadeInHighSlider,
                                     juce::Label& editVelocityFadeOutLabel,
                                     juce::Slider& editVelocityFadeOutLowSlider,
                                     juce::Slider& editVelocityFadeOutHighSlider,
                                     juce::Label& editRootLabel,
                                     juce::Slider& editRootSlider,
                                     juce::Label& editSampleStartLabel,
                                     juce::Slider& editSampleStartSlider,
                                     juce::Label& editSampleEndLabel,
                                     juce::Slider& editSampleEndSlider,
                                     juce::Label& editLoopStartLabel,
                                     juce::Slider& editLoopStartSlider,
                                     juce::Label& editLoopEndLabel,
                                     juce::Slider& editLoopEndSlider,
                                     juce::Label& editGainLabel,
                                     juce::Slider& editGainSlider,
                                     juce::Label& editPanLabel,
                                     juce::Slider& editPanSlider,
                                     juce::Label& editRoundRobinGroupLabel,
                                     juce::Slider& editRoundRobinGroupSlider,
                                     juce::Label& editRoundRobinPositionLabel,
                                     juce::Slider& editRoundRobinPositionSlider,
                                     juce::Label& editRoundRobinModeLabel,
                                     juce::ComboBox& editRoundRobinModeCombo,
                                     juce::Label& editChokeLabel,
                                     juce::Slider& editChokeSlider,
                                     juce::Label& editTriggerLabel,
                                     juce::ComboBox& editTriggerCombo,
                                     juce::Label& editLoopLabel,
                                     juce::ComboBox& editLoopCombo,
                                     juce::TextButton& editApplyButton,
                                     juce::Label& editStatusLabel)
    : summaryLabel_(summaryLabel),
      newLibraryButton_(newLibraryButton),
      addSampleButton_(addSampleButton),
      saveLibraryButton_(saveLibraryButton),
      refreshButton_(refreshButton),
      createZoneButton_(createZoneButton),
      duplicateZoneButton_(duplicateZoneButton),
      splitZoneButton_(splitZoneButton),
      deleteZoneButton_(deleteZoneButton),
      overview_(overview),
      zoneListBox_(zoneListBox),
      detailsText_(detailsText),
      editKeyLowLabel_(editKeyLowLabel),
      editKeyLowSlider_(editKeyLowSlider),
      editKeyHighLabel_(editKeyHighLabel),
      editKeyHighSlider_(editKeyHighSlider),
      editVelocityLowLabel_(editVelocityLowLabel),
      editVelocityLowSlider_(editVelocityLowSlider),
      editVelocityHighLabel_(editVelocityHighLabel),
      editVelocityHighSlider_(editVelocityHighSlider),
      editVelocityFadeInLabel_(editVelocityFadeInLabel),
      editVelocityFadeInLowSlider_(editVelocityFadeInLowSlider),
      editVelocityFadeInHighSlider_(editVelocityFadeInHighSlider),
      editVelocityFadeOutLabel_(editVelocityFadeOutLabel),
      editVelocityFadeOutLowSlider_(editVelocityFadeOutLowSlider),
      editVelocityFadeOutHighSlider_(editVelocityFadeOutHighSlider),
      editRootLabel_(editRootLabel),
      editRootSlider_(editRootSlider),
      editSampleStartLabel_(editSampleStartLabel),
      editSampleStartSlider_(editSampleStartSlider),
      editSampleEndLabel_(editSampleEndLabel),
      editSampleEndSlider_(editSampleEndSlider),
      editLoopStartLabel_(editLoopStartLabel),
      editLoopStartSlider_(editLoopStartSlider),
      editLoopEndLabel_(editLoopEndLabel),
      editLoopEndSlider_(editLoopEndSlider),
      editGainLabel_(editGainLabel),
      editGainSlider_(editGainSlider),
      editPanLabel_(editPanLabel),
      editPanSlider_(editPanSlider),
      editRoundRobinGroupLabel_(editRoundRobinGroupLabel),
      editRoundRobinGroupSlider_(editRoundRobinGroupSlider),
      editRoundRobinPositionLabel_(editRoundRobinPositionLabel),
      editRoundRobinPositionSlider_(editRoundRobinPositionSlider),
      editRoundRobinModeLabel_(editRoundRobinModeLabel),
      editRoundRobinModeCombo_(editRoundRobinModeCombo),
      editChokeLabel_(editChokeLabel),
      editChokeSlider_(editChokeSlider),
      editTriggerLabel_(editTriggerLabel),
      editTriggerCombo_(editTriggerCombo),
      editLoopLabel_(editLoopLabel),
      editLoopCombo_(editLoopCombo),
      editApplyButton_(editApplyButton),
      editStatusLabel_(editStatusLabel)
{
}

void PluginMappingPage::layout(juce::Rectangle<int> area) const
{
    auto mappingArea = area.reduced(8, 6);
    constexpr int kMappingOverviewPreferredHeight = 150;
    constexpr int kMappingOverviewMinimumHeight = 72;
    constexpr int kMappingEditRowHeight = 24;
    constexpr int kMappingEditRowGap = 2;
    constexpr int kMappingEditRowCount = 13;
    constexpr int kMappingEditApplyRowHeight = 24;
    constexpr int kMappingOverviewGap = 4;
    const int kMappingEditRequiredHeight =
        (kMappingEditRowCount * (kMappingEditRowHeight + kMappingEditRowGap)) + kMappingEditApplyRowHeight;

    auto header = mappingArea.removeFromTop(32);
    saveLibraryButton_.setBounds(header.removeFromLeft(150));
    header.removeFromLeft(8);
    deleteZoneButton_.setBounds(header.removeFromRight(100));
    header.removeFromRight(8);
    summaryLabel_.setBounds(header);
    for (auto* button : { &newLibraryButton_, &addSampleButton_, &createZoneButton_, &duplicateZoneButton_, &splitZoneButton_, &refreshButton_ })
        button->setBounds({});

    mappingArea.removeFromTop(4);
    const auto mappingOverviewHeight = juce::jlimit(
        kMappingOverviewMinimumHeight,
        kMappingOverviewPreferredHeight,
        juce::jmax(kMappingOverviewMinimumHeight, mappingArea.getHeight() - kMappingEditRequiredHeight - kMappingOverviewGap));
    overview_.setBounds(mappingArea.removeFromTop(mappingOverviewHeight));
    mappingArea.removeFromTop(kMappingOverviewGap);

    const auto detailWidth = juce::jlimit(220, 360, mappingArea.getWidth() / 3);
    auto details = mappingArea.removeFromRight(detailWidth);
    mappingArea.removeFromRight(10);
    zoneListBox_.setBounds(mappingArea);

    auto editPanel = details.removeFromTop(details.getHeight());
    auto layoutEditRow = [](juce::Rectangle<int>& panel, juce::Label& label, juce::Slider& slider)
    {
        auto row = panel.removeFromTop(24);
        label.setBounds(row.removeFromLeft(72));
        row.removeFromLeft(6);
        slider.setBounds(row);
        panel.removeFromTop(2);
    };
    auto layoutEditComboRow = [](juce::Rectangle<int>& panel, juce::Label& label, juce::ComboBox& combo)
    {
        auto row = panel.removeFromTop(24);
        label.setBounds(row.removeFromLeft(72));
        row.removeFromLeft(6);
        combo.setBounds(row);
        panel.removeFromTop(2);
    };

    layoutEditRow(editPanel, editKeyLowLabel_, editKeyLowSlider_);
    layoutEditRow(editPanel, editKeyHighLabel_, editKeyHighSlider_);
    layoutEditRow(editPanel, editVelocityLowLabel_, editVelocityLowSlider_);
    layoutEditRow(editPanel, editVelocityHighLabel_, editVelocityHighSlider_);
    layoutEditRow(editPanel, editRootLabel_, editRootSlider_);
    layoutEditRow(editPanel, editSampleStartLabel_, editSampleStartSlider_);
    layoutEditRow(editPanel, editSampleEndLabel_, editSampleEndSlider_);
    layoutEditRow(editPanel, editLoopStartLabel_, editLoopStartSlider_);
    layoutEditRow(editPanel, editLoopEndLabel_, editLoopEndSlider_);
    layoutEditRow(editPanel, editGainLabel_, editGainSlider_);
    layoutEditRow(editPanel, editPanLabel_, editPanSlider_);
    layoutEditComboRow(editPanel, editTriggerLabel_, editTriggerCombo_);
    layoutEditComboRow(editPanel, editLoopLabel_, editLoopCombo_);

    auto applyRow = editPanel.removeFromTop(24);
    editApplyButton_.setBounds(applyRow.removeFromLeft(110));
    applyRow.removeFromLeft(8);
    editStatusLabel_.setBounds(applyRow);

    details.removeFromTop(4);
    detailsText_.setBounds(details);
}
