#include "PluginPlayerPage.h"

namespace
{
juce::Colour uiPanelColour() { return juce::Colour(0xff1a212c); }
juce::Colour uiBorderColour() { return juce::Colour(0xff313d4c); }
} // namespace

PluginPlayerPage::PluginPlayerPage(juce::Label& keyboardLabel,
                                   juce::Component& statusDisplay,
                                   std::function<void(bool)> setCompactHeaderOverlayEnabled,
                                   juce::Component& openButton,
                                   juce::Viewport& keyboardViewport,
                                   juce::Label& padsLabel,
                                   std::array<juce::TextButton*, kPadCount> padButtons,
                                   std::array<juce::TextButton*, kPadCount> padAssignButtons,
                                   std::function<void()> onKeyboardViewportLayoutChanged)
    : keyboardLabel_(keyboardLabel),
    statusDisplay_(statusDisplay),
    setCompactHeaderOverlayEnabled_(std::move(setCompactHeaderOverlayEnabled)),
      openButton_(openButton),
      keyboardViewport_(keyboardViewport),
      padsLabel_(padsLabel),
      padButtons_(padButtons),
      padAssignButtons_(padAssignButtons),
      onKeyboardViewportLayoutChanged_(std::move(onKeyboardViewportLayoutChanged))
{
}

void PluginPlayerPage::layout(juce::Rectangle<int> area, const bool compactLayout)
{
    if (compactLayout)
    {
        area = area.reduced(6, 4);

        keyboardLabel_.setText("Performance Strip", juce::dontSendNotification);
        padsLabel_.setText("Quick Pads", juce::dontSendNotification);

        const int keyboardPanelHeight = juce::jlimit(100, 144, (area.getHeight() * 58) / 100);
        auto keyboardPanel = area.removeFromTop(keyboardPanelHeight).reduced(8, 6);
        auto keyboardHeader = keyboardPanel.removeFromTop(34);
        const auto keyboardStatusBounds = keyboardHeader;
        openButton_.setBounds({});
        keyboardLabel_.setBounds(keyboardHeader.removeFromTop(12).translated(0, 2));
        setCompactHeaderOverlayEnabled_(true);
        statusDisplay_.setBounds(keyboardStatusBounds);
        keyboardLabel_.toFront(false);
        keyboardPanel.removeFromTop(4);
        keyboardViewport_.setBounds(keyboardPanel.withTrimmedBottom(6));
        onKeyboardViewportLayoutChanged_();

        area.removeFromTop(4);
        auto padsPanel = area.reduced(8, 4);
        padsLabel_.setBounds(padsPanel.removeFromTop(14));
        padsPanel.removeFromTop(4);
        padsPanel = padsPanel.reduced(2, 0);

        constexpr int kCompactPadGap = 8;
        constexpr int kCompactPadMinSingleRowWidth = 92;
        const bool useTwoRows = padsPanel.getHeight() >= 96
            && padsPanel.getWidth() < ((static_cast<int>(kPadCount) * kCompactPadMinSingleRowWidth)
                + ((static_cast<int>(kPadCount) - 1) * kCompactPadGap));
        const int padCols = useTwoRows ? 4 : static_cast<int>(kPadCount);
        const int padRows = (static_cast<int>(kPadCount) + padCols - 1) / padCols;
        const int verticalGap = useTwoRows ? 8 : 0;
        const int padCellWidth = juce::jmax(1, (padsPanel.getWidth() - (padCols - 1) * kCompactPadGap) / padCols);
        const int availablePadHeight = juce::jmax(1,
            (padsPanel.getHeight() - (padRows - 1) * verticalGap) / padRows);
        const int preferredPadHeight = useTwoRows ? 48 : 46;
        const int minimumPadHeight = useTwoRows ? 34 : 32;
        const int padCellHeight = availablePadHeight >= minimumPadHeight
            ? juce::jmin(preferredPadHeight, availablePadHeight)
            : availablePadHeight;
        const int gridWidth = padCols * padCellWidth + (padCols - 1) * kCompactPadGap;
        const int gridHeight = padRows * padCellHeight + (padRows - 1) * verticalGap;
        auto gridArea = juce::Rectangle<int>(
            padsPanel.getCentreX() - (gridWidth / 2),
            padsPanel.getY() + juce::jmax(0, (padsPanel.getHeight() - gridHeight) / 2),
            gridWidth,
            gridHeight);

        for (int i = 0; i < static_cast<int>(kPadCount); ++i)
        {
            const int row = i / padCols;
            const int col = i % padCols;
            auto cell = juce::Rectangle<int>(
                gridArea.getX() + col * (padCellWidth + kCompactPadGap),
                gridArea.getY() + row * (padCellHeight + verticalGap),
                padCellWidth,
                padCellHeight);
            padButtons_[static_cast<std::size_t>(i)]->setBounds(cell);
            padAssignButtons_[static_cast<std::size_t>(i)]->setBounds({});
        }

        return;
    }

    area = area.reduced(8, 6);

    keyboardLabel_.setText("Piano", juce::dontSendNotification);
    padsLabel_.setText("Drum Pads", juce::dontSendNotification);
    openButton_.setBounds({});
    setCompactHeaderOverlayEnabled_(false);

    auto keyboardPanel = area.removeFromTop(computeKeyboardPanelHeight(area.getWidth()) + 52);
    keyboardPanel.reduce(10, 10);

    auto keyboardHeader = keyboardPanel.removeFromTop(64);
    auto keyboardTitleRow = keyboardHeader.removeFromTop(18);
    keyboardLabel_.setBounds(keyboardTitleRow);
    keyboardHeader.removeFromTop(6);
    statusDisplay_.setBounds(keyboardHeader.removeFromTop(36));

    keyboardPanel.removeFromTop(8);
    keyboardViewport_.setBounds(keyboardPanel);
    onKeyboardViewportLayoutChanged_();

    area.removeFromTop(10);
    auto padsPanel = area.reduced(10, 10);
    padsLabel_.setBounds(padsPanel.removeFromTop(22));
    padsPanel.removeFromTop(6);

    constexpr int kPadCols = 4;
    const int kPadRows = (static_cast<int>(kPadCount) + kPadCols - 1) / kPadCols;
    const int padGap = 8;
    const int padCellWidth = juce::jmax(80, (padsPanel.getWidth() - (kPadCols - 1) * padGap) / kPadCols);
    constexpr int kPreferredPadHeight = 96;
    const int availablePadHeight = (padsPanel.getHeight() - (kPadRows - 1) * padGap) / kPadRows;
    const int padCellHeight = juce::jlimit(72, kPreferredPadHeight, availablePadHeight);

    for (int i = 0; i < static_cast<int>(kPadCount); ++i)
    {
        const int row = i / kPadCols;
        const int col = i % kPadCols;
        auto cell = juce::Rectangle<int>(
            padsPanel.getX() + col * (padCellWidth + padGap),
            padsPanel.getY() + row * (padCellHeight + padGap),
            padCellWidth,
            padCellHeight);

        padButtons_[static_cast<std::size_t>(i)]->setBounds(cell);

        constexpr int kAssignW = 28;
        constexpr int kAssignH = 20;
        constexpr int kAssignPad = 6;
        const auto assignBounds = juce::Rectangle<int>(
            cell.getRight() - kAssignW - kAssignPad,
            cell.getBottom() - kAssignH - kAssignPad,
            kAssignW,
            kAssignH);
        padAssignButtons_[static_cast<std::size_t>(i)]->setBounds(assignBounds);
        padAssignButtons_[static_cast<std::size_t>(i)]->toFront(false);
    }
}

void PluginPlayerPage::paint(juce::Graphics& g, juce::Rectangle<int> area, const bool compactLayout) const
{
    area = area.reduced(8, 6);

    auto paintPanel = [&g](juce::Rectangle<int> bounds)
    {
        g.setColour(uiPanelColour());
        g.fillRoundedRectangle(bounds.toFloat(), 8.0f);
        g.setColour(uiBorderColour());
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 8.0f, 1.0f);
    };

    if (compactLayout)
    {
        auto keyboardPanel = area.removeFromTop(juce::jlimit(78, 116, (area.getHeight() * 53) / 100));
        area.removeFromTop(6);
        auto padsPanel = area;

        paintPanel(keyboardPanel);
        paintPanel(padsPanel);
        return;
    }

    auto keyboardPanel = area.removeFromTop(computeKeyboardPanelHeight(area.getWidth()));
    auto padsPanel = area.withTrimmedTop(10);

    paintPanel(keyboardPanel);
    paintPanel(padsPanel);
}

int PluginPlayerPage::computeKeyboardPanelHeight(const int availableWidth) noexcept
{
    return juce::jlimit(110, 190, availableWidth / 6);
}
