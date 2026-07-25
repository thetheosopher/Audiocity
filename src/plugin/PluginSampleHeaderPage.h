#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class PluginSampleHeaderPage final
{
public:
    PluginSampleHeaderPage(juce::TextButton& loadButton,
                           juce::TextEditor& presetFilterEditor,
                           juce::Label& presetCountLabel,
                           juce::ComboBox& presetCombo,
                           juce::TextButton& presetSaveButton,
                           juce::TextButton& presetRenameButton,
                           juce::TextButton& presetDeleteButton,
                           juce::TextButton& sampleBrowserRailToggleButton,
                           juce::TextButton& sampleInspectorRailToggleButton,
                           juce::TextButton& diagnosticsToggleButton);

    void layout(juce::Rectangle<int>& area) const;

private:
    juce::TextButton& loadButton_;
    juce::TextEditor& presetFilterEditor_;
    juce::Label& presetCountLabel_;
    juce::ComboBox& presetCombo_;
    juce::TextButton& presetSaveButton_;
    juce::TextButton& presetRenameButton_;
    juce::TextButton& presetDeleteButton_;
    juce::TextButton& sampleBrowserRailToggleButton_;
    juce::TextButton& sampleInspectorRailToggleButton_;
    juce::TextButton& diagnosticsToggleButton_;
};
