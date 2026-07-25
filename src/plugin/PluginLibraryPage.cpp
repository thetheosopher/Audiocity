#include "PluginLibraryPage.h"

namespace
{
juce::Colour uiLibraryBackgroundColour() { return juce::Colour(0xff252538); }
juce::Colour uiLibraryBorderColour() { return juce::Colour(0xff3a3a52); }
} // namespace

PluginLibraryPage::PluginLibraryPage(juce::Label& rootLabel,
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
                                     juce::Label& previewLabel)
    : rootLabel_(rootLabel),
      chooseRootButton_(chooseRootButton),
      refreshButton_(refreshButton),
      cancelButton_(cancelButton),
      bookmarkCombo_(bookmarkCombo),
      addBookmarkButton_(addBookmarkButton),
      removeBookmarkButton_(removeBookmarkButton),
      filterEditor_(filterEditor),
      sortCombo_(sortCombo),
      favoriteButton_(favoriteButton),
      favoritesOnlyToggle_(favoritesOnlyToggle),
      recentOnlyToggle_(recentOnlyToggle),
      tagFilterCombo_(tagFilterCombo),
      tagsEditor_(tagsEditor),
      applyTagsButton_(applyTagsButton),
      listBox_(listBox),
      countLabel_(countLabel),
      previewLabel_(previewLabel)
{
}

void PluginLibraryPage::layout(juce::Rectangle<int> browserArea, const bool compactLayout) const
{
    browserArea = browserArea.reduced(8, 6);

    if (compactLayout)
    {
        auto header = browserArea.removeFromTop(28);
        if (cancelButton_.isVisible())
        {
            cancelButton_.setBounds(header.removeFromRight(62));
            header.removeFromRight(6);
        }
        else
        {
            cancelButton_.setBounds({});
        }

        refreshButton_.setBounds(header.removeFromRight(72));
        header.removeFromRight(6);
        chooseRootButton_.setBounds(header.removeFromRight(30));
        header.removeFromRight(6);
        rootLabel_.setBounds(header);

        browserArea.removeFromTop(6);
        filterEditor_.setBounds(browserArea.removeFromTop(28));

        browserArea.removeFromTop(6);
        auto quickRow = browserArea.removeFromTop(28);
        auto quickLeft = quickRow.removeFromLeft((quickRow.getWidth() - 6) / 2);
        sortCombo_.setBounds(quickLeft);
        quickRow.removeFromLeft(6);
        favoriteButton_.setBounds(quickRow);

        browserArea.removeFromTop(6);
        auto toggleRow = browserArea.removeFromTop(28);
        auto toggleLeft = toggleRow.removeFromLeft((toggleRow.getWidth() - 6) / 2);
        favoritesOnlyToggle_.setBounds(toggleLeft);
        toggleRow.removeFromLeft(6);
        recentOnlyToggle_.setBounds(toggleRow);

        browserArea.removeFromTop(6);
        auto listArea = browserArea;
        auto statusRow = listArea.removeFromBottom(20);
        countLabel_.setBounds(statusRow.removeFromLeft(statusRow.getWidth() / 2));
        previewLabel_.setBounds(statusRow);
        listArea.removeFromBottom(4);
        listBox_.setBounds(listArea);

        bookmarkCombo_.setBounds({});
        addBookmarkButton_.setBounds({});
        removeBookmarkButton_.setBounds({});
        tagFilterCombo_.setBounds({});
        tagsEditor_.setBounds({});
        applyTagsButton_.setBounds({});
        return;
    }

    auto header = browserArea.removeFromTop(28);
    if (cancelButton_.isVisible())
    {
        cancelButton_.setBounds(header.removeFromRight(76));
        header.removeFromRight(6);
    }
    else
    {
        cancelButton_.setBounds({});
    }

    refreshButton_.setBounds(header.removeFromRight(84));
    header.removeFromRight(6);
    chooseRootButton_.setBounds(header.removeFromRight(30));
    header.removeFromRight(6);
    rootLabel_.setBounds(header);

    browserArea.removeFromTop(6);
    auto bookmarkRow = browserArea.removeFromTop(28);
    removeBookmarkButton_.setBounds(bookmarkRow.removeFromRight(78));
    bookmarkRow.removeFromRight(6);
    addBookmarkButton_.setBounds(bookmarkRow.removeFromRight(98));
    bookmarkRow.removeFromRight(6);
    bookmarkCombo_.setBounds(bookmarkRow);

    browserArea.removeFromTop(6);
    auto filterRow = browserArea.removeFromTop(28);
    constexpr int sortWidth = 104;
    constexpr int favoriteWidth = 86;
    constexpr int favoritesOnlyWidth = 96;
    constexpr int recentOnlyWidth = 78;
    constexpr int filterControlGap = 6;
    const auto controlWidth = sortWidth + favoriteWidth + favoritesOnlyWidth + recentOnlyWidth + (4 * filterControlGap);
    const auto filterWidth = juce::jmax(120, filterRow.getWidth() - controlWidth);
    filterEditor_.setBounds(filterRow.removeFromLeft(filterWidth));
    filterRow.removeFromLeft(filterControlGap);
    sortCombo_.setBounds(filterRow.removeFromLeft(sortWidth));
    filterRow.removeFromLeft(filterControlGap);
    favoriteButton_.setBounds(filterRow.removeFromLeft(favoriteWidth));
    filterRow.removeFromLeft(filterControlGap);
    favoritesOnlyToggle_.setBounds(filterRow.removeFromLeft(favoritesOnlyWidth));
    filterRow.removeFromLeft(filterControlGap);
    recentOnlyToggle_.setBounds(filterRow.removeFromLeft(recentOnlyWidth));

    browserArea.removeFromTop(6);
    auto tagRow = browserArea.removeFromTop(28);
    applyTagsButton_.setBounds(tagRow.removeFromRight(96));
    tagRow.removeFromRight(6);
    tagFilterCombo_.setBounds(tagRow.removeFromRight(132));
    tagRow.removeFromRight(6);
    tagsEditor_.setBounds(tagRow);

    browserArea.removeFromTop(6);
    auto listArea = browserArea;
    auto statusRow = listArea.removeFromBottom(20);
    countLabel_.setBounds(statusRow.removeFromLeft(statusRow.getWidth() / 2));
    previewLabel_.setBounds(statusRow);
    listArea.removeFromBottom(4);
    listBox_.setBounds(listArea);
}

void PluginLibraryPage::paint(juce::Graphics& g, const juce::Rectangle<int> browserArea) const
{
    g.setColour(uiLibraryBackgroundColour());
    g.fillRoundedRectangle(browserArea.toFloat(), 6.0f);
    g.setColour(uiLibraryBorderColour());
    g.drawRoundedRectangle(browserArea.toFloat().reduced(0.5f), 6.0f, 1.0f);
}
