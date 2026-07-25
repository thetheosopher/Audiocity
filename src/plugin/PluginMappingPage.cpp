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
    constexpr int kMappingEditRowHeight = 18;
    constexpr int kMappingEditRowGap = 2;
    constexpr int kMappingEditRowCount = 19;
    constexpr int kMappingEditApplyRowHeight = 24;
    constexpr int kMappingOverviewGap = 4;
    const int kMappingEditRequiredHeight =
        (kMappingEditRowCount * (kMappingEditRowHeight + kMappingEditRowGap)) + kMappingEditApplyRowHeight;

    auto header = mappingArea.removeFromTop(60);
    constexpr int kMappingHeaderGap = 6;
    constexpr int kMappingHeaderRowHeight = 27;
    constexpr int kMappingHeaderRowGap = 6;
    auto getMappingHeaderButtonWidth = [](juce::TextButton& button,
                                          const int buttonHeight,
                                          const int minWidth,
                                          const int horizontalPadding,
                                          const int maxWidth)
    {
        const auto font = button.getLookAndFeel().getTextButtonFont(button, buttonHeight);
        return juce::jlimit(minWidth,
                            maxWidth,
                            juce::GlyphArrangement::getStringWidthInt(font, button.getButtonText()) + horizontalPadding);
    };

    auto libraryRow = header.removeFromTop(kMappingHeaderRowHeight);
    header.removeFromTop(kMappingHeaderRowGap);
    auto zoneRow = header.removeFromTop(kMappingHeaderRowHeight);

    const auto newLibraryWidth = getMappingHeaderButtonWidth(newLibraryButton_, libraryRow.getHeight(), 104, 32, 132);
    const auto addSampleWidth = getMappingHeaderButtonWidth(addSampleButton_, libraryRow.getHeight(), 100, 32, 128);
    const auto saveLibraryWidth = getMappingHeaderButtonWidth(saveLibraryButton_, libraryRow.getHeight(), 112, 34, 142);

    newLibraryButton_.setBounds(libraryRow.removeFromLeft(newLibraryWidth));
    libraryRow.removeFromLeft(kMappingHeaderGap);
    addSampleButton_.setBounds(libraryRow.removeFromLeft(addSampleWidth));
    libraryRow.removeFromLeft(kMappingHeaderGap);
    saveLibraryButton_.setBounds(libraryRow.removeFromLeft(saveLibraryWidth));
    libraryRow.removeFromLeft(kMappingHeaderGap + 2);
    summaryLabel_.setBounds(libraryRow);

    const auto newZoneWidth = getMappingHeaderButtonWidth(createZoneButton_, zoneRow.getHeight(), 82, 28, 100);
    const auto duplicateWidth = getMappingHeaderButtonWidth(duplicateZoneButton_, zoneRow.getHeight(), 84, 30, 104);
    const auto splitWidth = getMappingHeaderButtonWidth(splitZoneButton_, zoneRow.getHeight(), 60, 26, 78);
    const auto deleteWidth = getMappingHeaderButtonWidth(deleteZoneButton_, zoneRow.getHeight(), 68, 26, 86);
    const auto refreshWidth = getMappingHeaderButtonWidth(refreshButton_, zoneRow.getHeight(), 78, 28, 96);

    createZoneButton_.setBounds(zoneRow.removeFromLeft(newZoneWidth));
    zoneRow.removeFromLeft(kMappingHeaderGap);
    duplicateZoneButton_.setBounds(zoneRow.removeFromLeft(duplicateWidth));
    zoneRow.removeFromLeft(kMappingHeaderGap);
    splitZoneButton_.setBounds(zoneRow.removeFromLeft(splitWidth));
    zoneRow.removeFromLeft(kMappingHeaderGap);
    deleteZoneButton_.setBounds(zoneRow.removeFromLeft(deleteWidth));
    zoneRow.removeFromLeft(kMappingHeaderGap);
    refreshButton_.setBounds(zoneRow.removeFromLeft(refreshWidth));

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
        auto row = panel.removeFromTop(18);
        label.setBounds(row.removeFromLeft(72));
        row.removeFromLeft(6);
        slider.setBounds(row);
        panel.removeFromTop(2);
    };
    auto layoutPairedEditRow = [](juce::Rectangle<int>& panel,
                                  juce::Label& label,
                                  juce::Slider& leftSlider,
                                  juce::Slider& rightSlider)
    {
        auto row = panel.removeFromTop(18);
        label.setBounds(row.removeFromLeft(72));
        row.removeFromLeft(6);
        auto left = row.removeFromLeft(row.getWidth() / 2);
        rightSlider.setBounds(row.withTrimmedLeft(4));
        leftSlider.setBounds(left.withTrimmedRight(4));
        panel.removeFromTop(2);
    };
    auto layoutEditComboRow = [](juce::Rectangle<int>& panel, juce::Label& label, juce::ComboBox& combo)
    {
        auto row = panel.removeFromTop(18);
        label.setBounds(row.removeFromLeft(72));
        row.removeFromLeft(6);
        combo.setBounds(row);
        panel.removeFromTop(2);
    };

    layoutEditRow(editPanel, editKeyLowLabel_, editKeyLowSlider_);
    layoutEditRow(editPanel, editKeyHighLabel_, editKeyHighSlider_);
    layoutEditRow(editPanel, editVelocityLowLabel_, editVelocityLowSlider_);
    layoutEditRow(editPanel, editVelocityHighLabel_, editVelocityHighSlider_);
    layoutPairedEditRow(editPanel, editVelocityFadeInLabel_, editVelocityFadeInLowSlider_, editVelocityFadeInHighSlider_);
    layoutPairedEditRow(editPanel, editVelocityFadeOutLabel_, editVelocityFadeOutLowSlider_, editVelocityFadeOutHighSlider_);
    layoutEditRow(editPanel, editRootLabel_, editRootSlider_);
    layoutEditRow(editPanel, editSampleStartLabel_, editSampleStartSlider_);
    layoutEditRow(editPanel, editSampleEndLabel_, editSampleEndSlider_);
    layoutEditRow(editPanel, editLoopStartLabel_, editLoopStartSlider_);
    layoutEditRow(editPanel, editLoopEndLabel_, editLoopEndSlider_);
    layoutEditRow(editPanel, editGainLabel_, editGainSlider_);
    layoutEditRow(editPanel, editPanLabel_, editPanSlider_);
    layoutEditRow(editPanel, editRoundRobinGroupLabel_, editRoundRobinGroupSlider_);
    layoutEditRow(editPanel, editRoundRobinPositionLabel_, editRoundRobinPositionSlider_);
    layoutEditComboRow(editPanel, editRoundRobinModeLabel_, editRoundRobinModeCombo_);
    layoutEditRow(editPanel, editChokeLabel_, editChokeSlider_);
    layoutEditComboRow(editPanel, editTriggerLabel_, editTriggerCombo_);
    layoutEditComboRow(editPanel, editLoopLabel_, editLoopCombo_);

    auto applyRow = editPanel.removeFromTop(24);
    editApplyButton_.setBounds(applyRow.removeFromLeft(110));
    applyRow.removeFromLeft(8);
    editStatusLabel_.setBounds(applyRow);

    details.removeFromTop(4);
    detailsText_.setBounds(details);
}
