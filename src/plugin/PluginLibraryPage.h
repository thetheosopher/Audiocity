#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class PluginLibraryPage final
{
public:
    PluginLibraryPage(juce::Label& rootLabel,
                      juce::TextButton& chooseRootButton,
                      juce::TextButton& refreshButton,
                      juce::TextButton& cancelButton,
                      juce::ComboBox& bookmarkCombo,
                      juce::TextButton& addBookmarkButton,
                      juce::TextButton& removeBookmarkButton,
                      juce::TextEditor& filterEditor,
                      juce::ComboBox& sortCombo,
                      juce::ToggleButton& favoriteButton,
                      juce::ToggleButton& favoritesOnlyToggle,
                      juce::ToggleButton& recentOnlyToggle,
                      juce::ComboBox& tagFilterCombo,
                      juce::TextEditor& tagsEditor,
                      juce::TextButton& applyTagsButton,
                      juce::ListBox& listBox,
                      juce::Label& countLabel,
                      juce::Label& previewLabel);

    void layout(juce::Rectangle<int> browserArea, bool compactLayout) const;
    void paint(juce::Graphics& g, juce::Rectangle<int> browserArea) const;

private:
    juce::Label& rootLabel_;
    juce::TextButton& chooseRootButton_;
    juce::TextButton& refreshButton_;
    juce::TextButton& cancelButton_;
    juce::ComboBox& bookmarkCombo_;
    juce::TextButton& addBookmarkButton_;
    juce::TextButton& removeBookmarkButton_;
    juce::TextEditor& filterEditor_;
    juce::ComboBox& sortCombo_;
    juce::ToggleButton& favoriteButton_;
    juce::ToggleButton& favoritesOnlyToggle_;
    juce::ToggleButton& recentOnlyToggle_;
    juce::ComboBox& tagFilterCombo_;
    juce::TextEditor& tagsEditor_;
    juce::TextButton& applyTagsButton_;
    juce::ListBox& listBox_;
    juce::Label& countLabel_;
    juce::Label& previewLabel_;
};
