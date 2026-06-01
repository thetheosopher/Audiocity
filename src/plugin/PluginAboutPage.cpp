#include "PluginAboutPage.h"

#include <BinaryData.h>

#include <array>

namespace
{
juce::Colour aboutPanelColour() { return juce::Colour(0xff1a212c); }
juce::Colour aboutBorderColour() { return juce::Colour(0xff313d4c); }
juce::Colour aboutTextMutedColour() { return juce::Colour(0xffaab0cc); }
juce::Colour aboutAccentColour() { return juce::Colour(0xff61d9ff); }

struct AboutRow
{
    const char* label;
    const char* detail;
};

constexpr std::array<AboutRow, 9> kFeatureRows{{
    { "Import", "Load WAV and AIFF plus SFZ, SoundFont, DecentSampler, Bitwig, XPM, TAL, TX16Wx, Korg, Ableton, EXS24, NKI, REX, RX2, and NCW" },
    { "Preset Strip", "Multi-preset imports open a chooser, and the Sample header keeps a searchable preset strip in view" },
    { "Browser Rail", "Persistent browser and preview controls stay visible on Sample, Mapping, Generate, and Capture" },
    { "Mapping", "Batch edit, create, duplicate, split, merge, chromatic remap, key spread, and root-note tools" },
    { "Slicing", "Transient slice imports, waveform overlays, and direct split or merge gestures for slice programs" },
    { "Modulation", "Route mod wheel, aftertouch, velocity, Macro 1, and Macro 2 to pitch, filter, and amp" },
    { "Performance", "Compact performance strip, external MIDI highlights, stereo output bars, and LED voice count" },
    { "Quality", "CPU, Fidelity, and Ultra modes with disk streaming, preload control, and live diagnostics" },
    { "Create", "Generate custom waves or capture audio while keeping the same browser and performance tools" }
}};

constexpr std::array<AboutRow, 9> kWorkflowRows{{
    { "Open Files", "Press Ctrl+O or drag a file in; multi-preset formats prompt for a preset before loading" },
    { "NCW", "Set AUDIOCITY_NCW_CONVERTER_COMMAND before loading NCW-backed libraries" },
    { "Preset Search", "Use the Sample top-bar search to filter preset names without leaving the current workflow" },
    { "Browser", "Use the preview buttons, Enter to load the selected row, and Esc to panic audio" },
    { "Mapping", "Ctrl+N creates a zone, Ctrl+A selects all, Ctrl+D duplicates, Ctrl+Shift+D splits, Delete removes" },
    { "Waveform", "Double-click slice boundaries to split; waveform menus expose merge and chromatic actions" },
    { "Performance", "Keys 1-7 switch tabs; Sample, Mapping, Generate, and Capture keep the compact keyboard visible" },
    { "Undo", "Ctrl+Z and Ctrl+Y walk one shared history across sample, settings, and mapping edits" },
    { "Inspect", "Wide Sample layouts show browser and inspector rails; Inspect mode enlarges the Output dials" }
}};
} // namespace

PluginAboutPage::PluginAboutPage()
{
    iconImage_ = juce::ImageFileFormat::loadFrom(BinaryData::audiocity_icon_128_png,
                                                 BinaryData::audiocity_icon_128_pngSize);

    addAndMakeVisible(gitHubButton_);
    gitHubButton_.onClick = []
    {
        juce::URL("https://github.com/thetheosopher/Audiocity").launchInDefaultBrowser();
    };

    addAndMakeVisible(coffeeButton_);
    coffeeButton_.onClick = []
    {
        juce::URL("https://buymeacoffee.com/theosopher").launchInDefaultBrowser();
    };
}

void PluginAboutPage::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    if (area.isEmpty())
        return;

    g.setColour(aboutPanelColour());
    g.fillRoundedRectangle(area.toFloat(), 8.0f);
    g.setColour(aboutBorderColour());
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 8.0f, 1.0f);

    area = area.reduced(14, 14);

    constexpr int kIconSize = 96;
    constexpr int kButtonHeight = 36;
    constexpr int kFooterHeight = 26;
    constexpr int kFooterGap = 14;
    constexpr int kTableGap = 16;
    constexpr int kTableTitleHeight = 28;
    constexpr int kTableHeaderHeight = 28;
    constexpr int kFeatureLabelWidth = 108;
    constexpr int kShortcutLabelWidth = 90;

    const int iconY = area.getY() + 20;
    const int iconX = area.getCentreX() - kIconSize / 2;

    if (iconImage_.isValid())
        g.drawImage(iconImage_,
                    juce::Rectangle<float>(static_cast<float>(iconX),
                                           static_cast<float>(iconY),
                                           static_cast<float>(kIconSize),
                                           static_cast<float>(kIconSize)),
                    juce::RectanglePlacement::centred);

    int textY = iconY + kIconSize + 16;

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(28.0f)).boldened());
    g.drawText("Audiocity", area.getX(), textY, area.getWidth(), 34, juce::Justification::centredTop);
    textY += 38;

    g.setColour(aboutTextMutedColour());
    g.setFont(juce::Font(juce::FontOptions(15.0f)));
    g.drawText("A hybrid sampler for import, slicing, modulation, and performance",
               area.getX(),
               textY,
               area.getWidth(),
               22,
               juce::Justification::centredTop);
    textY += 28;

    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText("VST3 Plugin & Standalone Application",
               area.getX(),
               textY,
               area.getWidth(),
               20,
               juce::Justification::centredTop);
    textY += 32;

    g.setColour(aboutAccentColour());
    g.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    g.drawText("Version " + juce::String(JucePlugin_VersionString),
               area.getX(),
               textY,
               area.getWidth(),
               20,
               juce::Justification::centredTop);
    textY += 28;

    auto lowerArea = area.withTrimmedTop(textY - area.getY());
    lowerArea.removeFromBottom(kButtonHeight + 22);
    auto footerArea = lowerArea.removeFromBottom(kFooterHeight);
    lowerArea.removeFromBottom(kFooterGap);

    auto drawTable = [&](juce::Rectangle<int> bounds,
                         const juce::String& title,
                         const juce::String& leftHeader,
                         const juce::String& rightHeader,
                         const int leftColumnWidth,
                         const auto& rows)
    {
        g.setColour(juce::Colour(0xff2c2f45));
        g.fillRoundedRectangle(bounds.toFloat(), 10.0f);
        g.setColour(juce::Colour(0xff434a65));
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 10.0f, 1.0f);

        auto content = bounds.reduced(12);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(14.0f)).boldened());
        g.drawText(title, content.removeFromTop(kTableTitleHeight), juce::Justification::centredLeft, true);

        auto header = content.removeFromTop(kTableHeaderHeight);
        g.setColour(aboutAccentColour());
        g.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
        g.drawText(leftHeader, header.removeFromLeft(leftColumnWidth), juce::Justification::centredLeft, true);
        g.drawText(rightHeader, header, juce::Justification::centredLeft, true);

        const int rowHeight = rows.empty() ? 0 : content.getHeight() / static_cast<int>(rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            auto row = content.removeFromTop(rowHeight);
            if (i != 0)
            {
                g.setColour(juce::Colour(0x30434a65));
                g.drawLine(static_cast<float>(row.getX()),
                           static_cast<float>(row.getY()),
                           static_cast<float>(row.getRight()),
                           static_cast<float>(row.getY()));
            }

            auto left = row.removeFromLeft(leftColumnWidth);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
            g.drawFittedText(rows[i].label, left, juce::Justification::centredLeft, 1);

            g.setColour(aboutTextMutedColour());
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawFittedText(rows[i].detail, row, juce::Justification::centredLeft, 2);
        }
    };

    auto tables = lowerArea;
    auto leftTable = tables.removeFromLeft((tables.getWidth() - kTableGap) / 2);
    tables.removeFromLeft(kTableGap);
    auto rightTable = tables;

    drawTable(leftTable,
              "Feature Highlights",
              "Area",
              "What it gives you",
              kFeatureLabelWidth,
              kFeatureRows);
    drawTable(rightTable,
              "Workflow Tips",
              "Shortcut",
              "Action",
              kShortcutLabelWidth,
              kWorkflowRows);

    g.setColour(juce::Colour(0xff8990b0));
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.drawText("Built with JUCE, tuned for deterministic offline tests, and designed for broad sampler import workflows.",
               footerArea,
               juce::Justification::centred,
               true);
}

void PluginAboutPage::resized()
{
    auto area = getLocalBounds().reduced(8, 6);
    constexpr int kButtonW = 200;
    constexpr int kButtonH = 36;
    constexpr int kButtonGap = 16;
    constexpr int kButtonBottomPadding = 22;

    area.removeFromBottom(kButtonBottomPadding);
    auto buttonStrip = area.removeFromBottom(kButtonH);

    const int totalButtonsW = kButtonW * 2 + kButtonGap;
    const int buttonX = buttonStrip.getX() + (buttonStrip.getWidth() - totalButtonsW) / 2;
    gitHubButton_.setBounds(buttonX, buttonStrip.getY(), kButtonW, kButtonH);
    coffeeButton_.setBounds(buttonX + kButtonW + kButtonGap, buttonStrip.getY(), kButtonW, kButtonH);
}