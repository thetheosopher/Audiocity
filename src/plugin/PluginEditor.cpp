#include "PluginEditor.h"

#include "ImportedProgramState.h"
#include "LibraryFileIndex.h"
#include "PeakPreviewCache.h"
#include "PluginProcessor.h"
#include "ProgramMappingModel.h"
#include "../engine/AudioFileSupport.h"
#include "../engine/LegacyNkiProbe.h"
#include <BinaryData.h>

#include <algorithm>
#include <cmath>
#include <thread>
#include <utility>

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
juce::Colour uiBackgroundColour() { return juce::Colour(0xff0f141b); }
juce::Colour uiChromeColour() { return juce::Colour(0xff151c25); }
juce::Colour uiPanelColour() { return juce::Colour(0xff1a212c); }
juce::Colour uiPanelRaisedColour() { return juce::Colour(0xff202834); }
juce::Colour uiBorderColour() { return juce::Colour(0xff313d4c); }
juce::Colour uiTextStrongColour() { return juce::Colour(0xffedf3ff); }
juce::Colour uiTextMutedColour() { return juce::Colour(0xff9ba7b9); }
juce::Colour uiAccentColour() { return juce::Colour(0xff78d7ff); }
juce::Colour uiAccentAmberColour() { return juce::Colour(0xffd8a55a); }
juce::Colour uiAccentGreenColour() { return juce::Colour(0xff6fd6ae); }

void paintSectionCard(juce::Graphics& g, juce::Rectangle<float> box, const juce::String& title)
{
    g.setColour(uiPanelColour());
    g.fillRoundedRectangle(box, 8.0f);

    g.setColour(uiBorderColour());
    g.drawRoundedRectangle(box.reduced(0.5f), 8.0f, 1.0f);

    auto header = box.removeFromTop(24.0f);
    g.setColour(uiPanelRaisedColour());
    g.fillRoundedRectangle(header, 8.0f);
    g.fillRect(header.withTrimmedTop(8.0f));

    auto accent = juce::Rectangle<float>(header.getX() + 9.0f, header.getY() + 6.0f, 3.0f, header.getHeight() - 12.0f);
    g.setColour(uiAccentColour());
    g.fillRoundedRectangle(accent, 1.5f);

    g.setColour(uiTextMutedColour().brighter(0.14f));
    g.setFont(juce::Font(juce::FontOptions(11.5f)).boldened());
    g.drawText(title, header.withTrimmedLeft(18.0f), juce::Justification::centredLeft, false);
}

class TabTextLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g,
                       bool isMouseOver, bool isMouseDown) override
    {
        auto area = button.getActiveArea().toFloat().reduced(5.0f, 3.0f);
        const bool isFront = button.isFrontTab();

        if (isFront)
        {
            g.setColour(uiPanelRaisedColour().brighter(0.04f));
            g.fillRoundedRectangle(area, 7.0f);
            g.setColour(uiBorderColour().brighter(0.08f));
            g.drawRoundedRectangle(area, 7.0f, 1.0f);
        }
        else if (isMouseOver || isMouseDown)
        {
            g.setColour(juce::Colours::white.withAlpha(isMouseDown ? 0.08f : 0.04f));
            g.fillRoundedRectangle(area, 7.0f);
        }

        if (isFront)
        {
            auto underline = area.withY(area.getBottom() - 2.5f).withHeight(2.5f);
            g.setColour(uiAccentColour());
            g.fillRoundedRectangle(underline, 1.25f);
        }

        drawTabButtonText(button, g, isMouseOver, isMouseDown);
    }

    void drawTabButtonText(juce::TabBarButton& button, juce::Graphics& g,
                           bool isMouseOver, bool isMouseDown) override
    {
        juce::ignoreUnused(isMouseDown);

        auto colour = button.isFrontTab()
            ? uiTextStrongColour()
            : uiTextMutedColour().withAlpha(isMouseOver ? 0.96f : 0.82f);
        if (auto* bar = button.findParentComponentOfClass<juce::TabbedButtonBar>())
        {
            const auto colourId = button.isFrontTab()
                ? juce::TabbedButtonBar::frontTextColourId
                : juce::TabbedButtonBar::tabTextColourId;

            if (bar->isColourSpecified(colourId))
                colour = bar->findColour(colourId);
            else if (isColourSpecified(colourId))
                colour = findColour(colourId);
        }

            auto font = juce::Font(juce::FontOptions(13.5f));
        if (button.isFrontTab())
            font = font.boldened();

        g.setColour(colour);
        g.setFont(font);
        g.drawText(button.getButtonText(), button.getTextArea(), juce::Justification::centred, false);
    }
};

TabTextLookAndFeel& getTabTextLookAndFeel()
{
    static TabTextLookAndFeel lookAndFeel;
    return lookAndFeel;
}

constexpr int kEditorTabBarHeight = 34;
constexpr int kEditorTabBarGap = 8;
constexpr int kSamplePresetBarHeight = 32;
constexpr int kSamplePresetControlHeight = 28;

constexpr auto kPresetFileExtension = ".acp";

int computeWaveformPeakResolution(const int waveformWidthPixels)
{
    const auto width = juce::jmax(1, waveformWidthPixels);
    return juce::jlimit(2048, 32768, juce::jmax(4096, width * 8));
}

int computePlayerKeyboardPanelHeight(const int availableWidth)
{
    return juce::jlimit(110, 190, availableWidth / 6);
}

juce::String formatHzAsKHz(const double sampleRate)
{
    if (sampleRate <= 0.0)
        return "-";

    return juce::String(sampleRate / 1000.0, 1) + " kHz";
}

juce::String formatHzNoDecimals(const double sampleRate)
{
    if (sampleRate <= 0.0)
        return "-";

    return juce::String(static_cast<int>(std::round(sampleRate))) + " Hz";
}

juce::String formatDurationFromSamples(const int sampleCount, const double sampleRate)
{
    if (sampleCount <= 0 || sampleRate <= 0.0)
        return "0.00 s";

    const auto seconds = static_cast<double>(sampleCount) / sampleRate;
    return juce::String(seconds, seconds >= 10.0 ? 1 : 2) + " s";
}

juce::String formatFileSizeString(const std::int64_t bytes)
{
    if (bytes <= 0)
        return "-";

    const auto value = static_cast<double>(bytes);
    constexpr double kKB = 1024.0;
    constexpr double kMB = kKB * 1024.0;
    constexpr double kGB = kMB * 1024.0;

    if (value >= kGB)
        return juce::String(value / kGB, 2) + " GB";
    if (value >= kMB)
        return juce::String(value / kMB, 2) + " MB";
    if (value >= kKB)
        return juce::String(value / kKB, 1) + " KB";

    return juce::String(bytes) + " B";
}

constexpr int kPlayerKeyboardMinMidiNote = 24;  // C0
constexpr int kPlayerKeyboardMaxMidiNote = 120; // C8

int countWhiteKeysInRange(const int minMidiNote, const int maxMidiNote)
{
    int whiteKeys = 0;
    for (int midiNote = juce::jmax(0, minMidiNote); midiNote <= juce::jmin(127, maxMidiNote); ++midiNote)
    {
        const auto noteClass = midiNote % 12;
        const bool isWhite = noteClass == 0 || noteClass == 2 || noteClass == 4
            || noteClass == 5 || noteClass == 7 || noteClass == 9 || noteClass == 11;
        if (isWhite)
            ++whiteKeys;
    }

    return juce::jmax(1, whiteKeys);
}

juce::String formatMidiNoteName(const int midiNote)
{
    static constexpr const char* kNoteNames[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    const auto clamped = juce::jlimit(0, 127, midiNote);
    const auto name = kNoteNames[clamped % 12];
    const auto octave = (clamped / 12) - 2;
    return juce::String(name) + juce::String(octave) + " (" + juce::String(clamped) + ")";
}

juce::String formatMidiNoteCompactName(const int midiNote)
{
    static constexpr const char* kNoteNames[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    const auto clamped = juce::jlimit(0, 127, midiNote);
    const auto name = kNoteNames[clamped % 12];
    const auto octave = (clamped / 12) - 2;
    return juce::String(name) + juce::String(octave);
}

juce::String formatPlayerPadButtonLabel(const int padIndex,
                                        const audiocity::plugin::PlayerPadAssignment& assignment,
                                        const bool compact)
{
    if (compact)
        return "P" + juce::String(padIndex + 1) + "  " + formatMidiNoteCompactName(assignment.noteNumber);

    return "Pad " + juce::String(padIndex + 1)
        + "  " + formatMidiNoteName(assignment.noteNumber)
        + "  Vel " + juce::String(assignment.velocity);
}

juce::String formatCompactPeakDb(const float peak)
{
    const auto db = juce::Decibels::gainToDecibels(peak, -100.0f);
    if (db <= -99.0f)
        return "-inf";

    return juce::String(std::round(db), 0);
}

int computePersistentPerformanceStripHeight(const int availableHeight)
{
    return juce::jlimit(156, 196, availableHeight / 4);
}

int computePersistentBrowserRailWidth(const int availableWidth)
{
    return juce::jlimit(248, 320, availableWidth / 4);
}

constexpr int kResponsiveEditorHorizontalMargin = 28;
constexpr int kSampleInspectorMinContentWidth = 900;
constexpr int kSampleTriColumnMinContentWidth = 1120;
constexpr int kCollapsedSampleInspectorCardHeight = 36;
constexpr int kSampleOutputInspectorCardHeight = 186;
constexpr int kExpandedSampleFilterModInspectorCardHeight = 282;
constexpr int kExpandedSampleEffectsInspectorCardHeight = 230;
constexpr int kSectionCardToggleWidth = 60;
constexpr int kSectionCardToggleRightPadding = 18;

int getSampleInspectorCardHeight(const bool expanded, const int expandedHeight) noexcept
{
    return expanded ? expandedHeight : kCollapsedSampleInspectorCardHeight;
}

juce::Rectangle<int> getSampleInspectorCardHeaderBounds(juce::Rectangle<int> bounds)
{
    bounds.setHeight(24);
    return bounds;
}

juce::Rectangle<int> getSectionCardToggleBounds(juce::Rectangle<int> bounds)
{
    auto headerBounds = getSampleInspectorCardHeaderBounds(bounds);
    auto toggleBounds = headerBounds.removeFromRight(kSectionCardToggleWidth + kSectionCardToggleRightPadding);
    return toggleBounds.withTrimmedRight(kSectionCardToggleRightPadding);
}

void paintSampleInspectorCardToggle(juce::Graphics& g,
                                    const juce::Rectangle<int> bounds,
                                    const bool expanded)
{
    if (bounds.isEmpty())
        return;

    g.setColour(expanded ? uiTextStrongColour() : uiTextMutedColour().brighter(0.08f));
    g.setFont(juce::Font(juce::FontOptions(10.5f)).boldened());
    g.drawText(expanded ? "- Hide" : "+ Show", getSectionCardToggleBounds(bounds), juce::Justification::centredRight, false);
}

enum class SampleLayoutMode
{
    inlineStack,
    workspaceInspector,
    browserWorkspaceInspector
};

int computeResponsiveContentWidth(const int editorWidth)
{
    return juce::jmax(0, editorWidth - kResponsiveEditorHorizontalMargin);
}

SampleLayoutMode resolveSampleLayoutModeForWidth(const int contentWidth) noexcept
{
    if (contentWidth >= kSampleTriColumnMinContentWidth)
        return SampleLayoutMode::browserWorkspaceInspector;

    if (contentWidth >= kSampleInspectorMinContentWidth)
        return SampleLayoutMode::workspaceInspector;

    return SampleLayoutMode::inlineStack;
}

int filterModeToComboId(const audiocity::engine::EngineCore::FilterSettings::Mode mode)
{
    using Mode = audiocity::engine::EngineCore::FilterSettings::Mode;
    switch (mode)
    {
        case Mode::lowPass12: return 1;
        case Mode::lowPass24: return 2;
        case Mode::highPass12: return 3;
        case Mode::highPass24: return 4;
        case Mode::bandPass12: return 5;
        case Mode::notch12: return 6;
        default: return 1;
    }
}

audiocity::engine::EngineCore::FilterSettings::Mode comboIdToFilterMode(const int comboId)
{
    using Mode = audiocity::engine::EngineCore::FilterSettings::Mode;
    switch (comboId)
    {
        case 2: return Mode::lowPass24;
        case 3: return Mode::highPass12;
        case 4: return Mode::highPass24;
        case 5: return Mode::bandPass12;
        case 6: return Mode::notch12;
        case 1:
        default:
            return Mode::lowPass12;
    }
}

int lfoShapeToComboId(const audiocity::engine::EngineCore::FilterSettings::LfoShape shape)
{
    using LfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape;
    switch (shape)
    {
        case LfoShape::sine: return 1;
        case LfoShape::triangle: return 2;
        case LfoShape::square: return 3;
        case LfoShape::sawUp: return 4;
        case LfoShape::sawDown: return 5;
        default: return 1;
    }
}

audiocity::engine::EngineCore::FilterSettings::LfoShape comboIdToLfoShape(const int comboId)
{
    using LfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape;
    switch (comboId)
    {
        case 2: return LfoShape::triangle;
        case 3: return LfoShape::square;
        case 4: return LfoShape::sawUp;
        case 5: return LfoShape::sawDown;
        case 1:
        default:
            return LfoShape::sine;
    }
}

class PadAssignmentDialogContent final : public juce::Component
{
public:
    PadAssignmentDialogContent(const int initialNote,
                               const int initialVelocity,
                               std::function<void(int, int)> onAccepted)
        : onAccepted_(std::move(onAccepted))
    {
        addAndMakeVisible(noteLabel_);
        addAndMakeVisible(noteSlider_);
        addAndMakeVisible(velocityLabel_);
        addAndMakeVisible(velocitySlider_);
        addAndMakeVisible(cancelButton_);
        addAndMakeVisible(applyButton_);

        noteLabel_.setText("MIDI Note", juce::dontSendNotification);
        noteLabel_.setJustificationType(juce::Justification::centredLeft);
        velocityLabel_.setText("Velocity", juce::dontSendNotification);
        velocityLabel_.setJustificationType(juce::Justification::centredLeft);

        noteSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        noteSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
        noteSlider_.setRange(0, 127, 1);
        noteSlider_.setValue(juce::jlimit(0, 127, initialNote), juce::dontSendNotification);

        velocitySlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        velocitySlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
        velocitySlider_.setRange(1, 127, 1);
        velocitySlider_.setValue(juce::jlimit(1, 127, initialVelocity), juce::dontSendNotification);

        cancelButton_.setButtonText("Cancel");
        applyButton_.setButtonText("Apply");

        cancelButton_.onClick = [this]
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(0);
        };

        applyButton_.onClick = [this]
        {
            if (onAccepted_)
            {
                onAccepted_(static_cast<int>(noteSlider_.getValue()),
                            static_cast<int>(velocitySlider_.getValue()));
            }
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(1);
        };
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        noteLabel_.setBounds(area.removeFromTop(18));
        noteSlider_.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
        velocityLabel_.setBounds(area.removeFromTop(18));
        velocitySlider_.setBounds(area.removeFromTop(28));
        area.removeFromTop(12);

        auto buttons = area.removeFromTop(26);
        applyButton_.setBounds(buttons.removeFromRight(78));
        buttons.removeFromRight(6);
        cancelButton_.setBounds(buttons.removeFromRight(78));
    }

private:
    juce::Label noteLabel_;
    juce::Slider noteSlider_;
    juce::Label velocityLabel_;
    juce::Slider velocitySlider_;
    juce::TextButton cancelButton_;
    juce::TextButton applyButton_;
    std::function<void(int, int)> onAccepted_;
};

class ImportedProgramChoiceDialogContent final : public juce::Component
{
public:
    ImportedProgramChoiceDialogContent(const juce::String& fileName,
                                      const std::vector<audiocity::plugin::ImportedProgramChoice>& choices,
                                                                            std::function<void(int)> onAccepted,
                                                                            std::function<void()> onCancelled)
                : choices_(choices),
                    onAccepted_(std::move(onAccepted)),
                    onCancelled_(std::move(onCancelled))
    {
        addAndMakeVisible(messageLabel_);
        addAndMakeVisible(choiceLabel_);
        addAndMakeVisible(choiceCombo_);
        addAndMakeVisible(detailLabel_);
        addAndMakeVisible(cancelButton_);
        addAndMakeVisible(importButton_);

        messageLabel_.setText("Select the embedded preset to import from " + fileName + ".",
                              juce::dontSendNotification);
        messageLabel_.setJustificationType(juce::Justification::centredLeft);

        choiceLabel_.setText("Preset", juce::dontSendNotification);
        choiceLabel_.setJustificationType(juce::Justification::centredLeft);

        detailLabel_.setJustificationType(juce::Justification::centredLeft);
        detailLabel_.setColour(juce::Label::textColourId, uiTextMutedColour());

        for (std::size_t index = 0; index < choices_.size(); ++index)
            choiceCombo_.addItem(choices_[index].label, static_cast<int>(index) + 1);

        choiceCombo_.onChange = [this]
        {
            const auto selectedIndex = choiceCombo_.getSelectedItemIndex();
            if (juce::isPositiveAndBelow(selectedIndex, static_cast<int>(choices_.size())))
            {
                detailLabel_.setText(choices_[static_cast<std::size_t>(selectedIndex)].detail,
                                     juce::dontSendNotification);
            }
        };

        if (!choices_.empty())
            choiceCombo_.setSelectedId(1, juce::sendNotificationSync);

        cancelButton_.setButtonText("Cancel");
        importButton_.setButtonText("Import");

        cancelButton_.onClick = [this]
        {
            if (onCancelled_)
                onCancelled_();
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(0);
        };

        importButton_.onClick = [this]
        {
            const auto selectedIndex = choiceCombo_.getSelectedItemIndex();
            if (juce::isPositiveAndBelow(selectedIndex, static_cast<int>(choices_.size())) && onAccepted_)
                onAccepted_(choices_[static_cast<std::size_t>(selectedIndex)].choiceIndex);

            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(1);
        };
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        messageLabel_.setBounds(area.removeFromTop(22));
        area.removeFromTop(8);
        choiceLabel_.setBounds(area.removeFromTop(18));
        choiceCombo_.setBounds(area.removeFromTop(26));
        area.removeFromTop(8);
        detailLabel_.setBounds(area.removeFromTop(20));
        area.removeFromTop(12);

        auto buttons = area.removeFromTop(26);
        importButton_.setBounds(buttons.removeFromRight(78));
        buttons.removeFromRight(6);
        cancelButton_.setBounds(buttons.removeFromRight(78));
    }

private:
    std::vector<audiocity::plugin::ImportedProgramChoice> choices_;
    juce::Label messageLabel_;
    juce::Label choiceLabel_;
    juce::ComboBox choiceCombo_;
    juce::Label detailLabel_;
    juce::TextButton cancelButton_;
    juce::TextButton importButton_;
    std::function<void(int)> onAccepted_;
    std::function<void()> onCancelled_;
};

void showImportedProgramChoiceDialog(juce::Component* owner,
                                     const juce::File& file,
                                     const audiocity::plugin::ImportedProgramChoiceProbe& probe,
                                     std::function<void(std::optional<int>)> onCompleted)
{
    if (!probe.hasMultipleChoices())
    {
        if (onCompleted)
            onCompleted(std::nullopt);
        return;
    }

    auto content = std::make_unique<ImportedProgramChoiceDialogContent>(file.getFileName(),
                                                                        probe.choices,
                                                                        [onCompleted](const int choiceIndex)
                                                                        {
                                                                            if (onCompleted)
                                                                                onCompleted(choiceIndex);
                                                                        },
                                                                        [onCompleted]()
                                                                        {
                                                                            if (onCompleted)
                                                                                onCompleted(std::nullopt);
                                                                        });
    content->setSize(460, 150);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Import " + audiocity::plugin::importedProgramFormatBadge(probe.format) + " Preset";
    options.content.setOwned(content.release());
    options.componentToCentreAround = owner;
    options.dialogBackgroundColour = juce::Colour(0xff252538);
    options.escapeKeyTriggersCloseButton = false;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

struct SamplePreviewData
{
    std::vector<float> peaks;
    juce::String metadataLine;
    juce::String loopFormatBadge;
    juce::String loopMetadataLine;
};

auto buildPreviewAndMetadata(const juce::File& file) -> SamplePreviewData
{
    auto getMetadataValueCaseInsensitive = [](const juce::StringPairArray& metadata,
                                              const juce::String& key) -> juce::String
    {
        const auto keys = metadata.getAllKeys();
        for (int i = 0; i < keys.size(); ++i)
        {
            if (keys[i].equalsIgnoreCase(key))
                return metadata.getValue(keys[i], {});
        }
        return {};
    };

    auto parseEmbeddedRootNote = [&getMetadataValueCaseInsensitive](const juce::StringPairArray& metadata) -> int
    {
        static const juce::StringArray candidateKeys
        {
            "MidiUnityNote",
            "RootNote",
            "ACID Root Note",
            "AcidRootNote",
            "acidrootnote"
        };

        for (const auto& key : candidateKeys)
        {
            const auto value = getMetadataValueCaseInsensitive(metadata, key).trim();
            if (value.isEmpty())
                continue;

            if (value.containsOnly("-0123456789"))
                return juce::jlimit(0, 127, value.getIntValue());
        }

        return -1;
    };

    auto parseLoopPoint = [&getMetadataValueCaseInsensitive](const juce::StringPairArray& metadata,
                                                              const juce::String& key) -> int
    {
        const auto value = getMetadataValueCaseInsensitive(metadata, key).trim();
        if (value.isEmpty() || !value.containsOnly("-0123456789"))
            return -1;

        return value.getIntValue();
    };

    auto parseTempo = [&getMetadataValueCaseInsensitive](const juce::StringPairArray& metadata) -> double
    {
        static const juce::StringArray candidateKeys
        {
            "Tempo",
            "BPM",
            "ACID Tempo",
            "AcidTempo",
            "acidtempo"
        };

        for (const auto& key : candidateKeys)
        {
            const auto value = getMetadataValueCaseInsensitive(metadata, key).trim();
            if (value.isEmpty())
                continue;

            const auto bpm = value.getDoubleValue();
            if (bpm > 0.0)
                return bpm;
        }

        return 0.0;
    };

    auto detectLoopFormatBadge = [&parseEmbeddedRootNote, &parseLoopPoint](
        const juce::File& sampleFile, const juce::StringPairArray& metadata) -> juce::String
    {
        const auto hasRootNote = parseEmbeddedRootNote(metadata) >= 0;
        const auto loopStart = parseLoopPoint(metadata, "Loop0Start");
        const auto loopEnd = parseLoopPoint(metadata, "Loop0End");
        const auto hasLoop = loopStart >= 0 && loopEnd > loopStart;
        if (!(hasRootNote && hasLoop))
            return {};

        const auto ext = sampleFile.getFileExtension().toLowerCase();
        if (ext == ".wav")
            return "Acidized";
        if (ext == ".aif" || ext == ".aiff")
            return "Apple Loop";
        return {};
    };

    auto buildData = [&detectLoopFormatBadge, &parseEmbeddedRootNote, &parseLoopPoint, &parseTempo](
        const juce::File& sampleFile) -> SamplePreviewData
    {
        constexpr int kPeakCount = 256;
        SamplePreviewData out;
        out.peaks.assign(static_cast<std::size_t>(kPeakCount), 0.0f);

        juce::AudioFormatManager formatManager;
        audiocity::engine::audio_file::registerAudioFormats(formatManager);
        auto openResult = audiocity::engine::audio_file::openReaderForFile(formatManager, sampleFile);
        auto reader = std::move(openResult.reader);
        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            out.metadataLine = "SR: --  Ch: --  Bit Depth: --  Duration: --  Samples: --";
            return out;
        }

        out.loopFormatBadge = detectLoopFormatBadge(sampleFile, reader->metadataValues);

        if (out.loopFormatBadge.isNotEmpty())
        {
            const auto root = parseEmbeddedRootNote(reader->metadataValues);
            const auto loopStart = parseLoopPoint(reader->metadataValues, "Loop0Start");
            const auto loopEnd = parseLoopPoint(reader->metadataValues, "Loop0End");
            const auto tempoBpm = parseTempo(reader->metadataValues);

            juce::StringArray parts;
            if (root >= 0)
                parts.add("Root: " + formatMidiNoteName(root));
            if (loopStart >= 0 && loopEnd > loopStart)
                parts.add("Loop: " + juce::String(loopStart) + "-" + juce::String(loopEnd));
            if (tempoBpm > 0.0)
                parts.add("Tempo: " + juce::String(tempoBpm, 2) + " BPM");

            out.loopMetadataLine = parts.joinIntoString("  |  ");
        }

        const auto totalSamples = static_cast<int64_t>(reader->lengthInSamples);
        const auto sampleRateHz = static_cast<int>(std::round(reader->sampleRate));
        const auto totalMs = static_cast<int64_t>(std::round((static_cast<double>(totalSamples) * 1000.0) / reader->sampleRate));
        const auto minutes = static_cast<int>(totalMs / 60000);
        const auto seconds = static_cast<int>((totalMs % 60000) / 1000);
        const auto millis = static_cast<int>(totalMs % 1000);
        const auto durationText = juce::String::formatted("%02d:%02d.%03d", minutes, seconds, millis);
        out.metadataLine = "SR: " + juce::String(sampleRateHz)
            + " Hz  Ch: " + juce::String(static_cast<int>(reader->numChannels))
            + "  Bit Depth: " + juce::String(reader->bitsPerSample)
            + "  Duration: " + durationText
            + "  Samples: " + juce::String(static_cast<juce::int64>(totalSamples));

        juce::AudioBuffer<float> scratchBuffer(1, 4096);
        for (int i = 0; i < kPeakCount; ++i)
        {
            const auto start = (static_cast<int64_t>(i) * totalSamples) / kPeakCount;
            const auto end = juce::jmax(start + 1, (static_cast<int64_t>(i + 1) * totalSamples) / kPeakCount);

            auto maxAbs = 0.0f;
            int64_t position = start;
            while (position < end)
            {
                const auto chunk = static_cast<int>(juce::jmin<int64_t>(scratchBuffer.getNumSamples(), end - position));
                if (!reader->read(&scratchBuffer, 0, chunk, position, true, true))
                    break;

                const auto* samples = scratchBuffer.getReadPointer(0);
                for (int s = 0; s < chunk; ++s)
                    maxAbs = juce::jmax(maxAbs, std::abs(samples[s]));

                position += chunk;
            }

            out.peaks[static_cast<std::size_t>(i)] = juce::jlimit(0.0f, 1.0f, maxAbs);
        }

        return out;
    };

    return buildData(file);
}
}

void AudiocityAudioProcessorEditor::GeneratedWaveformView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff121629));
    g.setColour(juce::Colours::white.withAlpha(0.22f));
    g.drawRect(getLocalBounds(), 1);

    if (waveform_.empty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawText("Generate a waveform", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto bounds = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
    const auto midY = bounds.getCentreY();

    g.setColour(juce::Colours::white.withAlpha(0.07f));
    constexpr int kGridDivisionsX = 8;
    constexpr int kGridDivisionsY = 4;
    for (int i = 1; i < kGridDivisionsX; ++i)
    {
        const auto x = bounds.getX() + bounds.getWidth() * (static_cast<float>(i) / static_cast<float>(kGridDivisionsX));
        g.drawLine(x, bounds.getY(), x, bounds.getBottom(), 1.0f);
    }
    for (int i = 1; i < kGridDivisionsY; ++i)
    {
        const auto y = bounds.getY() + bounds.getHeight() * (static_cast<float>(i) / static_cast<float>(kGridDivisionsY));
        g.drawLine(bounds.getX(), y, bounds.getRight(), y, 1.0f);
    }

    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawLine(bounds.getX(), midY, bounds.getRight(), midY, 1.0f);

    juce::Path path;
    const auto count = static_cast<int>(waveform_.size());
    for (int i = 0; i < count; ++i)
    {
        const auto x = bounds.getX() + (static_cast<float>(i) / static_cast<float>(juce::jmax(1, count - 1))) * bounds.getWidth();
        const auto y = midY - juce::jlimit(-1.0f, 1.0f, waveform_[static_cast<std::size_t>(i)]) * (bounds.getHeight() * 0.45f);
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }

    g.setColour(juce::Colour(0xff59ddff));
    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

int AudiocityAudioProcessorEditor::GeneratedWaveformView::sampleIndexFromX(const float x) const
{
    if (waveform_.empty())
        return 0;

    const auto bounds = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
    const auto norm = juce::jlimit(0.0f, 1.0f, (x - bounds.getX()) / juce::jmax(1.0f, bounds.getWidth()));
    return juce::jlimit(0, static_cast<int>(waveform_.size()) - 1,
        static_cast<int>(std::round(norm * static_cast<float>(juce::jmax(1, static_cast<int>(waveform_.size()) - 1)))));
}

float AudiocityAudioProcessorEditor::GeneratedWaveformView::sampleValueFromY(const float y) const
{
    const auto bounds = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
    const auto norm = juce::jlimit(0.0f, 1.0f, (y - bounds.getY()) / juce::jmax(1.0f, bounds.getHeight()));
    return juce::jlimit(-1.0f, 1.0f, 1.0f - norm * 2.0f);
}

void AudiocityAudioProcessorEditor::GeneratedWaveformView::applyPoint(const juce::Point<float>& position, const bool interpolateFromLast)
{
    if (waveform_.empty())
        return;

    const auto currentIndex = sampleIndexFromX(position.x);
    const auto currentValue = sampleValueFromY(position.y);

    if (interpolateFromLast && lastDrawIndex_ >= 0)
    {
        const auto startIndex = juce::jmin(lastDrawIndex_, currentIndex);
        const auto endIndex = juce::jmax(lastDrawIndex_, currentIndex);
        const auto distance = juce::jmax(1, endIndex - startIndex);

        for (int i = startIndex; i <= endIndex; ++i)
        {
            const auto t = static_cast<float>(i - startIndex) / static_cast<float>(distance);
            const auto value = (lastDrawIndex_ <= currentIndex)
                ? juce::jmap(t, lastDrawValue_, currentValue)
                : juce::jmap(t, currentValue, lastDrawValue_);
            waveform_[static_cast<std::size_t>(i)] = value;
        }
    }
    else
    {
        waveform_[static_cast<std::size_t>(currentIndex)] = currentValue;
    }

    lastDrawIndex_ = currentIndex;
    lastDrawValue_ = currentValue;

    if (onWaveChanged_)
        onWaveChanged_(waveform_);

    repaint();
}

void AudiocityAudioProcessorEditor::GeneratedWaveformView::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown())
        return;

    drawing_ = true;
    lastDrawIndex_ = -1;
    applyPoint(event.position, false);
}

void AudiocityAudioProcessorEditor::GeneratedWaveformView::mouseDrag(const juce::MouseEvent& event)
{
    if (!drawing_)
        return;

    applyPoint(event.position, true);
}

void AudiocityAudioProcessorEditor::GeneratedWaveformView::mouseUp(const juce::MouseEvent&)
{
    drawing_ = false;
    lastDrawIndex_ = -1;
}

void AudiocityAudioProcessorEditor::CaptureWaveformView::setState(
    const int totalSamples,
    const int visibleStart,
    const int visibleEnd,
    std::vector<MinMax> waveform,
    const int selectionStart,
    const int selectionEnd,
    const double sampleRate,
    const bool recording)
{
    totalSamples_ = juce::jmax(0, totalSamples);
    visibleStart_ = juce::jlimit(0, totalSamples_, visibleStart);
    visibleEnd_ = juce::jlimit(visibleStart_, totalSamples_, visibleEnd);
    waveform_ = std::move(waveform);
    selectionStart_ = juce::jlimit(0, totalSamples_, selectionStart);
    selectionEnd_ = juce::jlimit(selectionStart_, totalSamples_, selectionEnd);
    sampleRate_ = juce::jmax(1.0, sampleRate);
    recording_ = recording;
    repaint();
}

void AudiocityAudioProcessorEditor::CaptureWaveformView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff121629));
    g.setColour(juce::Colour(0xff3a3a52));
    g.drawRect(getLocalBounds(), 1);

    const auto bounds = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
    if (bounds.getWidth() <= 1.0f || bounds.getHeight() <= 1.0f)
        return;

    if (waveform_.empty() || totalSamples_ <= 0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawText("Record input to capture audio", getLocalBounds(), juce::Justification::centred);
        return;
    }

    if (selectionEnd_ > selectionStart_)
    {
        const auto sx = xFromSample(selectionStart_);
        const auto ex = xFromSample(selectionEnd_);
        g.setColour(juce::Colour(0xff61d9ff).withAlpha(0.18f));
        g.fillRect(juce::Rectangle<float>(juce::jmin(sx, ex), bounds.getY(), std::abs(ex - sx), bounds.getHeight()));
    }

    const auto midY = bounds.getCentreY();
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawLine(bounds.getX(), midY, bounds.getRight(), midY, 1.0f);

    g.setColour(juce::Colour(0xff59ddff));
    const auto peakCount = static_cast<int>(waveform_.size());
    for (int i = 0; i < peakCount; ++i)
    {
        const auto norm = peakCount > 1 ? static_cast<float>(i) / static_cast<float>(peakCount - 1) : 0.0f;
        const auto x = bounds.getX() + norm * bounds.getWidth();
        const auto minY = midY - juce::jlimit(-1.0f, 1.0f, waveform_[static_cast<std::size_t>(i)].max) * bounds.getHeight() * 0.45f;
        const auto maxY = midY - juce::jlimit(-1.0f, 1.0f, waveform_[static_cast<std::size_t>(i)].min) * bounds.getHeight() * 0.45f;
        g.drawLine(x, minY, x, maxY, 1.0f);
    }

    if (recording_)
    {
        g.setColour(juce::Colour(0xffff5b5b));
        g.fillEllipse(bounds.getX() + 6.0f, bounds.getY() + 6.0f, 10.0f, 10.0f);
    }

    if (selectionEnd_ > selectionStart_)
    {
        const auto selectionSamples = selectionEnd_ - selectionStart_;
        const auto selectionSeconds = static_cast<double>(selectionSamples) / sampleRate_;
        const auto selectionLabel = juce::String(selectionStart_) + ".." + juce::String(selectionEnd_)
            + "  |  " + juce::String(selectionSamples) + " samples  ("
            + juce::String(selectionSeconds, 3) + " s)";

        auto labelArea = bounds.toNearestInt().removeFromTop(22).removeFromRight(360).reduced(6, 2);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(labelArea.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xffd6ecff));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(selectionLabel, labelArea, juce::Justification::centredRight);
    }
}

int AudiocityAudioProcessorEditor::CaptureWaveformView::sampleFromX(const float x) const noexcept
{
    const auto range = juce::jmax(1, visibleEnd_ - visibleStart_);
    const auto bounds = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
    const auto normalized = juce::jlimit(0.0f, 1.0f,
        (x - bounds.getX()) / juce::jmax(1.0f, bounds.getWidth()));
    return visibleStart_ + static_cast<int>(std::round(normalized * static_cast<float>(range)));
}

float AudiocityAudioProcessorEditor::CaptureWaveformView::xFromSample(const int sample) const noexcept
{
    const auto range = juce::jmax(1, visibleEnd_ - visibleStart_);
    const auto bounds = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
    const auto normalized = static_cast<float>(juce::jlimit(visibleStart_, visibleEnd_, sample) - visibleStart_)
        / static_cast<float>(range);
    return bounds.getX() + normalized * bounds.getWidth();
}

void AudiocityAudioProcessorEditor::CaptureWaveformView::updateSelectionFromDrag(const float x)
{
    const auto current = juce::jlimit(0, totalSamples_, sampleFromX(x));
    selectionStart_ = juce::jmin(dragAnchorSample_, current);
    selectionEnd_ = juce::jmax(dragAnchorSample_, current);
    if (onSelectionChanged)
        onSelectionChanged(selectionStart_, selectionEnd_);
    repaint();
}

void AudiocityAudioProcessorEditor::CaptureWaveformView::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown())
        return;

    dragging_ = true;
    dragAnchorSample_ = juce::jlimit(0, totalSamples_, sampleFromX(event.position.x));
    selectionStart_ = dragAnchorSample_;
    selectionEnd_ = dragAnchorSample_;
    if (onSelectionChanged)
        onSelectionChanged(selectionStart_, selectionEnd_);
    repaint();
}

void AudiocityAudioProcessorEditor::CaptureWaveformView::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging_)
        return;

    updateSelectionFromDrag(event.position.x);
}

void AudiocityAudioProcessorEditor::CaptureWaveformView::mouseUp(const juce::MouseEvent& event)
{
    if (!dragging_)
        return;

    updateSelectionFromDrag(event.position.x);
    dragging_ = false;
}

void AudiocityAudioProcessorEditor::AmpEnvelopeGraph::paint(juce::Graphics& g)
{
    g.fillAll(uiChromeColour());
    g.setColour(uiBorderColour());
    g.drawRect(getLocalBounds(), 1);

    const auto geometry = getGeometry();
    const auto area = geometry.area;
    if (area.getWidth() <= 1.0f || area.getHeight() <= 1.0f)
        return;

    g.setColour(uiTextStrongColour().withAlpha(0.07f));
    for (int i = 1; i < 4; ++i)
    {
        const auto y = area.getY() + area.getHeight() * (static_cast<float>(i) / 4.0f);
        g.drawLine(area.getX(), y, area.getRight(), y, 1.0f);
    }

    auto yFromLevel = [&](float level)
    {
        return area.getBottom() - juce::jlimit(0.0f, 1.0f, level) * area.getHeight();
    };

    const auto x0 = area.getX();
    const auto y0 = yFromLevel(0.0f);
    const auto x1 = geometry.attackPoint.x;
    const auto y1 = geometry.attackPoint.y;
    const auto x2 = geometry.decayPoint.x;
    const auto y2 = geometry.decayPoint.y;
    const auto x3 = geometry.releasePoint.x;
    const auto y3 = geometry.releasePoint.y;
    const auto x4 = area.getRight();
    const auto y4 = yFromLevel(0.0f);

    juce::Path fill;
    fill.startNewSubPath(x0, y0);
    fill.lineTo(x1, y1);
    fill.lineTo(x2, y2);
    fill.lineTo(x3, y3);
    fill.lineTo(x4, y4);
    fill.lineTo(x4, area.getBottom());
    fill.lineTo(x0, area.getBottom());
    fill.closeSubPath();

    g.setColour(uiAccentColour().withAlpha(0.16f));
    g.fillPath(fill);

    juce::Path env;
    env.startNewSubPath(x0, y0);
    env.lineTo(x1, y1);
    env.lineTo(x2, y2);
    env.lineTo(x3, y3);
    env.lineTo(x4, y4);

    g.setColour(uiAccentColour());
    g.strokePath(env, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(uiTextMutedColour());
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    const auto labelY = juce::jmin(static_cast<int>(area.getBottom()) + 1, getHeight() - 12);
    const auto drawMarker = [&](const juce::String& text, const float centerX)
    {
        const int markerWidth = 14;
        const auto x = juce::jlimit(0, juce::jmax(0, getWidth() - markerWidth),
            static_cast<int>(std::round(centerX)) - markerWidth / 2);
        g.drawText(text, x, labelY, markerWidth, 10, juce::Justification::centred);
    };

    drawMarker("A", (x0 + x1) * 0.5f);
    drawMarker("D", (x1 + x2) * 0.5f);
    drawMarker("S", (x2 + x3) * 0.5f);
    drawMarker("R", (x3 + x4) * 0.5f);

    if (onEnvelopeEdited)
    {
        const auto drawHandle = [&](juce::Point<float> center, bool active)
        {
            const auto radius = active ? 5.0f : 4.0f;
            g.setColour(uiChromeColour());
            g.fillEllipse(center.x - radius - 1.0f, center.y - radius - 1.0f, (radius + 1.0f) * 2.0f, (radius + 1.0f) * 2.0f);
            g.setColour(active ? uiAccentAmberColour() : uiAccentColour().brighter(0.15f));
            g.fillEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
        };

        drawHandle(geometry.attackPoint, dragHandle_ == DragHandle::attack);
        drawHandle(geometry.decayPoint, dragHandle_ == DragHandle::decaySustain);
        drawHandle(geometry.releasePoint, dragHandle_ == DragHandle::release);

        g.setColour(uiTextMutedColour());
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("Drag nodes", getWidth() - 68, 6, 60, 12, juce::Justification::centredRight);
    }
}

void AudiocityAudioProcessorEditor::AmpEnvelopeGraph::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown() || !onEnvelopeEdited)
        return;

    const auto geometry = getGeometry();
    if (geometry.area.isEmpty())
        return;

    constexpr float kPickRadius = 12.0f;
    const auto distanceTo = [&](juce::Point<float> point)
    {
        return event.position.getDistanceFrom(point);
    };

    const auto attackDistance = distanceTo(geometry.attackPoint);
    const auto decayDistance = distanceTo(geometry.decayPoint);
    const auto releaseDistance = distanceTo(geometry.releasePoint);
    const auto nearest = juce::jmin(attackDistance, decayDistance, releaseDistance);

    if (nearest > kPickRadius)
        return;

    if (nearest == attackDistance)
        dragHandle_ = DragHandle::attack;
    else if (nearest == decayDistance)
        dragHandle_ = DragHandle::decaySustain;
    else
        dragHandle_ = DragHandle::release;

    mouseDrag(event);
}

void AudiocityAudioProcessorEditor::AmpEnvelopeGraph::mouseDrag(const juce::MouseEvent& event)
{
    if (dragHandle_ == DragHandle::none || !onEnvelopeEdited)
        return;

    const auto geometry = getGeometry();
    if (geometry.area.isEmpty())
        return;

    constexpr float kEdgePadding = 2.0f;
    const auto clampedX = juce::jlimit(geometry.area.getX() + kEdgePadding,
        geometry.area.getRight() - kEdgePadding,
        event.position.x);
    const auto proportion = (clampedX - geometry.area.getX()) / geometry.area.getWidth();

    auto nextAttack = attackMs_;
    auto nextDecay = decayMs_;
    auto nextSustain = sustain_;
    auto nextRelease = releaseMs_;

    switch (dragHandle_)
    {
        case DragHandle::attack:
            nextAttack = solveAttackMsForX(proportion, decayMs_, releaseMs_);
            break;
        case DragHandle::decaySustain:
            nextDecay = solveDecayMsForX(proportion, attackMs_, releaseMs_);
            nextSustain = levelFromY(event.position.y);
            break;
        case DragHandle::release:
            nextRelease = solveReleaseMsForX(proportion, attackMs_, decayMs_);
            nextSustain = levelFromY(event.position.y);
            break;
        case DragHandle::none:
            break;
    }

    setEnvelope(nextAttack, nextDecay, nextSustain, nextRelease);
    onEnvelopeEdited(attackMs_, decayMs_, sustain_, releaseMs_);
}

void AudiocityAudioProcessorEditor::AmpEnvelopeGraph::mouseUp(const juce::MouseEvent&)
{
    dragHandle_ = DragHandle::none;
    repaint();
}

AudiocityAudioProcessorEditor::AmpEnvelopeGraph::Geometry AudiocityAudioProcessorEditor::AmpEnvelopeGraph::getGeometry() const
{
    Geometry geometry;
    geometry.area = getLocalBounds().toFloat().reduced(10.0f, 8.0f);

    if (geometry.area.getWidth() <= 1.0f || geometry.area.getHeight() <= 1.0f)
        return geometry;

    const auto attack = juce::jmax(0.1f, attackMs_);
    const auto decay = juce::jmax(0.1f, decayMs_);
    const auto release = juce::jmax(0.1f, releaseMs_);
    const auto total = totalMsFor(attack, decay, release);

    auto xFromTime = [&](float timeMs)
    {
        return geometry.area.getX() + (timeMs / total) * geometry.area.getWidth();
    };
    auto yFromLevel = [&](float level)
    {
        return geometry.area.getBottom() - juce::jlimit(0.0f, 1.0f, level) * geometry.area.getHeight();
    };

    geometry.attackPoint = { xFromTime(attack), yFromLevel(1.0f) };
    geometry.decayPoint = { xFromTime(attack + decay), yFromLevel(sustain_) };
    geometry.releasePoint = { xFromTime(attack + decay + holdMsFor(attack, decay, release)), yFromLevel(sustain_) };
    return geometry;
}

float AudiocityAudioProcessorEditor::AmpEnvelopeGraph::levelFromY(float y) const
{
    const auto area = getGeometry().area;
    if (area.getHeight() <= 1.0f)
        return sustain_;

    return juce::jlimit(0.0f, 1.0f, (area.getBottom() - y) / area.getHeight());
}

float AudiocityAudioProcessorEditor::AmpEnvelopeGraph::holdMsFor(float attackMs, float decayMs, float releaseMs) noexcept
{
    return juce::jmax(5.0f, (attackMs + decayMs + releaseMs) * 0.3f);
}

float AudiocityAudioProcessorEditor::AmpEnvelopeGraph::totalMsFor(float attackMs, float decayMs, float releaseMs) noexcept
{
    return attackMs + decayMs + releaseMs + holdMsFor(attackMs, decayMs, releaseMs);
}

float AudiocityAudioProcessorEditor::AmpEnvelopeGraph::solveAttackMsForX(float proportion, float decayMs, float releaseMs) noexcept
{
    constexpr float kMinMs = 0.1f;
    constexpr float kMaxMs = 5000.0f;
    const auto target = juce::jlimit(0.0005f, 0.76f, proportion);

    auto low = kMinMs;
    auto high = kMaxMs;
    for (int iteration = 0; iteration < 24; ++iteration)
    {
        const auto mid = 0.5f * (low + high);
        const auto midProportion = mid / totalMsFor(mid, decayMs, releaseMs);
        if (midProportion < target)
            low = mid;
        else
            high = mid;
    }

    return 0.5f * (low + high);
}

float AudiocityAudioProcessorEditor::AmpEnvelopeGraph::solveDecayMsForX(float proportion, float attackMs, float releaseMs) noexcept
{
    constexpr float kMinMs = 0.1f;
    constexpr float kMaxMs = 5000.0f;
    const auto target = juce::jlimit(0.001f, 0.85f, proportion);

    auto low = kMinMs;
    auto high = kMaxMs;
    for (int iteration = 0; iteration < 24; ++iteration)
    {
        const auto mid = 0.5f * (low + high);
        const auto midProportion = (attackMs + mid) / totalMsFor(attackMs, mid, releaseMs);
        if (midProportion < target)
            low = mid;
        else
            high = mid;
    }

    return 0.5f * (low + high);
}

float AudiocityAudioProcessorEditor::AmpEnvelopeGraph::solveReleaseMsForX(float proportion, float attackMs, float decayMs) noexcept
{
    constexpr float kMinMs = 0.1f;
    constexpr float kMaxMs = 5000.0f;
    const auto target = juce::jlimit(0.15f, 0.999f, proportion);

    auto low = kMinMs;
    auto high = kMaxMs;
    for (int iteration = 0; iteration < 24; ++iteration)
    {
        const auto mid = 0.5f * (low + high);
        const auto midProportion = (attackMs + decayMs + holdMsFor(attackMs, decayMs, mid)) / totalMsFor(attackMs, decayMs, mid);
        if (midProportion > target)
            low = mid;
        else
            high = mid;
    }

    return 0.5f * (low + high);
}

void AudiocityAudioProcessorEditor::FilterResponseGraph::paint(juce::Graphics& g)
{
    g.fillAll(uiChromeColour());
    g.setColour(uiBorderColour());
    g.drawRect(getLocalBounds(), 1);

    const auto area = getLocalBounds().toFloat().reduced(10.0f, 8.0f);
    if (area.getWidth() <= 2.0f || area.getHeight() <= 2.0f)
        return;

    g.setColour(uiTextStrongColour().withAlpha(0.07f));
    for (int i = 1; i < 4; ++i)
    {
        const auto y = area.getY() + area.getHeight() * (static_cast<float>(i) / 4.0f);
        g.drawLine(area.getX(), y, area.getRight(), y, 1.0f);
    }

    constexpr std::array<float, 4> kGuidesHz{ 100.0f, 1000.0f, 5000.0f, 10000.0f };
    for (const auto hz : kGuidesHz)
    {
        const auto norm = std::log10(hz / 20.0f) / std::log10(1000.0f);
        const auto x = area.getX() + juce::jlimit(0.0f, 1.0f, norm) * area.getWidth();
        g.drawLine(x, area.getY(), x, area.getBottom(), 1.0f);
    }

    const auto q = 0.6f + resonance_ * 12.0f;
    auto magnitudeAtHz = [&](const float hz) -> float
    {
        const auto ratio = juce::jmax(0.00001f, hz / juce::jmax(20.0f, cutoffHz_));
        const auto ratioSquared = ratio * ratio;
        const auto oneMinusRatioSquared = 1.0f - ratioSquared;

        switch (modeId_)
        {
            case 2: // LP24
            {
                const auto lp12 = 1.0f / std::sqrt(1.0f + ratioSquared);
                return lp12 * lp12;
            }
            case 1: // LP12
                return 1.0f / std::sqrt(1.0f + ratioSquared);
            case 4: // HP24
            {
                const auto hp12 = ratio / std::sqrt(1.0f + ratioSquared);
                return hp12 * hp12;
            }
            case 3: // HP12
                return ratio / std::sqrt(1.0f + ratioSquared);
            case 5: // BP12
            {
                const auto denominator = std::sqrt(oneMinusRatioSquared * oneMinusRatioSquared
                                                   + (ratio / q) * (ratio / q));
                return juce::jmax(0.0f, (ratio / q) / juce::jmax(0.00001f, denominator));
            }
            case 6: // Notch
            {
                const auto numerator = std::abs(oneMinusRatioSquared);
                const auto denominator = std::sqrt(oneMinusRatioSquared * oneMinusRatioSquared
                                                   + (ratio / q) * (ratio / q));
                return juce::jmax(0.0f, numerator / juce::jmax(0.00001f, denominator));
            }
            default:
                return 1.0f;
        }
    };

    auto xToHz = [&](const float x) -> float
    {
        const auto norm = juce::jlimit(0.0f, 1.0f, (x - area.getX()) / juce::jmax(1.0f, area.getWidth()));
        return 20.0f * std::pow(1000.0f, norm);
    };

    auto gainToY = [&](const float magnitude) -> float
    {
        constexpr float kMinDb = -30.0f;
        constexpr float kMaxDb = 12.0f;
        const auto db = juce::jlimit(kMinDb, kMaxDb, 20.0f * std::log10(juce::jmax(0.00001f, magnitude)));
        const auto norm = (db - kMinDb) / (kMaxDb - kMinDb);
        return area.getBottom() - norm * area.getHeight();
    };

    juce::Path curve;
    for (int px = 0; px < static_cast<int>(std::round(area.getWidth())); ++px)
    {
        const auto x = area.getX() + static_cast<float>(px);
        const auto hz = xToHz(x);
        const auto y = gainToY(magnitudeAtHz(hz));
        if (px == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }

    g.setColour(uiAccentColour());
    g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto cutoffNorm = std::log10(cutoffHz_ / 20.0f) / std::log10(1000.0f);
    const auto cutoffX = area.getX() + juce::jlimit(0.0f, 1.0f, cutoffNorm) * area.getWidth();
    g.setColour(uiAccentAmberColour().withAlpha(0.94f));
    g.drawLine(cutoffX, area.getY(), cutoffX, area.getBottom(), 1.0f);

    const auto envCutoffHz = juce::jlimit(20.0f, 20000.0f, cutoffHz_ + envAmountHz_);
    const auto envNorm = std::log10(envCutoffHz / 20.0f) / std::log10(1000.0f);
    const auto envX = area.getX() + juce::jlimit(0.0f, 1.0f, envNorm) * area.getWidth();
    g.setColour(uiAccentGreenColour().withAlpha(0.82f));
    g.drawLine(envX, area.getY(), envX, area.getBottom(), 1.0f);
}

void AudiocityAudioProcessorEditor::StereoPeakMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(uiChromeColour());
    g.setColour(uiBorderColour());
    g.drawRect(bounds, 1);

    auto area = bounds.reduced(8, 6);
    if (area.getWidth() < 80 || area.getHeight() < 20)
        return;

    constexpr int labelWidth = 14;
    constexpr int rowGap = 5;
    const int rowHeight = juce::jmax(8, (area.getHeight() - rowGap) / 2);

    auto leftRow = area.removeFromTop(rowHeight);
    area.removeFromTop(rowGap);
    auto rightRow = area.removeFromTop(rowHeight);

    auto drawRow = [&](juce::Rectangle<int> row, const juce::String& label, const float level)
    {
        g.setColour(uiTextMutedColour());
        g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
        g.drawText(label, row.removeFromLeft(labelWidth), juce::Justification::centredLeft);

        auto meterArea = row.reduced(1, 1);
        g.setColour(uiBackgroundColour());
        g.fillRoundedRectangle(meterArea.toFloat(), 2.0f);
        g.setColour(uiBorderColour());
        g.drawRoundedRectangle(meterArea.toFloat(), 2.0f, 1.0f);

        const int segmentCount = 20;
        for (int i = 1; i < segmentCount; ++i)
        {
            const auto x = meterArea.getX() + static_cast<int>(std::round((meterArea.getWidth() * i) / static_cast<float>(segmentCount)));
            g.setColour(uiTextStrongColour().withAlpha(i % 5 == 0 ? 0.20f : 0.10f));
            g.drawVerticalLine(x, static_cast<float>(meterArea.getY() + 1), static_cast<float>(meterArea.getBottom() - 1));
        }

        if (clipZoneEnabled_)
        {
            constexpr float kClipThreshold = 0.70710678f; // ~ -3 dBFS
            const auto thresholdX = meterArea.getX()
                + static_cast<int>(std::round(static_cast<float>(meterArea.getWidth()) * kClipThreshold));

            auto clipArea = meterArea;
            clipArea.setX(juce::jlimit(meterArea.getX(), meterArea.getRight(), thresholdX));
            clipArea.setWidth(meterArea.getRight() - clipArea.getX());
            if (clipArea.getWidth() > 0)
            {
                g.setColour(juce::Colour(0xffff6a6a).withAlpha(0.10f));
                g.fillRect(clipArea);
            }

            g.setColour(juce::Colour(0xffff8f8f).withAlpha(0.55f));
            g.drawVerticalLine(thresholdX, static_cast<float>(meterArea.getY()), static_cast<float>(meterArea.getBottom()));
        }

        const auto clampedLevel = juce::jlimit(0.0f, 1.0f, level);
        auto fillArea = meterArea;
        fillArea.setWidth(static_cast<int>(std::round(static_cast<float>(meterArea.getWidth()) * clampedLevel)));

        if (fillArea.getWidth() > 0)
        {
            auto gradient = juce::ColourGradient(juce::Colour(0xff62d59d),
                                                 static_cast<float>(meterArea.getX()),
                                                 static_cast<float>(meterArea.getCentreY()),
                                                 juce::Colour(0xffef7f73),
                                                 static_cast<float>(meterArea.getRight()),
                                                 static_cast<float>(meterArea.getCentreY()),
                                                 false);
            gradient.addColour(0.72, uiAccentAmberColour());
            g.setGradientFill(gradient);
            g.fillRoundedRectangle(fillArea.toFloat(), 2.0f);

            const int peakX = juce::jlimit(meterArea.getX(), meterArea.getRight() - 1, fillArea.getRight() - 1);
            g.setColour(uiTextStrongColour().withAlpha(0.7f));
            g.drawVerticalLine(peakX, static_cast<float>(meterArea.getY()), static_cast<float>(meterArea.getBottom()));
        }
    };

    drawRow(leftRow, "L", leftLevel_);
    drawRow(rightRow, "R", rightLevel_);
}

// ─── WaveformView ──────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::WaveformView::setState(
    const int totalSamples, std::vector<std::vector<MinMax>> waveformByChannel,
    const int playbackStart, const int playbackEnd,
    const int loopStart, const int loopEnd,
    juce::String loopFormatBadge)
{
    totalSamples_ = juce::jmax(0, totalSamples);
    waveformByChannel_ = std::move(waveformByChannel);
    playbackStart_ = playbackStart;
    playbackEnd_ = playbackEnd;
    loopStart_ = loopStart;
    loopEnd_ = loopEnd;
    loopFormatBadge_ = std::move(loopFormatBadge);
    hoveredSliceBoundarySample_ = -1;
    hoveredSliceRegionIndex_ = -1;

    if (viewSampleCount_ <= 0)
        viewSampleCount_ = juce::jmax(1, totalSamples_);

    clampView();
    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::setVoicePlaybackPositions(const VoicePlaybackPositions& positions)
{
    voicePlaybackPositions_ = positions;
    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::resetView()
{
    viewStartSample_ = 0;
    viewSampleCount_ = juce::jmax(1, totalSamples_);
    clampView();
    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::setViewRange(const int viewStartSample, const int viewSampleCount)
{
    viewStartSample_ = juce::jmax(0, viewStartSample);
    viewSampleCount_ = juce::jmax(0, viewSampleCount);

    if (viewSampleCount_ <= 0)
        viewSampleCount_ = juce::jmax(1, totalSamples_);

    clampView();
    repaint();
}

int AudiocityAudioProcessorEditor::WaveformView::sampleFromX(const float x) const noexcept
{
    const auto width = juce::jmax(1.0f, static_cast<float>(getWidth()));
    const auto norm = juce::jlimit(0.0f, 1.0f, x / width);
    return viewStartSample_ + static_cast<int>(norm * static_cast<float>(juce::jmax(1, viewSampleCount_ - 1)));
}

bool AudiocityAudioProcessorEditor::WaveformView::isSliceViewActive() const noexcept
{
    return loopFormatBadge_ == "SLICE" && sliceMarkers_.size() >= 2;
}

float AudiocityAudioProcessorEditor::WaveformView::xFromSample(const int sample) const noexcept
{
    const auto width = juce::jmax(1.0f, static_cast<float>(getWidth()));
    const auto local = juce::jlimit(0, juce::jmax(1, viewSampleCount_ - 1), sample - viewStartSample_);
    return (static_cast<float>(local) / static_cast<float>(juce::jmax(1, viewSampleCount_ - 1))) * width;
}

int AudiocityAudioProcessorEditor::WaveformView::findNearestSliceBoundarySample(const int sampleIndex,
                                                                                const int toleranceSamples) const noexcept
{
    if (!isSliceViewActive())
        return -1;

    auto nearestBoundary = -1;
    auto bestDistance = toleranceSamples + 1;
    for (const auto markerSample : sliceMarkers_)
    {
        if (markerSample <= 0 || markerSample >= totalSamples_)
            continue;

        const auto distance = std::abs(markerSample - sampleIndex);
        if (distance <= toleranceSamples && distance < bestDistance)
        {
            bestDistance = distance;
            nearestBoundary = markerSample;
        }
    }

    return nearestBoundary;
}

int AudiocityAudioProcessorEditor::WaveformView::findSliceRegionIndexForSample(const int sampleIndex) const noexcept
{
    if (!isSliceViewActive())
        return -1;

    for (int markerIndex = 0; markerIndex + 1 < static_cast<int>(sliceMarkers_.size()); ++markerIndex)
    {
        const auto sliceStart = sliceMarkers_[static_cast<std::size_t>(markerIndex)];
        const auto sliceEnd = sliceMarkers_[static_cast<std::size_t>(markerIndex + 1)];
        if (sliceEnd <= sliceStart)
            continue;

        if (sampleIndex >= sliceStart && sampleIndex < sliceEnd)
            return markerIndex;
    }

    return -1;
}

juce::Range<int> AudiocityAudioProcessorEditor::WaveformView::getSliceRegionBounds(const int sliceRegionIndex) const noexcept
{
    if (!isSliceViewActive() || sliceRegionIndex < 0 || sliceRegionIndex + 1 >= static_cast<int>(sliceMarkers_.size()))
        return {};

    const auto sliceStart = sliceMarkers_[static_cast<std::size_t>(sliceRegionIndex)];
    const auto sliceEnd = sliceMarkers_[static_cast<std::size_t>(sliceRegionIndex + 1)];
    if (sliceEnd <= sliceStart)
        return {};

    return { sliceStart, sliceEnd };
}

void AudiocityAudioProcessorEditor::WaveformView::updateSliceHoverState(const juce::Point<float> position)
{
    const auto clearSliceHoverState = [this]()
    {
        if (hoveredSliceBoundarySample_ == -1 && hoveredSliceRegionIndex_ == -1)
            return;

        hoveredSliceBoundarySample_ = -1;
        hoveredSliceRegionIndex_ = -1;
        repaint();
    };

    if (!isSliceViewActive() || totalSamples_ <= 0)
    {
        clearSliceHoverState();
        return;
    }

    if (!getLocalBounds().toFloat().contains(position))
    {
        clearSliceHoverState();
        return;
    }

    const auto sampleIndex = juce::jlimit(0, juce::jmax(0, totalSamples_ - 1), sampleFromX(position.x));
    const auto samplesPerPixel = static_cast<double>(juce::jmax(1, viewSampleCount_))
        / static_cast<double>(juce::jmax(1, getWidth()));
    const auto boundaryTolerance = juce::jmax(8, static_cast<int>(std::ceil(samplesPerPixel * 8.0)));
    const auto nextBoundary = findNearestSliceBoundarySample(sampleIndex, boundaryTolerance);
    const auto nextRegion = findSliceRegionIndexForSample(sampleIndex);

    if (nextBoundary == hoveredSliceBoundarySample_ && nextRegion == hoveredSliceRegionIndex_)
        return;

    hoveredSliceBoundarySample_ = nextBoundary;
    hoveredSliceRegionIndex_ = nextRegion;
    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::clampView()
{
    const auto total = juce::jmax(1, totalSamples_);
    const auto minWindow = juce::jmin(32, total);
    viewSampleCount_ = juce::jlimit(minWindow, total, viewSampleCount_);
    viewStartSample_ = juce::jlimit(0, juce::jmax(0, total - viewSampleCount_), viewStartSample_);
}

void AudiocityAudioProcessorEditor::WaveformView::zoomAround(const float anchorX, const float zoomFactor)
{
    if (totalSamples_ <= 0)
        return;

    const auto anchorSample = sampleFromX(anchorX);
    const auto minWindow = juce::jmin(32, juce::jmax(1, totalSamples_));
    const auto nextCount = juce::jlimit(minWindow, juce::jmax(1, totalSamples_),
        static_cast<int>(std::round(static_cast<double>(viewSampleCount_) * zoomFactor)));
    const auto anchorNorm = juce::jlimit(0.0, 1.0,
        static_cast<double>(anchorX / juce::jmax(1.0f, static_cast<float>(getWidth()))));

    viewSampleCount_ = nextCount;
    viewStartSample_ = anchorSample - static_cast<int>(anchorNorm * static_cast<double>(viewSampleCount_));
    clampView();
    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::panByPixels(const float deltaX)
{
    if (totalSamples_ <= 0)
        return;

    const auto samplesPerPixel = static_cast<double>(juce::jmax(1, viewSampleCount_))
        / static_cast<double>(juce::jmax(1, getWidth()));
    viewStartSample_ -= static_cast<int>(std::round(static_cast<double>(deltaX) * samplesPerPixel));
    clampView();
    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::paint(juce::Graphics& g)
{
    g.fillAll(uiChromeColour().darker(0.35f));
    g.setColour(uiBorderColour());
    g.drawRect(getLocalBounds(), 1);

    if (totalSamples_ <= 0 || waveformByChannel_.empty())
    {
        g.setColour(uiTextMutedColour());
        g.drawText("Load a sample (WAV/AIFF)", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto bounds = getLocalBounds().toFloat();
    const auto channelCount = juce::jmax(1, static_cast<int>(waveformByChannel_.size()));
    const auto channelHeight = bounds.getHeight() / static_cast<float>(channelCount);
    const auto showCompactSliceLabels = isSliceViewActive() && static_cast<int>(sliceMarkers_.size()) <= 17;
    const auto hoveredSliceBounds = getSliceRegionBounds(hoveredSliceRegionIndex_);

    // ── Playback region markers ──
    const auto pbX1 = xFromSample(playbackStart_);
    const auto pbX2 = xFromSample(playbackEnd_);

    // ── Loop region markers ──
    const auto lx1 = xFromSample(loopStart_);
    const auto lx2 = xFromSample(loopEnd_);
    const auto loopLeft = juce::jmin(lx1, lx2);
    const auto loopRight = juce::jmax(lx1, lx2);

    const auto handleVisualX = [bounds](const float x)
    {
        return juce::jlimit(bounds.getX(), bounds.getRight(), x);
    };
    const auto pbHX1 = handleVisualX(pbX1);
    const auto pbHX2 = handleVisualX(pbX2);
    const auto lpHX1 = handleVisualX(loopLeft);
    const auto lpHX2 = handleVisualX(loopRight);

    for (int channel = 0; channel < channelCount; ++channel)
    {
        const auto laneY = bounds.getY() + static_cast<float>(channel) * channelHeight;
        const auto lane = juce::Rectangle<float>(bounds.getX(), laneY, bounds.getWidth(), channelHeight).reduced(0.0f, 1.0f);
        const auto centerY = lane.getCentreY();

        g.setColour(uiBackgroundColour().withAlpha(0.72f));
        g.fillRect(lane);

        g.setColour(uiBackgroundColour().withAlpha(0.68f));
        if (pbX1 > lane.getX())
            g.fillRect(juce::Rectangle<float>(lane.getX(), lane.getY(), pbX1 - lane.getX(), lane.getHeight()));
        if (pbX2 < lane.getRight())
            g.fillRect(juce::Rectangle<float>(pbX2, lane.getY(), lane.getRight() - pbX2, lane.getHeight()));

        g.setColour(uiAccentAmberColour().withAlpha(0.18f));
        g.fillRect(juce::Rectangle<float>(loopLeft, lane.getY(), juce::jmax(1.0f, loopRight - loopLeft), lane.getHeight()));

        if (!hoveredSliceBounds.isEmpty())
        {
            const auto sliceStartX = handleVisualX(xFromSample(hoveredSliceBounds.getStart()));
            const auto sliceEndX = handleVisualX(xFromSample(hoveredSliceBounds.getEnd()));
            if (sliceEndX > sliceStartX)
            {
                g.setColour(uiAccentColour().withAlpha(channel == 0 ? 0.12f : 0.08f));
                g.fillRect(juce::Rectangle<float>(sliceStartX, lane.getY(), sliceEndX - sliceStartX, lane.getHeight()));
            }
        }

        if (channelCount > 1)
        {
            g.setColour(uiBorderColour().withAlpha(0.65f));
            g.drawHorizontalLine(static_cast<int>(lane.getBottom()), lane.getX(), lane.getRight());
        }

        g.setColour(uiTextMutedColour());
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        juce::String channelName;
        if (channelCount == 2)
            channelName = (channel == 0 ? "Left" : "Right");
        else
            channelName = "Channel " + juce::String(channel + 1);
        g.drawText(channelName, lane.withTrimmedLeft(6.0f).withTrimmedTop(2.0f).removeFromTop(12.0f),
            juce::Justification::centredLeft, false);

        const auto& waveform = waveformByChannel_[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(waveformByChannel_.size()) - 1, channel))];
        const auto bucketCount = static_cast<int>(waveform.size());
        if (bucketCount > 0)
        {
            const auto samplesPerPixel = static_cast<float>(juce::jmax(1, viewSampleCount_))
                / juce::jmax(1.0f, lane.getWidth());
            const auto drawDiscreteSamples = samplesPerPixel <= 2.5f && viewSampleCount_ <= 4096;
            const auto yFromSample = [&](const float sampleValue)
            {
                const auto clamped = juce::jlimit(-1.0f, 1.0f, sampleValue);
                return centerY - clamped * lane.getHeight() * 0.45f;
            };

            if (drawDiscreteSamples)
            {
                const auto firstSample = juce::jlimit(0, juce::jmax(0, totalSamples_ - 1), viewStartSample_);
                const auto lastSample = juce::jlimit(firstSample, juce::jmax(0, totalSamples_ - 1),
                    viewStartSample_ + juce::jmax(1, viewSampleCount_) - 1);

                juce::Path topPath;
                juce::Path bottomPath;
                auto started = false;

                for (int sample = firstSample; sample <= lastSample; ++sample)
                {
                    const auto x = xFromSample(sample);
                    const auto norm = static_cast<float>(sample)
                        / static_cast<float>(juce::jmax(1, totalSamples_ - 1));
                    const auto bucketIndex = juce::jlimit(0, bucketCount - 1,
                        static_cast<int>(std::round(norm * static_cast<float>(juce::jmax(0, bucketCount - 1)))));

                    const auto& range = waveform[static_cast<std::size_t>(bucketIndex)];
                    const auto topSample = displayMode_ == DisplayMode::symmetricEnvelope
                        ? juce::jmax(std::abs(range.max), std::abs(range.min))
                        : range.max;
                    const auto bottomSample = displayMode_ == DisplayMode::symmetricEnvelope
                        ? -juce::jmax(std::abs(range.max), std::abs(range.min))
                        : range.min;
                    const auto topY = yFromSample(topSample);
                    const auto bottomY = yFromSample(bottomSample);

                    if (!started)
                    {
                        topPath.startNewSubPath(x, topY);
                        bottomPath.startNewSubPath(x, bottomY);
                        started = true;
                    }
                    else
                    {
                        topPath.lineTo(x, topY);
                        bottomPath.lineTo(x, bottomY);
                    }

                    g.setColour(uiAccentColour().withAlpha(0.22f));
                    g.drawLine(x, centerY, x, topY, 1.0f);
                    g.drawLine(x, centerY, x, bottomY, 1.0f);
                }

                g.setColour(uiAccentColour().withAlpha(0.96f));
                g.strokePath(topPath, juce::PathStrokeType(1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
                g.setColour(uiAccentColour().withAlpha(0.76f));
                g.strokePath(bottomPath, juce::PathStrokeType(1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
            }
            else
            {
                const auto pixelCount = juce::jmax(1, static_cast<int>(std::round(lane.getWidth())));
                std::vector<float> topYs(static_cast<std::size_t>(pixelCount + 1), centerY);
                std::vector<float> bottomYs(static_cast<std::size_t>(pixelCount + 1), centerY);
                juce::Path topPath;
                juce::Path bottomPath;

                for (int px = 0; px <= pixelCount; ++px)
                {
                    const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
                    const auto x = lane.getX() + t * lane.getWidth();
                    const auto sample = sampleFromX(x);

                    const auto norm = static_cast<float>(juce::jlimit(0, juce::jmax(1, totalSamples_ - 1), sample))
                        / static_cast<float>(juce::jmax(1, totalSamples_ - 1));
                    const auto peakPos = norm * static_cast<float>(juce::jmax(0, bucketCount - 1));
                    const auto i0 = juce::jlimit(0, bucketCount - 1, static_cast<int>(std::floor(peakPos)));
                    const auto i1 = juce::jlimit(0, bucketCount - 1, i0 + 1);
                    const auto frac = peakPos - static_cast<float>(i0);

                    const auto& r0 = waveform[static_cast<std::size_t>(i0)];
                    const auto& r1 = waveform[static_cast<std::size_t>(i1)];
                    const auto interpMax = r0.max + (r1.max - r0.max) * frac;
                    const auto interpMin = r0.min + (r1.min - r0.min) * frac;
                    const auto topSample = displayMode_ == DisplayMode::symmetricEnvelope
                        ? juce::jmax(std::abs(interpMax), std::abs(interpMin))
                        : interpMax;
                    const auto bottomSample = displayMode_ == DisplayMode::symmetricEnvelope
                        ? -juce::jmax(std::abs(interpMax), std::abs(interpMin))
                        : interpMin;

                    const auto topY = yFromSample(topSample);
                    const auto bottomY = yFromSample(bottomSample);
                    topYs[static_cast<std::size_t>(px)] = topY;
                    bottomYs[static_cast<std::size_t>(px)] = bottomY;

                    if (px == 0)
                    {
                        topPath.startNewSubPath(x, topY);
                        bottomPath.startNewSubPath(x, bottomY);
                    }
                    else
                    {
                        topPath.lineTo(x, topY);
                        bottomPath.lineTo(x, bottomY);
                    }
                }

                juce::Path fillPath;
                fillPath.startNewSubPath(lane.getX(), topYs.front());
                for (int px = 1; px <= pixelCount; ++px)
                {
                    const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
                    const auto x = lane.getX() + t * lane.getWidth();
                    fillPath.lineTo(x, topYs[static_cast<std::size_t>(px)]);
                }

                for (int px = pixelCount; px >= 0; --px)
                {
                    const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
                    const auto x = lane.getX() + t * lane.getWidth();
                    fillPath.lineTo(x, bottomYs[static_cast<std::size_t>(px)]);
                }
                fillPath.closeSubPath();

                juce::ColourGradient fillGradient(
                    uiAccentColour().withAlpha(0.30f), lane.getCentreX(), lane.getY(),
                    uiAccentColour().withAlpha(0.08f), lane.getCentreX(), lane.getBottom(),
                    false);
                fillGradient.addColour(0.5, uiAccentColour().withAlpha(0.18f));
                g.setGradientFill(fillGradient);
                g.fillPath(fillPath);

                g.setColour(uiAccentColour().withAlpha(0.94f));
                g.strokePath(topPath, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(uiAccentColour().withAlpha(0.72f));
                g.strokePath(bottomPath, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        g.setColour(uiAccentGreenColour().withAlpha(0.92f));
        g.drawLine(pbHX1, lane.getY(), pbHX1, lane.getBottom(), 1.5f);
        g.drawLine(pbHX2, lane.getY(), pbHX2, lane.getBottom(), 1.5f);

        g.setColour(uiAccentAmberColour().withAlpha(0.94f));
        g.drawLine(lpHX1, lane.getY(), lpHX1, lane.getBottom(), 1.5f);
        g.drawLine(lpHX2, lane.getY(), lpHX2, lane.getBottom(), 1.5f);

        if (!sliceMarkers_.empty())
        {
            for (int markerIndex = 0; markerIndex < static_cast<int>(sliceMarkers_.size()); ++markerIndex)
            {
                const auto markerSample = sliceMarkers_[static_cast<std::size_t>(markerIndex)];
                if (markerSample <= 0 || markerSample >= totalSamples_)
                    continue;

                if (markerSample < viewStartSample_
                    || markerSample > viewStartSample_ + juce::jmax(1, viewSampleCount_ - 1))
                {
                    continue;
                }

                const auto markerX = juce::jlimit(bounds.getX(), bounds.getRight(), xFromSample(markerSample));
                const auto hoveredBoundary = markerSample == hoveredSliceBoundarySample_;
                g.setColour((hoveredBoundary ? uiAccentAmberColour() : uiAccentColour()).withAlpha(hoveredBoundary ? 0.92f : 0.58f));
                g.drawLine(markerX, lane.getY(), markerX, lane.getBottom(), hoveredBoundary ? 1.7f : 1.0f);

                g.drawLine(markerX - 3.0f, lane.getY() + 2.0f, markerX + 3.0f, lane.getY() + 2.0f, hoveredBoundary ? 1.5f : 1.25f);
            }

            if (channel == 0 && showCompactSliceLabels)
            {
                for (int sliceIndex = 0; sliceIndex + 1 < static_cast<int>(sliceMarkers_.size()); ++sliceIndex)
                {
                    const auto sliceBounds = getSliceRegionBounds(sliceIndex);
                    if (sliceBounds.isEmpty())
                        continue;

                    if (sliceBounds.getEnd() < viewStartSample_
                        || sliceBounds.getStart() > viewStartSample_ + juce::jmax(1, viewSampleCount_ - 1))
                    {
                        continue;
                    }

                    const auto leftX = handleVisualX(xFromSample(sliceBounds.getStart()));
                    const auto rightX = handleVisualX(xFromSample(sliceBounds.getEnd()));
                    if (rightX - leftX < 18.0f)
                        continue;

                    auto labelBounds = juce::Rectangle<float>((leftX + rightX) * 0.5f - 9.0f, bounds.getY() + 4.0f, 18.0f, 11.0f);
                    const auto hoveredSlice = sliceIndex == hoveredSliceRegionIndex_;
                    g.setColour((hoveredSlice ? uiAccentAmberColour() : uiPanelRaisedColour()).withAlpha(0.94f));
                    g.fillRoundedRectangle(labelBounds, 3.0f);
                    g.setColour((hoveredSlice ? uiAccentAmberColour() : uiBorderColour()).withAlpha(0.95f));
                    g.drawRoundedRectangle(labelBounds, 3.0f, 1.0f);
                    g.setColour(uiTextStrongColour().withAlpha(0.96f));
                    g.setFont(juce::Font(juce::FontOptions(8.5f)).boldened());
                    g.drawText(juce::String(sliceIndex + 1), labelBounds.toNearestInt(), juce::Justification::centred, false);
                }
            }
        }

        for (int voiceIndex = 0; voiceIndex < static_cast<int>(voicePlaybackPositions_.size()); ++voiceIndex)
        {
            const auto sampleIndex = voicePlaybackPositions_[static_cast<std::size_t>(voiceIndex)];
            if (sampleIndex < 0)
                continue;

            const auto markerX = juce::jlimit(bounds.getX(), bounds.getRight(), xFromSample(sampleIndex));
            const auto hue = std::fmod(0.08f + static_cast<float>(voiceIndex) * 0.61803398875f, 1.0f);
            const auto markerColour = juce::Colour::fromHSV(hue, 0.68f, 0.98f, 0.92f);

            g.setColour(markerColour);
            g.drawLine(markerX, lane.getY(), markerX, lane.getBottom(), 1.35f);
        }
    }

    // ── Labels on handles ──
    g.setFont(10.0f);

    g.setColour(uiAccentGreenColour());
    g.drawText("P", static_cast<int>(pbHX1) - 6, static_cast<int>(bounds.getBottom()) - 14, 12, 12, juce::Justification::centred);
    g.drawText("P", static_cast<int>(pbHX2) - 6, static_cast<int>(bounds.getBottom()) - 14, 12, 12, juce::Justification::centred);

    g.setColour(uiAccentAmberColour());
    g.drawText("S", static_cast<int>(lpHX1) - 6, static_cast<int>(bounds.getY()) + 2, 12, 12, juce::Justification::centred);
    g.drawText("E", static_cast<int>(lpHX2) - 6, static_cast<int>(bounds.getY()) + 2, 12, 12, juce::Justification::centred);

    // Circular grab handles for easier interaction at the range ends
    constexpr float kHandleRadius = 4.5f;
    const auto topY = bounds.getY() + 10.0f;
    const auto bottomY = bounds.getBottom() - 10.0f;

    g.setColour(uiAccentAmberColour().withAlpha(0.96f));
    g.fillEllipse(lpHX1 - kHandleRadius, topY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f);
    g.fillEllipse(lpHX2 - kHandleRadius, topY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f);
    g.setColour(uiBorderColour());
    g.drawEllipse(lpHX1 - kHandleRadius, topY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.0f);
    g.drawEllipse(lpHX2 - kHandleRadius, topY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.0f);

    g.setColour(uiAccentGreenColour().withAlpha(0.96f));
    g.fillEllipse(pbHX1 - kHandleRadius, bottomY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f);
    g.fillEllipse(pbHX2 - kHandleRadius, bottomY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f);
    g.setColour(uiBorderColour());
    g.drawEllipse(pbHX1 - kHandleRadius, bottomY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.0f);
    g.drawEllipse(pbHX2 - kHandleRadius, bottomY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.0f);

    if (loopFormatBadge_.isNotEmpty())
    {
        const auto badgeWidth = loopFormatBadge_ == "Apple Loop" ? 84 : 66;
        auto badge = juce::Rectangle<float>(bounds.getRight() - static_cast<float>(badgeWidth) - 8.0f,
            bounds.getY() + 6.0f, static_cast<float>(badgeWidth), 16.0f);

        auto badgeColour = juce::Colour(0xff8fbf5e);
        if (loopFormatBadge_ == "Apple Loop")
            badgeColour = juce::Colour(0xffa593e8);
        else if (loopFormatBadge_ == "REX")
            badgeColour = juce::Colour(0xff7eb6e0);
        else if (loopFormatBadge_ == "SFZ")
            badgeColour = juce::Colour(0xffb3a5ff);
        else if (loopFormatBadge_ == "SLICE")
            badgeColour = juce::Colour(0xff78d7ff);
        g.setColour(badgeColour.withAlpha(0.16f));
        g.fillRoundedRectangle(badge, 4.0f);
        g.setColour(badgeColour.withAlpha(0.55f));
        g.drawRoundedRectangle(badge.reduced(0.5f), 4.0f, 1.0f);
        g.setColour(badgeColour.brighter(0.45f));
        g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
        g.drawText(loopFormatBadge_, badge, juce::Justification::centred, false);
    }
}

void AudiocityAudioProcessorEditor::WaveformView::mouseDown(const juce::MouseEvent& event)
{
    linkedPlaybackDuringLoopDrag_ = false;

    if (event.mods.isPopupMenu())
    {
        const auto clickSample = juce::jlimit(0,
            juce::jmax(0, totalSamples_ - 1),
            sampleFromX(event.position.x));
        const auto samplesPerPixel = static_cast<double>(juce::jmax(1, viewSampleCount_))
            / static_cast<double>(juce::jmax(1, getWidth()));
        const auto mergeBoundaryTolerance = juce::jmax(8,
            static_cast<int>(std::ceil(samplesPerPixel * 8.0)));
        const auto nearestSliceBoundarySample = static_cast<bool>(onMergeSliceRequested)
            ? findNearestSliceBoundarySample(clickSample, mergeBoundaryTolerance)
            : -1;

        const auto canMergeSlices = nearestSliceBoundarySample >= 0;
        const auto canSplitSlices = loopFormatBadge_ == "SLICE"
            && static_cast<int>(sliceMarkers_.size()) >= 2
            && static_cast<bool>(onSplitSliceRequested);
        juce::PopupMenu menu;
        menu.addItem(1, "Signed", true, displayMode_ == DisplayMode::signedWaveform);
        menu.addItem(2, "Symmetric", true, displayMode_ == DisplayMode::symmetricEnvelope);
        menu.addSeparator();
        menu.addItem(3, "Merge Adjacent Slices Here", canMergeSlices);
        menu.addItem(4, "Split Slice Here", canSplitSlices);
        menu.addItem(5, "Auto Slice Transients", autoSliceEnabled_);
        const auto clickScreenPosition = event.getScreenPosition();
        const juce::Rectangle<int> targetArea(clickScreenPosition.x, clickScreenPosition.y, 1, 1);

        juce::Component::SafePointer<WaveformView> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(targetArea),
            [safeThis, clickSample, nearestSliceBoundarySample](int selectedId)
            {
                if (safeThis == nullptr)
                    return;

                if (selectedId == 1)
                {
                    safeThis->setDisplayMode(DisplayMode::signedWaveform);
                    if (safeThis->onDisplayModeSelected)
                        safeThis->onDisplayModeSelected(DisplayMode::signedWaveform);
                }
                else if (selectedId == 2)
                {
                    safeThis->setDisplayMode(DisplayMode::symmetricEnvelope);
                    if (safeThis->onDisplayModeSelected)
                        safeThis->onDisplayModeSelected(DisplayMode::symmetricEnvelope);
                }
                else if (selectedId == 3)
                {
                    if (safeThis->onMergeSliceRequested)
                        safeThis->onMergeSliceRequested(nearestSliceBoundarySample);
                }
                else if (selectedId == 4)
                {
                    if (safeThis->onSplitSliceRequested)
                        safeThis->onSplitSliceRequested(clickSample);
                }
                else if (selectedId == 5)
                {
                    if (safeThis->onAutoSliceRequested)
                        safeThis->onAutoSliceRequested();
                }
            });

        dragMode_ = DragMode::none;
        return;
    }

    if (event.mods.isMiddleButtonDown())
    {
        dragMode_ = DragMode::pan;
        dragAnchorViewStart_ = viewStartSample_;
        return;
    }

    // Check proximity to all four handles — pick the nearest one
    const auto visualHandleX = [this](const float x)
    {
        return juce::jlimit(0.0f, static_cast<float>(juce::jmax(0, getWidth())), x);
    };

    const auto topY = 10.0f;
    const auto bottomY = juce::jmax(10.0f, static_cast<float>(getHeight()) - 10.0f);

    struct Handle { DragMode mode; float x; float y; };
    const Handle handles[] = {
        { DragMode::dragPlaybackStart, visualHandleX(xFromSample(playbackStart_)), bottomY },
        { DragMode::dragPlaybackEnd,   visualHandleX(xFromSample(playbackEnd_)), bottomY },
        { DragMode::dragLoopStart,     visualHandleX(xFromSample(loopStart_)), topY },
        { DragMode::dragLoopEnd,       visualHandleX(xFromSample(loopEnd_)), topY },
    };

    constexpr float kHandlePickPx = 12.0f;
    float bestDist = kHandlePickPx + 1.0f;
    DragMode bestMode = DragMode::none;

    for (const auto& h : handles)
    {
        const auto dx = event.position.x - h.x;
        const auto dy = event.position.y - h.y;
        const auto dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= kHandlePickPx && dist < bestDist)
        {
            bestDist = dist;
            bestMode = h.mode;
        }
    }

    dragMode_ = bestMode;
}

void AudiocityAudioProcessorEditor::WaveformView::mouseMove(const juce::MouseEvent& event)
{
    updateSliceHoverState(event.position);
}

void AudiocityAudioProcessorEditor::WaveformView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragMode_ == DragMode::pan)
    {
        const auto samplesPerPixel = static_cast<double>(juce::jmax(1, viewSampleCount_))
            / static_cast<double>(juce::jmax(1, getWidth()));
        viewStartSample_ = dragAnchorViewStart_
            - static_cast<int>(std::round(static_cast<double>(event.getDistanceFromDragStartX()) * samplesPerPixel));
        clampView();
        repaint();
        return;
    }

    const auto sample = juce::jlimit(0, juce::jmax(0, totalSamples_ - 1), sampleFromX(event.position.x));
    const auto shiftHeld = event.mods.isShiftDown();

    if (dragMode_ == DragMode::dragLoopStart)
    {
        if (shiftHeld)
        {
            loopStart_ = juce::jlimit(0, juce::jmax(0, loopEnd_ - 1), sample);
            playbackStart_ = loopStart_;
            linkedPlaybackDuringLoopDrag_ = true;
            if (onPlaybackPreview)
                onPlaybackPreview(playbackStart_, playbackEnd_);
        }
        else
        {
            // Loop start must stay >= playbackStart_ and < loopEnd_
            loopStart_ = juce::jlimit(playbackStart_, juce::jmax(playbackStart_, loopEnd_ - 1), sample);
        }

        if (onLoopPreview)
            onLoopPreview(loopStart_, loopEnd_);
    }
    else if (dragMode_ == DragMode::dragLoopEnd)
    {
        if (shiftHeld)
        {
            loopEnd_ = juce::jlimit(loopStart_ + 1, juce::jmax(loopStart_ + 1, totalSamples_ - 1), sample);
            playbackEnd_ = loopEnd_;
            linkedPlaybackDuringLoopDrag_ = true;
            if (onPlaybackPreview)
                onPlaybackPreview(playbackStart_, playbackEnd_);
        }
        else
        {
            // Loop end must stay <= playbackEnd_ and > loopStart_
            loopEnd_ = juce::jlimit(loopStart_ + 1, juce::jmax(loopStart_ + 1, playbackEnd_), sample);
        }

        if (onLoopPreview)
            onLoopPreview(loopStart_, loopEnd_);
    }
    else if (dragMode_ == DragMode::dragPlaybackStart)
    {
        // Playback start must stay <= loopStart_
        playbackStart_ = juce::jlimit(0, juce::jmax(0, loopStart_), sample);
        if (onPlaybackPreview)
            onPlaybackPreview(playbackStart_, playbackEnd_);
    }
    else if (dragMode_ == DragMode::dragPlaybackEnd)
    {
        // Playback end must stay >= loopEnd_
        playbackEnd_ = juce::jlimit(loopEnd_, juce::jmax(0, totalSamples_ - 1), sample);
        if (onPlaybackPreview)
            onPlaybackPreview(playbackStart_, playbackEnd_);
    }
    else
    {
        return;
    }

    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::mouseUp(const juce::MouseEvent& event)
{
    if (dragMode_ == DragMode::dragLoopStart || dragMode_ == DragMode::dragLoopEnd)
    {
        if (event.mods.isShiftDown())
        {
            if (dragMode_ == DragMode::dragLoopStart)
                playbackStart_ = loopStart_;
            else
                playbackEnd_ = loopEnd_;

            linkedPlaybackDuringLoopDrag_ = true;
        }

        const auto committedPlaybackStart = playbackStart_;
        const auto committedPlaybackEnd = playbackEnd_;
        const auto committedLoopStart = loopStart_;
        const auto committedLoopEnd = loopEnd_;

        if (linkedPlaybackDuringLoopDrag_ && onPlaybackCommitted)
        {
            onPlaybackCommitted(committedPlaybackStart, committedPlaybackEnd);
        }

        if (onLoopCommitted)
        {
            onLoopCommitted(committedLoopStart, committedLoopEnd);
        }
    }
    else if (dragMode_ == DragMode::dragPlaybackStart || dragMode_ == DragMode::dragPlaybackEnd)
    {
        const auto committedPlaybackStart = playbackStart_;
        const auto committedPlaybackEnd = playbackEnd_;

        if (onPlaybackCommitted)
        {
            onPlaybackCommitted(committedPlaybackStart, committedPlaybackEnd);
        }
    }

    dragMode_ = DragMode::none;
    linkedPlaybackDuringLoopDrag_ = false;
}

void AudiocityAudioProcessorEditor::WaveformView::mouseExit(const juce::MouseEvent&)
{
    updateSliceHoverState({ -1.0f, -1.0f });
}

void AudiocityAudioProcessorEditor::WaveformView::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (isSliceViewActive() && static_cast<bool>(onSplitSliceRequested))
    {
        const auto clickSample = juce::jlimit(0,
            juce::jmax(0, totalSamples_ - 1),
            sampleFromX(event.position.x));
        onSplitSliceRequested(clickSample);
        return;
    }

    if (onResetRangesRequested)
        onResetRangesRequested();
    else
        resetView();
}

void AudiocityAudioProcessorEditor::WaveformView::mouseWheelMove(
    const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto panAmount = (wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY) * 120.0f;
    if (event.mods.isShiftDown() || wheel.deltaX != 0.0f)
    {
        panByPixels(-panAmount);
        return;
    }

    if (wheel.deltaY != 0.0f)
    {
        const auto zoomFactor = wheel.deltaY > 0.0f ? 0.85f : 1.2f;
        zoomAround(event.position.x, zoomFactor);
    }
}

void MappingOverviewComponent::setRows(std::vector<audiocity::plugin::ProgramZoneListRow> rows)
{
    dragState_.reset();
    rows_ = std::move(rows);
    repaint();
}

void MappingOverviewComponent::setSelectedZoneIndex(const int zoneIndex)
{
    if (selectedZoneIndex_ == zoneIndex)
        return;

    selectedZoneIndex_ = zoneIndex;
    repaint();
}

MappingOverviewComponent::OverviewLayout MappingOverviewComponent::getOverviewLayout() const
{
    OverviewLayout layout;
    auto bounds = getLocalBounds().reduced(8);
    if (bounds.isEmpty())
        return layout;

    layout.title = bounds.removeFromTop(18);
    bounds.removeFromTop(4);
    layout.keyboard = bounds.removeFromBottom(18);
    bounds.removeFromBottom(5);
    layout.plot = bounds;
    return layout;
}

juce::Rectangle<int> MappingOverviewComponent::getZoneBounds(
    const audiocity::plugin::ProgramZoneListRow& row,
    const juce::Rectangle<int> plot) const
{
    if (plot.isEmpty())
        return {};

    const auto keyLow = juce::jlimit(0, 127, row.keyLow);
    const auto keyHigh = juce::jlimit(keyLow, 127, row.keyHigh);
    const auto velocityLow = juce::jlimit(0, 127, row.velocityLow);
    const auto velocityHigh = juce::jlimit(velocityLow, 127, row.velocityHigh);

    const auto left = plot.getX() + static_cast<int>(std::floor(static_cast<double>(keyLow) / 128.0 * plot.getWidth()));
    const auto right = plot.getX() + static_cast<int>(std::ceil(static_cast<double>(keyHigh + 1) / 128.0 * plot.getWidth()));
    const auto top = plot.getBottom() - static_cast<int>(std::ceil(static_cast<double>(velocityHigh + 1) / 128.0 * plot.getHeight()));
    const auto bottom = plot.getBottom() - static_cast<int>(std::floor(static_cast<double>(velocityLow) / 128.0 * plot.getHeight()));
    auto zoneBounds = juce::Rectangle<int>(left, top, juce::jmax(2, right - left), juce::jmax(2, bottom - top));
    return zoneBounds.getIntersection(plot);
}

int MappingOverviewComponent::findZoneIndexAt(const juce::Point<int> position) const
{
    const auto layout = getOverviewLayout();
    if (!layout.plot.contains(position))
        return -1;

    for (auto rowIndex = static_cast<int>(rows_.size()) - 1; rowIndex >= 0; --rowIndex)
    {
        const auto& row = rows_[static_cast<std::size_t>(rowIndex)];
        if (getZoneBounds(row, layout.plot).contains(position))
            return row.zoneIndex;
    }

    return -1;
}

const audiocity::plugin::ProgramZoneListRow* MappingOverviewComponent::findRowByZoneIndex(const int zoneIndex) const
{
    for (const auto& row : rows_)
    {
        if (row.zoneIndex == zoneIndex)
            return &row;
    }

    return nullptr;
}

audiocity::plugin::ProgramZoneOverviewDragMode MappingOverviewComponent::getDragModeForPosition(
    const audiocity::plugin::ProgramZoneListRow& row,
    const juce::Point<int> position,
    const juce::Rectangle<int> plot) const
{
    constexpr int kResizeHandlePx = 6;
    const auto bounds = getZoneBounds(row, plot);
    const auto nearLeft = std::abs(position.x - bounds.getX()) <= kResizeHandlePx;
    const auto nearRight = std::abs(position.x - bounds.getRight()) <= kResizeHandlePx;
    const auto nearTop = std::abs(position.y - bounds.getY()) <= kResizeHandlePx;
    const auto nearBottom = std::abs(position.y - bounds.getBottom()) <= kResizeHandlePx;

    if (nearLeft && bounds.getWidth() >= (kResizeHandlePx * 2))
        return audiocity::plugin::ProgramZoneOverviewDragMode::keyLow;

    if (nearRight && bounds.getWidth() >= (kResizeHandlePx * 2))
        return audiocity::plugin::ProgramZoneOverviewDragMode::keyHigh;

    if (nearTop && bounds.getHeight() >= (kResizeHandlePx * 2))
        return audiocity::plugin::ProgramZoneOverviewDragMode::velocityHigh;

    if (nearBottom && bounds.getHeight() >= (kResizeHandlePx * 2))
        return audiocity::plugin::ProgramZoneOverviewDragMode::velocityLow;

    return audiocity::plugin::ProgramZoneOverviewDragMode::move;
}

int MappingOverviewComponent::positionToNoteValue(const int x, const juce::Rectangle<int> plot) const
{
    if (plot.getWidth() <= 1)
        return audiocity::engine::kMidiNoteMin;

    const auto clampedX = juce::jlimit(plot.getX(), plot.getRight() - 1, x);
    const auto normalized = static_cast<double>(clampedX - plot.getX())
        / static_cast<double>(plot.getWidth() - 1);
    return juce::jlimit(audiocity::engine::kMidiNoteMin,
                        audiocity::engine::kMidiNoteMax,
                        static_cast<int>(std::round(normalized * audiocity::engine::kMidiNoteMax)));
}

int MappingOverviewComponent::positionToVelocityValue(const int y, const juce::Rectangle<int> plot) const
{
    if (plot.getHeight() <= 1)
        return audiocity::engine::kVelocityMin;

    const auto clampedY = juce::jlimit(plot.getY(), plot.getBottom() - 1, y);
    const auto normalized = static_cast<double>((plot.getBottom() - 1) - clampedY)
        / static_cast<double>(plot.getHeight() - 1);
    return juce::jlimit(audiocity::engine::kVelocityMin,
                        audiocity::engine::kVelocityMax,
                        static_cast<int>(std::round(normalized * audiocity::engine::kVelocityMax)));
}

void MappingOverviewComponent::updateDragPreview(const juce::Point<int> position)
{
    if (!dragState_.has_value())
        return;

    const auto layout = getOverviewLayout();
    if (layout.plot.isEmpty())
        return;

    const auto noteValue = positionToNoteValue(position.x, layout.plot);
    const auto velocityValue = positionToVelocityValue(position.y, layout.plot);
    const auto nextEdit = audiocity::plugin::makeProgramZoneOverviewEdit(
        dragState_->originalRow,
        dragState_->mode,
        noteValue,
        velocityValue,
        noteValue - dragState_->startNoteValue,
        velocityValue - dragState_->startVelocityValue);

    if (nextEdit.keyLow == dragState_->previewEdit.keyLow
        && nextEdit.keyHigh == dragState_->previewEdit.keyHigh
        && nextEdit.velocityLow == dragState_->previewEdit.velocityLow
        && nextEdit.velocityHigh == dragState_->previewEdit.velocityHigh)
    {
        return;
    }

    dragState_->previewEdit = nextEdit;
    repaint();
}

void MappingOverviewComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff202034));

    const auto layout = getOverviewLayout();
    if (layout.plot.isEmpty())
        return;

    g.setColour(juce::Colour(0xff3a3a52));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 5.0f, 1.0f);

    g.setColour(juce::Colour(0xffc7c7d8));
    g.setFont(juce::Font(juce::FontOptions(11.0f)).boldened());
    g.drawText("Key / Velocity Overview", layout.title, juce::Justification::centredLeft, true);

    g.setColour(juce::Colour(0xff19192b));
    g.fillRoundedRectangle(layout.plot.toFloat(), 4.0f);

    g.setColour(juce::Colour(0x363a3a52));
    for (int key = 12; key < 128; key += 12)
    {
        const auto x = layout.plot.getX() + static_cast<int>(std::round(static_cast<double>(key) / 127.0 * layout.plot.getWidth()));
        g.drawVerticalLine(x, static_cast<float>(layout.plot.getY()), static_cast<float>(layout.plot.getBottom()));
    }

    for (int velocity = 32; velocity < 128; velocity += 32)
    {
        const auto y = layout.plot.getBottom() - static_cast<int>(std::round(static_cast<double>(velocity) / 127.0 * layout.plot.getHeight()));
        g.drawHorizontalLine(y, static_cast<float>(layout.plot.getX()), static_cast<float>(layout.plot.getRight()));
    }

    auto colourForZone = [](const int zoneIndex)
    {
        const auto hue = std::fmod(0.55f + static_cast<float>(zoneIndex) * 0.137f, 1.0f);
        return juce::Colour::fromHSV(hue, 0.58f, 0.88f, 0.70f);
    };

    for (const auto& storedRow : rows_)
    {
        auto row = storedRow;
        if (dragState_.has_value() && row.zoneIndex == dragState_->originalRow.zoneIndex)
        {
            row.keyLow = dragState_->previewEdit.keyLow;
            row.keyHigh = dragState_->previewEdit.keyHigh;
            row.velocityLow = dragState_->previewEdit.velocityLow;
            row.velocityHigh = dragState_->previewEdit.velocityHigh;
        }

        const auto zoneBounds = getZoneBounds(row, layout.plot);
        const auto colour = colourForZone(row.zoneIndex);
        const auto selected = row.zoneIndex == selectedZoneIndex_;
        g.setColour(colour.withAlpha(0.34f));
        g.fillRoundedRectangle(zoneBounds.toFloat(), 3.0f);
        g.setColour(selected ? juce::Colour(0xffffffff) : colour.withAlpha(0.95f));
        g.drawRoundedRectangle(zoneBounds.toFloat().reduced(0.5f), 3.0f, selected ? 2.0f : 1.2f);

        if (zoneBounds.getWidth() >= 28 && zoneBounds.getHeight() >= 14)
        {
            g.setColour(juce::Colour(0xfff4f6ff));
            g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
            g.drawText("Z" + juce::String(row.zoneIndex + 1), zoneBounds.reduced(4, 1), juce::Justification::centredLeft, true);
        }
    }

    if (rows_.empty())
    {
        g.setColour(juce::Colour(0xff808098));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("No zones", layout.plot, juce::Justification::centred, true);
    }

    for (int key = 0; key < 128; ++key)
    {
        const auto x = layout.keyboard.getX() + static_cast<int>(std::floor(static_cast<double>(key) / 128.0 * layout.keyboard.getWidth()));
        const auto nextX = layout.keyboard.getX() + static_cast<int>(std::ceil(static_cast<double>(key + 1) / 128.0 * layout.keyboard.getWidth()));
        const auto note = key % 12;
        const auto isBlackKey = note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
        g.setColour(isBlackKey ? juce::Colour(0xff11111e) : juce::Colour(0xffd7dce8));
        g.fillRect(x, layout.keyboard.getY(), juce::jmax(1, nextX - x), layout.keyboard.getHeight());
    }
    g.setColour(juce::Colour(0xff3a3a52));
    g.drawRect(layout.keyboard);
}

void MappingOverviewComponent::mouseDown(const juce::MouseEvent& event)
{
    dragState_.reset();

    const auto zoneIndex = findZoneIndexAt(event.getPosition());
    if (zoneIndex < 0)
        return;

    setSelectedZoneIndex(zoneIndex);
    if (onZoneSelected)
        onZoneSelected(zoneIndex);

    const auto* row = findRowByZoneIndex(zoneIndex);
    const auto layout = getOverviewLayout();
    if (row == nullptr || layout.plot.isEmpty())
        return;

    DragState dragState;
    dragState.originalRow = *row;
    dragState.previewEdit.zoneIndex = row->zoneIndex;
    dragState.previewEdit.keyLow = row->keyLow;
    dragState.previewEdit.keyHigh = row->keyHigh;
    dragState.previewEdit.velocityLow = row->velocityLow;
    dragState.previewEdit.velocityHigh = row->velocityHigh;
    dragState.previewEdit.rootMidiNote = row->rootMidiNote;
    dragState.startPosition = event.getPosition();
    dragState.startNoteValue = positionToNoteValue(event.getPosition().x, layout.plot);
    dragState.startVelocityValue = positionToVelocityValue(event.getPosition().y, layout.plot);
    dragState.mode = getDragModeForPosition(*row, event.getPosition(), layout.plot);
    dragState_ = dragState;
}

void MappingOverviewComponent::mouseDrag(const juce::MouseEvent& event)
{
    updateDragPreview(event.getPosition());
}

void MappingOverviewComponent::mouseUp(const juce::MouseEvent&)
{
    if (!dragState_.has_value())
        return;

    const auto changed = dragState_->previewEdit.keyLow != dragState_->originalRow.keyLow
        || dragState_->previewEdit.keyHigh != dragState_->originalRow.keyHigh
        || dragState_->previewEdit.velocityLow != dragState_->originalRow.velocityLow
        || dragState_->previewEdit.velocityHigh != dragState_->originalRow.velocityHigh;
    const auto committedEdit = dragState_->previewEdit;
    dragState_.reset();
    repaint();

    if (changed && onZoneEditCommitted)
        onZoneEditCommitted(committedEdit);
}

// ─── Player modulation panel ─────────────────────────────────────────────────

PlayerModulationPanel::PlayerModulationPanel(AudiocityAudioProcessor& processor)
    : processor_(processor)
{
    forEachDial([this](CcLearnDial& dial, const juce::String&)
    {
        addAndMakeVisible(dial);
        dial.setDoubleClickResetValue(0.0);
        dial.onValueChange = [this] { pushToProcessor(); };
    });
}

std::array<PlayerModulationPanel::RouteDialSet, 5> PlayerModulationPanel::routeDialSets() noexcept
{
    return {{
        { RouteSlot::modWheel, &modWheelPitchDial_, &modWheelFilterDial_, &modWheelAmpDial_,
            "modWheelPitch", "modWheelFilter", "modWheelAmp",
            "MW Pitch - Route MIDI CC1 to pitch in cents",
            "MW Filt - Route MIDI CC1 to filter cutoff in Hz",
            "MW Amp - Route MIDI CC1 to gain trim in percent" },
        { RouteSlot::aftertouch, &aftertouchPitchDial_, &aftertouchFilterDial_, &aftertouchAmpDial_,
            "aftertouchPitch", "aftertouchFilter", "aftertouchAmp",
            "AT Pitch - Route channel aftertouch to pitch in cents",
            "AT Filt - Route channel aftertouch to filter cutoff in Hz",
            "AT Amp - Route channel aftertouch to gain trim in percent" },
        { RouteSlot::velocity, &velocityPitchDial_, &velocityFilterDial_, &velocityAmpDial_,
            "velocityPitch", "velocityFilter", "velocityAmp",
            "Vel Pitch - Route note-on velocity to pitch in cents",
            "Vel Filt - Route note-on velocity to filter cutoff in Hz",
            "Vel Amp - Route note-on velocity to gain trim in percent" },
        { RouteSlot::macro1, &macro1PitchDial_, &macro1FilterDial_, &macro1AmpDial_,
            "macro1Pitch", "macro1Filter", "macro1Amp",
            "M1 Pitch - Route Macro 1 to pitch in cents",
            "M1 Filt - Route Macro 1 to filter cutoff in Hz",
            "M1 Amp - Route Macro 1 to gain trim in percent" },
        { RouteSlot::macro2, &macro2PitchDial_, &macro2FilterDial_, &macro2AmpDial_,
            "macro2Pitch", "macro2Filter", "macro2Amp",
            "M2 Pitch - Route Macro 2 to pitch in cents",
            "M2 Filt - Route Macro 2 to filter cutoff in Hz",
            "M2 Amp - Route Macro 2 to gain trim in percent" }
    }};
}

std::array<PlayerModulationPanel::MacroDialSet, 2> PlayerModulationPanel::macroDialSets() noexcept
{
    return {{
        { 0, &macro1ValueDial_, "macro1Value", "Macro 1 - Macro control value from 0% to 100%" },
        { 1, &macro2ValueDial_, "macro2Value", "Macro 2 - Macro control value from 0% to 100%" }
    }};
}

void PlayerModulationPanel::forEachDial(const std::function<void(CcLearnDial&, const juce::String&)>& visitor)
{
    for (const auto& route : routeDialSets())
    {
        visitor(*route.pitchDial, route.pitchParamId);
        visitor(*route.filterDial, route.filterParamId);
        visitor(*route.ampDial, route.ampParamId);
    }

    for (const auto& macro : macroDialSets())
        visitor(*macro.valueDial, macro.valueParamId);
}

void PlayerModulationPanel::paint(juce::Graphics& g)
{
    constexpr int kDial = 56;
    constexpr int kGrpPadH = 12;
    constexpr int kGrpPadV = 8;
    constexpr int kGrpHdr = 22;
    constexpr int kRowH = 154;
    constexpr int kGrpGap = 10;
    constexpr int kDialGap = 4;

    auto area = getLocalBounds();
    paintGroupBox(g, area.removeFromTop(kRowH), "Expressive Mod");
    area.removeFromTop(kGrpGap);
    paintGroupBox(g, area.removeFromTop(kRowH), "Macro Mod");

    struct SummarySourceState
    {
        juce::String shortLabel;
        juce::Colour colour;
        float pitch = 0.0f;
        float filter = 0.0f;
        float amp = 0.0f;
    };

    const auto expressiveSummarySources = std::array<SummarySourceState, 3>{ {
        { "MW", juce::Colour(0xff61d9ff), static_cast<float>(modWheelPitchDial_.getValue()), static_cast<float>(modWheelFilterDial_.getValue()), static_cast<float>(modWheelAmpDial_.getValue()) },
        { "AT", juce::Colour(0xffffba75), static_cast<float>(aftertouchPitchDial_.getValue()), static_cast<float>(aftertouchFilterDial_.getValue()), static_cast<float>(aftertouchAmpDial_.getValue()) },
        { "VEL", juce::Colour(0xff62d59d), static_cast<float>(velocityPitchDial_.getValue()), static_cast<float>(velocityFilterDial_.getValue()), static_cast<float>(velocityAmpDial_.getValue()) }
    } };

    const auto macroSummarySources = std::array<SummarySourceState, 2>{ {
        { "M1", juce::Colour(0xff4fc3ff), static_cast<float>(macro1PitchDial_.getValue()), static_cast<float>(macro1FilterDial_.getValue()), static_cast<float>(macro1AmpDial_.getValue()) },
        { "M2", juce::Colour(0xffff976b), static_cast<float>(macro2PitchDial_.getValue()), static_cast<float>(macro2FilterDial_.getValue()), static_cast<float>(macro2AmpDial_.getValue()) }
    } };

    const auto paintDestinationChip = [&g](juce::Rectangle<float> chipBounds,
                                           const juce::String& label,
                                           const float value,
                                           const float magnitudeMax,
                                           const juce::Colour activeColour)
    {
        g.setColour(juce::Colour(0xff1a2031));
        g.fillRoundedRectangle(chipBounds, 3.5f);
        g.setColour(juce::Colour(0xff34415f));
        g.drawRoundedRectangle(chipBounds.reduced(0.5f), 3.5f, 1.0f);

        const auto normalized = juce::jlimit(0.0f, 1.0f,
            magnitudeMax > 0.0f ? std::abs(value) / magnitudeMax : 0.0f);
        if (normalized > 0.0f)
        {
            auto fillBounds = chipBounds.reduced(1.5f);
            fillBounds.setWidth(fillBounds.getWidth() * normalized);
            const auto fillColour = value >= 0.0f
                ? activeColour.withAlpha(0.85f)
                : juce::Colour(0xffef7f73).withAlpha(0.85f);
            g.setColour(fillColour);
            g.fillRoundedRectangle(fillBounds, 2.5f);
        }

        g.setColour(juce::Colour(0xffe5e5ef));
        g.setFont(juce::Font(juce::FontOptions(9.0f)).boldened());
        g.drawText(label, chipBounds.toNearestInt(), juce::Justification::centred, false);
    };

    const auto routeLabel = [](const PlayerModulationPanel::RouteSlot slot, const float macroValue)
    {
        switch (slot)
        {
            case PlayerModulationPanel::RouteSlot::modWheel: return juce::String("MW");
            case PlayerModulationPanel::RouteSlot::aftertouch: return juce::String("AT");
            case PlayerModulationPanel::RouteSlot::velocity: return juce::String("VEL");
            case PlayerModulationPanel::RouteSlot::macro1: return juce::String("M1 ") + juce::String(std::round(macroValue)) + "%";
            case PlayerModulationPanel::RouteSlot::macro2: return juce::String("M2 ") + juce::String(std::round(macroValue)) + "%";
        }

        return juce::String();
    };

    for (const auto& route : routeDialSets())
    {
        auto clusterBounds = route.pitchDial->getBounds()
            .getUnion(route.filterDial->getBounds())
            .getUnion(route.ampDial->getBounds());
        if (clusterBounds.isEmpty())
            continue;

        auto feedbackBounds = clusterBounds.withY(clusterBounds.getBottom() + 4).withHeight(14).toFloat();
        auto labelBounds = feedbackBounds.removeFromLeft(route.slot == RouteSlot::macro1 || route.slot == RouteSlot::macro2 ? 50.0f : 34.0f);
        g.setColour(juce::Colour(0xff9ea6c5));
        g.setFont(juce::Font(juce::FontOptions(9.0f)).boldened());

        const auto macroValue = route.slot == RouteSlot::macro1 ? static_cast<float>(macro1ValueDial_.getValue())
            : (route.slot == RouteSlot::macro2 ? static_cast<float>(macro2ValueDial_.getValue()) : 0.0f);
        g.drawText(routeLabel(route.slot, macroValue), labelBounds.toNearestInt(), juce::Justification::centredLeft, false);

        constexpr float kChipGap = 4.0f;
        const auto chipWidth = (feedbackBounds.getWidth() - (2.0f * kChipGap)) / 3.0f;
        auto pitchChip = feedbackBounds.removeFromLeft(chipWidth);
        feedbackBounds.removeFromLeft(kChipGap);
        auto filterChip = feedbackBounds.removeFromLeft(chipWidth);
        feedbackBounds.removeFromLeft(kChipGap);
        auto ampChip = feedbackBounds;

        paintDestinationChip(pitchChip, "Pitch", static_cast<float>(route.pitchDial->getValue()), 1200.0f, juce::Colour(0xff61d9ff));
        paintDestinationChip(filterChip, "Filter", static_cast<float>(route.filterDial->getValue()), 20000.0f, juce::Colour(0xfff2c14e));
        paintDestinationChip(ampChip, "Amp", static_cast<float>(route.ampDial->getValue()), 100.0f, juce::Colour(0xff62d59d));
    }

    const auto computeSummaryArea = [kDial, kDialGap](juce::Rectangle<int> body,
                                                      const int clusterCount,
                                                      const int dialsPerCluster)
    {
        auto remainder = body;
        for (int cluster = 0; cluster < clusterCount; ++cluster)
        {
            for (int dial = 0; dial < dialsPerCluster; ++dial)
            {
                remainder.removeFromLeft(kDial);
                remainder.removeFromLeft(kDialGap);
            }
        }

        return remainder.reduced(4, 2);
    };

    const auto paintDestinationMatrix = [&g](juce::Rectangle<int> bounds,
                                             const juce::String& title,
                                             const auto& sourceStates)
    {
        if (bounds.getWidth() < 118 || bounds.getHeight() < 58)
            return;

        struct FocusState
        {
            juce::String text;
            juce::Colour colour = juce::Colour(0xff7f8aa6);
            float magnitude = 0.0f;
        };

        const auto countActiveRoutes = [&sourceStates]()
        {
            int activeCount = 0;
            for (const auto& source : sourceStates)
            {
                activeCount += std::abs(source.pitch) >= 1.0f ? 1 : 0;
                activeCount += std::abs(source.filter) >= 25.0f ? 1 : 0;
                activeCount += std::abs(source.amp) >= 1.0f ? 1 : 0;
            }

            return activeCount;
        };

        const auto formatPitchValue = [](const float value)
        {
            return juce::String(value >= 0.0f ? "+" : "-")
                + juce::String(static_cast<int>(std::round(std::abs(value))))
                + "c";
        };

        const auto formatFilterValue = [](const float value)
        {
            const auto magnitude = std::abs(value);
            const auto sign = value >= 0.0f ? "+" : "-";
            if (magnitude >= 1000.0f)
                return juce::String(sign) + juce::String(magnitude / 1000.0f, 1) + "k";

            return juce::String(sign) + juce::String(static_cast<int>(std::round(magnitude)));
        };

        const auto formatAmpValue = [](const float value)
        {
            return juce::String(value >= 0.0f ? "+" : "-")
                + juce::String(static_cast<int>(std::round(std::abs(value))))
                + "%";
        };

        FocusState focusState;
        const auto captureFocus = [&sourceStates, &focusState](const juce::String& destinationLabel,
                                                               auto&& valueGetter,
                                                               auto&& formatter)
        {
            for (std::size_t index = 0; index < sourceStates.size(); ++index)
            {
                const auto value = static_cast<float>(valueGetter(sourceStates[index]));
                const auto magnitude = std::abs(value);
                if (magnitude <= focusState.magnitude)
                    continue;

                focusState.magnitude = magnitude;
                focusState.text = sourceStates[index].shortLabel + " -> " + destinationLabel + " " + formatter(value);
                focusState.colour = value >= 0.0f ? sourceStates[index].colour : juce::Colour(0xffef7f73);
            }
        };

        captureFocus("Pitch", [](const auto& source) { return source.pitch; }, formatPitchValue);
        captureFocus("Filter", [](const auto& source) { return source.filter; }, formatFilterValue);
        captureFocus("Amp", [](const auto& source) { return source.amp; }, formatAmpValue);

        auto card = bounds.toFloat();
        g.setColour(juce::Colour(0xff101827).withAlpha(0.9f));
        g.fillRoundedRectangle(card, 6.0f);
        g.setColour(juce::Colour(0xff34415f).withAlpha(0.95f));
        g.drawRoundedRectangle(card.reduced(0.5f), 6.0f, 1.0f);

        auto inner = bounds.reduced(10, 8);
        auto header = inner.removeFromTop(28);
        auto titleRow = header.removeFromTop(14);
        const int activeWidth = juce::jmin(58, titleRow.getWidth() / 3);
        auto activeArea = titleRow.removeFromRight(activeWidth);
        g.setColour(juce::Colour(0xff8fb7ff));
        g.setFont(juce::Font(juce::FontOptions(9.0f)));
        g.drawText(juce::String(countActiveRoutes()) + " active", activeArea, juce::Justification::centredRight, false);

        g.setColour(juce::Colour(0xffe5e5ef));
        g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
        g.drawFittedText(title, titleRow, juce::Justification::centredLeft, 1);

        auto focusRow = header.removeFromTop(10);
        g.setColour(focusState.magnitude > 0.0f ? focusState.colour : juce::Colour(0xff7f8aa6));
        g.setFont(juce::Font(juce::FontOptions(8.0f)).boldened());
        g.drawFittedText(focusState.magnitude > 0.0f ? juce::String("Focus ") + focusState.text : juce::String("Focus idle"),
                         focusRow,
                         juce::Justification::centredLeft,
                         1);

        inner.removeFromTop(2);

        const auto paintDestinationRow = [&g, &sourceStates](juce::Rectangle<int> rowBounds,
                                                             const juce::String& destinationLabel,
                                                             auto&& valueGetter,
                                                             const float magnitudeMax)
        {
            auto labelBounds = rowBounds.removeFromLeft(42);
            g.setColour(juce::Colour(0xff9ea6c5));
            g.setFont(juce::Font(juce::FontOptions(9.0f)).boldened());
            g.drawText(destinationLabel, labelBounds, juce::Justification::centredLeft, false);

            constexpr int kSegmentGap = 4;
            const auto segmentWidth = (rowBounds.getWidth() - (juce::jmax(0, static_cast<int>(sourceStates.size()) - 1) * kSegmentGap))
                / juce::jmax(1, static_cast<int>(sourceStates.size()));

            int dominantIndex = -1;
            float dominantMagnitude = 0.0f;
            for (std::size_t index = 0; index < sourceStates.size(); ++index)
            {
                const auto magnitude = std::abs(static_cast<float>(valueGetter(sourceStates[index])));
                if (magnitude > dominantMagnitude)
                {
                    dominantMagnitude = magnitude;
                    dominantIndex = static_cast<int>(index);
                }
            }

            for (std::size_t index = 0; index < sourceStates.size(); ++index)
            {
                auto segmentBounds = rowBounds.removeFromLeft(segmentWidth).toFloat();
                if (index + 1 < sourceStates.size())
                    rowBounds.removeFromLeft(kSegmentGap);

                g.setColour(juce::Colour(0xff192131));
                g.fillRoundedRectangle(segmentBounds, 4.0f);

                const auto value = valueGetter(sourceStates[index]);
                const auto normalized = juce::jlimit(0.0f, 1.0f,
                    magnitudeMax > 0.0f ? std::abs(value) / magnitudeMax : 0.0f);
                const auto emphasisColour = value >= 0.0f ? sourceStates[index].colour : juce::Colour(0xffef7f73);
                const auto isDominant = dominantMagnitude > 0.0f && static_cast<int>(index) == dominantIndex;
                if (normalized > 0.0f)
                {
                    auto fillBounds = segmentBounds.reduced(1.5f);
                    fillBounds.setWidth(fillBounds.getWidth() * normalized);
                    g.setColour(emphasisColour.withAlpha(isDominant ? 0.92f : 0.86f));
                    g.fillRoundedRectangle(fillBounds, 3.0f);
                }

                g.setColour(isDominant ? emphasisColour.withAlpha(0.95f) : juce::Colour(0xff2b3850));
                g.drawRoundedRectangle(segmentBounds.reduced(0.5f), 4.0f, isDominant ? 1.4f : 1.0f);

                g.setColour(normalized > 0.0f ? juce::Colour(0xfff5f7ff) : juce::Colour(0xff7f8aa6));
                g.setFont(juce::Font(juce::FontOptions(8.5f)).boldened());
                const auto polarity = normalized > 0.0f ? (value >= 0.0f ? "+" : "-") : "";
                g.drawText(sourceStates[index].shortLabel + polarity,
                           segmentBounds.toNearestInt(),
                           juce::Justification::centred,
                           false);
            }
        };

        constexpr int kRowGap = 5;
        auto pitchRow = inner.removeFromTop(17);
        paintDestinationRow(pitchRow, "Pitch", [](const auto& source) { return source.pitch; }, 1200.0f);
        inner.removeFromTop(kRowGap);

        auto filterRow = inner.removeFromTop(17);
        paintDestinationRow(filterRow, "Filter", [](const auto& source) { return source.filter; }, 20000.0f);
        inner.removeFromTop(kRowGap);

        auto ampRow = inner.removeFromTop(17);
        paintDestinationRow(ampRow, "Amp", [](const auto& source) { return source.amp; }, 100.0f);
    };

    auto summaryGroups = getLocalBounds();
    auto expressiveBody = summaryGroups.removeFromTop(kRowH).withTrimmedTop(kGrpHdr).reduced(kGrpPadH, kGrpPadV);
    summaryGroups.removeFromTop(kGrpGap);
    auto macroBody = summaryGroups.removeFromTop(kRowH).withTrimmedTop(kGrpHdr).reduced(kGrpPadH, kGrpPadV);

    paintDestinationMatrix(computeSummaryArea(expressiveBody, 3, 3), "Destinations", expressiveSummarySources);
    paintDestinationMatrix(computeSummaryArea(macroBody, 2, 4), "Macro Routes", macroSummarySources);
}

void PlayerModulationPanel::resized()
{
    constexpr int kDial = 56;
    constexpr int kGrpPadH = 12;
    constexpr int kGrpPadV = 8;
    constexpr int kGrpHdr = 22;
    constexpr int kGrpGap = 10;
    constexpr int kDialGap = 4;
    constexpr int kRowH = 154;
    constexpr int kFeedbackH = 14;
    constexpr int kFeedbackGap = 4;

    auto area = getLocalBounds();
    auto expressiveInner = area.removeFromTop(kRowH).withTrimmedTop(kGrpHdr).reduced(kGrpPadH, kGrpPadV);
    expressiveInner.removeFromBottom(kFeedbackGap + kFeedbackH);
    area.removeFromTop(kGrpGap);
    auto macroInner = area.removeFromTop(kRowH).withTrimmedTop(kGrpHdr).reduced(kGrpPadH, kGrpPadV);
    macroInner.removeFromBottom(kFeedbackGap + kFeedbackH);

    const auto routes = routeDialSets();
    for (int index = 0; index < 3; ++index)
    {
        const auto& route = routes[static_cast<std::size_t>(index)];
        route.pitchDial->setBounds(expressiveInner.removeFromLeft(kDial));
        expressiveInner.removeFromLeft(kDialGap);
        route.filterDial->setBounds(expressiveInner.removeFromLeft(kDial));
        expressiveInner.removeFromLeft(kDialGap);
        route.ampDial->setBounds(expressiveInner.removeFromLeft(kDial));
        expressiveInner.removeFromLeft(kDialGap);
    }

    const auto macros = macroDialSets();
    for (int index = 0; index < 2; ++index)
    {
        const auto& macro = macros[static_cast<std::size_t>(index)];
        const auto& route = routes[static_cast<std::size_t>(index + 3)];
        macro.valueDial->setBounds(macroInner.removeFromLeft(kDial));
        macroInner.removeFromLeft(kDialGap);
        route.pitchDial->setBounds(macroInner.removeFromLeft(kDial));
        macroInner.removeFromLeft(kDialGap);
        route.filterDial->setBounds(macroInner.removeFromLeft(kDial));
        macroInner.removeFromLeft(kDialGap);
        route.ampDial->setBounds(macroInner.removeFromLeft(kDial));
        macroInner.removeFromLeft(kDialGap);
    }
}

void PlayerModulationPanel::syncFromProcessor()
{
    const auto routing = processor_.getModulationRoutingSettings();
    const auto macroValues = processor_.getMacroControlValues();

    for (const auto& routeDials : routeDialSets())
    {
        const auto* route = &routing.modWheel;
        switch (routeDials.slot)
        {
            case RouteSlot::modWheel: route = &routing.modWheel; break;
            case RouteSlot::aftertouch: route = &routing.aftertouch; break;
            case RouteSlot::velocity: route = &routing.velocity; break;
            case RouteSlot::macro1: route = &routing.macros[0]; break;
            case RouteSlot::macro2: route = &routing.macros[1]; break;
        }

        routeDials.pitchDial->setValue(route->toPitchCents, juce::dontSendNotification);
        routeDials.filterDial->setValue(route->toFilterHz, juce::dontSendNotification);
        routeDials.ampDial->setValue(route->toAmp * 100.0f, juce::dontSendNotification);
    }

    for (const auto& macro : macroDialSets())
        macro.valueDial->setValue(macroValues[macro.index] * 100.0f, juce::dontSendNotification);

    repaint();
}

void PlayerModulationPanel::setControlTooltips()
{
    for (const auto& route : routeDialSets())
    {
        route.pitchDial->setLabelTooltip(route.pitchTooltip);
        route.filterDial->setLabelTooltip(route.filterTooltip);
        route.ampDial->setLabelTooltip(route.ampTooltip);
    }

    for (const auto& macro : macroDialSets())
        macro.valueDial->setLabelTooltip(macro.valueTooltip);
}

void PlayerModulationPanel::pushToProcessor()
{
    auto routing = processor_.getModulationRoutingSettings();
    auto macroValues = processor_.getMacroControlValues();

    for (const auto& routeDials : routeDialSets())
    {
        auto* route = &routing.modWheel;
        switch (routeDials.slot)
        {
            case RouteSlot::modWheel: route = &routing.modWheel; break;
            case RouteSlot::aftertouch: route = &routing.aftertouch; break;
            case RouteSlot::velocity: route = &routing.velocity; break;
            case RouteSlot::macro1: route = &routing.macros[0]; break;
            case RouteSlot::macro2: route = &routing.macros[1]; break;
        }

        route->toPitchCents = static_cast<float>(routeDials.pitchDial->getValue());
        route->toFilterHz = static_cast<float>(routeDials.filterDial->getValue());
        route->toAmp = juce::jlimit(-1.0f, 1.0f, static_cast<float>(routeDials.ampDial->getValue()) / 100.0f);
    }

    for (const auto& macro : macroDialSets())
        macroValues[macro.index] = juce::jlimit(0.0f, 1.0f, static_cast<float>(macro.valueDial->getValue()) / 100.0f);

    processor_.setModulationRoutingSettings(routing);
    processor_.setMacroControlValues(macroValues);
    repaint();
}

void PlayerModulationPanel::paintGroupBox(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title) const
{
    paintSectionCard(g, bounds.toFloat(), title);
}

// ─── Editor Constructor ────────────────────────────────────────────────────────

AudiocityAudioProcessorEditor::AudiocityAudioProcessorEditor(AudiocityAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
    processor_(processor),
    modulationPanel_(processor),
    mappingZoneListModel_(*this)
{
    setName("Audiocity");
    setSize(980, 860);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    setLookAndFeel(&dialLaf_);
    tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this, 700);
    tooltipWindow_->setLookAndFeel(&dialLaf_);

    addAndMakeVisible(tabBar_);
    tabBar_.setLookAndFeel(&getTabTextLookAndFeel());
    tabBar_.setTabBarDepth(kEditorTabBarHeight);
    tabBar_.setColour(juce::TabbedButtonBar::frontTextColourId, uiTextStrongColour());
    tabBar_.setColour(juce::TabbedButtonBar::tabTextColourId, uiTextMutedColour());
    tabBar_.addTab("Sample", uiPanelColour(), &tabSamplePage_, false);
    tabBar_.addTab("Library", uiPanelColour(), &tabLibraryPage_, false);
    tabBar_.addTab("Mapping", uiPanelColour(), &tabMappingPage_, false);
    tabBar_.addTab("Player", uiPanelColour(), &tabPlayerPage_, false);
    tabBar_.addTab("Generate", uiPanelColour(), &tabGeneratePage_, false);
    tabBar_.addTab("Capture", uiPanelColour(), &tabCapturePage_, false);
    tabBar_.addTab("About", uiPanelColour(), &tabAboutPage_, false);
    currentTabIndex_ = juce::jlimit(0, tabBar_.getNumTabs() - 1, processor_.getEditorTabIndex());
    processor_.setEditorTabIndex(currentTabIndex_);
    sampleInspectorFilterModExpanded_ = processor_.getSampleInspectorFilterModExpanded();
    sampleInspectorEffectsExpanded_ = processor_.getSampleInspectorEffectsExpanded();
    tabBar_.setCurrentTabIndex(currentTabIndex_);

    addAndMakeVisible(sampleControlsViewport_);
    sampleControlsViewport_.setScrollBarsShown(true, false);
    sampleControlsViewport_.setViewedComponent(&sampleControlsContent_, false);
    sampleControlsContent_.onPaint = [this](juce::Graphics& g) { paintGroupBoxes(g); };
    sampleControlsContent_.onMouseDown = [this](const juce::MouseEvent& event) { handleSampleControlsMouseDown(event); };

    // Player pane
    addAndMakeVisible(playerKeyboardLabel_);
    playerKeyboardLabel_.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(playerStatusLabel_);
    playerStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    playerStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8fb7ff));

    addAndMakeVisible(playerOpenButton_);
    playerOpenButton_.onClick = [this]
    {
        tabBar_.setCurrentTabIndex(3);
        currentTabIndex_ = 3;
        processor_.setEditorTabIndex(currentTabIndex_);
        updateTabVisibility();
        resized();
        repaint();
    };

    addAndMakeVisible(playerKeyboardViewport_);
    playerKeyboardViewport_.setViewedComponent(&playerKeyboard_, false);
    playerKeyboardViewport_.setScrollBarsShown(false, false);

    playerKeyboardState_.addListener(this);
    playerKeyboard_.setAvailableRange(kPlayerKeyboardMinMidiNote, kPlayerKeyboardMaxMidiNote);
    playerKeyboard_.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffd7dde8));
    playerKeyboard_.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff20253a));
    playerKeyboard_.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour(0xff61d9ff).withAlpha(0.60f));
    playerKeyboard_.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour(0xff8aa9dc).withAlpha(0.35f));

    addAndMakeVisible(playerPadsLabel_);
    playerPadsLabel_.setJustificationType(juce::Justification::centredLeft);

    playerPadAssignments_ = processor_.getAllPlayerPadAssignments();
    for (int i = 0; i < kPlayerPadCount; ++i)
    {
        auto& padButton = playerPadButtons_[static_cast<std::size_t>(i)];
        auto& assignButton = playerPadAssignButtons_[static_cast<std::size_t>(i)];

        addAndMakeVisible(padButton);
        addAndMakeVisible(assignButton);

        assignButton.setButtonText("...");
        padButton.setClickingTogglesState(false);

        padButton.onEngagementChanged = [this, i](const bool engaged)
        {
            auto& wasHeld = playerPadHeld_[static_cast<std::size_t>(i)];
            if (engaged == wasHeld)
                return;

            const auto assignment = playerPadAssignments_[static_cast<std::size_t>(i)];
            if (engaged)
                processor_.enqueueUiMidiNoteOn(assignment.noteNumber, assignment.velocity);
            else
                processor_.enqueueUiMidiNoteOff(assignment.noteNumber);

            wasHeld = engaged;
        };

        assignButton.onClick = [this, i]
        {
            showPadAssignmentDialog(i);
        };
    }
    refreshPlayerPadButtons();
    updatePerformanceStripStatus(0.0f, 0.0f);

    // Generate pane
    addAndMakeVisible(generateWaveformView_);
    addAndMakeVisible(generateSineButton_);
    addAndMakeVisible(generateRampButton_);
    addAndMakeVisible(generateSquareButton_);
    addAndMakeVisible(generateSawtoothButton_);
    addAndMakeVisible(generateTriangleButton_);
    addAndMakeVisible(generatePulseButton_);
    addAndMakeVisible(generateRandomButton_);
    addAndMakeVisible(generateSamplesLabel_);
    addAndMakeVisible(generateSamplesCombo_);
    addAndMakeVisible(generateBitDepthLabel_);
    addAndMakeVisible(generateBitDepthCombo_);
    addAndMakeVisible(generateSketchSmoothingLabel_);
    addAndMakeVisible(generateSketchSmoothingCombo_);
    addAndMakeVisible(generatePulseWidthLabel_);
    addAndMakeVisible(generatePulseWidthSlider_);
    addAndMakeVisible(generatePreviewButton_);
    addAndMakeVisible(generateFrequencyLabel_);
    addAndMakeVisible(generateFrequencyCombo_);
    addAndMakeVisible(generateLoadAsSampleButton_);

    generateSamplesLabel_.setJustificationType(juce::Justification::centredLeft);
    generateBitDepthLabel_.setJustificationType(juce::Justification::centredLeft);
    generateSketchSmoothingLabel_.setJustificationType(juce::Justification::centredLeft);
    generatePulseWidthLabel_.setJustificationType(juce::Justification::centredLeft);
    generateFrequencyLabel_.setJustificationType(juce::Justification::centredLeft);

    for (int power = 4; power <= 13; ++power)
    {
        const auto sampleCount = 1 << power;
        generateSamplesCombo_.addItem(juce::String(sampleCount), sampleCount);
    }
    generateSamplesCombo_.setSelectedId(processor_.getGenerateSampleCount(), juce::dontSendNotification);
    if (generateSamplesCombo_.getSelectedId() <= 0)
        generateSamplesCombo_.setSelectedId(1024, juce::dontSendNotification);
    generateSamplesCombo_.onChange = [this]
    {
        processor_.setGenerateSampleCount(getSelectedGenerateSampleCount());
        regenerateWaveform();
    };

    generateBitDepthCombo_.addItem("8 bit", 8);
    generateBitDepthCombo_.addItem("16 bit", 16);
    generateBitDepthCombo_.addItem("24 bit", 24);
    generateBitDepthCombo_.setSelectedId(processor_.getGenerateBitDepth(), juce::dontSendNotification);
    if (generateBitDepthCombo_.getSelectedId() <= 0)
        generateBitDepthCombo_.setSelectedId(16, juce::dontSendNotification);
    generateBitDepthCombo_.onChange = [this]
    {
        processor_.setGenerateBitDepth(getSelectedGenerateBitDepth());
        regenerateWaveform();
    };

    generateSketchSmoothingCombo_.addItem("Line", 1);
    generateSketchSmoothingCombo_.addItem("Curve", 2);
    generateSketchSmoothingCombo_.setSelectedId(processor_.getGenerateSketchSmoothing(), juce::dontSendNotification);
    if (generateSketchSmoothingCombo_.getSelectedId() <= 0)
        generateSketchSmoothingCombo_.setSelectedId(1, juce::dontSendNotification);
    selectedSketchSmoothing_ = generateSketchSmoothingCombo_.getSelectedId() == 2
        ? SketchedWaveSmoothing::curve
        : SketchedWaveSmoothing::line;
    generateSketchSmoothingCombo_.onChange = [this]
    {
        processor_.setGenerateSketchSmoothing(generateSketchSmoothingCombo_.getSelectedId());
        selectedSketchSmoothing_ = generateSketchSmoothingCombo_.getSelectedId() == 2
            ? SketchedWaveSmoothing::curve
            : SketchedWaveSmoothing::line;
    };

    generatePulseWidthSlider_.setRange(1.0, 99.0, 1.0);
    generatePulseWidthSlider_.setValue(processor_.getGeneratePulseWidth(), juce::dontSendNotification);
    generatePulseWidthSlider_.setTextValueSuffix(" %");
    generatePulseWidthSlider_.setDoubleClickReturnValue(true, 5.0);
    generatePulseWidthSlider_.onValueChange = [this]
    {
        processor_.setGeneratePulseWidth(static_cast<float>(generatePulseWidthSlider_.getValue()));
        regenerateWaveform();
    };

    for (int midi = 0; midi <= 127; ++midi)
        generateFrequencyCombo_.addItem(formatMidiNoteName(midi), midi + 1);
    generateFrequencyCombo_.setSelectedId(processor_.getGenerateFrequencyMidiNote() + 1, juce::dontSendNotification);
    if (generateFrequencyCombo_.getSelectedId() <= 0)
        generateFrequencyCombo_.setSelectedId(61, juce::dontSendNotification);
    generateFrequencyCombo_.onChange = [this]
    {
        const auto selected = generateFrequencyCombo_.getSelectedId();
        if (selected > 0)
            processor_.setGenerateFrequencyMidiNote(selected - 1);
        processor_.setGeneratedWaveformPreviewMidiNote(getSelectedGenerateMidiNote());
    };

    selectedGeneratedWaveType_ = static_cast<GeneratedWaveType>(juce::jlimit(0, 6, processor_.getGenerateWaveType()));
    processor_.setGenerateWaveType(static_cast<int>(selectedGeneratedWaveType_));

    auto bindWaveButton = [this](juce::TextButton& button, const GeneratedWaveType type)
    {
        button.onClick = [this, type]
        {
            selectedGeneratedWaveType_ = type;
            processor_.setGenerateWaveType(static_cast<int>(type));
            updateGeneratePulseWidthControlState();
            regenerateWaveform();
        };
    };

    bindWaveButton(generateSineButton_, GeneratedWaveType::sine);
    bindWaveButton(generateRampButton_, GeneratedWaveType::ramp);
    bindWaveButton(generateSquareButton_, GeneratedWaveType::square);
    bindWaveButton(generateSawtoothButton_, GeneratedWaveType::sawtooth);
    bindWaveButton(generateTriangleButton_, GeneratedWaveType::triangle);
    bindWaveButton(generatePulseButton_, GeneratedWaveType::pulse);
    bindWaveButton(generateRandomButton_, GeneratedWaveType::random);

    generatePreviewButton_.onClick = [this]
    {
        if (processor_.isGeneratedWaveformPreviewPlaying())
            processor_.stopGeneratedWaveformPreview();
        else
        {
            processor_.setGeneratedWaveformPreview(generatedWaveform_);
            processor_.setGeneratedWaveformPreviewMidiNote(getSelectedGenerateMidiNote());
            processor_.startGeneratedWaveformPreview();
        }
        updateGeneratePreviewButtonText();
    };

    capturePlayButton_.onClick = [this]
    {
        if (processor_.isSamplePreviewPlaying())
            processor_.panicAllAudio();
        else
            processor_.previewCapturedAudio();

        updateGeneratePreviewButtonText();
    };

    captureNormalizeButton_.onClick = [this]
    {
        if (processor_.normalizeCapturedAudio(0.9f))
        {
            refreshCaptureWaveform(true);
            updateCaptureUiState();
        }
    };

    generateLoadAsSampleButton_.onClick = [this]
    {
        processor_.stopGeneratedWaveformPreview();
        updateGeneratePreviewButtonText();

        const auto selectedMidiNote = getSelectedGenerateMidiNote();
        processor_.loadGeneratedWaveformAsSample(generatedWaveform_, selectedMidiNote);
        processor_.setRootMidiNote(selectedMidiNote);
        clearSelectedPresetAfterSourceLoad();
        tabBar_.setCurrentTabIndex(0);
        currentTabIndex_ = 0;
        processor_.setEditorTabIndex(currentTabIndex_);
        updateTabVisibility();
        resized();
        refreshUI(true);
    };

    generateWaveformView_.setWaveChangedCallback([this](const std::vector<float>& sketchedWave)
    {
        applySketchedWaveform(sketchedWave);
    });

    updateGeneratePulseWidthControlState();

    regenerateWaveform();

    // Capture pane
    addAndMakeVisible(captureWaveformView_);
    addAndMakeVisible(captureRecordButton_);
    addAndMakeVisible(captureClearButton_);
    addAndMakeVisible(captureCutButton_);
    addAndMakeVisible(captureTrimButton_);
    addAndMakeVisible(captureLoadAsSampleButton_);
    addAndMakeVisible(capturePlayButton_);
    addAndMakeVisible(captureNormalizeButton_);
    addAndMakeVisible(captureSourceLabel_);
    addAndMakeVisible(captureSampleRateLabel_);
    addAndMakeVisible(captureSampleRateCombo_);
    addAndMakeVisible(captureChannelLabel_);
    addAndMakeVisible(captureChannelCombo_);
    addAndMakeVisible(captureBitDepthLabel_);
    addAndMakeVisible(captureBitDepthCombo_);
    addAndMakeVisible(captureRootNoteLabel_);
    addAndMakeVisible(captureRootNoteCombo_);
    addAndMakeVisible(captureInputLevelLabel_);
    addAndMakeVisible(captureInputLevelSlider_);
    addAndMakeVisible(captureInputVuMeter_);
    captureInputVuMeter_.setClipZoneEnabled(true);
    addAndMakeVisible(captureStatusLabel_);

    captureSourceLabel_.setJustificationType(juce::Justification::centredLeft);
    captureSampleRateLabel_.setJustificationType(juce::Justification::centredLeft);
    captureChannelLabel_.setJustificationType(juce::Justification::centredLeft);
    captureBitDepthLabel_.setJustificationType(juce::Justification::centredLeft);
    captureRootNoteLabel_.setJustificationType(juce::Justification::centredLeft);
    captureInputLevelLabel_.setJustificationType(juce::Justification::centredLeft);
    captureStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    captureStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8a8aa0));

    captureSampleRateCombo_.addItem("Host", 1);
    captureSampleRateCombo_.addItem("22050", 22050);
    captureSampleRateCombo_.addItem("32000", 32000);
    captureSampleRateCombo_.addItem("44100", 44100);
    captureSampleRateCombo_.addItem("48000", 48000);
    const auto targetCaptureRate = processor_.getCaptureTargetSampleRate();
    captureSampleRateCombo_.setSelectedId(targetCaptureRate <= 0 ? 1 : targetCaptureRate, juce::dontSendNotification);
    captureSampleRateCombo_.onChange = [this]
    {
        const auto id = captureSampleRateCombo_.getSelectedId();
        processor_.setCaptureTargetSampleRate(id == 1 ? 0 : id);
    };

    captureChannelCombo_.addItem("Mono Sum", 1);
    captureChannelCombo_.addItem("Left", 2);
    captureChannelCombo_.addItem("Right", 3);
    captureChannelCombo_.addItem("Stereo", 4);
    captureChannelCombo_.setSelectedId(processor_.getCaptureChannelMode() + 1, juce::dontSendNotification);
    captureChannelCombo_.onChange = [this]
    {
        processor_.setCaptureChannelMode(captureChannelCombo_.getSelectedId() - 1);
        refreshCaptureWaveform(true);
    };

    captureBitDepthCombo_.addItem("16 bit", 16);
    captureBitDepthCombo_.addItem("24 bit", 24);
    captureBitDepthCombo_.addItem("32 float", 32);
    captureBitDepthCombo_.setSelectedId(processor_.getCaptureBitDepth(), juce::dontSendNotification);
    captureBitDepthCombo_.onChange = [this]
    {
        processor_.setCaptureBitDepth(captureBitDepthCombo_.getSelectedId());
    };

    for (int note = 0; note <= 127; ++note)
        captureRootNoteCombo_.addItem(formatMidiNoteName(note), note + 1);
    captureRootNoteCombo_.setSelectedId(61, juce::dontSendNotification); // C3 default

    captureInputLevelSlider_.setRange(0.0, 200.0, 0.1);
    captureInputLevelSlider_.setTextValueSuffix(" %");
    captureInputLevelSlider_.setValue(processor_.getCaptureInputGain() * 100.0f, juce::dontSendNotification);
    captureInputLevelSlider_.onValueChange = [this]
    {
        processor_.setCaptureInputGain(static_cast<float>(captureInputLevelSlider_.getValue()) / 100.0f);
    };

    captureWaveformView_.onSelectionChanged = [this](const int start, const int end)
    {
        captureSelectionStart_ = juce::jlimit(0, captureDisplayTotalSamples_, start);
        captureSelectionEnd_ = juce::jlimit(captureSelectionStart_, captureDisplayTotalSamples_, end);
        updateCaptureUiState();
    };

    captureRecordButton_.onClick = [this]
    {
        const auto shouldStop = processor_.isInputCaptureRecording();
        if (shouldStop)
            processor_.stopInputCapture();
        else
            processor_.startInputCapture();

        refreshCaptureWaveform(true);
        updateCaptureUiState();
    };

    captureClearButton_.onClick = [this]
    {
        processor_.clearInputCapture();
        captureSelectionStart_ = 0;
        captureSelectionEnd_ = 0;
        refreshCaptureWaveform(true);
        updateCaptureUiState();
    };

    captureCutButton_.onClick = [this]
    {
        if (processor_.cutCapturedAudioRange(captureSelectionStart_, captureSelectionEnd_))
        {
            captureSelectionEnd_ = captureSelectionStart_;
            refreshCaptureWaveform(true);
            updateCaptureUiState();
        }
    };

    captureTrimButton_.onClick = [this]
    {
        if (processor_.trimCapturedAudioRange(captureSelectionStart_, captureSelectionEnd_))
        {
            captureSelectionStart_ = 0;
            captureSelectionEnd_ = processor_.getCapturedInputSamples();
            refreshCaptureWaveform(true);
            updateCaptureUiState();
        }
    };

    captureLoadAsSampleButton_.onClick = [this]
    {
        const auto start = captureSelectionEnd_ > captureSelectionStart_ ? captureSelectionStart_ : 0;
        const auto end = captureSelectionEnd_ > captureSelectionStart_
            ? captureSelectionEnd_
            : processor_.getCapturedInputSamples();

        const auto selectedRoot = juce::jlimit(0, 127, captureRootNoteCombo_.getSelectedId() - 1);
        processor_.setRootMidiNote(selectedRoot);
        if (!processor_.loadCapturedAudioAsSample(start, end))
            return;

        clearSelectedPresetAfterSourceLoad();
        tabBar_.setCurrentTabIndex(0);
        currentTabIndex_ = 0;
        processor_.setEditorTabIndex(currentTabIndex_);
        updateTabVisibility();
        resized();
        refreshUI(true);
    };

    // Sample browser pane
    addAndMakeVisible(sampleBrowserRootLabel_);
    sampleBrowserRootLabel_.setJustificationType(juce::Justification::centredLeft);
    sampleBrowserRootLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8a8aa0));
    sampleBrowserRootLabel_.setText("< Select Folder >", juce::dontSendNotification);

    addAndMakeVisible(sampleBrowserChooseRootButton_);
    sampleBrowserChooseRootButton_.onClick = [this] { chooseSampleRootFolder(); };

    addAndMakeVisible(sampleBrowserRefreshButton_);
    sampleBrowserRefreshButton_.onClick = [this]
    {
        if (sampleRootFolderPath_.isEmpty())
            return;

        const juce::File rootFolder(sampleRootFolderPath_);
        if (!rootFolder.isDirectory())
            return;

        if (sampleScanInProgress_.load(std::memory_order_relaxed))
            cancelSampleRootScan();

        scanSampleRootFolder(rootFolder);
    };

    addAndMakeVisible(sampleBrowserCancelButton_);
    sampleBrowserCancelButton_.onClick = [this]
    {
        cancelSampleRootScan();
    };

    addAndMakeVisible(sampleBrowserBookmarkCombo_);
    sampleBrowserBookmarkCombo_.setTextWhenNothingSelected("Bookmarks");
    sampleBrowserBookmarkCombo_.onChange = [this] { scanSelectedSampleBrowserBookmark(); };

    addAndMakeVisible(sampleBrowserAddBookmarkButton_);
    sampleBrowserAddBookmarkButton_.onClick = [this] { addCurrentSampleRootBookmark(); };

    addAndMakeVisible(sampleBrowserRemoveBookmarkButton_);
    sampleBrowserRemoveBookmarkButton_.onClick = [this] { removeSelectedSampleBrowserBookmark(); };

    addAndMakeVisible(sampleBrowserFilterEditor_);
    sampleBrowserFilterEditor_.setTextToShowWhenEmpty("Search samples...", juce::Colours::grey);
    sampleBrowserFilterEditor_.onTextChange = [this] { rebuildVisibleSampleList(); };

    addAndMakeVisible(sampleBrowserSortCombo_);
    sampleBrowserSortCombo_.addItem("Name", 1);
    sampleBrowserSortCombo_.addItem("Relative Path", 2);
    sampleBrowserSortCombo_.addItem("Recent", 3);
    sampleBrowserSortCombo_.setSelectedId(1, juce::dontSendNotification);
    sampleBrowserSortCombo_.onChange = [this] { rebuildVisibleSampleList(); };

    addAndMakeVisible(sampleBrowserFavoriteButton_);
    sampleBrowserFavoriteButton_.onClick = [this] { toggleSelectedBrowserFavorite(); };

    addAndMakeVisible(sampleBrowserFavoritesOnlyToggle_);
    sampleBrowserFavoritesOnlyToggle_.onClick = [this] { rebuildVisibleSampleList(); };

    addAndMakeVisible(sampleBrowserRecentOnlyToggle_);
    sampleBrowserRecentOnlyToggle_.onClick = [this] { rebuildVisibleSampleList(); };

    addAndMakeVisible(sampleBrowserTagFilterCombo_);
    sampleBrowserTagFilterCombo_.setTextWhenNothingSelected("All Tags");
    sampleBrowserTagFilterCombo_.onChange = [this] { rebuildVisibleSampleList(); };

    addAndMakeVisible(sampleBrowserTagsEditor_);
    sampleBrowserTagsEditor_.setTextToShowWhenEmpty("Tags", juce::Colours::grey);
    sampleBrowserTagsEditor_.onReturnKey = [this] { applySelectedBrowserTags(); };

    addAndMakeVisible(sampleBrowserApplyTagsButton_);
    sampleBrowserApplyTagsButton_.onClick = [this] { applySelectedBrowserTags(); };

    addAndMakeVisible(sampleBrowserListBox_);
    sampleBrowserListBox_.setModel(this);
    sampleBrowserListBox_.setRowHeight(66);
    sampleBrowserListBox_.setMultipleSelectionEnabled(false);

    addAndMakeVisible(sampleBrowserCountLabel_);
    sampleBrowserCountLabel_.setJustificationType(juce::Justification::centredLeft);
    sampleBrowserCountLabel_.setText("No folder selected", juce::dontSendNotification);

    addAndMakeVisible(sampleBrowserPreviewLabel_);
    sampleBrowserPreviewLabel_.setJustificationType(juce::Justification::centredRight);
    sampleBrowserPreviewLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff61d9ff));
    sampleBrowserPreviewLabel_.setText({}, juce::dontSendNotification);

    addAndMakeVisible(mappingSummaryLabel_);
    mappingSummaryLabel_.setJustificationType(juce::Justification::centredLeft);
    mappingSummaryLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffe5e5ef));
    mappingSummaryLabel_.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());

    addAndMakeVisible(mappingRefreshButton_);
    mappingRefreshButton_.onClick = [this] { refreshMappingZoneRows(); };

    addAndMakeVisible(mappingCreateZoneButton_);
    mappingCreateZoneButton_.onClick = [this] { createMappingZone(); };

    addAndMakeVisible(mappingDuplicateZoneButton_);
    mappingDuplicateZoneButton_.onClick = [this] { duplicateSelectedMappingZone(); };

    addAndMakeVisible(mappingSplitZoneButton_);
    mappingSplitZoneButton_.onClick = [this] { splitSelectedMappingZone(); };

    addAndMakeVisible(mappingDeleteZoneButton_);
    mappingDeleteZoneButton_.onClick = [this] { deleteSelectedMappingZone(); };

    addAndMakeVisible(mappingOverview_);
    mappingOverview_.onZoneSelected = [this](const int zoneIndex)
    {
        if (selectMappingZoneByIndex(zoneIndex))
            updateMappingDetails();
    };
    mappingOverview_.onZoneEditCommitted = [this](const audiocity::plugin::ProgramZoneEdit& edit)
    {
        commitMappingZoneEdit(edit, "Zone " + juce::String(edit.zoneIndex + 1) + " updated from overview");
    };

    addAndMakeVisible(mappingZoneListBox_);
    mappingZoneListBox_.setModel(&mappingZoneListModel_);
    mappingZoneListBox_.setRowHeight(58);
    mappingZoneListBox_.setMultipleSelectionEnabled(true);

    addAndMakeVisible(mappingDetailsText_);
    mappingDetailsText_.setMultiLine(true);
    mappingDetailsText_.setReadOnly(true);
    mappingDetailsText_.setScrollbarsShown(true);
    mappingDetailsText_.setCaretVisible(false);
    mappingDetailsText_.setPopupMenuEnabled(false);
    mappingDetailsText_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff202034));
    mappingDetailsText_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff3a3a52));
    mappingDetailsText_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff3a3a52));
    mappingDetailsText_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffe5e5ef));
    mappingDetailsText_.setFont(juce::Font(juce::FontOptions(12.0f)));

    auto configureMappingEditLabel = [this](juce::Label& label)
    {
        addAndMakeVisible(label);
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, juce::Colour(0xffc7c7d8));
        label.setFont(juce::Font(juce::FontOptions(11.0f)).boldened());
    };

    auto configureMappingEditSlider = [this](juce::Slider& slider,
                                             const double minimum = 0.0,
                                             const double maximum = 127.0,
                                             const double interval = 1.0,
                                             const int textBoxWidth = 48)
    {
        addAndMakeVisible(slider);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, textBoxWidth, 16);
        slider.setRange(minimum, maximum, interval);
        slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff61d9ff));
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xfff4f6ff));
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe5e5ef));
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff202034));
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff3a3a52));
    };

    auto configureMappingEditCombo = [this](juce::ComboBox& combo)
    {
        addAndMakeVisible(combo);
        combo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff202034));
        combo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a3a52));
        combo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe5e5ef));
        combo.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff61d9ff));
    };

    configureMappingEditLabel(mappingEditKeyLowLabel_);
    configureMappingEditSlider(mappingEditKeyLowSlider_);
    configureMappingEditLabel(mappingEditKeyHighLabel_);
    configureMappingEditSlider(mappingEditKeyHighSlider_);
    configureMappingEditLabel(mappingEditVelocityLowLabel_);
    configureMappingEditSlider(mappingEditVelocityLowSlider_);
    configureMappingEditLabel(mappingEditVelocityHighLabel_);
    configureMappingEditSlider(mappingEditVelocityHighSlider_);
    auto configureMappingFadeSlider = [&configureMappingEditSlider](juce::Slider& slider)
    {
        configureMappingEditSlider(slider, -1.0, 127.0, 1.0, 44);
        slider.textFromValueFunction = [](double value)
        {
            return value < 0.0 ? juce::String("Off") : juce::String(static_cast<int>(std::round(value)));
        };
        slider.valueFromTextFunction = [](const juce::String& text)
        {
            const auto trimmed = text.trim();
            return trimmed.equalsIgnoreCase("off") ? -1.0 : trimmed.getDoubleValue();
        };
    };
    configureMappingEditLabel(mappingEditVelocityFadeInLabel_);
    configureMappingFadeSlider(mappingEditVelocityFadeInLowSlider_);
    configureMappingFadeSlider(mappingEditVelocityFadeInHighSlider_);
    configureMappingEditLabel(mappingEditVelocityFadeOutLabel_);
    configureMappingFadeSlider(mappingEditVelocityFadeOutLowSlider_);
    configureMappingFadeSlider(mappingEditVelocityFadeOutHighSlider_);
    configureMappingEditLabel(mappingEditRootLabel_);
    configureMappingEditSlider(mappingEditRootSlider_);
    configureMappingEditLabel(mappingEditSampleStartLabel_);
    configureMappingEditSlider(mappingEditSampleStartSlider_, 0.0, 1.0, 1.0, 68);
    configureMappingEditLabel(mappingEditSampleEndLabel_);
    configureMappingEditSlider(mappingEditSampleEndSlider_, 0.0, 1.0, 1.0, 68);
    configureMappingEditLabel(mappingEditLoopStartLabel_);
    configureMappingEditSlider(mappingEditLoopStartSlider_, 0.0, 1.0, 1.0, 68);
    configureMappingEditLabel(mappingEditLoopEndLabel_);
    configureMappingEditSlider(mappingEditLoopEndSlider_, 0.0, 1.0, 1.0, 68);
    configureMappingEditLabel(mappingEditGainLabel_);
    configureMappingEditSlider(mappingEditGainSlider_, -60.0, 24.0, 0.1, 58);
    configureMappingEditLabel(mappingEditPanLabel_);
    configureMappingEditSlider(mappingEditPanSlider_, -100.0, 100.0, 1.0, 54);
    configureMappingEditLabel(mappingEditRoundRobinGroupLabel_);
    configureMappingEditSlider(mappingEditRoundRobinGroupSlider_, 0.0, 64.0, 1.0);
    configureMappingEditLabel(mappingEditRoundRobinPositionLabel_);
    configureMappingEditSlider(mappingEditRoundRobinPositionSlider_, 0.0, 64.0, 1.0);
    configureMappingEditLabel(mappingEditRoundRobinModeLabel_);
    configureMappingEditCombo(mappingEditRoundRobinModeCombo_);
    mappingEditRoundRobinModeCombo_.addItem("Ordered", 1);
    mappingEditRoundRobinModeCombo_.addItem("Cycle Random", 2);
    configureMappingEditLabel(mappingEditChokeLabel_);
    configureMappingEditSlider(mappingEditChokeSlider_, 0.0, 64.0, 1.0);
    configureMappingEditLabel(mappingEditTriggerLabel_);
    configureMappingEditCombo(mappingEditTriggerCombo_);
    mappingEditTriggerCombo_.addItem("Gate", 1);
    mappingEditTriggerCombo_.addItem("One-Shot", 2);
    mappingEditTriggerCombo_.addItem("Release", 3);
    configureMappingEditLabel(mappingEditLoopLabel_);
    configureMappingEditCombo(mappingEditLoopCombo_);
    mappingEditLoopCombo_.addItem("Off", 1);
    mappingEditLoopCombo_.addItem("Sustain", 2);
    mappingEditLoopCombo_.addItem("Continuous", 3);

    addAndMakeVisible(mappingEditApplyButton_);
    mappingEditApplyButton_.onClick = [this] { applySelectedMappingZoneEdit(); };

    auto makeBatchEditCallback = [this](bool* dirtyFlag)
    {
        return [this, dirtyFlag]
        {
            if (suppressMappingBatchEditTracking_)
                return;

            if (mappingZoneListBox_.getSelectedRows().size() <= 1)
                return;

            *dirtyFlag = true;
            updateMappingEditControls();
        };
    };

    mappingEditVelocityFadeInLowSlider_.onValueChange = makeBatchEditCallback(&mappingBatchVelocityFadeEdited_);
    mappingEditVelocityFadeInHighSlider_.onValueChange = makeBatchEditCallback(&mappingBatchVelocityFadeEdited_);
    mappingEditVelocityFadeOutLowSlider_.onValueChange = makeBatchEditCallback(&mappingBatchVelocityFadeEdited_);
    mappingEditVelocityFadeOutHighSlider_.onValueChange = makeBatchEditCallback(&mappingBatchVelocityFadeEdited_);
    mappingEditGainSlider_.onValueChange = makeBatchEditCallback(&mappingBatchGainEdited_);
    mappingEditPanSlider_.onValueChange = makeBatchEditCallback(&mappingBatchPanEdited_);
    mappingEditRoundRobinGroupSlider_.onValueChange = makeBatchEditCallback(&mappingBatchRoundRobinGroupEdited_);
    mappingEditRoundRobinModeCombo_.onChange = makeBatchEditCallback(&mappingBatchRoundRobinModeEdited_);
    mappingEditChokeSlider_.onValueChange = makeBatchEditCallback(&mappingBatchChokeEdited_);
    mappingEditTriggerCombo_.onChange = makeBatchEditCallback(&mappingBatchTriggerEdited_);
    mappingEditLoopCombo_.onChange = makeBatchEditCallback(&mappingBatchLoopEdited_);

    addAndMakeVisible(mappingEditStatusLabel_);
    mappingEditStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    mappingEditStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff9ea6c5));
    mappingEditStatusLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));

    // Sample info row
    addAndMakeVisible(samplePathLabel_);
    samplePathLabel_.setJustificationType(juce::Justification::centredLeft);
    samplePathLabel_.setText("No sample loaded", juce::dontSendNotification);

    auto configureSampleInfoPair = [](juce::Label& keyLabel, juce::Label& valueLabel)
    {
        keyLabel.setJustificationType(juce::Justification::centredLeft);
        keyLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8a8aa0));
        keyLabel.setFont(juce::Font(juce::FontOptions(12.0f)));

        valueLabel.setJustificationType(juce::Justification::centredLeft);
        valueLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff4f6ff));
        valueLabel.setFont(juce::Font(juce::FontOptions(12.5f)).boldened());
    };

    configureSampleInfoPair(sampleInfoSourceLabel_, sampleInfoSourceValue_);
    configureSampleInfoPair(sampleInfoRateLabel_, sampleInfoRateValue_);
    configureSampleInfoPair(sampleInfoBitDepthLabel_, sampleInfoBitDepthValue_);
    configureSampleInfoPair(sampleInfoChannelsLabel_, sampleInfoChannelsValue_);
    configureSampleInfoPair(sampleInfoDurationLabel_, sampleInfoDurationValue_);
    configureSampleInfoPair(sampleInfoFileSizeLabel_, sampleInfoFileSizeValue_);
    configureSampleInfoPair(sampleInfoSamplesLabel_, sampleInfoSamplesValue_);
    configureSampleInfoPair(sampleInfoPlaybackLabel_, sampleInfoPlaybackValue_);
    configureSampleInfoPair(sampleInfoPlaybackDurationLabel_, sampleInfoPlaybackDurationValue_);
    configureSampleInfoPair(sampleInfoLoopLabel_, sampleInfoLoopValue_);
    configureSampleInfoPair(sampleInfoLoopDurationLabel_, sampleInfoLoopDurationValue_);
    configureSampleInfoPair(sampleInfoTempoLabel_, sampleInfoTempoValue_);
    configureSampleInfoPair(sampleInfoMetaRootLabel_, sampleInfoMetaRootValue_);
    sampleInfoSourceLabel_.setText("Path", juce::dontSendNotification);

    addAndMakeVisible(presetFilterEditor_);
    presetFilterEditor_.setTextToShowWhenEmpty("Find preset...", juce::Colours::grey);
    presetFilterEditor_.applyFontToAllText(juce::Font(juce::FontOptions(13.0f)));
    presetFilterEditor_.setIndents(10, 4);
    presetFilterEditor_.onTextChange = [this]
    {
        refreshPresetList(currentPresetName_);
    };

    addAndMakeVisible(presetCountLabel_);
    presetCountLabel_.setJustificationType(juce::Justification::centredRight);
    presetCountLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8a8aa0));
    presetCountLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));

    addAndMakeVisible(presetCombo_);
    presetCombo_.setTextWhenNoChoicesAvailable("No Presets");
    presetCombo_.setTextWhenNothingSelected("Preset...");
    presetCombo_.onChange = [this]
    {
        updatePresetActionButtons();

        if (!suppressPresetComboChange_)
            loadPresetFromSelection();
    };

    addAndMakeVisible(presetSaveButton_);
    presetSaveButton_.onClick = [this]
    {
        promptSavePreset();
    };

    addAndMakeVisible(presetRenameButton_);
    presetRenameButton_.onClick = [this]
    {
        renameSelectedPreset();
    };

    addAndMakeVisible(presetDeleteButton_);
    presetDeleteButton_.onClick = [this]
    {
        deleteSelectedPreset();
    };

    addAndMakeVisible(loadButton_);
    loadButton_.setTooltip("Load Sample, SFZ, or REX");
    loadButton_.onClick = [this] { openSampleChooser(); };

    addAndMakeVisible(sampleBrowserRailToggleButton_);
    sampleBrowserRailToggleButton_.setClickingTogglesState(true);
    sampleBrowserRailToggleButton_.setToggleState(sampleBrowserRailEnabled_, juce::dontSendNotification);
    sampleBrowserRailToggleButton_.setTooltip("Show the browser rail. At medium Sample widths it replaces the inspector.");
    sampleBrowserRailToggleButton_.onClick = [this]
    {
        sampleBrowserRailEnabled_ = sampleBrowserRailToggleButton_.getToggleState();

        const auto sampleLayoutMode = resolveSampleLayoutModeForWidth(computeResponsiveContentWidth(getWidth()));
        if (currentTabIndex_ == 0
            && sampleLayoutMode == SampleLayoutMode::workspaceInspector
            && sampleBrowserRailEnabled_)
        {
            sampleInspectorRailEnabled_ = false;
        }

        sampleInspectorRailToggleButton_.setToggleState(sampleInspectorRailEnabled_, juce::dontSendNotification);
        updateTabVisibility();
        resized();
        repaint();
    };

    addAndMakeVisible(sampleInspectorRailToggleButton_);
    sampleInspectorRailToggleButton_.setClickingTogglesState(true);
    sampleInspectorRailToggleButton_.setToggleState(sampleInspectorRailEnabled_, juce::dontSendNotification);
    sampleInspectorRailToggleButton_.setTooltip("Show the right-side inspector. At medium Sample widths it replaces the browser rail.");
    sampleInspectorRailToggleButton_.onClick = [this]
    {
        sampleInspectorRailEnabled_ = sampleInspectorRailToggleButton_.getToggleState();

        const auto sampleLayoutMode = resolveSampleLayoutModeForWidth(computeResponsiveContentWidth(getWidth()));
        if (currentTabIndex_ == 0
            && sampleLayoutMode == SampleLayoutMode::workspaceInspector
            && sampleInspectorRailEnabled_)
        {
            sampleBrowserRailEnabled_ = false;
        }

        sampleBrowserRailToggleButton_.setToggleState(sampleBrowserRailEnabled_, juce::dontSendNotification);
        updateTabVisibility();
        resized();
        repaint();
    };

    addAndMakeVisible(diagnosticsToggleButton_);
    diagnosticsToggleButton_.setClickingTogglesState(true);
    diagnosticsToggleButton_.setToggleState(showDiagnosticsPanel_, juce::dontSendNotification);
    diagnosticsToggleButton_.onClick = [this]
    {
        showDiagnosticsPanel_ = diagnosticsToggleButton_.getToggleState();
        resized();
        repaint();
    };

    addAndMakeVisible(waveformResetRangesButton_);
    waveformResetRangesButton_.onClick = [this]
    {
        if (waveformView_.onResetRangesRequested)
            waveformView_.onResetRangesRequested();
    };

    addAndMakeVisible(waveformInteractionSummaryLabel_);
    waveformInteractionSummaryLabel_.setJustificationType(juce::Justification::centredLeft);
    waveformInteractionSummaryLabel_.setColour(juce::Label::textColourId, uiTextMutedColour());
    waveformInteractionSummaryLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));

    waveformView_.setDisplayMode(processor_.getWaveformDisplayMode() == 2
        ? WaveformView::DisplayMode::symmetricEnvelope
        : WaveformView::DisplayMode::signedWaveform);
    waveformView_.onDisplayModeSelected = [this](const WaveformView::DisplayMode mode)
    {
        processor_.setWaveformDisplayMode(mode == WaveformView::DisplayMode::symmetricEnvelope ? 2 : 1);
    };

            refreshPresetList();

    addAndMakeVisible(rootNoteLabel_);
    rootNoteLabel_.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(rootNoteCombo_);
    for (int note = 0; note <= 127; ++note)
        rootNoteCombo_.addItem(formatMidiNoteName(note), note + 1);
    rootNoteCombo_.setSelectedId(61, juce::dontSendNotification);
    rootNoteCombo_.onChange = [this]
    {
        const auto selected = rootNoteCombo_.getSelectedId();
        if (selected > 0)
            processor_.setRootMidiNote(selected - 1);
    };

    addAndMakeVisible(tuneCoarseDial_);
    tuneCoarseDial_.setDoubleClickResetValue(0.0);
    tuneCoarseDial_.onValueChange = [this]
    {
        processor_.setCoarseTuneSemitones(static_cast<float>(tuneCoarseDial_.getValue()));
    };

    addAndMakeVisible(tuneFineDial_);
    tuneFineDial_.setDoubleClickResetValue(0.0);
    tuneFineDial_.onValueChange = [this]
    {
        processor_.setFineTuneCents(static_cast<float>(tuneFineDial_.getValue()));
    };

    addAndMakeVisible(pitchBendRangeDial_);
    pitchBendRangeDial_.setDoubleClickResetValue(2.0);
    pitchBendRangeDial_.onValueChange = [this]
    {
        processor_.setPitchBendRangeSemitones(static_cast<float>(pitchBendRangeDial_.getValue()));
    };
    addAndMakeVisible(pitchLfoRateDial_);
    pitchLfoRateDial_.setDoubleClickResetValue(0.0);
    pitchLfoRateDial_.setSkewFactorFromMidPoint(2.0);
    pitchLfoRateDial_.onValueChange = [this] { pushPitchLfoSettings(); };
    addAndMakeVisible(pitchLfoDepthDial_);
    pitchLfoDepthDial_.setDoubleClickResetValue(0.0);
    pitchLfoDepthDial_.onValueChange = [this] { pushPitchLfoSettings(); };

    // Waveform
    addAndMakeVisible(waveformView_);
    waveformView_.onLoopPreview = [this](const int ls, const int le)
    {
        loopStartDial_.setValue(ls, juce::dontSendNotification);
        loopEndDial_.setValue(le, juce::dontSendNotification);
        updateSampleInformationDisplay();
    };
    waveformView_.onLoopCommitted = [this](const int ls, const int le)
    {
        loopStartDial_.setValue(ls, juce::dontSendNotification);
        loopEndDial_.setValue(le, juce::dontSendNotification);
        applyLoopPoints();
    };
    waveformView_.onPlaybackPreview = [this](const int ps, const int pe)
    {
        playbackStartDial_.setValue(ps, juce::dontSendNotification);
        playbackEndDial_.setValue(pe, juce::dontSendNotification);
        updateSampleInformationDisplay();
    };
    waveformView_.onPlaybackCommitted = [this](const int ps, const int pe)
    {
        playbackStartDial_.setValue(ps, juce::dontSendNotification);
        playbackEndDial_.setValue(pe, juce::dontSendNotification);
        pushPlaybackWindow();
    };
    waveformView_.onResetRangesRequested = [this]
    {
        const auto sampleLength = processor_.getLoadedSampleLength();
        if (sampleLength <= 0)
        {
            waveformView_.resetView();
            return;
        }

        const int defaultStart = 0;
        const int defaultEnd = sampleLength - 1;

        playbackStartDial_.setValue(defaultStart, juce::dontSendNotification);
        playbackEndDial_.setValue(defaultEnd, juce::dontSendNotification);
        loopStartDial_.setValue(defaultStart, juce::dontSendNotification);
        loopEndDial_.setValue(defaultEnd, juce::dontSendNotification);

        processor_.setSampleWindow(defaultStart, defaultEnd);
        processor_.setLoopPoints(defaultStart, defaultEnd);

        const auto waveformBadge = processor_.hasImportedProgram()
            ? audiocity::plugin::importedProgramFormatBadge(processor_.getImportedProgramFormat())
            : processor_.getLoadedSampleLoopFormatBadge();
        waveformView_.setState(sampleLength, getLoadedSampleWaveformMinMaxByChannel(),
            defaultStart, defaultEnd, defaultStart, defaultEnd,
            waveformBadge);
        updateSampleInformationDisplay();
        waveformView_.resetView();
    };
    waveformView_.onAutoSliceRequested = [this]
    {
        autoSliceLoadedSample();
    };
    waveformView_.onMergeSliceRequested = [this](const int boundarySample)
    {
        mergeLoadedSliceAtBoundary(boundarySample);
    };
    waveformView_.onSplitSliceRequested = [this](const int sampleIndex)
    {
        splitLoadedSliceAtSample(sampleIndex);
    };

    // Playback mode
    addAndMakeVisible(playbackModeLabel_);
    addAndMakeVisible(playbackModeGateButton_);
    addAndMakeVisible(playbackModeOneShotButton_);
    addAndMakeVisible(playbackModeLoopButton_);

    constexpr int kPlaybackModeRadioGroup = 42001;
    playbackModeGateButton_.setRadioGroupId(kPlaybackModeRadioGroup);
    playbackModeOneShotButton_.setRadioGroupId(kPlaybackModeRadioGroup);
    playbackModeLoopButton_.setRadioGroupId(kPlaybackModeRadioGroup);

    playbackModeGateButton_.setToggleState(true, juce::dontSendNotification);
    playbackModeGateButton_.onClick = [this]
    {
        if (playbackModeGateButton_.getToggleState())
            processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::gate);
    };
    playbackModeOneShotButton_.onClick = [this]
    {
        if (playbackModeOneShotButton_.getToggleState())
            processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::oneShot);
    };
    playbackModeLoopButton_.onClick = [this]
    {
        if (playbackModeLoopButton_.getToggleState())
            processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::loop);
    };

    // Playback window (trim) controls
    addAndMakeVisible(playbackStartDial_);
    addAndMakeVisible(playbackEndDial_);
    playbackStartDial_.onValueChange = [this]
    {
        enforcePlaybackLoopConstraints();
        pushPlaybackWindow();
    };
    playbackEndDial_.onValueChange = [this]
    {
        enforcePlaybackLoopConstraints();
        pushPlaybackWindow();
    };

    // Loop controls
    addAndMakeVisible(loopStartDial_);
    addAndMakeVisible(loopEndDial_);
    addAndMakeVisible(loopCrossfadeDial_);
    loopCrossfadeDial_.setDoubleClickResetValue(0.0);
    loopStartDial_.onValueChange = [this]
    {
        enforcePlaybackLoopConstraints();
        applyLoopPoints();
    };
    loopEndDial_.onValueChange = [this]
    {
        enforcePlaybackLoopConstraints();
        applyLoopPoints();
    };
    loopCrossfadeDial_.onValueChange = [this]
    {
        processor_.setLoopCrossfadeSamples(juce::jmax(0, static_cast<int>(loopCrossfadeDial_.getValue())));
    };

    // Performance
    addAndMakeVisible(monoToggle_);
    addAndMakeVisible(legatoToggle_);
    addAndMakeVisible(velocityCurveLabel_);
    velocityCurveLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(velocityCurveCombo_);
    velocityCurveCombo_.addItem("Vel Linear", 1);
    velocityCurveCombo_.addItem("Vel Soft", 2);
    velocityCurveCombo_.addItem("Vel Hard", 3);
    velocityCurveCombo_.setSelectedId(1, juce::dontSendNotification);
    velocityCurveCombo_.onChange = [this]
    {
        const auto selected = velocityCurveCombo_.getSelectedId();
        auto curve = AudiocityAudioProcessor::VelocityCurve::linear;
        if (selected == 2)
            curve = AudiocityAudioProcessor::VelocityCurve::soft;
        else if (selected == 3)
            curve = AudiocityAudioProcessor::VelocityCurve::hard;
        processor_.setVelocityCurve(curve);
    };
    monoToggle_.onClick = [this] { pushPerformanceControls(); };
    legatoToggle_.onClick = [this] { pushPerformanceControls(); };

    addAndMakeVisible(glideDial_);
    glideDial_.setDoubleClickResetValue(0.0);
    glideDial_.setSkewFactorFromMidPoint(80.0);
    glideDial_.onValueChange = [this]
    {
        processor_.setGlideSeconds(static_cast<float>(glideDial_.getValue()) / 1000.0f);
    };

    addAndMakeVisible(polyphonyDial_);
    polyphonyDial_.setDoubleClickResetValue(64.0);
    polyphonyDial_.onValueChange = [this]
    {
        processor_.setPolyphonyLimit(juce::jlimit(1, 64, static_cast<int>(std::round(polyphonyDial_.getValue()))));
    };

    // Amp ADSR
    addAndMakeVisible(ampAttackDial_);
    ampAttackDial_.setDoubleClickResetValue(0.1);
    ampAttackDial_.setSkewFactorFromMidPoint(100.0);
    addAndMakeVisible(ampDecayDial_);
    ampDecayDial_.setDoubleClickResetValue(1.0);
    ampDecayDial_.setSkewFactorFromMidPoint(100.0);
    addAndMakeVisible(ampSustainDial_);
    ampSustainDial_.setDoubleClickResetValue(100.0);
    addAndMakeVisible(ampReleaseDial_);
    ampReleaseDial_.setDoubleClickResetValue(5.0);
    ampReleaseDial_.setSkewFactorFromMidPoint(100.0);
    addAndMakeVisible(ampLfoRateDial_);
    ampLfoRateDial_.setDoubleClickResetValue(0.0);
    ampLfoRateDial_.setSkewFactorFromMidPoint(2.0);
    addAndMakeVisible(ampLfoDepthDial_);
    ampLfoDepthDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(ampLfoShapeLabel_);
    ampLfoShapeLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(ampLfoShapeCombo_);
    ampLfoShapeCombo_.addItem("Sine", 1);
    ampLfoShapeCombo_.addItem("Triangle", 2);
    ampLfoShapeCombo_.addItem("Square", 3);
    ampLfoShapeCombo_.addItem("Saw Up", 4);
    ampLfoShapeCombo_.addItem("Saw Down", 5);
    ampLfoShapeCombo_.setSelectedId(1, juce::dontSendNotification);
    ampLfoShapeCombo_.onChange = [this] { pushAmpLfoSettings(); };
    addAndMakeVisible(ampEnvelopeGraph_);
    ampEnvelopeGraph_.onEnvelopeEdited = [this](float attackMs, float decayMs, float sustain, float releaseMs)
    {
        ampAttackDial_.setValue(attackMs, juce::dontSendNotification);
        ampDecayDial_.setValue(decayMs, juce::dontSendNotification);
        ampSustainDial_.setValue(sustain * 100.0f, juce::dontSendNotification);
        ampReleaseDial_.setValue(releaseMs, juce::dontSendNotification);
        pushAmpEnvelope();
        updateAmpEnvelopeGraphFromDials();
    };
    ampAttackDial_.onValueChange = [this] { pushAmpEnvelope(); updateAmpEnvelopeGraphFromDials(); };
    ampDecayDial_.onValueChange = [this] { pushAmpEnvelope(); updateAmpEnvelopeGraphFromDials(); };
    ampSustainDial_.onValueChange = [this] { pushAmpEnvelope(); updateAmpEnvelopeGraphFromDials(); };
    ampReleaseDial_.onValueChange = [this] { pushAmpEnvelope(); updateAmpEnvelopeGraphFromDials(); };
    ampLfoRateDial_.onValueChange = [this] { pushAmpLfoSettings(); };
    ampLfoDepthDial_.onValueChange = [this] { pushAmpLfoSettings(); };

    // Filter
    addAndMakeVisible(filterCutoffDial_);
    filterCutoffDial_.setDoubleClickResetValue(18000.0);
    filterCutoffDial_.setSkewFactorFromMidPoint(1000.0);
    addAndMakeVisible(filterResDial_);
    filterResDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(filterEnvAmtDial_);
    filterEnvAmtDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(filterTypeLabel_);
    addAndMakeVisible(filterResponseGraph_);
    filterTypeLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(filterTypeCombo_);
    filterTypeCombo_.addItem("LP 12", 1);
    filterTypeCombo_.addItem("LP 24", 2);
    filterTypeCombo_.addItem("HP 12", 3);
    filterTypeCombo_.addItem("HP 24", 4);
    filterTypeCombo_.addItem("BP 12", 5);
    filterTypeCombo_.addItem("Notch", 6);
    filterTypeCombo_.setSelectedId(1, juce::dontSendNotification);
    filterTypeCombo_.onChange = [this] { pushFilterSettings(); };

    addAndMakeVisible(filterAttackDial_);
    filterAttackDial_.setDoubleClickResetValue(0.1);
    filterAttackDial_.setSkewFactorFromMidPoint(100.0);
    addAndMakeVisible(filterDecayDial_);
    filterDecayDial_.setDoubleClickResetValue(1.0);
    filterDecayDial_.setSkewFactorFromMidPoint(100.0);
    addAndMakeVisible(filterSustainDial_);
    filterSustainDial_.setDoubleClickResetValue(100.0);
    addAndMakeVisible(filterReleaseDial_);
    filterReleaseDial_.setDoubleClickResetValue(5.0);
    filterReleaseDial_.setSkewFactorFromMidPoint(100.0);
    addAndMakeVisible(filterEnvelopeGraph_);
    filterEnvelopeGraph_.onEnvelopeEdited = [this](float attackMs, float decayMs, float sustain, float releaseMs)
    {
        filterAttackDial_.setValue(attackMs, juce::dontSendNotification);
        filterDecayDial_.setValue(decayMs, juce::dontSendNotification);
        filterSustainDial_.setValue(sustain * 100.0f, juce::dontSendNotification);
        filterReleaseDial_.setValue(releaseMs, juce::dontSendNotification);
        pushFilterEnvelope();
        updateFilterEnvelopeGraphFromDials();
    };
    addAndMakeVisible(filterKeytrackDial_);
    filterKeytrackDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(filterVelDial_);
    filterVelDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(filterLfoRateDial_);
    filterLfoRateDial_.setDoubleClickResetValue(0.0);
    filterLfoRateDial_.setSkewFactorFromMidPoint(2.0);
    addAndMakeVisible(filterLfoRateKeyDial_);
    addAndMakeVisible(filterLfoAmtDial_);
    filterLfoAmtDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(filterLfoAmtKeyDial_);
    addAndMakeVisible(filterLfoStartPhaseDial_);
    addAndMakeVisible(filterLfoStartRandDial_);
    addAndMakeVisible(filterLfoFadeInDial_);
    filterLfoFadeInDial_.setSkewFactorFromMidPoint(200.0);
    addAndMakeVisible(filterLfoShapeLabel_);
    filterLfoShapeLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(filterLfoShapeCombo_);
    filterLfoShapeCombo_.addItem("Sine", 1);
    filterLfoShapeCombo_.addItem("Triangle", 2);
    filterLfoShapeCombo_.addItem("Square", 3);
    filterLfoShapeCombo_.addItem("Saw Up", 4);
    filterLfoShapeCombo_.addItem("Saw Down", 5);
    filterLfoShapeCombo_.setSelectedId(1, juce::dontSendNotification);
    filterLfoShapeCombo_.onChange = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoRetriggerToggle_);
    filterLfoRetriggerToggle_.onClick = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoTempoSyncToggle_);
    filterLfoTempoSyncToggle_.onClick = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoRateKeySyncToggle_);
    filterLfoRateKeySyncToggle_.onClick = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoKeytrackLinearToggle_);
    filterLfoKeytrackLinearToggle_.onClick = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoUnipolarToggle_);
    filterLfoUnipolarToggle_.onClick = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoDivisionLabel_);
    filterLfoDivisionLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(filterLfoDivisionCombo_);
    filterLfoDivisionCombo_.addItem("1/16", 1);
    filterLfoDivisionCombo_.addItem("1/16T", 2);
    filterLfoDivisionCombo_.addItem("1/16.", 3);
    filterLfoDivisionCombo_.addItem("1/8", 4);
    filterLfoDivisionCombo_.addItem("1/8T", 5);
    filterLfoDivisionCombo_.addItem("1/8.", 6);
    filterLfoDivisionCombo_.addItem("1/4", 7);
    filterLfoDivisionCombo_.addItem("1/4T", 8);
    filterLfoDivisionCombo_.addItem("1/4.", 9);
    filterLfoDivisionCombo_.addItem("1/2", 10);
    filterLfoDivisionCombo_.addItem("1/1", 11);
    filterLfoDivisionCombo_.addItem("2/1", 12);
    filterLfoDivisionCombo_.setSelectedId(7, juce::dontSendNotification);
    filterLfoDivisionCombo_.onChange = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterKeytrackSnapLabel_);
    filterKeytrackSnapLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(filterKeytrackSnapCombo_);
    filterKeytrackSnapCombo_.addItem("Snap...", 1);
    filterKeytrackSnapCombo_.addItem("-100%", 2);
    filterKeytrackSnapCombo_.addItem("0%", 3);
    filterKeytrackSnapCombo_.addItem("50%", 4);
    filterKeytrackSnapCombo_.addItem("100%", 5);
    filterKeytrackSnapCombo_.addItem("200%", 6);
    filterKeytrackSnapCombo_.setSelectedId(1, juce::dontSendNotification);
    filterKeytrackSnapCombo_.onChange = [this]
    {
        const auto selected = filterKeytrackSnapCombo_.getSelectedId();
        if (selected <= 1)
            return;

        int snapPercent = 0;
        switch (selected)
        {
            case 2: snapPercent = -100; break;
            case 3: snapPercent = 0; break;
            case 4: snapPercent = 50; break;
            case 5: snapPercent = 100; break;
            case 6: snapPercent = 200; break;
            default: break;
        }

        filterKeytrackDial_.setValue(static_cast<double>(snapPercent));
        filterKeytrackSnapCombo_.setSelectedId(1, juce::dontSendNotification);
    };
    filterCutoffDial_.onValueChange = [this] { pushFilterSettings(); };
    filterResDial_.onValueChange = [this] { pushFilterSettings(); };
    filterEnvAmtDial_.onValueChange = [this] { pushFilterSettings(); };
    filterKeytrackDial_.onValueChange = [this] { pushFilterSettings(); };
    filterVelDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoRateDial_.setShiftWheelFineFactor(8.0);
    filterLfoRateDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoRateKeyDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoAmtDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoAmtKeyDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoStartPhaseDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoStartRandDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoFadeInDial_.onValueChange = [this] { pushFilterSettings(); };
    filterAttackDial_.onValueChange = [this] { pushFilterEnvelope(); updateFilterEnvelopeGraphFromDials(); };
    filterDecayDial_.onValueChange = [this] { pushFilterEnvelope(); updateFilterEnvelopeGraphFromDials(); };
    filterSustainDial_.onValueChange = [this] { pushFilterEnvelope(); updateFilterEnvelopeGraphFromDials(); };
    filterReleaseDial_.onValueChange = [this] { pushFilterEnvelope(); updateFilterEnvelopeGraphFromDials(); };

    filterLfoRateKeyDial_.setVisible(false);
    filterLfoAmtKeyDial_.setVisible(false);
    filterLfoStartPhaseDial_.setVisible(false);
    filterLfoStartRandDial_.setVisible(false);
    filterLfoFadeInDial_.setVisible(false);
    filterKeytrackSnapLabel_.setVisible(false);
    filterKeytrackSnapCombo_.setVisible(false);
    filterLfoRateKeySyncToggle_.setVisible(false);
    filterLfoKeytrackLinearToggle_.setVisible(false);
    filterLfoUnipolarToggle_.setVisible(false);

    // Quality / Preload
    addAndMakeVisible(qualityLabel_);
    addAndMakeVisible(qualityCpuButton_);
    addAndMakeVisible(qualityFidelityButton_);
    addAndMakeVisible(qualityUltraButton_);

    constexpr int kQualityRadioGroup = 42002;
    qualityCpuButton_.setRadioGroupId(kQualityRadioGroup);
    qualityFidelityButton_.setRadioGroupId(kQualityRadioGroup);
    qualityUltraButton_.setRadioGroupId(kQualityRadioGroup);

    qualityFidelityButton_.setToggleState(true, juce::dontSendNotification);
    qualityCpuButton_.onClick = [this]
    {
        if (qualityCpuButton_.getToggleState())
            processor_.setQualityTier(AudiocityAudioProcessor::QualityTier::cpu);
    };
    qualityFidelityButton_.onClick = [this]
    {
        if (qualityFidelityButton_.getToggleState())
            processor_.setQualityTier(AudiocityAudioProcessor::QualityTier::fidelity);
    };
    qualityUltraButton_.onClick = [this]
    {
        if (qualityUltraButton_.getToggleState())
            processor_.setQualityTier(AudiocityAudioProcessor::QualityTier::ultra);
    };

    addAndMakeVisible(preloadDial_);
    preloadDial_.setDoubleClickResetValue(32768.0);
    addAndMakeVisible(masterVolumeDial_);
    addAndMakeVisible(panDial_);
    addAndMakeVisible(outputLevelMeter_);
    addAndMakeVisible(reverbMixDial_);
    addAndMakeVisible(delayTimeDial_);
    addAndMakeVisible(delayFeedbackDial_);
    addAndMakeVisible(delayMixDial_);
    addAndMakeVisible(delayTempoSyncToggle_);
    addAndMakeVisible(dcFilterEnabledToggle_);
    addAndMakeVisible(dcFilterCutoffDial_);
    addAndMakeVisible(autopanRateDial_);
    addAndMakeVisible(autopanDepthDial_);
    addAndMakeVisible(saturationDriveDial_);
    addAndMakeVisible(saturationModeCombo_);
    masterVolumeDial_.setDoubleClickResetValue(100.0);
    panDial_.setDoubleClickResetValue(0.0);
    reverbMixDial_.setDoubleClickResetValue(0.0);
    delayTimeDial_.setDoubleClickResetValue(320.0);
    delayTimeDial_.setSkewFactorFromMidPoint(250.0);
    delayFeedbackDial_.setDoubleClickResetValue(35.0);
    delayMixDial_.setDoubleClickResetValue(0.0);
    dcFilterCutoffDial_.setDoubleClickResetValue(10.0);
    dcFilterCutoffDial_.setSkewFactorFromMidPoint(10.0);
    autopanRateDial_.setDoubleClickResetValue(0.5);
    autopanRateDial_.setSkewFactorFromMidPoint(1.0);
    autopanDepthDial_.setDoubleClickResetValue(0.0);
    saturationDriveDial_.setDoubleClickResetValue(0.0);
    saturationModeCombo_.addItem("Soft Clip", 1);
    saturationModeCombo_.addItem("Hard Clip", 2);
    saturationModeCombo_.addItem("Tape", 3);
    saturationModeCombo_.addItem("Tube", 4);
    saturationModeCombo_.setSelectedId(1, juce::dontSendNotification);
    preloadDial_.onValueChange = [this]
    {
        processor_.setPreloadSamples(juce::jmax(256, static_cast<int>(preloadDial_.getValue())));
        refreshUI();
    };
    masterVolumeDial_.onValueChange = [this]
    {
        processor_.setMasterVolume(static_cast<float>(masterVolumeDial_.getValue()) / 100.0f);
    };
    panDial_.onValueChange = [this]
    {
        processor_.setPan(static_cast<float>(panDial_.getValue()) / 100.0f);
    };
    reverbMixDial_.onValueChange = [this]
    {
        processor_.setReverbMix(static_cast<float>(reverbMixDial_.getValue()) / 100.0f);
    };
    delayTimeDial_.onValueChange = [this] { pushDelaySettings(); };
    delayFeedbackDial_.onValueChange = [this] { pushDelaySettings(); };
    delayMixDial_.onValueChange = [this] { pushDelaySettings(); };
    delayTempoSyncToggle_.onClick = [this] { pushDelaySettings(); };
    dcFilterEnabledToggle_.onClick = [this] { pushDcFilterSettings(); };
    dcFilterCutoffDial_.onValueChange = [this] { pushDcFilterSettings(); };
    autopanRateDial_.onValueChange = [this] { pushAutopanSettings(); };
    autopanDepthDial_.onValueChange = [this] { pushAutopanSettings(); };
    saturationDriveDial_.onValueChange = [this] { pushSaturationSettings(); };
    saturationModeCombo_.onChange = [this] { pushSaturationSettings(); };

    // Reverse / Fade
    addAndMakeVisible(reverseToggle_);
    reverseToggle_.onClick = [this]
    {
        processor_.setReversePlayback(reverseToggle_.getToggleState());
    };

    addAndMakeVisible(fadeInDial_);
    addAndMakeVisible(fadeOutDial_);
    fadeInDial_.setDoubleClickResetValue(0.0);
    fadeOutDial_.setDoubleClickResetValue(0.0);
    auto pushFades = [this]
    {
        processor_.setFadeSamples(
            juce::jmax(0, static_cast<int>(fadeInDial_.getValue())),
            juce::jmax(0, static_cast<int>(fadeOutDial_.getValue())));
    };
    fadeInDial_.onValueChange = pushFades;
    fadeOutDial_.onValueChange = pushFades;

    // Diagnostics
    addAndMakeVisible(diagnosticsLabel_);
    diagnosticsLabel_.setJustificationType(juce::Justification::centredLeft);
    diagnosticsLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));

    programMapText_.setMultiLine(true);
    programMapText_.setReadOnly(true);
    programMapText_.setScrollbarsShown(true);
    programMapText_.setCaretVisible(false);
    programMapText_.setPopupMenuEnabled(false);
    programMapText_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff202034));
    programMapText_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff3a3a52));
    programMapText_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff3a3a52));
    programMapText_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffe5e5ef));
    programMapText_.setFont(juce::Font(juce::FontOptions(12.0f)));

    // Register all dials with their parameter IDs for CC mapping
    allDials_ = {
        { &playbackStartDial_,  "playbackStart" },
        { &playbackEndDial_,    "playbackEnd" },
        { &loopStartDial_,      "loopStart" },
        { &loopEndDial_,        "loopEnd" },
        { &loopCrossfadeDial_,  "loopCrossfade" },
        { &glideDial_,          "glide" },
        { &polyphonyDial_,      "polyphony" },
        { &ampAttackDial_,      "ampAttack" },
        { &ampDecayDial_,       "ampDecay" },
        { &ampSustainDial_,     "ampSustain" },
        { &ampReleaseDial_,     "ampRelease" },
        { &ampLfoRateDial_,     "ampLfoRate" },
        { &ampLfoDepthDial_,    "ampLfoDepth" },
        { &filterCutoffDial_,   "filterCutoff" },
        { &filterResDial_,      "filterRes" },
        { &filterEnvAmtDial_,   "filterEnvAmt" },
        { &filterAttackDial_,   "filterAttack" },
        { &filterDecayDial_,    "filterDecay" },
        { &filterSustainDial_,  "filterSustain" },
        { &filterReleaseDial_,  "filterRelease" },
        { &filterKeytrackDial_, "filterKeytrack" },
        { &filterVelDial_,      "filterVel" },
        { &filterLfoRateDial_,  "filterLfoRate" },
        { &filterLfoRateKeyDial_, "filterLfoRateKeytrack" },
        { &filterLfoAmtDial_,   "filterLfoAmount" },
        { &filterLfoAmtKeyDial_, "filterLfoAmountKeytrack" },
        { &filterLfoStartPhaseDial_, "filterLfoStartPhase" },
        { &filterLfoStartRandDial_, "filterLfoStartRand" },
        { &filterLfoFadeInDial_, "filterLfoFadeIn" },
        { &tuneCoarseDial_,     "tuneCoarse" },
        { &tuneFineDial_,       "tuneFine" },
        { &pitchBendRangeDial_, "pitchBendRange" },
        { &pitchLfoRateDial_,   "pitchLfoRate" },
        { &pitchLfoDepthDial_,  "pitchLfoDepth" },
        { &preloadDial_,        "preload" },
        { &masterVolumeDial_,   "masterVolume" },
        { &panDial_,            "pan" },
        { &reverbMixDial_,      "reverbMix" },
        { &delayTimeDial_,      "delayTime" },
        { &delayFeedbackDial_,  "delayFeedback" },
        { &delayMixDial_,       "delayMix" },
        { &dcFilterCutoffDial_, "dcFilterCutoff" },
        { &autopanRateDial_,    "autopanRate" },
        { &autopanDepthDial_,   "autopanDepth" },
        { &saturationDriveDial_,"saturationDrive" },
        { &fadeInDial_,         "fadeIn" },
        { &fadeOutDial_,        "fadeOut" },
    };

    modulationPanel_.forEachDial([this](CcLearnDial& dial, const juce::String& paramId)
    {
        allDials_.push_back({ &dial, paramId });
    });

    for (auto& [dial, paramId] : allDials_)
    {
        dial->onCcClearedByUser = [this, paramId]
        {
            processor_.clearCcMappingByParam(paramId);
        };
    }

    auto addToSampleControls = [this](juce::Component& c)
    {
        sampleControlsContent_.addAndMakeVisible(c);
    };

    addToSampleControls(playbackStartDial_);
    addToSampleControls(sampleInfoSourceLabel_);
    addToSampleControls(sampleInfoSourceValue_);
    addToSampleControls(sampleInfoRateLabel_);
    addToSampleControls(sampleInfoRateValue_);
    addToSampleControls(sampleInfoBitDepthLabel_);
    addToSampleControls(sampleInfoBitDepthValue_);
    addToSampleControls(sampleInfoChannelsLabel_);
    addToSampleControls(sampleInfoChannelsValue_);
    addToSampleControls(sampleInfoDurationLabel_);
    addToSampleControls(sampleInfoDurationValue_);
    addToSampleControls(sampleInfoFileSizeLabel_);
    addToSampleControls(sampleInfoFileSizeValue_);
    addToSampleControls(sampleInfoSamplesLabel_);
    addToSampleControls(sampleInfoSamplesValue_);
    addToSampleControls(sampleInfoPlaybackLabel_);
    addToSampleControls(sampleInfoPlaybackValue_);
    addToSampleControls(sampleInfoPlaybackDurationLabel_);
    addToSampleControls(sampleInfoPlaybackDurationValue_);
    addToSampleControls(sampleInfoLoopLabel_);
    addToSampleControls(sampleInfoLoopValue_);
    addToSampleControls(sampleInfoLoopDurationLabel_);
    addToSampleControls(sampleInfoLoopDurationValue_);
    addToSampleControls(sampleInfoTempoLabel_);
    addToSampleControls(sampleInfoTempoValue_);
    addToSampleControls(sampleInfoMetaRootLabel_);
    addToSampleControls(sampleInfoMetaRootValue_);
    addToSampleControls(sampleInfoBadge_);
    addToSampleControls(playbackEndDial_);
    addToSampleControls(loopStartDial_);
    addToSampleControls(loopEndDial_);
    addToSampleControls(loopCrossfadeDial_);
    addToSampleControls(playbackModeLabel_);
    addToSampleControls(playbackModeGateButton_);
    addToSampleControls(playbackModeOneShotButton_);
    addToSampleControls(playbackModeLoopButton_);
    addToSampleControls(monoToggle_);
    addToSampleControls(legatoToggle_);
    addToSampleControls(reverseToggle_);
    addToSampleControls(velocityCurveLabel_);
    addToSampleControls(velocityCurveCombo_);
    addToSampleControls(glideDial_);
    addToSampleControls(polyphonyDial_);
    addToSampleControls(rootNoteLabel_);
    addToSampleControls(rootNoteCombo_);
    addToSampleControls(tuneCoarseDial_);
    addToSampleControls(tuneFineDial_);
    addToSampleControls(pitchBendRangeDial_);
    addToSampleControls(pitchLfoRateDial_);
    addToSampleControls(pitchLfoDepthDial_);
    addToSampleControls(modulationPanel_);
    addToSampleControls(ampAttackDial_);
    addToSampleControls(ampDecayDial_);
    addToSampleControls(ampSustainDial_);
    addToSampleControls(ampReleaseDial_);
    addToSampleControls(ampLfoRateDial_);
    addToSampleControls(ampLfoDepthDial_);
    addToSampleControls(ampLfoShapeLabel_);
    addToSampleControls(ampLfoShapeCombo_);
    addToSampleControls(ampEnvelopeGraph_);
    addToSampleControls(filterCutoffDial_);
    addToSampleControls(filterResDial_);
    addToSampleControls(filterEnvAmtDial_);
    addToSampleControls(filterTypeLabel_);
    addToSampleControls(filterTypeCombo_);
    addToSampleControls(filterResponseGraph_);
    addToSampleControls(filterAttackDial_);
    addToSampleControls(filterDecayDial_);
    addToSampleControls(filterSustainDial_);
    addToSampleControls(filterReleaseDial_);
    addToSampleControls(filterEnvelopeGraph_);
    addToSampleControls(filterKeytrackDial_);
    addToSampleControls(filterVelDial_);
    addToSampleControls(filterLfoRateDial_);
    addToSampleControls(filterLfoRateKeyDial_);
    addToSampleControls(filterLfoAmtDial_);
    addToSampleControls(filterLfoAmtKeyDial_);
    addToSampleControls(filterLfoStartPhaseDial_);
    addToSampleControls(filterLfoStartRandDial_);
    addToSampleControls(filterLfoFadeInDial_);
    addToSampleControls(filterLfoShapeLabel_);
    addToSampleControls(filterLfoShapeCombo_);
    addToSampleControls(filterLfoRetriggerToggle_);
    addToSampleControls(filterLfoTempoSyncToggle_);
    addToSampleControls(filterLfoRateKeySyncToggle_);
    addToSampleControls(filterLfoKeytrackLinearToggle_);
    addToSampleControls(filterLfoUnipolarToggle_);
    addToSampleControls(filterLfoDivisionLabel_);
    addToSampleControls(filterLfoDivisionCombo_);
    addToSampleControls(filterKeytrackSnapLabel_);
    addToSampleControls(filterKeytrackSnapCombo_);
    addToSampleControls(fadeInDial_);
    addToSampleControls(fadeOutDial_);
    addToSampleControls(qualityLabel_);
    addToSampleControls(qualityCpuButton_);
    addToSampleControls(qualityFidelityButton_);
    addToSampleControls(qualityUltraButton_);
    addToSampleControls(preloadDial_);
    addToSampleControls(masterVolumeDial_);
    addToSampleControls(panDial_);
    addToSampleControls(outputLevelMeter_);
    addToSampleControls(reverbMixDial_);
    addToSampleControls(delayTimeDial_);
    addToSampleControls(delayFeedbackDial_);
    addToSampleControls(delayMixDial_);
    addToSampleControls(delayTempoSyncToggle_);
    addToSampleControls(dcFilterEnabledToggle_);
    addToSampleControls(dcFilterCutoffDial_);
    addToSampleControls(autopanRateDial_);
    addToSampleControls(autopanDepthDial_);
    addToSampleControls(saturationDriveDial_);
    addToSampleControls(saturationModeCombo_);
    addToSampleControls(programMapText_);
    addToSampleControls(diagnosticsLabel_);

    // About pane
    aboutIconImage_ = juce::ImageFileFormat::loadFrom(BinaryData::audiocity_icon_128_png,
                                                       BinaryData::audiocity_icon_128_pngSize);
    addAndMakeVisible(aboutGitHubButton_);
    aboutGitHubButton_.onClick = []
    {
        juce::URL("https://github.com/thetheosopher/Audiocity").launchInDefaultBrowser();
    };
    addAndMakeVisible(aboutCoffeeButton_);
    aboutCoffeeButton_.onClick = []
    {
        juce::URL("https://buymeacoffee.com/theosopher").launchInDefaultBrowser();
    };

    setupTooltips();
    refreshSampleBrowserBookmarks();
    refreshMappingZoneRows();
    rebuildVisibleSampleList();

    const auto restoredSampleRoot = processor_.getSampleBrowserRootFolder();
    if (restoredSampleRoot.isNotEmpty())
    {
        const juce::File restoredFolder(restoredSampleRoot);
        if (restoredFolder.isDirectory())
            scanSampleRootFolder(restoredFolder);
        else
            sampleBrowserRootLabel_.setText("< Select Folder >", juce::dontSendNotification);
    }

    refreshSampleBrowserBookmarks();

    updateTabVisibility();
    refreshUI();
    refreshCaptureWaveform(true);
    updateCaptureUiState();
    lastSettingsSnapshot_ = captureSettingsSnapshot();
    syncCcMappingsFromProcessor();
    startTimerHz(60);
}

AudiocityAudioProcessorEditor::~AudiocityAudioProcessorEditor()
{
    stopTimer();
    playerKeyboardState_.removeListener(this);
    tabBar_.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void AudiocityAudioProcessorEditor::handleNoteOn(juce::MidiKeyboardState* source,
                                                 const int midiChannel,
                                                 const int midiNoteNumber,
                                                 const float velocity)
{
    juce::ignoreUnused(source, midiChannel);
    processor_.enqueueUiMidiNoteOn(midiNoteNumber,
        juce::jlimit(1, 127, static_cast<int>(std::round(velocity * 127.0f))));
}

void AudiocityAudioProcessorEditor::handleNoteOff(juce::MidiKeyboardState* source,
                                                  const int midiChannel,
                                                  const int midiNoteNumber,
                                                  const float velocity)
{
    juce::ignoreUnused(source, midiChannel, velocity);
    processor_.enqueueUiMidiNoteOff(midiNoteNumber);
}

// ─── Timer: poll CC FIFO ───────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::timerCallback()
{
    updateGeneratePreviewButtonText();

    processor_.setWaveformViewRange(waveformView_.getViewStartSample(), waveformView_.getViewSampleCount());

    waveformView_.setVoicePlaybackPositions(processor_.getVoicePlaybackPositions());

    const auto outputPeaks = processor_.consumeOutputPeakLevels();
    outputLevelMeter_.pushLevels(outputPeaks.left, outputPeaks.right);
    updatePerformanceStripStatus(outputPeaks.left, outputPeaks.right);

    if (currentTabIndex_ == 5 || processor_.isInputCaptureRecording())
    {
        refreshCaptureWaveform(processor_.isInputCaptureRecording());
        updateCaptureUiState();
        const auto capturePeaks = processor_.consumeCaptureInputPeakLevels();
        captureInputVuMeter_.pushLevels(capturePeaks.left, capturePeaks.right);
    }

    // ── Process deferred drag-and-drop (safe: outside OLE modal loop) ──
    if (hasPendingDrop_)
    {
        DBG("[DnD] timerCallback: processing pending drop");
        hasPendingDrop_ = false;
        auto droppedFiles = std::move(pendingDropFiles_);
        pendingDropFiles_.clear();

        for (const auto& path : droppedFiles)
        {
            auto normalizedPath = path.trim();
            DBG("[DnD]   raw path: \"" + normalizedPath + "\"");

            // Normalise file:// URIs to a local path (string ops first, URL fallback)
            if (normalizedPath.startsWithIgnoreCase("file:///"))
                normalizedPath = normalizedPath.substring(8).replace("/", "\\");
            else if (normalizedPath.startsWithIgnoreCase("file://"))
                normalizedPath = normalizedPath.substring(7).replace("/", "\\");

            // Percent-decode common URL-encoded chars (%20 → space, etc.)
            normalizedPath = juce::URL::removeEscapeChars(normalizedPath);

            DBG("[DnD]   normalized: \"" + normalizedPath + "\"");

            const juce::File dropped(normalizedPath);
            DBG("[DnD]   existsAsFile: " + juce::String(dropped.existsAsFile() ? "yes" : "no"));
            if (!dropped.existsAsFile())
                continue;

            const auto ext = dropped.getFileExtension().toLowerCase();
            DBG("[DnD]   ext: \"" + ext + "\"");
            if (isSupportedSampleFile(dropped))
            {
                DBG("[DnD]   loading instrument...");
                loadFileAsInstrument(dropped, [this](const bool loaded)
                {
                    if (loaded)
                    {
                        DBG("[DnD]   load succeeded, refreshing UI");
                        clearSelectedPresetAfterSourceLoad();
                        refreshUI(true);
                    }
                    else
                    {
                        DBG("[DnD]   load FAILED");
                    }
                });
                break;
            }
        }
        DBG("[DnD] timerCallback: pending drop processing complete");
    }

    const auto selectedTab = tabBar_.getCurrentTabIndex();
    if (selectedTab != currentTabIndex_)
    {
        currentTabIndex_ = selectedTab;
        processor_.setEditorTabIndex(currentTabIndex_);
        updateTabVisibility();
        resized();
        repaint();

        if (currentTabIndex_ == 1)
            sampleBrowserListBox_.grabKeyboardFocus();
        else if (currentTabIndex_ == 2)
        {
            refreshMappingZoneRows();
            mappingZoneListBox_.grabKeyboardFocus();
        }
        else if (currentTabIndex_ == 5)
        {
            refreshCaptureWaveform(true);
            updateCaptureUiState();
        }
    }

    AudiocityAudioProcessor::CcEvent event{};
    while (processor_.popCcEvent(event))
    {
        // Check if any dial is armed for CC learn
        for (auto& [dial, paramId] : allDials_)
        {
            if (dial->isCcLearnArmed())
            {
                dial->assignCc(event.ccNumber);
                processor_.setCcMapping(event.ccNumber, paramId);
                break;
            }
        }

        // Route mapped CC to the correct dial
        const auto mappedParam = processor_.getParamForCc(event.ccNumber);
        if (mappedParam.isNotEmpty())
        {
            for (auto& [dial, paramId] : allDials_)
            {
                if (paramId == mappedParam)
                {
                    dial->handleCcValue(event.value);
                    break;
                }
            }
        }
    }

    if (currentTabIndex_ == 1 || shouldShowPersistentBrowserRail())
    {
        const auto scanning = sampleScanInProgress_.load(std::memory_order_relaxed);
        const bool shouldShowCancelButton = (currentTabIndex_ == 1 || shouldShowPersistentBrowserRail()) && scanning;
        if (sampleBrowserCancelButton_.isVisible() != shouldShowCancelButton)
        {
            sampleBrowserCancelButton_.setVisible(shouldShowCancelButton);
            resized();
        }

        const auto statusText = scanning
            ? juce::String("Scanning...")
            : (processor_.isSamplePreviewPlaying()
                ? juce::String("Previewing...  |  Dbl-click load")
                : juce::String("Click preview  |  Dbl-click load"));
        sampleBrowserPreviewLabel_.setText(
            statusText,
            juce::dontSendNotification);
    }

    syncImportedProgramMappingUndoContext();

    const auto currentSnapshot = captureSettingsSnapshot();
    if (lastSettingsSnapshot_.has_value() && *lastSettingsSnapshot_ != currentSnapshot)
        editorUndoHistory_.recordSettingsChange(*lastSettingsSnapshot_, currentSnapshot, 1, "Edit Settings");

    lastSettingsSnapshot_ = currentSnapshot;

    syncAutomatedControlsFromProcessor();
}

void AudiocityAudioProcessorEditor::syncAutomatedControlsFromProcessor()
{
    if (currentTabIndex_ != 0)
        return;

    const auto playbackStart = static_cast<double>(processor_.getSampleWindowStart());
    const auto playbackEnd = static_cast<double>(processor_.getSampleWindowEnd());
    const auto loopStart = static_cast<double>(processor_.getLoopStart());
    const auto loopEnd = static_cast<double>(processor_.getLoopEnd());
    const auto loopXfade = static_cast<double>(processor_.getLoopCrossfadeSamples());

    if (std::abs(playbackStartDial_.getValue() - playbackStart) > 0.5)
    {
        playbackStartDial_.setValue(playbackStart, juce::dontSendNotification);
    }
    if (std::abs(playbackEndDial_.getValue() - playbackEnd) > 0.5)
    {
        playbackEndDial_.setValue(playbackEnd, juce::dontSendNotification);
    }
    if (std::abs(loopStartDial_.getValue() - loopStart) > 0.5)
    {
        loopStartDial_.setValue(loopStart, juce::dontSendNotification);
    }
    if (std::abs(loopEndDial_.getValue() - loopEnd) > 0.5)
    {
        loopEndDial_.setValue(loopEnd, juce::dontSendNotification);
    }
    if (std::abs(loopCrossfadeDial_.getValue() - loopXfade) > 0.5)
        loopCrossfadeDial_.setValue(loopXfade, juce::dontSendNotification);

    const auto rootId = processor_.getRootMidiNote() + 1;
    const bool isEditingRootNote = rootNoteCombo_.hasKeyboardFocus(true) || rootNoteCombo_.isPopupActive();
    if (!isEditingRootNote && rootNoteCombo_.getSelectedId() != rootId)
        rootNoteCombo_.setSelectedId(rootId, juce::dontSendNotification);
    tuneCoarseDial_.setValue(processor_.getCoarseTuneSemitones(), juce::dontSendNotification);
    tuneFineDial_.setValue(processor_.getFineTuneCents(), juce::dontSendNotification);
    pitchBendRangeDial_.setValue(processor_.getPitchBendRangeSemitones(), juce::dontSendNotification);
    const auto pitchLfo = processor_.getPitchLfoSettings();
    pitchLfoRateDial_.setValue(pitchLfo.rateHz, juce::dontSendNotification);
    pitchLfoDepthDial_.setValue(pitchLfo.depthCents, juce::dontSendNotification);
    modulationPanel_.syncFromProcessor();

    const auto playbackMode = processor_.getPlaybackMode();
    playbackModeGateButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::gate,
        juce::dontSendNotification);
    playbackModeOneShotButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::oneShot,
        juce::dontSendNotification);
    playbackModeLoopButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::loop,
        juce::dontSendNotification);

    monoToggle_.setToggleState(processor_.getMonoMode(), juce::dontSendNotification);
    legatoToggle_.setToggleState(processor_.getLegatoMode(), juce::dontSendNotification);
    legatoToggle_.setEnabled(processor_.getMonoMode());
    reverseToggle_.setToggleState(processor_.getReversePlayback(), juce::dontSendNotification);

    glideDial_.setValue(processor_.getGlideSeconds() * 1000.0f, juce::dontSendNotification);
    polyphonyDial_.setValue(static_cast<double>(processor_.getPolyphonyLimit()), juce::dontSendNotification);
    fadeInDial_.setValue(processor_.getFadeInSamples(), juce::dontSendNotification);
    fadeOutDial_.setValue(processor_.getFadeOutSamples(), juce::dontSendNotification);

    const auto amp = processor_.getAmpEnvelope();
    ampAttackDial_.setValue(amp.attackSeconds * 1000.0f, juce::dontSendNotification);
    ampDecayDial_.setValue(amp.decaySeconds * 1000.0f, juce::dontSendNotification);
    ampSustainDial_.setValue(amp.sustainLevel * 100.0f, juce::dontSendNotification);
    ampReleaseDial_.setValue(amp.releaseSeconds * 1000.0f, juce::dontSendNotification);
    const auto ampLfo = processor_.getAmpLfoSettings();
    ampLfoRateDial_.setValue(ampLfo.rateHz, juce::dontSendNotification);
    ampLfoDepthDial_.setValue(ampLfo.depth * 100.0f, juce::dontSendNotification);
    ampLfoShapeCombo_.setSelectedId(static_cast<int>(ampLfo.shape) + 1, juce::dontSendNotification);

    const auto filter = processor_.getFilterSettings();
    filterCutoffDial_.setValue(filter.baseCutoffHz, juce::dontSendNotification);
    filterResDial_.setValue(static_cast<double>(filter.resonance) * 100.0, juce::dontSendNotification);
    filterEnvAmtDial_.setValue(filter.envAmountHz, juce::dontSendNotification);
    filterKeytrackDial_.setValue(filter.keyTracking * 100.0f, juce::dontSendNotification);
    filterVelDial_.setValue(filter.velocityAmountHz, juce::dontSendNotification);
    filterLfoRateDial_.setValue(filter.lfoRateHz, juce::dontSendNotification);
    filterLfoRateKeyDial_.setValue(filter.lfoRateKeyTracking * 100.0f, juce::dontSendNotification);
    filterLfoAmtDial_.setValue(filter.lfoAmountHz, juce::dontSendNotification);
    filterLfoAmtKeyDial_.setValue(filter.lfoAmountKeyTracking * 100.0f, juce::dontSendNotification);
    filterLfoStartPhaseDial_.setValue(filter.lfoStartPhaseDegrees, juce::dontSendNotification);
    filterLfoStartRandDial_.setValue(filter.lfoStartPhaseRandomDegrees, juce::dontSendNotification);
    filterLfoFadeInDial_.setValue(filter.lfoFadeInMs, juce::dontSendNotification);
    filterLfoShapeCombo_.setSelectedId(lfoShapeToComboId(filter.lfoShape), juce::dontSendNotification);
    filterLfoRetriggerToggle_.setToggleState(filter.lfoRetrigger, juce::dontSendNotification);
    filterLfoTempoSyncToggle_.setToggleState(filter.lfoTempoSync, juce::dontSendNotification);
    filterLfoRateKeySyncToggle_.setToggleState(filter.lfoRateKeytrackInTempoSync, juce::dontSendNotification);
    filterLfoDivisionCombo_.setSelectedId(filter.lfoSyncDivision + 1, juce::dontSendNotification);
    filterLfoRateDial_.setEnabled(!filter.lfoTempoSync);
    filterLfoRateKeySyncToggle_.setEnabled(filter.lfoTempoSync);
    filterLfoDivisionLabel_.setEnabled(filter.lfoTempoSync);
    filterLfoDivisionCombo_.setEnabled(filter.lfoTempoSync);
    const auto filterTypeId = filterModeToComboId(filter.mode);
    const bool isEditingFilterType = filterTypeCombo_.hasKeyboardFocus(true) || filterTypeCombo_.isPopupActive();
    if (!isEditingFilterType && filterTypeCombo_.getSelectedId() != filterTypeId)
        filterTypeCombo_.setSelectedId(filterTypeId, juce::dontSendNotification);
    const auto filterEnv = processor_.getFilterEnvelope();
    filterAttackDial_.setValue(filterEnv.attackSeconds * 1000.0f, juce::dontSendNotification);
    filterDecayDial_.setValue(filterEnv.decaySeconds * 1000.0f, juce::dontSendNotification);
    filterSustainDial_.setValue(filterEnv.sustainLevel * 100.0f, juce::dontSendNotification);
    filterReleaseDial_.setValue(filterEnv.releaseSeconds * 1000.0f, juce::dontSendNotification);

    const auto velCurve = processor_.getVelocityCurve();
    const bool isEditingVelocityCurve = velocityCurveCombo_.hasKeyboardFocus(true) || velocityCurveCombo_.isPopupActive();
    if (!isEditingVelocityCurve)
    {
        velocityCurveCombo_.setSelectedId(
            velCurve == AudiocityAudioProcessor::VelocityCurve::soft ? 2
                : (velCurve == AudiocityAudioProcessor::VelocityCurve::hard ? 3 : 1),
            juce::dontSendNotification);
    }

    qualityCpuButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::cpu,
        juce::dontSendNotification);
    qualityFidelityButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::fidelity,
        juce::dontSendNotification);
    qualityUltraButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::ultra,
        juce::dontSendNotification);
    masterVolumeDial_.setValue(processor_.getMasterVolume() * 100.0f, juce::dontSendNotification);
    panDial_.setValue(processor_.getPan() * 100.0f, juce::dontSendNotification);
    reverbMixDial_.setValue(processor_.getReverbMix() * 100.0f, juce::dontSendNotification);
    const auto delay = processor_.getDelaySettings();
    delayTimeDial_.setValue(delay.timeMs, juce::dontSendNotification);
    delayFeedbackDial_.setValue(delay.feedback * 100.0f, juce::dontSendNotification);
    delayMixDial_.setValue(delay.mix * 100.0f, juce::dontSendNotification);
    delayTempoSyncToggle_.setToggleState(delay.tempoSync, juce::dontSendNotification);
    const auto dcFilter = processor_.getDcFilterSettings();
    dcFilterEnabledToggle_.setToggleState(dcFilter.enabled, juce::dontSendNotification);
    dcFilterCutoffDial_.setValue(dcFilter.cutoffHz, juce::dontSendNotification);
    const auto autopan = processor_.getAutopanSettings();
    autopanRateDial_.setValue(autopan.rateHz, juce::dontSendNotification);
    autopanDepthDial_.setValue(autopan.depth * 100.0f, juce::dontSendNotification);
    const auto saturation = processor_.getSaturationSettings();
    saturationDriveDial_.setValue(saturation.drive * 100.0f, juce::dontSendNotification);
    saturationModeCombo_.setSelectedId(static_cast<int>(saturation.mode) + 1, juce::dontSendNotification);

    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::updateTabVisibility()
{
    const bool showSampleTab = (currentTabIndex_ == 0);
    const bool showLibraryTab = (currentTabIndex_ == 1);
    const bool showMappingTab = (currentTabIndex_ == 2);
    const bool showPlayerTab = (currentTabIndex_ == 3);
    const bool showGenerateTab = (currentTabIndex_ == 4);
    const bool showCaptureTab = (currentTabIndex_ == 5);
    const bool showAboutTab = (currentTabIndex_ == 6);
    const bool showBrowserRail = shouldShowPersistentBrowserRail();
    const bool showPerformanceStrip = shouldShowPersistentPerformanceStrip();
    const bool useProgramMapInspector = showSampleTab && shouldShowSampleProgramMapInspector();
    const bool useFilterModInspector = showSampleTab && shouldShowSampleFilterModInspector();
    const bool useEffectsInspector = showSampleTab && shouldShowSampleEffectsInspector();
    const bool showBrowserSurface = showLibraryTab || showBrowserRail;
    const bool showBrowserCancelButton = showBrowserSurface && sampleScanInProgress_.load(std::memory_order_relaxed);
    const int browserRowHeight = showBrowserRail ? 54 : 66;
    const bool showFilterModSurface = showSampleTab && (useFilterModInspector || isSampleGroupExpanded("filterMod"));

    if (!showSampleTab)
        clearSampleInformationComponentBounds();

    if (sampleBrowserListBox_.getRowHeight() != browserRowHeight)
        sampleBrowserListBox_.setRowHeight(browserRowHeight);

    sampleBrowserRootLabel_.setVisible(showBrowserSurface);
    sampleBrowserChooseRootButton_.setVisible(showBrowserSurface);
    sampleBrowserRefreshButton_.setVisible(showBrowserSurface);
    sampleBrowserCancelButton_.setVisible(showBrowserCancelButton);
    sampleBrowserBookmarkCombo_.setVisible(showLibraryTab);
    sampleBrowserAddBookmarkButton_.setVisible(showLibraryTab);
    sampleBrowserRemoveBookmarkButton_.setVisible(showLibraryTab);
    sampleBrowserFilterEditor_.setVisible(showLibraryTab || showBrowserRail);
    sampleBrowserSortCombo_.setVisible(showLibraryTab || showBrowserRail);
    sampleBrowserFavoriteButton_.setVisible(showLibraryTab || showBrowserRail);
    sampleBrowserFavoritesOnlyToggle_.setVisible(showLibraryTab || showBrowserRail);
    sampleBrowserRecentOnlyToggle_.setVisible(showLibraryTab || showBrowserRail);
    sampleBrowserTagFilterCombo_.setVisible(showLibraryTab);
    sampleBrowserTagsEditor_.setVisible(showLibraryTab);
    sampleBrowserApplyTagsButton_.setVisible(showLibraryTab);
    sampleBrowserListBox_.setVisible(showLibraryTab || showBrowserRail);
    sampleBrowserCountLabel_.setVisible(showLibraryTab || showBrowserRail);
    sampleBrowserPreviewLabel_.setVisible(showLibraryTab || showBrowserRail);

    mappingSummaryLabel_.setVisible(showMappingTab);
    mappingRefreshButton_.setVisible(showMappingTab);
    mappingCreateZoneButton_.setVisible(showMappingTab);
    mappingDuplicateZoneButton_.setVisible(showMappingTab);
    mappingSplitZoneButton_.setVisible(showMappingTab);
    mappingDeleteZoneButton_.setVisible(showMappingTab);
    mappingOverview_.setVisible(showMappingTab);
    mappingZoneListBox_.setVisible(showMappingTab);
    mappingDetailsText_.setVisible(showMappingTab);
    mappingEditKeyLowLabel_.setVisible(showMappingTab);
    mappingEditKeyLowSlider_.setVisible(showMappingTab);
    mappingEditKeyHighLabel_.setVisible(showMappingTab);
    mappingEditKeyHighSlider_.setVisible(showMappingTab);
    mappingEditVelocityLowLabel_.setVisible(showMappingTab);
    mappingEditVelocityLowSlider_.setVisible(showMappingTab);
    mappingEditVelocityHighLabel_.setVisible(showMappingTab);
    mappingEditVelocityHighSlider_.setVisible(showMappingTab);
    mappingEditVelocityFadeInLabel_.setVisible(showMappingTab);
    mappingEditVelocityFadeInLowSlider_.setVisible(showMappingTab);
    mappingEditVelocityFadeInHighSlider_.setVisible(showMappingTab);
    mappingEditVelocityFadeOutLabel_.setVisible(showMappingTab);
    mappingEditVelocityFadeOutLowSlider_.setVisible(showMappingTab);
    mappingEditVelocityFadeOutHighSlider_.setVisible(showMappingTab);
    mappingEditRootLabel_.setVisible(showMappingTab);
    mappingEditRootSlider_.setVisible(showMappingTab);
    mappingEditSampleStartLabel_.setVisible(showMappingTab);
    mappingEditSampleStartSlider_.setVisible(showMappingTab);
    mappingEditSampleEndLabel_.setVisible(showMappingTab);
    mappingEditSampleEndSlider_.setVisible(showMappingTab);
    mappingEditLoopStartLabel_.setVisible(showMappingTab);
    mappingEditLoopStartSlider_.setVisible(showMappingTab);
    mappingEditLoopEndLabel_.setVisible(showMappingTab);
    mappingEditLoopEndSlider_.setVisible(showMappingTab);
    mappingEditGainLabel_.setVisible(showMappingTab);
    mappingEditGainSlider_.setVisible(showMappingTab);
    mappingEditPanLabel_.setVisible(showMappingTab);
    mappingEditPanSlider_.setVisible(showMappingTab);
    mappingEditRoundRobinGroupLabel_.setVisible(showMappingTab);
    mappingEditRoundRobinGroupSlider_.setVisible(showMappingTab);
    mappingEditRoundRobinPositionLabel_.setVisible(showMappingTab);
    mappingEditRoundRobinPositionSlider_.setVisible(showMappingTab);
    mappingEditRoundRobinModeLabel_.setVisible(showMappingTab);
    mappingEditRoundRobinModeCombo_.setVisible(showMappingTab);
    mappingEditChokeLabel_.setVisible(showMappingTab);
    mappingEditChokeSlider_.setVisible(showMappingTab);
    mappingEditTriggerLabel_.setVisible(showMappingTab);
    mappingEditTriggerCombo_.setVisible(showMappingTab);
    mappingEditLoopLabel_.setVisible(showMappingTab);
    mappingEditLoopCombo_.setVisible(showMappingTab);
    mappingEditApplyButton_.setVisible(showMappingTab);
    mappingEditStatusLabel_.setVisible(showMappingTab);

    samplePathLabel_.setVisible(false);
    presetFilterEditor_.setVisible(showSampleTab);
    presetCountLabel_.setVisible(showSampleTab);
    presetCombo_.setVisible(showSampleTab);
    presetSaveButton_.setVisible(showSampleTab);
    presetRenameButton_.setVisible(showSampleTab);
    presetDeleteButton_.setVisible(showSampleTab);
    loadButton_.setVisible(showSampleTab);
    sampleBrowserRailToggleButton_.setVisible(showSampleTab);
    sampleInspectorRailToggleButton_.setVisible(showSampleTab);
    if (showSampleTab)
    {
        sampleBrowserRailToggleButton_.setToggleState(showBrowserRail, juce::dontSendNotification);
        sampleInspectorRailToggleButton_.setToggleState(shouldShowSampleInspectorRail(), juce::dontSendNotification);
    }
    diagnosticsToggleButton_.setVisible(showSampleTab);
    waveformResetRangesButton_.setVisible(showSampleTab);
    waveformInteractionSummaryLabel_.setVisible(showSampleTab);
    sampleControlsViewport_.setVisible(showSampleTab);
    rootNoteLabel_.setVisible(showSampleTab);
    rootNoteCombo_.setVisible(showSampleTab);
    tuneCoarseDial_.setVisible(showSampleTab);
    tuneFineDial_.setVisible(showSampleTab);
    pitchBendRangeDial_.setVisible(showSampleTab);
    pitchLfoRateDial_.setVisible(showSampleTab);
    pitchLfoDepthDial_.setVisible(showSampleTab);
    waveformView_.setVisible(showSampleTab);
    playbackModeLabel_.setVisible(showSampleTab);
    playbackModeGateButton_.setVisible(showSampleTab);
    playbackModeOneShotButton_.setVisible(showSampleTab);
    playbackModeLoopButton_.setVisible(showSampleTab);
    reverseToggle_.setVisible(showSampleTab);
    playbackStartDial_.setVisible(showSampleTab);
    playbackEndDial_.setVisible(showSampleTab);
    loopStartDial_.setVisible(showSampleTab);
    loopEndDial_.setVisible(showSampleTab);
    loopCrossfadeDial_.setVisible(showSampleTab);
    monoToggle_.setVisible(showSampleTab);
    legatoToggle_.setVisible(showSampleTab);
    velocityCurveLabel_.setVisible(showSampleTab);
    velocityCurveCombo_.setVisible(showSampleTab);
    glideDial_.setVisible(showSampleTab);
    polyphonyDial_.setVisible(showSampleTab);
    modulationPanel_.setVisible(showSampleTab);
    ampAttackDial_.setVisible(showSampleTab);
    ampDecayDial_.setVisible(showSampleTab);
    ampSustainDial_.setVisible(showSampleTab);
    ampReleaseDial_.setVisible(showSampleTab);
    ampLfoRateDial_.setVisible(showSampleTab);
    ampLfoDepthDial_.setVisible(showSampleTab);
    ampLfoShapeLabel_.setVisible(showSampleTab);
    ampLfoShapeCombo_.setVisible(showSampleTab);
    filterCutoffDial_.setVisible(showSampleTab);
    filterResDial_.setVisible(showSampleTab);
    filterEnvAmtDial_.setVisible(showSampleTab);
    filterTypeLabel_.setVisible(showSampleTab);
    filterTypeCombo_.setVisible(showSampleTab);
    filterResponseGraph_.setVisible(showSampleTab);
    filterAttackDial_.setVisible(showFilterModSurface);
    filterDecayDial_.setVisible(showFilterModSurface);
    filterSustainDial_.setVisible(showFilterModSurface);
    filterReleaseDial_.setVisible(showFilterModSurface);
    filterEnvelopeGraph_.setVisible(showFilterModSurface);
    filterKeytrackDial_.setVisible(showFilterModSurface);
    filterVelDial_.setVisible(showFilterModSurface);
    filterLfoRateDial_.setVisible(showFilterModSurface);
    filterLfoRateKeyDial_.setVisible(showSampleTab && isSampleGroupExpanded("filterMod"));
    filterLfoAmtDial_.setVisible(showFilterModSurface);
    filterLfoAmtKeyDial_.setVisible(showSampleTab && isSampleGroupExpanded("filterMod"));
    filterLfoStartPhaseDial_.setVisible(showSampleTab && isSampleGroupExpanded("filterMod"));
    filterLfoStartRandDial_.setVisible(showSampleTab && isSampleGroupExpanded("filterMod"));
    filterLfoFadeInDial_.setVisible(showSampleTab && isSampleGroupExpanded("filterMod"));
    filterLfoShapeLabel_.setVisible(showFilterModSurface);
    filterLfoShapeCombo_.setVisible(showFilterModSurface);
    filterLfoRetriggerToggle_.setVisible(showFilterModSurface);
    filterLfoTempoSyncToggle_.setVisible(showFilterModSurface);
    filterLfoRateKeySyncToggle_.setVisible(showSampleTab && isSampleGroupExpanded("filterMod"));
    filterLfoKeytrackLinearToggle_.setVisible(showSampleTab && isSampleGroupExpanded("filterMod"));
    filterLfoUnipolarToggle_.setVisible(showSampleTab && isSampleGroupExpanded("filterMod"));
    filterLfoDivisionLabel_.setVisible(showFilterModSurface);
    filterLfoDivisionCombo_.setVisible(showFilterModSurface);
    filterKeytrackSnapLabel_.setVisible(showSampleTab && isSampleGroupExpanded("filterMod"));
    filterKeytrackSnapCombo_.setVisible(showSampleTab && isSampleGroupExpanded("filterMod"));
    fadeInDial_.setVisible(showSampleTab);
    fadeOutDial_.setVisible(showSampleTab);
    qualityLabel_.setVisible(showSampleTab);
    qualityCpuButton_.setVisible(showSampleTab);
    qualityFidelityButton_.setVisible(showSampleTab);
    qualityUltraButton_.setVisible(showSampleTab);
    preloadDial_.setVisible(showSampleTab);
    masterVolumeDial_.setVisible(showSampleTab);
    panDial_.setVisible(showSampleTab);
    outputLevelMeter_.setVisible(showSampleTab);
    reverbMixDial_.setVisible(showSampleTab && (useEffectsInspector || isSampleGroupExpanded("effects")));
    delayTimeDial_.setVisible(showSampleTab && (useEffectsInspector || isSampleGroupExpanded("effects")));
    delayFeedbackDial_.setVisible(showSampleTab && (useEffectsInspector || isSampleGroupExpanded("effects")));
    delayMixDial_.setVisible(showSampleTab && (useEffectsInspector || isSampleGroupExpanded("effects")));
    delayTempoSyncToggle_.setVisible(showSampleTab && (useEffectsInspector || isSampleGroupExpanded("effects")));
    dcFilterEnabledToggle_.setVisible(showSampleTab && (useEffectsInspector || isSampleGroupExpanded("effects")));
    dcFilterCutoffDial_.setVisible(showSampleTab && (useEffectsInspector || isSampleGroupExpanded("effects")));
    autopanRateDial_.setVisible(showSampleTab && (useEffectsInspector || isSampleGroupExpanded("effects")));
    autopanDepthDial_.setVisible(showSampleTab && (useEffectsInspector || isSampleGroupExpanded("effects")));
    saturationDriveDial_.setVisible(showSampleTab && (useEffectsInspector || isSampleGroupExpanded("effects")));
    saturationModeCombo_.setVisible(showSampleTab && (useEffectsInspector || isSampleGroupExpanded("effects")));
    programMapText_.setVisible(showSampleTab && processor_.hasImportedProgram()
        && (useProgramMapInspector || isSampleGroupExpanded("programMap")));
    diagnosticsLabel_.setVisible(showSampleTab && showDiagnosticsPanel_);

    playerKeyboardLabel_.setVisible(showPlayerTab || showPerformanceStrip);
    playerStatusLabel_.setVisible(showPerformanceStrip);
    playerOpenButton_.setVisible(showPerformanceStrip);
    playerKeyboardViewport_.setVisible(showPlayerTab || showPerformanceStrip);
    playerPadsLabel_.setVisible(showPlayerTab || showPerformanceStrip);
    for (int i = 0; i < kPlayerPadCount; ++i)
    {
        playerPadButtons_[static_cast<std::size_t>(i)].setVisible(showPlayerTab || showPerformanceStrip);
        playerPadAssignButtons_[static_cast<std::size_t>(i)].setVisible(showPlayerTab);
    }

    refreshPlayerPadButtons();

    generateWaveformView_.setVisible(showGenerateTab);
    generateSineButton_.setVisible(showGenerateTab);
    generateRampButton_.setVisible(showGenerateTab);
    generateSquareButton_.setVisible(showGenerateTab);
    generateSawtoothButton_.setVisible(showGenerateTab);
    generateTriangleButton_.setVisible(showGenerateTab);
    generatePulseButton_.setVisible(showGenerateTab);
    generateRandomButton_.setVisible(showGenerateTab);
    generateSamplesLabel_.setVisible(showGenerateTab);
    generateSamplesCombo_.setVisible(showGenerateTab);
    generateBitDepthLabel_.setVisible(showGenerateTab);
    generateBitDepthCombo_.setVisible(showGenerateTab);
    generateSketchSmoothingLabel_.setVisible(showGenerateTab);
    generateSketchSmoothingCombo_.setVisible(showGenerateTab);
    generatePulseWidthLabel_.setVisible(showGenerateTab);
    generatePulseWidthSlider_.setVisible(showGenerateTab);
    generatePreviewButton_.setVisible(showGenerateTab);
    generateFrequencyLabel_.setVisible(showGenerateTab);
    generateFrequencyCombo_.setVisible(showGenerateTab);
    generateLoadAsSampleButton_.setVisible(showGenerateTab);

    captureWaveformView_.setVisible(showCaptureTab);
    captureRecordButton_.setVisible(showCaptureTab);
    captureClearButton_.setVisible(showCaptureTab);
    captureCutButton_.setVisible(showCaptureTab);
    captureTrimButton_.setVisible(showCaptureTab);
    captureLoadAsSampleButton_.setVisible(showCaptureTab);
    capturePlayButton_.setVisible(showCaptureTab);
    captureNormalizeButton_.setVisible(showCaptureTab);
    captureSourceLabel_.setVisible(showCaptureTab);
    captureSampleRateLabel_.setVisible(showCaptureTab);
    captureSampleRateCombo_.setVisible(showCaptureTab);
    captureChannelLabel_.setVisible(showCaptureTab);
    captureChannelCombo_.setVisible(showCaptureTab);
    captureBitDepthLabel_.setVisible(showCaptureTab);
    captureBitDepthCombo_.setVisible(showCaptureTab);
    captureRootNoteLabel_.setVisible(showCaptureTab);
    captureRootNoteCombo_.setVisible(showCaptureTab);
    captureInputLevelLabel_.setVisible(showCaptureTab);
    captureInputLevelSlider_.setVisible(showCaptureTab);
    captureInputVuMeter_.setVisible(showCaptureTab);
    captureStatusLabel_.setVisible(showCaptureTab);

    aboutGitHubButton_.setVisible(showAboutTab);
    aboutCoffeeButton_.setVisible(showAboutTab);
}

int AudiocityAudioProcessorEditor::getNumRows()
{
    return static_cast<int>(visibleSampleEntryIndices_.size());
}

void AudiocityAudioProcessorEditor::paintListBoxItem(
    const int rowNumber, juce::Graphics& g, const int width, const int height, const bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(visibleSampleEntryIndices_.size()))
        return;

    const auto sourceIndex = visibleSampleEntryIndices_[static_cast<std::size_t>(rowNumber)];
    const auto& entry = allSampleEntries_[static_cast<std::size_t>(sourceIndex)];
    const bool compactBrowserRow = shouldShowPersistentBrowserRail();
    const bool previewSupported = !entry.file.getFileExtension().equalsIgnoreCase(".sfz");
    const bool rowPreviewing = previewSupported
        && processor_.isSamplePreviewPlaying()
        && sourceIndex == lastPreviewedBrowserSourceIndex_;
    const auto rowBounds = juce::Rectangle<int>(0, 0, width, height).reduced(2, 1);

    if (rowIsSelected)
    {
        g.setColour(juce::Colour(0xff2d3f66));
        g.fillRoundedRectangle(rowBounds.toFloat(), 4.0f);
    }
    else
    {
        g.setColour((rowNumber % 2 == 0) ? juce::Colour(0x22252538) : juce::Colour(0x16252538));
        g.fillRoundedRectangle(rowBounds.toFloat(), 4.0f);
    }

    auto content = rowBounds.reduced(compactBrowserRow ? 5 : 6, 4);
    auto thumbBounds = content.removeFromLeft(compactBrowserRow ? 82 : 120).withTrimmedBottom(1);
    content.removeFromLeft(compactBrowserRow ? 6 : 8);

    g.setColour(juce::Colour(0xff1f2f4d));
    g.fillRoundedRectangle(thumbBounds.toFloat(), 4.0f);
    g.setColour(juce::Colour(0xff3a5a8a));
    g.drawRoundedRectangle(thumbBounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

    if (!entry.previewPeaks.empty())
    {
        const auto centerY = static_cast<float>(thumbBounds.getCentreY());
        const auto peakCount = static_cast<int>(entry.previewPeaks.size());
        const auto pixelCount = juce::jmax(1, thumbBounds.getWidth() - 2);

        std::vector<float> topYs(static_cast<std::size_t>(pixelCount + 1), centerY);
        std::vector<float> bottomYs(static_cast<std::size_t>(pixelCount + 1), centerY);
        juce::Path topPath;
        juce::Path bottomPath;

        for (int px = 0; px <= pixelCount; ++px)
        {
            const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
            const auto x = static_cast<float>(thumbBounds.getX()) + t * static_cast<float>(thumbBounds.getWidth() - 1);

            const auto peakPos = t * static_cast<float>(juce::jmax(0, peakCount - 1));
            const auto i0 = juce::jlimit(0, peakCount - 1, static_cast<int>(std::floor(peakPos)));
            const auto i1 = juce::jlimit(0, peakCount - 1, i0 + 1);
            const auto frac = peakPos - static_cast<float>(i0);

            const auto a0 = entry.previewPeaks[static_cast<std::size_t>(i0)];
            const auto a1 = entry.previewPeaks[static_cast<std::size_t>(i1)];
            const auto ampNorm = juce::jlimit(0.0f, 1.0f, a0 + (a1 - a0) * frac);
            const auto amp = ampNorm * (thumbBounds.getHeight() * 0.44f);

            const auto topY = centerY - amp;
            const auto bottomY = centerY + amp;
            topYs[static_cast<std::size_t>(px)] = topY;
            bottomYs[static_cast<std::size_t>(px)] = bottomY;

            if (px == 0)
            {
                topPath.startNewSubPath(x, topY);
                bottomPath.startNewSubPath(x, bottomY);
            }
            else
            {
                topPath.lineTo(x, topY);
                bottomPath.lineTo(x, bottomY);
            }
        }

        juce::Path fillPath;
        fillPath.startNewSubPath(static_cast<float>(thumbBounds.getX()), topYs.front());
        for (int px = 1; px <= pixelCount; ++px)
        {
            const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
            const auto x = static_cast<float>(thumbBounds.getX()) + t * static_cast<float>(thumbBounds.getWidth() - 1);
            fillPath.lineTo(x, topYs[static_cast<std::size_t>(px)]);
        }
        for (int px = pixelCount; px >= 0; --px)
        {
            const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
            const auto x = static_cast<float>(thumbBounds.getX()) + t * static_cast<float>(thumbBounds.getWidth() - 1);
            fillPath.lineTo(x, bottomYs[static_cast<std::size_t>(px)]);
        }
        fillPath.closeSubPath();

        juce::ColourGradient fillGradient(
            juce::Colour(0xff61d9ff).withAlpha(0.35f), static_cast<float>(thumbBounds.getCentreX()), static_cast<float>(thumbBounds.getY()),
            juce::Colour(0xff61d9ff).withAlpha(0.10f), static_cast<float>(thumbBounds.getCentreX()), static_cast<float>(thumbBounds.getBottom()),
            false);
        g.setGradientFill(fillGradient);
        g.fillPath(fillPath);

        g.setColour(juce::Colour(0xff61d9ff).withAlpha(0.9f));
        g.strokePath(topPath, juce::PathStrokeType(1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xff61d9ff).withAlpha(0.65f));
        g.strokePath(bottomPath, juce::PathStrokeType(1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto firstLine = content.removeFromTop(17);
    auto pathArea = firstLine;
    auto drawRowBadge = [&g, &pathArea](const juce::String& text, const juce::Colour colour, const int badgeWidth)
    {
        if (pathArea.getWidth() < badgeWidth + 80)
            return;

        auto badgeArea = pathArea.removeFromRight(badgeWidth);
        g.setColour(colour);
        g.fillRoundedRectangle(badgeArea.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xffdfe6ff));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(text, badgeArea, juce::Justification::centred, false);
        pathArea.removeFromRight(6);
    };

    if (entry.isFavorite)
        drawRowBadge("FAV", juce::Colour(0xff9a6a22), 38);
    if (entry.isRecent)
        drawRowBadge("REC", juce::Colour(0xff23636e), 38);
    if (entry.loopFormatBadge.isNotEmpty())
    {
        const auto badgeWidth = entry.loopFormatBadge == "Apple Loop" ? 84 : 66;
        const auto badgeColour = entry.loopFormatBadge == "Apple Loop"
            ? juce::Colour(0xff5b4b8a)
            : (entry.loopFormatBadge == "SFZ" ? juce::Colour(0xff6c5ce7) : juce::Colour(0xff4b6b2a));
        drawRowBadge(entry.loopFormatBadge, badgeColour, badgeWidth);
    }

    const auto pathWidth = compactBrowserRow
        ? juce::jmax(92, pathArea.getWidth() / 3)
        : juce::jmax(140, pathArea.getWidth() / 2);
    auto fileArea = pathArea.removeFromLeft(juce::jmax(40, pathArea.getWidth() - pathWidth - 6));

    g.setColour(juce::Colour(0xffe5e5ef));
    g.setFont(juce::Font(juce::FontOptions(compactBrowserRow ? 12.0f : 13.0f)));
    g.drawText(entry.fileName, fileArea, juce::Justification::centredLeft, true);

    g.setColour(juce::Colour(0xffa5a5b8));
    g.setFont(juce::Font(juce::FontOptions(compactBrowserRow ? 10.0f : 10.5f)));
    g.drawText(entry.relativePath, pathArea, juce::Justification::centredRight, true);

    auto detailsLine = entry.metadataLine;
    if (entry.loopMetadataLine.isNotEmpty())
        detailsLine += "  |  " + entry.loopMetadataLine;
    if (!entry.tags.isEmpty())
        detailsLine += "  |  Tags: " + entry.tags.joinIntoString(", ");

    g.setColour(juce::Colour(0xffc7c7d8));
    g.setFont(juce::Font(juce::FontOptions(compactBrowserRow ? 10.0f : 11.0f)));
    g.drawText(detailsLine, content.removeFromTop(15), juce::Justification::centredLeft, true);

    if (compactBrowserRow)
    {
        auto actionLine = content.removeFromTop(13);
        g.setColour(rowPreviewing ? juce::Colour(0xff61d9ff) : juce::Colour(0xff9ea6c5));
        g.setFont(juce::Font(juce::FontOptions(9.5f)));
        const auto actionText = rowPreviewing
            ? juce::String("Previewing  |  Dbl-click load")
            : (previewSupported ? juce::String("Click preview  |  Dbl-click load")
                                : juce::String("Dbl-click load"));
        g.drawText(actionText, actionLine, juce::Justification::centredLeft, true);
    }

}

void AudiocityAudioProcessorEditor::paintMappingListRow(
    const int rowNumber, juce::Graphics& g, const int width, const int height, const bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(mappingZoneRows_.size()))
        return;

    const auto& row = mappingZoneRows_[static_cast<std::size_t>(rowNumber)];
    const auto rowBounds = juce::Rectangle<int>(0, 0, width, height).reduced(2, 1);

    g.setColour(rowIsSelected ? juce::Colour(0xff2d3f66)
                              : ((rowNumber % 2 == 0) ? juce::Colour(0x22252538) : juce::Colour(0x16252538)));
    g.fillRoundedRectangle(rowBounds.toFloat(), 4.0f);

    auto content = rowBounds.reduced(8, 5);
    auto firstLine = content.removeFromTop(18);
    auto badgeArea = firstLine.removeFromLeft(44);
    g.setColour(juce::Colour(0xff3a5a8a));
    g.fillRoundedRectangle(badgeArea.toFloat(), 4.0f);
    g.setColour(juce::Colour(0xffdfe6ff));
    g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
    g.drawText("Z" + juce::String(row.zoneIndex + 1), badgeArea, juce::Justification::centred, false);

    firstLine.removeFromLeft(8);
    g.setColour(juce::Colour(0xffe5e5ef));
    g.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    g.drawText(row.sampleName, firstLine, juce::Justification::centredLeft, true);

    auto secondLine = content.removeFromTop(17);
    g.setColour(juce::Colour(0xffc7c7d8));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText("Key " + row.keyRange + "  Vel " + row.velocityRange + "  Root " + row.rootNote,
               secondLine,
               juce::Justification::centredLeft,
               true);

    auto thirdLine = content.removeFromTop(15);
    g.setColour(juce::Colour(0xff9ea6c5));
    g.setFont(juce::Font(juce::FontOptions(10.5f)));
    g.drawText("Trig " + row.triggerMode + "  Loop " + row.loopMode + "  RR " + row.roundRobin + " " + row.roundRobinMode + "  Choke " + row.chokeGroup,
               thirdLine,
               juce::Justification::centredLeft,
               true);
}

int AudiocityAudioProcessorEditor::MappingZoneListModel::getNumRows()
{
    return static_cast<int>(owner_.mappingZoneRows_.size());
}

void AudiocityAudioProcessorEditor::MappingZoneListModel::paintListBoxItem(
    const int rowNumber, juce::Graphics& g, const int width, const int height, const bool rowIsSelected)
{
    owner_.paintMappingListRow(rowNumber, g, width, height, rowIsSelected);
}

void AudiocityAudioProcessorEditor::MappingZoneListModel::listBoxItemClicked(const int row, const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() && !owner_.mappingZoneListBox_.isRowSelected(row))
        owner_.mappingZoneListBox_.selectRow(row, false, true);

    owner_.updateMappingDetails();

    if (event.mods.isPopupMenu())
        owner_.showMappingZoneContextMenu(row, event.getScreenPosition());
}

void AudiocityAudioProcessorEditor::MappingZoneListModel::listBoxItemDoubleClicked(const int row, const juce::MouseEvent&)
{
    owner_.mappingZoneListBox_.selectRow(row, juce::dontSendNotification);
    owner_.updateMappingDetails();
}

void AudiocityAudioProcessorEditor::MappingZoneListModel::selectedRowsChanged(const int)
{
    owner_.updateMappingDetails();
}

void AudiocityAudioProcessorEditor::MappingZoneListModel::returnKeyPressed(const int lastRowSelected)
{
    owner_.mappingZoneListBox_.selectRow(lastRowSelected, juce::dontSendNotification);
    owner_.updateMappingDetails();
}

void AudiocityAudioProcessorEditor::refreshMappingZoneRows()
{
    syncImportedProgramMappingUndoContext();
    mappingZoneRows_ = processor_.getImportedProgramZoneRows();
    mappingOverview_.setRows(mappingZoneRows_);

    const auto programName = processor_.getImportedProgramName();
    const auto zoneCount = static_cast<int>(mappingZoneRows_.size());
    mappingSummaryLabel_.setText(
        programName.isNotEmpty()
            ? programName + " - " + juce::String(zoneCount) + " mapped zone" + (zoneCount == 1 ? "" : "s")
            : juce::String("No imported program"),
        juce::dontSendNotification);

    mappingZoneListBox_.updateContent();
    if (zoneCount > 0 && mappingZoneListBox_.getSelectedRow() < 0)
        mappingZoneListBox_.selectRow(0, juce::dontSendNotification);
    else if (zoneCount == 0)
        mappingZoneListBox_.deselectAllRows();

    updateMappingDetails();
    mappingZoneListBox_.repaint();
}

void AudiocityAudioProcessorEditor::syncImportedProgramMappingUndoContext()
{
    const auto programPath = processor_.getImportedProgramPath();
    if (programPath.isEmpty())
    {
        if (mappingUndoProgramPath_.isNotEmpty())
        {
            editorUndoHistory_.clear();
            lastSettingsSnapshot_ = captureSettingsSnapshot();
            mappingUndoProgramPath_.clear();
        }
        return;
    }

    if (!programPath.equalsIgnoreCase(mappingUndoProgramPath_))
    {
        editorUndoHistory_.clear();
        lastSettingsSnapshot_ = captureSettingsSnapshot();
        mappingUndoProgramPath_ = programPath;
    }
}

std::vector<int> AudiocityAudioProcessorEditor::getSelectedMappingRowIndices() const
{
    std::vector<int> selectedRows;
    const auto rows = mappingZoneListBox_.getSelectedRows();
    selectedRows.reserve(static_cast<std::size_t>(rows.size()));
    for (int index = 0; index < rows.size(); ++index)
        selectedRows.push_back(rows[index]);

    return selectedRows;
}

std::vector<int> AudiocityAudioProcessorEditor::getSelectedMappingZoneIndices() const
{
    std::vector<int> zoneIndices;
    const auto selectedRows = getSelectedMappingRowIndices();
    zoneIndices.reserve(selectedRows.size());

    for (const auto row : selectedRows)
    {
        if (row < 0 || row >= static_cast<int>(mappingZoneRows_.size()))
            continue;

        zoneIndices.push_back(mappingZoneRows_[static_cast<std::size_t>(row)].zoneIndex);
    }

    return zoneIndices;
}

audiocity::plugin::ProgramMappingStateSnapshot AudiocityAudioProcessorEditor::captureImportedProgramMappingState() const
{
    audiocity::plugin::ProgramMappingStateSnapshot snapshot;
    snapshot.hasImportedProgram = processor_.hasImportedProgram();
    snapshot.programPath = processor_.getImportedProgramPath();
    if (snapshot.hasImportedProgram)
        snapshot.mappingState = processor_.createImportedProgramMappingState();

    return snapshot;
}

void AudiocityAudioProcessorEditor::recordImportedProgramMappingChange(
    const audiocity::plugin::ProgramMappingStateSnapshot& beforeState,
                                                                      const juce::String& label)
{
    const auto afterState = captureImportedProgramMappingState();
    editorUndoHistory_.recordMappingChange(beforeState, afterState, label.toStdString());
    mappingUndoProgramPath_ = afterState.programPath;
}

bool AudiocityAudioProcessorEditor::applyImportedProgramMappingHistoryState(
    const audiocity::plugin::ProgramMappingStateSnapshot& mappingState,
                                                                            const juce::String& statusText)
{
    if (!mappingState.hasImportedProgram)
        return false;

    const auto currentProgramPath = processor_.getImportedProgramPath();
    if (!currentProgramPath.equalsIgnoreCase(mappingState.programPath))
        return false;

    const auto selectedZoneIndices = getSelectedMappingZoneIndices();
    const auto preferredRow = mappingZoneListBox_.getSelectedRow();
    if (!processor_.applyImportedProgramMappingState(mappingState.mappingState))
        return false;

    mappingEditStatusLabel_.setText(statusText, juce::dontSendNotification);
    refreshMappingZoneRows();

    if (!selectedZoneIndices.empty())
        selectMappingZoneIndices(selectedZoneIndices);

    if (mappingZoneListBox_.getSelectedRow() < 0)
        selectClosestMappingRow(preferredRow >= 0 ? preferredRow : 0);

    updateMappingDetails();
    updateDiagnosticsStatusText();
    return true;
}

void AudiocityAudioProcessorEditor::resetMappingBatchEditTracking(const std::vector<int>& selectedRows)
{
    mappingBatchTrackedSelectionRows_ = selectedRows;
    mappingBatchVelocityFadeEdited_ = false;
    mappingBatchGainEdited_ = false;
    mappingBatchPanEdited_ = false;
    mappingBatchRoundRobinGroupEdited_ = false;
    mappingBatchRoundRobinModeEdited_ = false;
    mappingBatchChokeEdited_ = false;
    mappingBatchTriggerEdited_ = false;
    mappingBatchLoopEdited_ = false;
}

bool AudiocityAudioProcessorEditor::hasPendingMappingBatchEdit() const noexcept
{
    return mappingBatchVelocityFadeEdited_
        || mappingBatchGainEdited_
        || mappingBatchPanEdited_
        || mappingBatchRoundRobinGroupEdited_
        || mappingBatchRoundRobinModeEdited_
        || mappingBatchChokeEdited_
        || mappingBatchTriggerEdited_
        || mappingBatchLoopEdited_;
}

void AudiocityAudioProcessorEditor::selectMappingZoneIndices(const std::vector<int>& zoneIndices)
{
    mappingZoneListBox_.deselectAllRows();

    int lastSelectedRow = -1;
    for (const auto zoneIndex : zoneIndices)
    {
        for (int row = 0; row < static_cast<int>(mappingZoneRows_.size()); ++row)
        {
            if (mappingZoneRows_[static_cast<std::size_t>(row)].zoneIndex != zoneIndex)
                continue;

            mappingZoneListBox_.selectRow(row, false, false);
            lastSelectedRow = row;
            break;
        }
    }

    if (lastSelectedRow >= 0)
        mappingZoneListBox_.scrollToEnsureRowIsOnscreen(lastSelectedRow);
}

void AudiocityAudioProcessorEditor::selectAllMappingZones()
{
    mappingZoneListBox_.deselectAllRows();

    const auto rowCount = static_cast<int>(mappingZoneRows_.size());
    for (int row = 0; row < rowCount; ++row)
        mappingZoneListBox_.selectRow(row, false, false);

    if (rowCount > 0)
        mappingZoneListBox_.scrollToEnsureRowIsOnscreen(rowCount - 1);

    updateMappingDetails();
}

bool AudiocityAudioProcessorEditor::selectMappingZoneByIndex(const int zoneIndex)
{
    for (int row = 0; row < static_cast<int>(mappingZoneRows_.size()); ++row)
    {
        if (mappingZoneRows_[static_cast<std::size_t>(row)].zoneIndex != zoneIndex)
            continue;

        mappingZoneListBox_.selectRow(row, juce::dontSendNotification);
        mappingZoneListBox_.scrollToEnsureRowIsOnscreen(row);
        return true;
    }

    return false;
}

void AudiocityAudioProcessorEditor::selectClosestMappingRow(const int preferredRow)
{
    const auto rowCount = static_cast<int>(mappingZoneRows_.size());
    if (rowCount <= 0)
    {
        mappingZoneListBox_.deselectAllRows();
        return;
    }

    const auto rowToSelect = juce::jlimit(0, rowCount - 1, preferredRow);
    mappingZoneListBox_.selectRow(rowToSelect, juce::dontSendNotification);
    mappingZoneListBox_.scrollToEnsureRowIsOnscreen(rowToSelect);
}

void AudiocityAudioProcessorEditor::showMappingZoneContextMenu(const int row, const juce::Point<int> screenPosition)
{
    if (row < 0 || row >= static_cast<int>(mappingZoneRows_.size()))
        return;

    if (!mappingZoneListBox_.isRowSelected(row))
        mappingZoneListBox_.selectRow(row, false, true);

    updateMappingDetails();

    const auto selectedRows = getSelectedMappingRowIndices();
    if (selectedRows.empty())
        return;

    const auto singleSelection = selectedRows.size() == 1;
    const auto& selectedRow = mappingZoneRows_[static_cast<std::size_t>(selectedRows.front())];
    juce::PopupMenu menu;
    menu.addItem(1, "New Zone\tCtrl+N", processor_.hasImportedProgram());
    menu.addItem(2, "Duplicate Zone\tCtrl+D", singleSelection);
    menu.addItem(3, "Split Zone\tCtrl+Shift+D", singleSelection && selectedRow.keyHigh > selectedRow.keyLow);
    menu.addItem(4,
                 singleSelection ? "Delete Zone\tDelete" : "Delete Selected Zones\tDelete",
                 true);
    menu.addSeparator();
    menu.addItem(5,
                 singleSelection ? "Clear Velocity Fades" : "Clear Velocity Fades for Selected Zones",
                 true);
    menu.addItem(6,
                 singleSelection ? "Map Zone Chromatically from C1" : "Map Selected Zones Chromatically from C1",
                 true);
    menu.addItem(7,
                 singleSelection ? "Map Zone To Root Note" : "Map Selected Zones To Root Notes",
                 true);
    menu.addItem(8,
                 "Spread Selected Zones Across Current Key Range",
                 selectedRows.size() > 1);
    menu.addItem(9,
                 selectedRows.size() == 1
                     ? "Derive Root Note From Key Center"
                     : "Derive Root Notes From Key Centers",
                 true);
    menu.addSeparator();
    menu.addItem(10,
                 "Select All Zones\tCtrl+A",
                 static_cast<int>(selectedRows.size()) < static_cast<int>(mappingZoneRows_.size()));

    const juce::Rectangle<int> targetArea(screenPosition.x, screenPosition.y, 1, 1);
    juce::Component::SafePointer<AudiocityAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(targetArea),
        [safeThis](const int selectedId)
        {
            if (safeThis == nullptr)
                return;

            switch (selectedId)
            {
                case 1:
                    safeThis->createMappingZone();
                    break;
                case 2:
                    safeThis->duplicateSelectedMappingZone();
                    break;
                case 3:
                    safeThis->splitSelectedMappingZone();
                    break;
                case 4:
                    safeThis->deleteSelectedMappingZone();
                    break;
                case 5:
                    safeThis->clearSelectedMappingVelocityFades();
                    break;
                case 6:
                    safeThis->remapSelectedMappingZonesChromatically();
                    break;
                case 7:
                    safeThis->mapSelectedMappingZonesToRootNotes();
                    break;
                case 8:
                    safeThis->spreadSelectedMappingZonesAcrossKeyRange();
                    break;
                case 9:
                    safeThis->deriveSelectedMappingZoneRootsFromKeyRange();
                    break;
                case 10:
                    safeThis->selectAllMappingZones();
                    break;
                default:
                    break;
            }
        });
}

void AudiocityAudioProcessorEditor::updateMappingDetails()
{
    const auto selectedRows = getSelectedMappingRowIndices();
    if (selectedRows.size() == 1)
    {
        const auto selectedRow = selectedRows.front();
        const auto& row = mappingZoneRows_[static_cast<std::size_t>(selectedRow)];
        mappingOverview_.setSelectedZoneIndex(row.zoneIndex);
        mappingDetailsText_.setText(row.detailText, juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    if (!selectedRows.empty())
    {
        mappingOverview_.setSelectedZoneIndex(-1);

        juce::String zoneList;
        const auto maxZonesToList = juce::jmin(6, static_cast<int>(selectedRows.size()));
        for (int index = 0; index < maxZonesToList; ++index)
        {
            if (zoneList.isNotEmpty())
                zoneList += ", ";

            const auto rowIndex = selectedRows[static_cast<std::size_t>(index)];
            zoneList += "Z" + juce::String(mappingZoneRows_[static_cast<std::size_t>(rowIndex)].zoneIndex + 1);
        }

        if (static_cast<int>(selectedRows.size()) > maxZonesToList)
            zoneList += " ...";

        mappingDetailsText_.setText(
            juce::String(static_cast<int>(selectedRows.size())) + " zones selected"
                + "\nZones: " + zoneList
                + "\nBatch edit: Vel Fades, Gain, Pan, RR Group, RR Mode, Choke, Trigger, and Loop."
                + "\nChange a batch control, then Apply to fan it across the selection."
                + "\nBatch actions: Delete, Clear Velocity Fades, or right-click for the Mapping menu.",
            juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    mappingOverview_.setSelectedZoneIndex(-1);
    mappingDetailsText_.setText(processor_.getImportedProgramMapSummary(), juce::dontSendNotification);
    updateMappingEditControls();
}

void AudiocityAudioProcessorEditor::updateMappingEditControls()
{
    const auto selectedRows = getSelectedMappingRowIndices();
    const auto selectionChanged = selectedRows != mappingBatchTrackedSelectionRows_;
    if (selectionChanged)
        resetMappingBatchEditTracking(selectedRows);

    const auto hasSelection = !selectedRows.empty();
    const auto hasSingleSelection = selectedRows.size() == 1;
    const auto hasBatchSelection = selectedRows.size() > 1;
    const auto hasPendingBatchEdit = hasBatchSelection && hasPendingMappingBatchEdit();
    const auto hasImportedProgram = processor_.hasImportedProgram();

    mappingEditKeyLowSlider_.setEnabled(hasSingleSelection);
    mappingEditKeyHighSlider_.setEnabled(hasSingleSelection);
    mappingEditVelocityLowSlider_.setEnabled(hasSingleSelection);
    mappingEditVelocityHighSlider_.setEnabled(hasSingleSelection);
    mappingEditVelocityFadeInLowSlider_.setEnabled(hasSelection);
    mappingEditVelocityFadeInHighSlider_.setEnabled(hasSelection);
    mappingEditVelocityFadeOutLowSlider_.setEnabled(hasSelection);
    mappingEditVelocityFadeOutHighSlider_.setEnabled(hasSelection);
    mappingEditRootSlider_.setEnabled(hasSingleSelection);
    mappingEditSampleStartSlider_.setEnabled(hasSingleSelection);
    mappingEditSampleEndSlider_.setEnabled(hasSingleSelection);
    mappingEditLoopStartSlider_.setEnabled(hasSingleSelection);
    mappingEditLoopEndSlider_.setEnabled(hasSingleSelection);
    mappingEditGainSlider_.setEnabled(hasSelection);
    mappingEditPanSlider_.setEnabled(hasSelection);
    mappingEditRoundRobinGroupSlider_.setEnabled(hasSelection);
    mappingEditRoundRobinPositionSlider_.setEnabled(hasSingleSelection);
    mappingEditRoundRobinModeCombo_.setEnabled(hasSelection);
    mappingEditChokeSlider_.setEnabled(hasSelection);
    mappingEditTriggerCombo_.setEnabled(hasSelection);
    mappingEditLoopCombo_.setEnabled(hasSelection);
    mappingEditApplyButton_.setEnabled(hasSingleSelection || hasPendingBatchEdit);
    mappingEditApplyButton_.setButtonText(hasBatchSelection ? "Apply Batch" : "Apply Zone");
    mappingCreateZoneButton_.setEnabled(hasImportedProgram);
    mappingDuplicateZoneButton_.setEnabled(hasSingleSelection);
    mappingDeleteZoneButton_.setEnabled(hasSelection);
    mappingSplitZoneButton_.setEnabled(hasSingleSelection);

    if (!hasSelection)
    {
        mappingEditStatusLabel_.setText({}, juce::dontSendNotification);
        return;
    }

    if (!hasSingleSelection)
        return;

    const auto selectedRow = selectedRows.front();
    const auto& row = mappingZoneRows_[static_cast<std::size_t>(selectedRow)];
    if (selectionChanged || hasSingleSelection || !hasPendingBatchEdit)
    {
        const juce::ScopedValueSetter<bool> batchTrackingGuard(suppressMappingBatchEditTracking_, true);
        mappingEditVelocityFadeInLowSlider_.setValue(row.velocityFadeInLow, juce::dontSendNotification);
        mappingEditVelocityFadeInHighSlider_.setValue(row.velocityFadeInHigh, juce::dontSendNotification);
        mappingEditVelocityFadeOutLowSlider_.setValue(row.velocityFadeOutLow, juce::dontSendNotification);
        mappingEditVelocityFadeOutHighSlider_.setValue(row.velocityFadeOutHigh, juce::dontSendNotification);
        mappingEditGainSlider_.setValue(row.gainDbValue, juce::dontSendNotification);
        mappingEditPanSlider_.setValue(row.panValue * 100.0f, juce::dontSendNotification);
        mappingEditRoundRobinGroupSlider_.setValue(row.roundRobinGroup, juce::dontSendNotification);
        mappingEditRoundRobinModeCombo_.setSelectedId(
            row.roundRobinModeValue == audiocity::engine::RoundRobinMode::cycleRandom ? 2 : 1,
            juce::dontSendNotification);
        mappingEditChokeSlider_.setValue(row.chokeGroupId, juce::dontSendNotification);
        mappingEditTriggerCombo_.setSelectedId(
            row.triggerModeValue == audiocity::engine::ZoneTriggerMode::release
                ? 3
                : (row.triggerModeValue == audiocity::engine::ZoneTriggerMode::oneShot ? 2 : 1),
            juce::dontSendNotification);
        switch (row.loopModeValue)
        {
            case audiocity::engine::ZoneLoopMode::sustain:
                mappingEditLoopCombo_.setSelectedId(2, juce::dontSendNotification);
                break;
            case audiocity::engine::ZoneLoopMode::continuous:
                mappingEditLoopCombo_.setSelectedId(3, juce::dontSendNotification);
                break;
            case audiocity::engine::ZoneLoopMode::noLoop:
            default:
                mappingEditLoopCombo_.setSelectedId(1, juce::dontSendNotification);
                break;
        }
    }

    if (!hasSingleSelection)
        return;

    mappingSplitZoneButton_.setEnabled(row.keyHigh > row.keyLow);
    const auto sampleMax = juce::jmax(0, row.sampleLength - 1);
    mappingEditSampleStartSlider_.setRange(0.0, static_cast<double>(sampleMax), 1.0);
    mappingEditSampleEndSlider_.setRange(0.0, static_cast<double>(sampleMax), 1.0);
    mappingEditLoopStartSlider_.setRange(0.0, static_cast<double>(sampleMax), 1.0);
    mappingEditLoopEndSlider_.setRange(0.0, static_cast<double>(sampleMax), 1.0);

    const juce::ScopedValueSetter<bool> batchTrackingGuard(suppressMappingBatchEditTracking_, true);
    mappingEditKeyLowSlider_.setValue(row.keyLow, juce::dontSendNotification);
    mappingEditKeyHighSlider_.setValue(row.keyHigh, juce::dontSendNotification);
    mappingEditVelocityLowSlider_.setValue(row.velocityLow, juce::dontSendNotification);
    mappingEditVelocityHighSlider_.setValue(row.velocityHigh, juce::dontSendNotification);
    mappingEditRootSlider_.setValue(row.rootMidiNote, juce::dontSendNotification);
    mappingEditSampleStartSlider_.setValue(row.sampleStart, juce::dontSendNotification);
    mappingEditSampleEndSlider_.setValue(row.sampleEnd, juce::dontSendNotification);
    mappingEditLoopStartSlider_.setValue(row.loopStart >= 0 ? row.loopStart : row.sampleStart, juce::dontSendNotification);
    mappingEditLoopEndSlider_.setValue(row.loopEnd >= 0 ? row.loopEnd : row.sampleEnd, juce::dontSendNotification);
    mappingEditRoundRobinPositionSlider_.setValue(row.roundRobinPosition, juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::applySelectedMappingZoneEdit()
{
    const auto selectedRows = getSelectedMappingRowIndices();
    if (selectedRows.empty())
    {
        updateMappingEditControls();
        return;
    }

    if (selectedRows.size() > 1)
    {
        if (!hasPendingMappingBatchEdit())
        {
            mappingEditStatusLabel_.setText("Change a batch control before applying", juce::dontSendNotification);
            updateMappingEditControls();
            return;
        }

        const auto beforeState = captureImportedProgramMappingState();
        std::vector<audiocity::plugin::ProgramZoneEdit> edits;
        edits.reserve(selectedRows.size());
        std::vector<int> zoneIndices;
        zoneIndices.reserve(selectedRows.size());
        for (const auto selectedRowValue : selectedRows)
        {
            const auto& row = mappingZoneRows_[static_cast<std::size_t>(selectedRowValue)];
            audiocity::plugin::ProgramZoneEdit edit;
            edit.zoneIndex = row.zoneIndex;
            edit.keyLow = row.keyLow;
            edit.keyHigh = row.keyHigh;
            edit.velocityLow = row.velocityLow;
            edit.velocityHigh = row.velocityHigh;
            edit.rootMidiNote = row.rootMidiNote;

            if (mappingBatchVelocityFadeEdited_)
            {
                edit.velocityFadeInLow = static_cast<int>(std::round(mappingEditVelocityFadeInLowSlider_.getValue()));
                edit.velocityFadeInHigh = static_cast<int>(std::round(mappingEditVelocityFadeInHighSlider_.getValue()));
                edit.velocityFadeOutLow = static_cast<int>(std::round(mappingEditVelocityFadeOutLowSlider_.getValue()));
                edit.velocityFadeOutHigh = static_cast<int>(std::round(mappingEditVelocityFadeOutHighSlider_.getValue()));
                edit.hasVelocityFadeIn = true;
                edit.hasVelocityFadeOut = true;
            }

            if (mappingBatchGainEdited_)
            {
                edit.gainDb = static_cast<float>(mappingEditGainSlider_.getValue());
                edit.hasGainDb = true;
            }

            if (mappingBatchPanEdited_)
            {
                edit.pan = static_cast<float>(mappingEditPanSlider_.getValue() / 100.0);
                edit.hasPan = true;
            }

            if (mappingBatchRoundRobinGroupEdited_)
            {
                edit.roundRobinGroup = static_cast<int>(std::round(mappingEditRoundRobinGroupSlider_.getValue()));
                edit.hasRoundRobinGroup = true;
            }

            if (mappingBatchRoundRobinModeEdited_)
            {
                edit.roundRobinMode = mappingEditRoundRobinModeCombo_.getSelectedId() == 2
                    ? audiocity::engine::RoundRobinMode::cycleRandom
                    : audiocity::engine::RoundRobinMode::ordered;
                edit.hasRoundRobinMode = true;
            }

            if (mappingBatchChokeEdited_)
            {
                edit.chokeGroupId = static_cast<int>(std::round(mappingEditChokeSlider_.getValue()));
                edit.hasChokeGroupId = true;
            }

            if (mappingBatchTriggerEdited_)
            {
                edit.triggerMode = mappingEditTriggerCombo_.getSelectedId() == 3
                    ? audiocity::engine::ZoneTriggerMode::release
                    : (mappingEditTriggerCombo_.getSelectedId() == 2
                        ? audiocity::engine::ZoneTriggerMode::oneShot
                        : audiocity::engine::ZoneTriggerMode::gate);
                edit.hasTriggerMode = true;
            }

            if (mappingBatchLoopEdited_)
            {
                switch (mappingEditLoopCombo_.getSelectedId())
                {
                    case 2:
                        edit.loopMode = audiocity::engine::ZoneLoopMode::sustain;
                        break;
                    case 3:
                        edit.loopMode = audiocity::engine::ZoneLoopMode::continuous;
                        break;
                    case 1:
                    default:
                        edit.loopMode = audiocity::engine::ZoneLoopMode::noLoop;
                        break;
                }
                edit.hasLoopMode = true;
            }

            edits.push_back(edit);
            zoneIndices.push_back(row.zoneIndex);
        }

        if (!processor_.updateImportedProgramZoneMappings(edits))
        {
            mappingEditStatusLabel_.setText("Batch update failed", juce::dontSendNotification);
            updateMappingEditControls();
            return;
        }

        recordImportedProgramMappingChange(beforeState, "Update Mapping Batch");
        resetMappingBatchEditTracking();
        mappingEditStatusLabel_.setText(
            juce::String(static_cast<int>(zoneIndices.size())) + " zones updated",
            juce::dontSendNotification);
        refreshMappingZoneRows();
        selectMappingZoneIndices(zoneIndices);
        updateMappingDetails();
        updateDiagnosticsStatusText();
        return;
    }

    const auto selectedRow = selectedRows.front();
    const auto zoneIndex = mappingZoneRows_[static_cast<std::size_t>(selectedRow)].zoneIndex;
    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = zoneIndex;
    edit.keyLow = static_cast<int>(std::round(mappingEditKeyLowSlider_.getValue()));
    edit.keyHigh = static_cast<int>(std::round(mappingEditKeyHighSlider_.getValue()));
    edit.velocityLow = static_cast<int>(std::round(mappingEditVelocityLowSlider_.getValue()));
    edit.velocityHigh = static_cast<int>(std::round(mappingEditVelocityHighSlider_.getValue()));
    edit.velocityFadeInLow = static_cast<int>(std::round(mappingEditVelocityFadeInLowSlider_.getValue()));
    edit.velocityFadeInHigh = static_cast<int>(std::round(mappingEditVelocityFadeInHighSlider_.getValue()));
    edit.velocityFadeOutLow = static_cast<int>(std::round(mappingEditVelocityFadeOutLowSlider_.getValue()));
    edit.velocityFadeOutHigh = static_cast<int>(std::round(mappingEditVelocityFadeOutHighSlider_.getValue()));
    edit.rootMidiNote = static_cast<int>(std::round(mappingEditRootSlider_.getValue()));
    edit.sampleStart = static_cast<int>(std::round(mappingEditSampleStartSlider_.getValue()));
    edit.sampleEnd = static_cast<int>(std::round(mappingEditSampleEndSlider_.getValue()));
    edit.loopStart = static_cast<int>(std::round(mappingEditLoopStartSlider_.getValue()));
    edit.loopEnd = static_cast<int>(std::round(mappingEditLoopEndSlider_.getValue()));
    edit.gainDb = static_cast<float>(mappingEditGainSlider_.getValue());
    edit.pan = static_cast<float>(mappingEditPanSlider_.getValue() / 100.0);
    edit.roundRobinGroup = static_cast<int>(std::round(mappingEditRoundRobinGroupSlider_.getValue()));
    edit.roundRobinPosition = static_cast<int>(std::round(mappingEditRoundRobinPositionSlider_.getValue()));
    edit.roundRobinMode = mappingEditRoundRobinModeCombo_.getSelectedId() == 2
        ? audiocity::engine::RoundRobinMode::cycleRandom
        : audiocity::engine::RoundRobinMode::ordered;
    edit.chokeGroupId = static_cast<int>(std::round(mappingEditChokeSlider_.getValue()));
    edit.triggerMode = mappingEditTriggerCombo_.getSelectedId() == 3
        ? audiocity::engine::ZoneTriggerMode::release
        : (mappingEditTriggerCombo_.getSelectedId() == 2
            ? audiocity::engine::ZoneTriggerMode::oneShot
            : audiocity::engine::ZoneTriggerMode::gate);
    switch (mappingEditLoopCombo_.getSelectedId())
    {
        case 2:
            edit.loopMode = audiocity::engine::ZoneLoopMode::sustain;
            break;
        case 3:
            edit.loopMode = audiocity::engine::ZoneLoopMode::continuous;
            break;
        case 1:
        default:
            edit.loopMode = audiocity::engine::ZoneLoopMode::noLoop;
            break;
    }
    edit.hasSampleStart = true;
    edit.hasSampleEnd = true;
    edit.hasLoopStart = true;
    edit.hasLoopEnd = true;
    edit.hasVelocityFadeIn = true;
    edit.hasVelocityFadeOut = true;
    edit.hasGainDb = true;
    edit.hasPan = true;
    edit.hasRoundRobinGroup = true;
    edit.hasRoundRobinPosition = true;
    edit.hasRoundRobinMode = true;
    edit.hasChokeGroupId = true;
    edit.hasTriggerMode = true;
    edit.hasLoopMode = true;

    commitMappingZoneEdit(edit, "Zone " + juce::String(zoneIndex + 1) + " updated");
}

void AudiocityAudioProcessorEditor::createMappingZone()
{
    if (!processor_.hasImportedProgram())
    {
        updateMappingEditControls();
        return;
    }

    const auto selectedRows = getSelectedMappingRowIndices();
    const auto seedZoneIndex = selectedRows.size() == 1
        ? mappingZoneRows_[static_cast<std::size_t>(selectedRows.front())].zoneIndex
        : -1;

    auto defaultSampleAssetIndex = -1;
    if (selectedRows.size() == 1)
        defaultSampleAssetIndex = mappingZoneRows_[static_cast<std::size_t>(selectedRows.front())].sampleAssetIndex;

    const auto sampleAssetNames = processor_.getImportedProgramSampleAssetNames();
    if (sampleAssetNames.size() <= 1)
    {
        createMappingZoneForSampleAsset(defaultSampleAssetIndex, seedZoneIndex);
        return;
    }

    if (defaultSampleAssetIndex < 0 || defaultSampleAssetIndex >= sampleAssetNames.size())
        defaultSampleAssetIndex = 0;

    juce::PopupMenu menu;
    for (int sampleAssetIndex = 0; sampleAssetIndex < sampleAssetNames.size(); ++sampleAssetIndex)
    {
        menu.addItem(sampleAssetIndex + 1,
                     sampleAssetNames[sampleAssetIndex],
                     true,
                     sampleAssetIndex == defaultSampleAssetIndex);
    }

    juce::Component::SafePointer<AudiocityAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mappingCreateZoneButton_),
        [safeThis, seedZoneIndex](const int selectedId)
        {
            if (safeThis == nullptr || selectedId <= 0)
                return;

            safeThis->createMappingZoneForSampleAsset(selectedId - 1, seedZoneIndex);
        });
}

void AudiocityAudioProcessorEditor::createMappingZoneForSampleAsset(const int sampleAssetIndex,
                                                                    const int seedZoneIndex)
{
    const auto beforeState = captureImportedProgramMappingState();
    const auto newZoneIndex = sampleAssetIndex >= 0
        ? processor_.createImportedProgramZoneForSampleAsset(sampleAssetIndex, seedZoneIndex)
        : processor_.createImportedProgramZone(seedZoneIndex);
    if (newZoneIndex < 0)
    {
        mappingEditStatusLabel_.setText("Create failed", juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    recordImportedProgramMappingChange(beforeState, "Create Mapping Zone");
    mappingEditStatusLabel_.setText(
        seedZoneIndex >= 0
            ? "Zone " + juce::String(newZoneIndex + 1) + " created from zone " + juce::String(seedZoneIndex + 1)
            : "Zone " + juce::String(newZoneIndex + 1) + " created",
        juce::dontSendNotification);
    refreshMappingZoneRows();
    selectMappingZoneByIndex(newZoneIndex);
    updateMappingDetails();
    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::duplicateSelectedMappingZone()
{
    const auto selectedRows = getSelectedMappingRowIndices();
    if (selectedRows.size() != 1)
    {
        if (!selectedRows.empty())
            mappingEditStatusLabel_.setText("Select one zone to duplicate", juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    const auto selectedRow = selectedRows.front();
    const auto zoneIndex = mappingZoneRows_[static_cast<std::size_t>(selectedRow)].zoneIndex;
    const auto beforeState = captureImportedProgramMappingState();
    const auto newZoneIndex = processor_.duplicateImportedProgramZone(zoneIndex);
    if (newZoneIndex < 0)
    {
        mappingEditStatusLabel_.setText("Duplicate failed", juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    recordImportedProgramMappingChange(beforeState, "Duplicate Mapping Zone");
    mappingEditStatusLabel_.setText("Zone " + juce::String(zoneIndex + 1) + " duplicated", juce::dontSendNotification);
    refreshMappingZoneRows();
    selectMappingZoneByIndex(newZoneIndex);
    updateMappingDetails();
    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::splitSelectedMappingZone()
{
    const auto selectedRows = getSelectedMappingRowIndices();
    if (selectedRows.size() != 1)
    {
        if (!selectedRows.empty())
            mappingEditStatusLabel_.setText("Select one zone to split", juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    const auto selectedRow = selectedRows.front();
    const auto zoneIndex = mappingZoneRows_[static_cast<std::size_t>(selectedRow)].zoneIndex;
    const auto beforeState = captureImportedProgramMappingState();
    const auto newZoneIndex = processor_.splitImportedProgramZone(zoneIndex);
    if (newZoneIndex < 0)
    {
        mappingEditStatusLabel_.setText("Split failed", juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    recordImportedProgramMappingChange(beforeState, "Split Mapping Zone");
    mappingEditStatusLabel_.setText("Zone " + juce::String(zoneIndex + 1) + " split", juce::dontSendNotification);
    refreshMappingZoneRows();
    selectMappingZoneByIndex(newZoneIndex);
    updateMappingDetails();
    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::deleteSelectedMappingZone()
{
    const auto selectedRows = getSelectedMappingRowIndices();
    if (selectedRows.empty())
    {
        updateMappingEditControls();
        return;
    }

    std::vector<int> zoneIndices;
    zoneIndices.reserve(selectedRows.size());
    for (const auto selectedRow : selectedRows)
        zoneIndices.push_back(mappingZoneRows_[static_cast<std::size_t>(selectedRow)].zoneIndex);

    const auto beforeState = captureImportedProgramMappingState();
    if (!processor_.deleteImportedProgramZones(zoneIndices))
    {
        mappingEditStatusLabel_.setText("Delete failed", juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    const auto deletedCount = static_cast<int>(zoneIndices.size());
    recordImportedProgramMappingChange(beforeState, deletedCount == 1 ? "Delete Mapping Zone" : "Delete Mapping Zones");
    mappingEditStatusLabel_.setText(
        deletedCount == 1
            ? "Zone " + juce::String(zoneIndices.front() + 1) + " deleted"
            : juce::String(deletedCount) + " zones deleted",
        juce::dontSendNotification);
    refreshMappingZoneRows();
    selectClosestMappingRow(*std::min_element(selectedRows.begin(), selectedRows.end()));
    updateMappingDetails();
    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::clearSelectedMappingVelocityFades()
{
    const auto selectedRows = getSelectedMappingRowIndices();
    if (selectedRows.empty())
    {
        updateMappingEditControls();
        return;
    }

    const auto beforeState = captureImportedProgramMappingState();
    std::vector<audiocity::plugin::ProgramZoneEdit> edits;
    edits.reserve(selectedRows.size());
    std::vector<int> zoneIndices;
    zoneIndices.reserve(selectedRows.size());
    for (const auto selectedRow : selectedRows)
    {
        const auto& row = mappingZoneRows_[static_cast<std::size_t>(selectedRow)];
        audiocity::plugin::ProgramZoneEdit edit;
        edit.zoneIndex = row.zoneIndex;
        edit.keyLow = row.keyLow;
        edit.keyHigh = row.keyHigh;
        edit.velocityLow = row.velocityLow;
        edit.velocityHigh = row.velocityHigh;
        edit.rootMidiNote = row.rootMidiNote;
        edit.velocityFadeInLow = -1;
        edit.velocityFadeInHigh = -1;
        edit.velocityFadeOutLow = -1;
        edit.velocityFadeOutHigh = -1;
        edit.hasVelocityFadeIn = true;
        edit.hasVelocityFadeOut = true;

        edits.push_back(edit);
        zoneIndices.push_back(row.zoneIndex);
    }

    if (!processor_.updateImportedProgramZoneMappings(edits))
    {
        mappingEditStatusLabel_.setText("Fade clear failed", juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    recordImportedProgramMappingChange(beforeState,
        zoneIndices.size() == 1 ? "Clear Mapping Fades" : "Clear Mapping Fades Batch");
    mappingEditStatusLabel_.setText(
        zoneIndices.size() == 1
            ? "Zone " + juce::String(zoneIndices.front() + 1) + " fades cleared"
            : juce::String(static_cast<int>(zoneIndices.size())) + " zones fades cleared",
        juce::dontSendNotification);
    resetMappingBatchEditTracking();
    refreshMappingZoneRows();
    selectMappingZoneIndices(zoneIndices);
    updateMappingDetails();
    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::remapSelectedMappingZonesChromatically()
{
    const auto selectedRows = getSelectedMappingRowIndices();
    if (selectedRows.empty())
    {
        updateMappingEditControls();
        return;
    }

    constexpr int kChromaticBaseMidiNote = 36;

    std::vector<int> zoneIndices;
    zoneIndices.reserve(selectedRows.size());
    for (const auto selectedRow : selectedRows)
        zoneIndices.push_back(mappingZoneRows_[static_cast<std::size_t>(selectedRow)].zoneIndex);

    const auto beforeState = captureImportedProgramMappingState();
    if (!processor_.remapImportedProgramZonesChromatically(zoneIndices, kChromaticBaseMidiNote))
    {
        mappingEditStatusLabel_.setText("Chromatic remap failed", juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    recordImportedProgramMappingChange(beforeState,
        zoneIndices.size() == 1 ? "Map Zone Chromatically" : "Map Zones Chromatically");
    mappingEditStatusLabel_.setText(
        zoneIndices.size() == 1
            ? "Zone " + juce::String(zoneIndices.front() + 1) + " mapped to C1"
            : juce::String(static_cast<int>(zoneIndices.size())) + " zones mapped chromatically from C1",
        juce::dontSendNotification);
    resetMappingBatchEditTracking();
    refreshMappingZoneRows();
    selectMappingZoneIndices(zoneIndices);
    updateMappingDetails();
    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::mapSelectedMappingZonesToRootNotes()
{
    const auto selectedRows = getSelectedMappingRowIndices();
    if (selectedRows.empty())
    {
        updateMappingEditControls();
        return;
    }

    std::vector<int> zoneIndices;
    zoneIndices.reserve(selectedRows.size());
    for (const auto selectedRow : selectedRows)
        zoneIndices.push_back(mappingZoneRows_[static_cast<std::size_t>(selectedRow)].zoneIndex);

    const auto beforeState = captureImportedProgramMappingState();
    if (!processor_.mapImportedProgramZonesToRootNotes(zoneIndices))
    {
        mappingEditStatusLabel_.setText("Map-to-root failed", juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    recordImportedProgramMappingChange(beforeState,
        zoneIndices.size() == 1 ? "Map Zone To Root" : "Map Zones To Roots");
    mappingEditStatusLabel_.setText(
        zoneIndices.size() == 1
            ? "Zone " + juce::String(zoneIndices.front() + 1) + " mapped to its root note"
            : juce::String(static_cast<int>(zoneIndices.size())) + " zones mapped to their root notes",
        juce::dontSendNotification);
    resetMappingBatchEditTracking();
    refreshMappingZoneRows();
    selectMappingZoneIndices(zoneIndices);
    updateMappingDetails();
    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::spreadSelectedMappingZonesAcrossKeyRange()
{
    const auto selectedRows = getSelectedMappingRowIndices();
    if (selectedRows.size() < 2)
    {
        updateMappingEditControls();
        return;
    }

    std::vector<int> zoneIndices;
    zoneIndices.reserve(selectedRows.size());
    for (const auto selectedRow : selectedRows)
        zoneIndices.push_back(mappingZoneRows_[static_cast<std::size_t>(selectedRow)].zoneIndex);

    const auto beforeState = captureImportedProgramMappingState();
    if (!processor_.spreadImportedProgramZonesAcrossKeyRange(zoneIndices))
    {
        mappingEditStatusLabel_.setText("Key-range spread failed", juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    recordImportedProgramMappingChange(beforeState, "Spread Zones Across Key Range");
    mappingEditStatusLabel_.setText(
        juce::String(static_cast<int>(zoneIndices.size())) + " zones spread across their current key range",
        juce::dontSendNotification);
    resetMappingBatchEditTracking();
    refreshMappingZoneRows();
    selectMappingZoneIndices(zoneIndices);
    updateMappingDetails();
    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::deriveSelectedMappingZoneRootsFromKeyRange()
{
    const auto selectedRows = getSelectedMappingRowIndices();
    if (selectedRows.empty())
    {
        updateMappingEditControls();
        return;
    }

    std::vector<int> zoneIndices;
    zoneIndices.reserve(selectedRows.size());
    for (const auto selectedRow : selectedRows)
        zoneIndices.push_back(mappingZoneRows_[static_cast<std::size_t>(selectedRow)].zoneIndex);

    const auto beforeState = captureImportedProgramMappingState();
    if (!processor_.deriveImportedProgramZoneRootsFromKeyRanges(zoneIndices))
    {
        mappingEditStatusLabel_.setText("Root-note derive failed", juce::dontSendNotification);
        updateMappingEditControls();
        return;
    }

    recordImportedProgramMappingChange(beforeState,
        zoneIndices.size() == 1 ? "Derive Root Note" : "Derive Root Notes");
    mappingEditStatusLabel_.setText(
        zoneIndices.size() == 1
            ? "Zone " + juce::String(zoneIndices.front() + 1) + " root derived from key center"
            : juce::String(static_cast<int>(zoneIndices.size())) + " zone roots derived from key centers",
        juce::dontSendNotification);
    resetMappingBatchEditTracking();
    refreshMappingZoneRows();
    selectMappingZoneIndices(zoneIndices);
    updateMappingDetails();
    updateDiagnosticsStatusText();
}

bool AudiocityAudioProcessorEditor::commitMappingZoneEdit(const audiocity::plugin::ProgramZoneEdit& edit,
                                                          const juce::String& statusText)
{
    const auto beforeState = captureImportedProgramMappingState();
    if (!processor_.updateImportedProgramZoneMapping(edit))
    {
        mappingEditStatusLabel_.setText("Update failed", juce::dontSendNotification);
        updateMappingEditControls();
        return false;
    }

    recordImportedProgramMappingChange(beforeState, statusText);
    mappingEditStatusLabel_.setText(statusText, juce::dontSendNotification);
    refreshMappingZoneRows();

    selectMappingZoneByIndex(edit.zoneIndex);

    updateMappingDetails();
    updateDiagnosticsStatusText();
    return true;
}

void AudiocityAudioProcessorEditor::listBoxItemClicked(const int row, const juce::MouseEvent& event)
{
    if (event.mods.isShiftDown())
    {
        loadSampleFromBrowserRow(row);
        return;
    }

    previewSampleFromBrowserRow(row);
}

void AudiocityAudioProcessorEditor::listBoxItemDoubleClicked(const int row, const juce::MouseEvent&)
{
    loadSampleFromBrowserRow(row);
}

void AudiocityAudioProcessorEditor::selectedRowsChanged(const int lastRowSelected)
{
    updateBrowserLibraryControls();

    if (currentTabIndex_ != 1 && !shouldShowPersistentBrowserRail())
        return;

    previewSampleFromBrowserRow(lastRowSelected, false);
}

void AudiocityAudioProcessorEditor::returnKeyPressed(const int lastRowSelected)
{
    if (currentTabIndex_ != 1)
        return;

    loadSampleFromBrowserRow(lastRowSelected);
}

void AudiocityAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    if (currentTabIndex_ == 0 && !event.mods.isPopupMenu() && event.mods.isLeftButtonDown())
    {
        if (getSampleInspectorCardHeaderBounds(sampleInspectorFilterModBounds_).contains(event.getPosition()))
        {
            sampleInspectorFilterModExpanded_ = !sampleInspectorFilterModExpanded_;
            processor_.setSampleInspectorFilterModExpanded(sampleInspectorFilterModExpanded_);
            resized();
            repaint();
            return;
        }

        if (getSampleInspectorCardHeaderBounds(sampleInspectorEffectsBounds_).contains(event.getPosition()))
        {
            sampleInspectorEffectsExpanded_ = !sampleInspectorEffectsExpanded_;
            processor_.setSampleInspectorEffectsExpanded(sampleInspectorEffectsExpanded_);
            resized();
            repaint();
            return;
        }
    }

    isResizingSampleList_ = false;
}

void AudiocityAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
}

void AudiocityAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    isResizingSampleList_ = false;
}

void AudiocityAudioProcessorEditor::handleSampleControlsMouseDown(const juce::MouseEvent& event)
{
    if (currentTabIndex_ != 0 || event.mods.isPopupMenu() || !event.mods.isLeftButtonDown())
        return;

    for (const auto& group : groupBoxes_)
    {
        if (!group.collapsible)
            continue;

        auto headerBounds = group.bounds;
        headerBounds.setHeight(24);
        if (headerBounds.contains(event.getPosition()))
        {
            toggleSampleGroupExpanded(group.key);
            return;
        }
    }
}

bool AudiocityAudioProcessorEditor::isSampleGroupExpanded(const juce::String& key) const
{
    if (key == "programMap")
        return sampleProgramMapExpanded_;
    if (key == "modulation")
        return sampleModulationExpanded_;
    if (key == "filterMod")
        return sampleFilterModExpanded_;
    if (key == "effects")
        return sampleEffectsExpanded_;

    return true;
}

bool AudiocityAudioProcessorEditor::isSampleGroupCollapsible(const juce::String& key) const
{
    return key == "programMap"
        || key == "filterMod"
        || key == "effects";
}

void AudiocityAudioProcessorEditor::toggleSampleGroupExpanded(const juce::String& key)
{
    if (key == "programMap")
        sampleProgramMapExpanded_ = !sampleProgramMapExpanded_;
    else if (key == "modulation")
        sampleModulationExpanded_ = !sampleModulationExpanded_;
    else if (key == "filterMod")
        sampleFilterModExpanded_ = !sampleFilterModExpanded_;
    else if (key == "effects")
        sampleEffectsExpanded_ = !sampleEffectsExpanded_;
    else
        return;

    updateTabVisibility();
    resized();
    repaint();
}

bool AudiocityAudioProcessorEditor::isSupportedSampleFile(const juce::File& file) const
{
    return audiocity::plugin::LibraryFileIndex::isSupportedFile(file, processor_.isRexRuntimeAvailable());
}

bool AudiocityAudioProcessorEditor::importInstrumentFileByFormat(
    const juce::File& file,
    const audiocity::plugin::ImportedProgramFormat format,
    const int selectedChoiceIndex)
{
    if (file.getFileExtension().equalsIgnoreCase(".nki"))
        return processor_.importLegacyNkiProgram(file);

    switch (format)
    {
        case audiocity::plugin::ImportedProgramFormat::sfz:
            return processor_.importSfzProgram(file);
        case audiocity::plugin::ImportedProgramFormat::rex:
            return processor_.importRexSliceProgram(file);
        case audiocity::plugin::ImportedProgramFormat::sf2:
            return processor_.importSf2Program(file, selectedChoiceIndex >= 0 ? selectedChoiceIndex : 0);
        case audiocity::plugin::ImportedProgramFormat::decentSampler:
            return processor_.importDecentSamplerProgram(file);
        case audiocity::plugin::ImportedProgramFormat::bitwigMultisample:
            return processor_.importBitwigMultisampleProgram(file);
        case audiocity::plugin::ImportedProgramFormat::mpcKeygroup:
            return processor_.importMpcKeygroupProgram(file);
        case audiocity::plugin::ImportedProgramFormat::bento1010:
            return processor_.import1010MusicPresetProgram(file);
        case audiocity::plugin::ImportedProgramFormat::talSampler:
            return processor_.importTalSamplerProgram(file);
        case audiocity::plugin::ImportedProgramFormat::tx16wx:
            return processor_.importTx16WxProgram(file);
        case audiocity::plugin::ImportedProgramFormat::korgMultisample:
            return processor_.importKorgMultisampleProgram(file);
        case audiocity::plugin::ImportedProgramFormat::abletonSampler:
            return processor_.importAbletonSamplerProgram(file);
        case audiocity::plugin::ImportedProgramFormat::distingExPreset:
            return processor_.importDistingExPresetProgram(file);
        case audiocity::plugin::ImportedProgramFormat::korgKmp:
            return processor_.importKorgKmpProgram(file);
        case audiocity::plugin::ImportedProgramFormat::logicExs24:
            return processor_.importLogicExs24Program(file);
        case audiocity::plugin::ImportedProgramFormat::nnxt:
            return processor_.importNnxtProgram(file);
        case audiocity::plugin::ImportedProgramFormat::unknown:
        default:
            return processor_.loadSampleFromFile(file);
    }
}

void AudiocityAudioProcessorEditor::completeInstrumentLoad(const juce::File& file,
                                                          const bool loaded,
                                                          const bool cancelledByUser,
                                                          const std::function<void(bool)>& completion)
{
    if (loaded)
    {
        editorUndoHistory_.clear();
        mappingUndoProgramPath_ = processor_.getImportedProgramPath();
        lastSettingsSnapshot_ = captureSettingsSnapshot();
        processor_.markLibraryRecent(file.getFullPathName());
        refreshBrowserEntryLibraryFlags();
        rebuildVisibleSampleList();
    }
    else if (!cancelledByUser)
    {
        updateDiagnosticsStatusText();

        if (file.getFileExtension().equalsIgnoreCase(".nki"))
        {
            const auto probe = audiocity::engine::nki::probeFile(file);
            if (!probe.missingSampleReferences.isEmpty())
                promptForNkiSampleFolder(file);
        }
    }

    if (completion)
        completion(loaded);
}

bool AudiocityAudioProcessorEditor::loadFileAsInstrument(const juce::File& file,
                                                         std::function<void(bool)> completion)
{
    processor_.panicAllAudio();
    updateGeneratePreviewButtonText();

    const auto choiceProbe = audiocity::plugin::probeImportedProgramChoices(file);
    const auto detectedFormat = choiceProbe.format != audiocity::plugin::ImportedProgramFormat::unknown
        ? choiceProbe.format
        : audiocity::plugin::detectImportedProgramFormat(file.getFullPathName());
    if (choiceProbe.hasMultipleChoices())
    {
        auto safeThis = juce::Component::SafePointer<AudiocityAudioProcessorEditor>(this);
        showImportedProgramChoiceDialog(this,
                                        file,
                                        choiceProbe,
                                        [safeThis,
                                         file,
                                         detectedFormat,
                                         completion = std::move(completion)](std::optional<int> choiceIndex) mutable
                                        {
                                            if (safeThis == nullptr)
                                                return;

                                            if (!choiceIndex.has_value())
                                            {
                                                safeThis->completeInstrumentLoad(file, false, true, completion);
                                                return;
                                            }

                                            const auto loaded = safeThis->importInstrumentFileByFormat(file,
                                                                                                       detectedFormat,
                                                                                                       *choiceIndex);
                                            safeThis->completeInstrumentLoad(file, loaded, false, completion);
                                        });

        return false;
    }

    const auto loaded = importInstrumentFileByFormat(file, detectedFormat, -1);
    completeInstrumentLoad(file, loaded, false, completion);
    return loaded;
}

void AudiocityAudioProcessorEditor::autoSliceLoadedSample()
{
    if (processor_.hasImportedProgram())
        return;

    const auto samplePath = processor_.getLoadedSamplePath();
    if (samplePath.isEmpty())
        return;

    const juce::File file(samplePath);
    processor_.panicAllAudio();
    updateGeneratePreviewButtonText();

    if (!processor_.importTransientSliceProgram(file))
    {
        updateDiagnosticsStatusText();
        return;
    }

    editorUndoHistory_.clear();
    mappingUndoProgramPath_ = processor_.getImportedProgramPath();
    lastSettingsSnapshot_ = captureSettingsSnapshot();
    processor_.markLibraryRecent(file.getFullPathName());
    refreshBrowserEntryLibraryFlags();
    rebuildVisibleSampleList();
    clearSelectedPresetAfterSourceLoad();
    refreshUI(true);
    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::mergeLoadedSliceAtBoundary(const int boundarySample)
{
    if (!processor_.hasImportedProgram()
        || processor_.getImportedProgramFormat() != audiocity::plugin::ImportedProgramFormat::sampleSlices)
    {
        return;
    }

    const auto beforeState = captureImportedProgramMappingState();
    const auto mergedZoneIndex = processor_.mergeImportedProgramSlicesAtSampleBoundary(boundarySample);
    if (mergedZoneIndex < 0)
    {
        updateDiagnosticsStatusText();
        return;
    }

    recordImportedProgramMappingChange(beforeState, "Merge Slices");
    resetMappingBatchEditTracking();
    refreshMappingZoneRows();
    selectMappingZoneIndices({ mergedZoneIndex });
    waveformView_.setSliceMarkers(processor_.getImportedProgramSliceMarkerSamples());
    programMapText_.setText(processor_.getImportedProgramMapSummary(), juce::dontSendNotification);
    updateMappingDetails();
    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::splitLoadedSliceAtSample(const int sampleIndex)
{
    if (!processor_.hasImportedProgram()
        || processor_.getImportedProgramFormat() != audiocity::plugin::ImportedProgramFormat::sampleSlices)
    {
        return;
    }

    const auto beforeState = captureImportedProgramMappingState();
    const auto newZoneIndex = processor_.splitImportedProgramSliceAtSample(sampleIndex);
    if (newZoneIndex < 0)
    {
        updateDiagnosticsStatusText();
        return;
    }

    recordImportedProgramMappingChange(beforeState, "Split Slice");
    resetMappingBatchEditTracking();
    refreshMappingZoneRows();
    selectMappingZoneIndices({ newZoneIndex });
    waveformView_.setSliceMarkers(processor_.getImportedProgramSliceMarkerSamples());
    programMapText_.setText(processor_.getImportedProgramMapSummary(), juce::dontSendNotification);
    updateMappingDetails();
    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::chooseSampleRootFolder()
{
    fileChooser_ = std::make_unique<juce::FileChooser>("Choose sample root folder", juce::File{});

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        const auto selected = chooser.getResult();
        if (selected == juce::File{} || !selected.isDirectory())
            return;

        scanSampleRootFolder(selected);
    });
}

void AudiocityAudioProcessorEditor::promptForNkiSampleFolder(const juce::File& nkiFile)
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Locate Samples — choose the folder containing the NKI's sample files",
        nkiFile.getParentDirectory());

    const auto chooserFlags = juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories;
    fileChooser_->launchAsync(chooserFlags, [this, nkiFile](const juce::FileChooser& chooser)
    {
        const auto selected = chooser.getResult();
        if (!selected.isDirectory())
            return;

        processor_.panicAllAudio();
        updateGeneratePreviewButtonText();

        if (processor_.importLegacyNkiProgramWithSearchFolder(nkiFile, selected))
        {
            editorUndoHistory_.clear();
            mappingUndoProgramPath_ = processor_.getImportedProgramPath();
            lastSettingsSnapshot_ = captureSettingsSnapshot();
            processor_.markLibraryRecent(nkiFile.getFullPathName());
            refreshBrowserEntryLibraryFlags();
            rebuildVisibleSampleList();
        }

        updateDiagnosticsStatusText();
    });
}

void AudiocityAudioProcessorEditor::cancelSampleRootScan()
{
    if (!sampleScanInProgress_.load(std::memory_order_relaxed))
        return;

    ++sampleScanGeneration_;
    sampleScanInProgress_.store(false, std::memory_order_relaxed);
}

void AudiocityAudioProcessorEditor::scanSampleRootFolder(const juce::File& rootFolder)
{
    const auto newRootPath = rootFolder.getFullPathName();
    const auto rootChanged = sampleRootFolderPath_.isNotEmpty()
        && !sampleRootFolderPath_.equalsIgnoreCase(newRootPath);

    audiocity::plugin::PeakPreviewCacheStore peakCacheStore(
        audiocity::plugin::PeakPreviewCacheStore::getDefaultCacheFile());

    if (rootChanged)
        peakCacheStore.reset();

    auto peakCacheData = peakCacheStore.load();
    if (!peakCacheData.libraryRootPath.equalsIgnoreCase(newRootPath))
        peakCacheData.entries.clear();

    sampleRootFolderPath_ = newRootPath;
    processor_.setSampleBrowserRootFolder(sampleRootFolderPath_);
    sampleBrowserRootLabel_.setText(sampleRootFolderPath_, juce::dontSendNotification);
    refreshSampleBrowserBookmarks();

    allSampleEntries_.clear();
    visibleSampleEntryIndices_.clear();
    rebuildVisibleSampleList();

    sampleScanInProgress_.store(true, std::memory_order_relaxed);
    const auto scanGeneration = ++sampleScanGeneration_;
    const auto includeRexFiles = processor_.isRexRuntimeAvailable();
    auto safeThis = juce::Component::SafePointer<AudiocityAudioProcessorEditor>(this);

    std::thread([safeThis, rootFolder, scanGeneration, includeRexFiles, peakCacheStore, cacheEntries = std::move(peakCacheData.entries)]() mutable
    {
        std::vector<SampleListEntry> batch;
        batch.reserve(24);
        std::unordered_map<std::string, audiocity::plugin::PeakPreviewCacheEntry> updatedCacheEntries;

        auto flushBatchToUi = [safeThis, scanGeneration](std::vector<SampleListEntry>& batchToFlush)
        {
            if (batchToFlush.empty())
                return;

            auto uiBatch = std::move(batchToFlush);
            batchToFlush.clear();

            juce::MessageManager::callAsync([safeThis, scanGeneration, batch = std::move(uiBatch)]() mutable
            {
                if (safeThis == nullptr)
                    return;

                auto* self = safeThis.getComponent();
                if (scanGeneration != self->sampleScanGeneration_.load())
                    return;

                self->allSampleEntries_.insert(self->allSampleEntries_.end(),
                    std::make_move_iterator(batch.begin()),
                    std::make_move_iterator(batch.end()));
                self->rebuildVisibleSampleList();
            });
        };

        for (const auto& entry : juce::RangedDirectoryIterator(rootFolder, true, "*", juce::File::findFiles))
        {
            if (safeThis == nullptr)
                return;

            auto* self = safeThis.getComponent();
            if (scanGeneration != self->sampleScanGeneration_.load())
                return;

            const auto indexedEntry = audiocity::plugin::LibraryFileIndex::createEntryForFile(
                rootFolder,
                entry.getFile(),
                includeRexFiles);
            if (!indexedEntry.has_value())
                continue;

            const auto& indexedFile = *indexedEntry;
            SampleListEntry item;
            item.file = indexedFile.file;
            item.relativePath = indexedFile.relativePath;
            item.fileName = indexedFile.fileName;
            item.fileNameLower = item.fileName.toLowerCase();
            item.relativePathLower = item.relativePath.toLowerCase();

            const auto cacheKey = audiocity::plugin::makePeakPreviewCacheKey(indexedFile.file);
            const auto fileSizeBytes = indexedFile.sizeBytes;

            SamplePreviewData previewData;
            if (indexedFile.isInstrument)
            {
                if (indexedFile.extensionLower == ".nki")
                {
                    previewData.metadataLine = "NKI instrument (legacy subset)";
                    previewData.loopFormatBadge = "NKI";
                }
                else
                {
                    previewData.metadataLine = "SFZ instrument";
                    previewData.loopFormatBadge = "SFZ";
                }
            }
            else if (const auto cacheIt = cacheEntries.find(cacheKey);
                cacheIt != cacheEntries.end() && cacheIt->second.fileSizeBytes == fileSizeBytes)
            {
                previewData.peaks = cacheIt->second.peaks;
                previewData.metadataLine = cacheIt->second.metadataLine;
                previewData.loopFormatBadge = cacheIt->second.loopFormatBadge;
                previewData.loopMetadataLine = cacheIt->second.loopMetadataLine;
            }
            else
            {
                previewData = buildPreviewAndMetadata(indexedFile.file);
            }

            updatedCacheEntries[cacheKey] = {
                fileSizeBytes,
                previewData.peaks,
                previewData.metadataLine,
                previewData.loopFormatBadge,
                previewData.loopMetadataLine
            };

            item.previewPeaks = std::move(previewData.peaks);
            item.metadataLine = std::move(previewData.metadataLine);
            item.loopFormatBadge = std::move(previewData.loopFormatBadge);
            item.loopMetadataLine = std::move(previewData.loopMetadataLine);
            batch.push_back(std::move(item));

            if (batch.size() >= 24)
                flushBatchToUi(batch);
        }

        flushBatchToUi(batch);

        if (safeThis == nullptr)
            return;

        auto* self = safeThis.getComponent();
        if (scanGeneration != self->sampleScanGeneration_.load())
            return;

        audiocity::plugin::PeakPreviewCacheData newCacheData;
        newCacheData.libraryRootPath = rootFolder.getFullPathName();
        newCacheData.entries = std::move(updatedCacheEntries);
        peakCacheStore.save(newCacheData);

        juce::MessageManager::callAsync([safeThis, scanGeneration]()
        {
            if (safeThis == nullptr)
                return;

            auto* self = safeThis.getComponent();
            if (scanGeneration != self->sampleScanGeneration_.load())
                return;

            self->sampleScanInProgress_.store(false, std::memory_order_relaxed);
        });
    }).detach();
}

void AudiocityAudioProcessorEditor::refreshSampleBrowserBookmarks()
{
    const auto metadata = processor_.getLibraryMetadataSnapshot();
    const auto bookmarks = metadata.getBookmarkPaths();

    sampleBrowserBookmarkCombo_.clear(juce::dontSendNotification);

    auto selectedId = 0;
    for (int index = 0; index < bookmarks.size(); ++index)
    {
        const auto id = index + 1;
        sampleBrowserBookmarkCombo_.addItem(bookmarks[index], id);
        if (sampleRootFolderPath_.isNotEmpty() && bookmarks[index].equalsIgnoreCase(sampleRootFolderPath_))
            selectedId = id;
    }

    sampleBrowserBookmarkCombo_.setSelectedId(selectedId, juce::dontSendNotification);
    sampleBrowserBookmarkCombo_.setEnabled(bookmarks.size() > 0);
    sampleBrowserAddBookmarkButton_.setEnabled(sampleRootFolderPath_.isNotEmpty());
    sampleBrowserRemoveBookmarkButton_.setEnabled(selectedId > 0);
}

void AudiocityAudioProcessorEditor::refreshSampleBrowserTagFilter()
{
    const auto previousSelection = sampleBrowserTagFilterCombo_.getSelectedId() > 1
        ? sampleBrowserTagFilterCombo_.getText().trim()
        : juce::String();

    const auto metadata = processor_.getLibraryMetadataSnapshot();
    const auto tags = metadata.getAllTags();

    sampleBrowserTagFilterCombo_.clear(juce::dontSendNotification);
    sampleBrowserTagFilterCombo_.addItem("All Tags", 1);

    auto selectedId = 1;
    for (int index = 0; index < tags.size(); ++index)
    {
        const auto id = index + 2;
        sampleBrowserTagFilterCombo_.addItem(tags[index], id);
        if (previousSelection.isNotEmpty() && tags[index].equalsIgnoreCase(previousSelection))
            selectedId = id;
    }

    sampleBrowserTagFilterCombo_.setSelectedId(selectedId, juce::dontSendNotification);
    sampleBrowserTagFilterCombo_.setEnabled(tags.size() > 0);
}

void AudiocityAudioProcessorEditor::scanSelectedSampleBrowserBookmark()
{
    const auto selectedPath = sampleBrowserBookmarkCombo_.getText().trim();
    if (selectedPath.isEmpty())
    {
        refreshSampleBrowserBookmarks();
        return;
    }

    const juce::File selectedFolder(selectedPath);
    if (!selectedFolder.isDirectory())
    {
        sampleBrowserRootLabel_.setText(selectedPath + " (missing)", juce::dontSendNotification);
        sampleBrowserCountLabel_.setText("Bookmarked folder not found", juce::dontSendNotification);
        sampleBrowserRemoveBookmarkButton_.setEnabled(true);
        return;
    }

    if (sampleScanInProgress_.load(std::memory_order_relaxed))
        cancelSampleRootScan();

    scanSampleRootFolder(selectedFolder);
}

void AudiocityAudioProcessorEditor::addCurrentSampleRootBookmark()
{
    if (sampleRootFolderPath_.isEmpty())
        return;

    const juce::File rootFolder(sampleRootFolderPath_);
    if (!rootFolder.isDirectory())
        return;

    processor_.addLibraryBookmark(sampleRootFolderPath_);
    refreshSampleBrowserBookmarks();
}

void AudiocityAudioProcessorEditor::removeSelectedSampleBrowserBookmark()
{
    const auto selectedPath = sampleBrowserBookmarkCombo_.getText().trim();
    if (selectedPath.isEmpty())
        return;

    processor_.removeLibraryBookmark(selectedPath);
    refreshSampleBrowserBookmarks();
}

void AudiocityAudioProcessorEditor::refreshBrowserEntryLibraryFlags()
{
    const auto metadata = processor_.getLibraryMetadataSnapshot();
    for (auto& item : allSampleEntries_)
    {
        const auto path = item.file.getFullPathName();
        item.isFavorite = metadata.isFavorite(path);
        item.recentRank = metadata.recentRank(path);
        item.isRecent = item.recentRank >= 0;
        item.tags = metadata.getTags(path);
        item.tagsLower = item.tags.joinIntoString(" ").toLowerCase();
    }
}

void AudiocityAudioProcessorEditor::updateBrowserLibraryControls()
{
    const auto row = sampleBrowserListBox_.getSelectedRow();
    const auto hasSelection = row >= 0 && row < static_cast<int>(visibleSampleEntryIndices_.size());
    sampleBrowserFavoriteButton_.setEnabled(hasSelection);
    sampleBrowserTagsEditor_.setEnabled(hasSelection);
    sampleBrowserApplyTagsButton_.setEnabled(hasSelection);

    if (!hasSelection)
    {
        sampleBrowserFavoriteButton_.setToggleState(false, juce::dontSendNotification);
        sampleBrowserTagsEditor_.setText({}, juce::dontSendNotification);
        return;
    }

    const auto sourceIndex = visibleSampleEntryIndices_[static_cast<std::size_t>(row)];
    const auto& item = allSampleEntries_[static_cast<std::size_t>(sourceIndex)];
    sampleBrowserFavoriteButton_.setToggleState(item.isFavorite, juce::dontSendNotification);
    sampleBrowserTagsEditor_.setText(item.tags.joinIntoString(", "), juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::toggleSelectedBrowserFavorite()
{
    const auto row = sampleBrowserListBox_.getSelectedRow();
    if (row < 0 || row >= static_cast<int>(visibleSampleEntryIndices_.size()))
    {
        updateBrowserLibraryControls();
        return;
    }

    const auto sourceIndex = visibleSampleEntryIndices_[static_cast<std::size_t>(row)];
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(allSampleEntries_.size()))
    {
        updateBrowserLibraryControls();
        return;
    }

    const auto file = allSampleEntries_[static_cast<std::size_t>(sourceIndex)].file;
    processor_.setLibraryFavorite(file.getFullPathName(), sampleBrowserFavoriteButton_.getToggleState());
    refreshBrowserEntryLibraryFlags();
    rebuildVisibleSampleList();

    for (int visibleIndex = 0; visibleIndex < static_cast<int>(visibleSampleEntryIndices_.size()); ++visibleIndex)
    {
        if (visibleSampleEntryIndices_[static_cast<std::size_t>(visibleIndex)] == sourceIndex)
        {
            sampleBrowserListBox_.selectRow(visibleIndex);
            break;
        }
    }

    updateBrowserLibraryControls();
}

void AudiocityAudioProcessorEditor::applySelectedBrowserTags()
{
    const auto row = sampleBrowserListBox_.getSelectedRow();
    if (row < 0 || row >= static_cast<int>(visibleSampleEntryIndices_.size()))
    {
        updateBrowserLibraryControls();
        return;
    }

    const auto sourceIndex = visibleSampleEntryIndices_[static_cast<std::size_t>(row)];
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(allSampleEntries_.size()))
    {
        updateBrowserLibraryControls();
        return;
    }

    juce::StringArray tags;
    tags.addTokens(sampleBrowserTagsEditor_.getText(), ",", "\"");
    tags.trim();
    tags.removeEmptyStrings();

    const auto file = allSampleEntries_[static_cast<std::size_t>(sourceIndex)].file;
    processor_.setLibraryTags(file.getFullPathName(), tags);
    refreshBrowserEntryLibraryFlags();
    rebuildVisibleSampleList();

    for (int visibleIndex = 0; visibleIndex < static_cast<int>(visibleSampleEntryIndices_.size()); ++visibleIndex)
    {
        if (visibleSampleEntryIndices_[static_cast<std::size_t>(visibleIndex)] == sourceIndex)
        {
            sampleBrowserListBox_.selectRow(visibleIndex);
            break;
        }
    }

    updateBrowserLibraryControls();
}

void AudiocityAudioProcessorEditor::rebuildVisibleSampleList()
{
    visibleSampleEntryIndices_.clear();
    refreshBrowserEntryLibraryFlags();
    refreshSampleBrowserTagFilter();

    const auto needle = sampleBrowserFilterEditor_.getText().trim().toLowerCase();
    const auto tagNeedle = needle.startsWithChar('#') ? needle.substring(1) : needle;
    const auto selectedTagFilter = sampleBrowserTagFilterCombo_.getSelectedId() > 1
        ? sampleBrowserTagFilterCombo_.getText().trim()
        : juce::String();
    const auto favoritesOnly = sampleBrowserFavoritesOnlyToggle_.getToggleState();
    const auto recentOnly = sampleBrowserRecentOnlyToggle_.getToggleState();
    int favoriteCount = 0;
    int recentCount = 0;
    for (int i = 0; i < static_cast<int>(allSampleEntries_.size()); ++i)
    {
        const auto& item = allSampleEntries_[static_cast<std::size_t>(i)];
        if (item.isFavorite)
            ++favoriteCount;
        if (item.isRecent)
            ++recentCount;

        const auto matches = needle.isEmpty()
            || item.fileNameLower.contains(needle)
            || item.relativePathLower.contains(needle)
            || (!tagNeedle.isEmpty() && item.tagsLower.contains(tagNeedle));

        const auto matchesLibraryFilters = (!favoritesOnly || item.isFavorite)
            && (!recentOnly || item.isRecent);
        auto matchesTagFilter = selectedTagFilter.isEmpty();
        for (const auto& tag : item.tags)
        {
            if (tag.equalsIgnoreCase(selectedTagFilter))
            {
                matchesTagFilter = true;
                break;
            }
        }

        if (matches && matchesLibraryFilters && matchesTagFilter)
            visibleSampleEntryIndices_.push_back(i);
    }

    const auto sortMode = sampleBrowserSortCombo_.getSelectedId();
    std::sort(visibleSampleEntryIndices_.begin(), visibleSampleEntryIndices_.end(),
        [this, sortMode](const int lhs, const int rhs)
        {
            const auto& a = allSampleEntries_[static_cast<std::size_t>(lhs)];
            const auto& b = allSampleEntries_[static_cast<std::size_t>(rhs)];

            if (sortMode == 3)
            {
                if (a.recentRank != b.recentRank)
                {
                    if (a.recentRank < 0)
                        return false;
                    if (b.recentRank < 0)
                        return true;
                    return a.recentRank < b.recentRank;
                }

                if (a.fileNameLower == b.fileNameLower)
                    return a.relativePathLower < b.relativePathLower;
                return a.fileNameLower < b.fileNameLower;
            }

            if (sortMode == 2)
            {
                if (a.relativePathLower == b.relativePathLower)
                    return a.fileNameLower < b.fileNameLower;
                return a.relativePathLower < b.relativePathLower;
            }

            if (a.fileNameLower == b.fileNameLower)
                return a.relativePathLower < b.relativePathLower;
            return a.fileNameLower < b.fileNameLower;
        });

    sampleBrowserListBox_.updateContent();
    sampleBrowserListBox_.repaint();

    if (sampleRootFolderPath_.isEmpty())
        sampleBrowserCountLabel_.setText("No folder selected", juce::dontSendNotification);
    else
    {
        auto countText = juce::String(visibleSampleEntryIndices_.size())
            + " / " + juce::String(allSampleEntries_.size()) + " items";
        if (favoriteCount > 0 || recentCount > 0)
            countText += " | " + juce::String(favoriteCount) + " fav | " + juce::String(recentCount) + " recent";

        sampleBrowserCountLabel_.setText(countText, juce::dontSendNotification);
    }

    updateBrowserLibraryControls();
}

void AudiocityAudioProcessorEditor::loadSampleFromBrowserRow(const int row)
{
    if (row < 0 || row >= static_cast<int>(visibleSampleEntryIndices_.size()))
        return;

    const auto sourceIndex = visibleSampleEntryIndices_[static_cast<std::size_t>(row)];
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(allSampleEntries_.size()))
        return;

    lastPreviewedBrowserSourceIndex_ = sourceIndex;

    const auto& file = allSampleEntries_[static_cast<std::size_t>(sourceIndex)].file;
    loadFileAsInstrument(file, [this](const bool loaded)
    {
        if (!loaded)
            return;

        clearSelectedPresetAfterSourceLoad();
        tabBar_.setCurrentTabIndex(0);
        currentTabIndex_ = 0;
        processor_.setEditorTabIndex(currentTabIndex_);
        updateTabVisibility();
        resized();
        repaint();
        refreshUI(true);
    });
}

void AudiocityAudioProcessorEditor::previewSampleFromBrowserRow(const int row, const bool forceRestart)
{
    if (row < 0 || row >= static_cast<int>(visibleSampleEntryIndices_.size()))
        return;

    const auto sourceIndex = visibleSampleEntryIndices_[static_cast<std::size_t>(row)];
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(allSampleEntries_.size()))
        return;

    if (!forceRestart && sourceIndex == lastPreviewedBrowserSourceIndex_)
        return;

    lastPreviewedBrowserSourceIndex_ = sourceIndex;

    const auto& file = allSampleEntries_[static_cast<std::size_t>(sourceIndex)].file;
    if (file.getFileExtension().equalsIgnoreCase(".sfz"))
        return;

    processor_.previewSampleFromFile(file);
    updateGeneratePreviewButtonText();
}

void AudiocityAudioProcessorEditor::paintSampleBrowserPane(
    juce::Graphics& g, const juce::Rectangle<int> browserArea) const
{
    g.setColour(juce::Colour(0xff252538));
    g.fillRoundedRectangle(browserArea.toFloat(), 6.0f);
    g.setColour(juce::Colour(0xff3a3a52));
    g.drawRoundedRectangle(browserArea.toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void AudiocityAudioProcessorEditor::updatePerformanceStripStatus(const float outputLeftPeak,
                                                                const float outputRightPeak)
{
    const bool previewPlaying = processor_.isGeneratedWaveformPreviewPlaying() || processor_.isSamplePreviewPlaying();
    const int activeVoices = processor_.getActiveVoiceCount();
    const auto stateText = previewPlaying ? juce::String("Preview")
                                          : (activeVoices > 0 ? juce::String("Live") : juce::String("Ready"));

    playerStatusLabel_.setText(
        stateText
            + "  V" + juce::String(activeVoices)
            + "  Out " + formatCompactPeakDb(outputLeftPeak)
            + " / " + formatCompactPeakDb(outputRightPeak)
            + " dB",
        juce::dontSendNotification);
}

bool AudiocityAudioProcessorEditor::shouldShowPersistentPerformanceStrip() const noexcept
{
    return currentTabIndex_ != 3 && currentTabIndex_ != 6;
}

bool AudiocityAudioProcessorEditor::shouldShowPersistentBrowserRail() const noexcept
{
    if (currentTabIndex_ == 1 || currentTabIndex_ == 3 || currentTabIndex_ == 6)
        return false;

    if (currentTabIndex_ == 0)
    {
        const auto sampleLayoutMode = resolveSampleLayoutModeForWidth(computeResponsiveContentWidth(getWidth()));
        if (!sampleBrowserRailEnabled_ || sampleLayoutMode == SampleLayoutMode::inlineStack)
            return false;

        if (sampleLayoutMode == SampleLayoutMode::browserWorkspaceInspector)
            return true;

        return !sampleInspectorRailEnabled_;
    }

    return true;
}

bool AudiocityAudioProcessorEditor::shouldShowSampleInspectorRail() const noexcept
{
    if (currentTabIndex_ != 0 || !sampleInspectorRailEnabled_)
        return false;

    const auto sampleLayoutMode = resolveSampleLayoutModeForWidth(computeResponsiveContentWidth(getWidth()));
    return sampleLayoutMode != SampleLayoutMode::inlineStack
        && (sampleLayoutMode == SampleLayoutMode::browserWorkspaceInspector
            || !sampleBrowserRailEnabled_);
}

bool AudiocityAudioProcessorEditor::shouldShowWideSampleInspectorMode() const noexcept
{
    if (!shouldShowSampleInspectorRail())
        return false;

    const auto sampleLayoutMode = resolveSampleLayoutModeForWidth(computeResponsiveContentWidth(getWidth()));
    return sampleLayoutMode == SampleLayoutMode::browserWorkspaceInspector
        && shouldShowPersistentBrowserRail();
}

bool AudiocityAudioProcessorEditor::shouldShowSampleProgramMapInspector() const noexcept
{
    if (!shouldShowWideSampleInspectorMode() || !processor_.hasImportedProgram())
        return false;

    const int filterModCardHeight = getSampleInspectorCardHeight(
        sampleInspectorFilterModExpanded_,
        kExpandedSampleFilterModInspectorCardHeight);
    const int effectsCardHeight = getSampleInspectorCardHeight(
        sampleInspectorEffectsExpanded_,
        kExpandedSampleEffectsInspectorCardHeight);

    const int availableWithProgramMap = getAvailableSampleAdvancedInspectorHeight(true);
    const bool anyAdvancedFitsWithProgramMap = availableWithProgramMap >= filterModCardHeight
        || availableWithProgramMap >= effectsCardHeight;
    if (anyAdvancedFitsWithProgramMap)
        return true;

    const int availableWithoutProgramMap = getAvailableSampleAdvancedInspectorHeight(false);
    const bool anyAdvancedFitsWithoutProgramMap = availableWithoutProgramMap >= filterModCardHeight
        || availableWithoutProgramMap >= effectsCardHeight;
    return !anyAdvancedFitsWithoutProgramMap;
}

int AudiocityAudioProcessorEditor::getAvailableSampleAdvancedInspectorHeight(const bool reserveProgramMap) const noexcept
{
    constexpr int kSampleInfoCardHeight = 278;
    constexpr int kSampleProgramMapCardHeight = 188;
    constexpr int kTopGap = 10;
    constexpr int kMargin = 14;

    int availableHeight = getHeight() - (kMargin * 2) - kEditorTabBarHeight - kEditorTabBarGap;
    if (shouldShowPersistentPerformanceStrip())
        availableHeight -= computePersistentPerformanceStripHeight(availableHeight) + kTopGap;

    availableHeight -= kSamplePresetBarHeight + kTopGap;
    availableHeight -= kSampleInfoCardHeight + kTopGap;
    if (reserveProgramMap)
        availableHeight -= kSampleProgramMapCardHeight + kTopGap;
    availableHeight -= kSampleOutputInspectorCardHeight + kTopGap;

    return availableHeight;
}

bool AudiocityAudioProcessorEditor::shouldShowSampleFilterModInspector() const noexcept
{
    if (!shouldShowWideSampleInspectorMode())
        return false;

    constexpr int kTopGap = 10;

    const int availableHeight = getAvailableSampleAdvancedInspectorHeight(shouldShowSampleProgramMapInspector());
    const int filterModCardHeight = getSampleInspectorCardHeight(
        sampleInspectorFilterModExpanded_,
        kExpandedSampleFilterModInspectorCardHeight);
    const int effectsCardHeight = getSampleInspectorCardHeight(
        sampleInspectorEffectsExpanded_,
        kExpandedSampleEffectsInspectorCardHeight);
    const int requiredForBoth = filterModCardHeight + kTopGap + effectsCardHeight;
    return availableHeight >= requiredForBoth || availableHeight >= filterModCardHeight;
}

bool AudiocityAudioProcessorEditor::shouldShowSampleEffectsInspector() const noexcept
{
    if (!shouldShowWideSampleInspectorMode())
        return false;

    constexpr int kTopGap = 10;

    const int availableHeight = getAvailableSampleAdvancedInspectorHeight(shouldShowSampleProgramMapInspector());
    const int filterModCardHeight = getSampleInspectorCardHeight(
        sampleInspectorFilterModExpanded_,
        kExpandedSampleFilterModInspectorCardHeight);
    const int effectsCardHeight = getSampleInspectorCardHeight(
        sampleInspectorEffectsExpanded_,
        kExpandedSampleEffectsInspectorCardHeight);
    const int requiredForBoth = filterModCardHeight + kTopGap + effectsCardHeight;
    if (availableHeight >= requiredForBoth)
        return true;

    return availableHeight >= effectsCardHeight
        && availableHeight < filterModCardHeight;
}

void AudiocityAudioProcessorEditor::layoutSampleBrowserArea(juce::Rectangle<int> browserArea,
                                                            const bool compactLayout)
{
    browserArea = browserArea.reduced(8, 6);

    if (compactLayout)
    {
        auto header = browserArea.removeFromTop(28);
        if (sampleBrowserCancelButton_.isVisible())
        {
            sampleBrowserCancelButton_.setBounds(header.removeFromRight(62));
            header.removeFromRight(6);
        }
        else
        {
            sampleBrowserCancelButton_.setBounds({});
        }

        sampleBrowserRefreshButton_.setBounds(header.removeFromRight(72));
        header.removeFromRight(6);
        sampleBrowserChooseRootButton_.setBounds(header.removeFromRight(30));
        header.removeFromRight(6);
        sampleBrowserRootLabel_.setBounds(header);

        browserArea.removeFromTop(6);
        sampleBrowserFilterEditor_.setBounds(browserArea.removeFromTop(28));

        browserArea.removeFromTop(6);
        auto quickRow = browserArea.removeFromTop(28);
        auto quickLeft = quickRow.removeFromLeft((quickRow.getWidth() - 6) / 2);
        sampleBrowserSortCombo_.setBounds(quickLeft);
        quickRow.removeFromLeft(6);
        sampleBrowserFavoriteButton_.setBounds(quickRow);

        browserArea.removeFromTop(6);
        auto toggleRow = browserArea.removeFromTop(28);
        auto toggleLeft = toggleRow.removeFromLeft((toggleRow.getWidth() - 6) / 2);
        sampleBrowserFavoritesOnlyToggle_.setBounds(toggleLeft);
        toggleRow.removeFromLeft(6);
        sampleBrowserRecentOnlyToggle_.setBounds(toggleRow);

        browserArea.removeFromTop(6);
        auto listArea = browserArea;
        auto statusRow = listArea.removeFromBottom(20);
        sampleBrowserCountLabel_.setBounds(statusRow.removeFromLeft(statusRow.getWidth() / 2));
        sampleBrowserPreviewLabel_.setBounds(statusRow);
        listArea.removeFromBottom(4);
        sampleBrowserListBox_.setBounds(listArea);

        sampleBrowserBookmarkCombo_.setBounds({});
        sampleBrowserAddBookmarkButton_.setBounds({});
        sampleBrowserRemoveBookmarkButton_.setBounds({});
        sampleBrowserTagFilterCombo_.setBounds({});
        sampleBrowserTagsEditor_.setBounds({});
        sampleBrowserApplyTagsButton_.setBounds({});
        return;
    }

    auto header = browserArea.removeFromTop(28);
    if (sampleBrowserCancelButton_.isVisible())
    {
        sampleBrowserCancelButton_.setBounds(header.removeFromRight(76));
        header.removeFromRight(6);
    }
    else
    {
        sampleBrowserCancelButton_.setBounds({});
    }

    sampleBrowserRefreshButton_.setBounds(header.removeFromRight(84));
    header.removeFromRight(6);
    sampleBrowserChooseRootButton_.setBounds(header.removeFromRight(30));
    header.removeFromRight(6);
    sampleBrowserRootLabel_.setBounds(header);

    browserArea.removeFromTop(6);
    auto bookmarkRow = browserArea.removeFromTop(28);
    sampleBrowserRemoveBookmarkButton_.setBounds(bookmarkRow.removeFromRight(78));
    bookmarkRow.removeFromRight(6);
    sampleBrowserAddBookmarkButton_.setBounds(bookmarkRow.removeFromRight(98));
    bookmarkRow.removeFromRight(6);
    sampleBrowserBookmarkCombo_.setBounds(bookmarkRow);

    browserArea.removeFromTop(6);
    auto filterRow = browserArea.removeFromTop(28);
    constexpr int sortWidth = 104;
    constexpr int favoriteWidth = 86;
    constexpr int favoritesOnlyWidth = 96;
    constexpr int recentOnlyWidth = 78;
    constexpr int filterControlGap = 6;
    const auto controlWidth = sortWidth + favoriteWidth + favoritesOnlyWidth + recentOnlyWidth + (4 * filterControlGap);
    auto filterWidth = juce::jmax(120, filterRow.getWidth() - controlWidth);
    sampleBrowserFilterEditor_.setBounds(filterRow.removeFromLeft(filterWidth));
    filterRow.removeFromLeft(filterControlGap);
    sampleBrowserSortCombo_.setBounds(filterRow.removeFromLeft(sortWidth));
    filterRow.removeFromLeft(filterControlGap);
    sampleBrowserFavoriteButton_.setBounds(filterRow.removeFromLeft(favoriteWidth));
    filterRow.removeFromLeft(filterControlGap);
    sampleBrowserFavoritesOnlyToggle_.setBounds(filterRow.removeFromLeft(favoritesOnlyWidth));
    filterRow.removeFromLeft(filterControlGap);
    sampleBrowserRecentOnlyToggle_.setBounds(filterRow.removeFromLeft(recentOnlyWidth));

    browserArea.removeFromTop(6);
    auto tagRow = browserArea.removeFromTop(28);
    sampleBrowserApplyTagsButton_.setBounds(tagRow.removeFromRight(96));
    tagRow.removeFromRight(6);
    sampleBrowserTagFilterCombo_.setBounds(tagRow.removeFromRight(132));
    tagRow.removeFromRight(6);
    sampleBrowserTagsEditor_.setBounds(tagRow);

    browserArea.removeFromTop(6);
    auto listArea = browserArea;
    auto statusRow = listArea.removeFromBottom(20);
    sampleBrowserCountLabel_.setBounds(statusRow.removeFromLeft(statusRow.getWidth() / 2));
    sampleBrowserPreviewLabel_.setBounds(statusRow);
    listArea.removeFromBottom(4);
    sampleBrowserListBox_.setBounds(listArea);
}

void AudiocityAudioProcessorEditor::layoutPlayerPerformanceArea(juce::Rectangle<int> area, const bool compactLayout)
{
    area = area.reduced(8, 6);

    if (compactLayout)
    {
        playerKeyboardLabel_.setText("Performance Strip", juce::dontSendNotification);
        playerPadsLabel_.setText("Quick Pads", juce::dontSendNotification);

        const int keyboardPanelHeight = juce::jlimit(78, 116, (area.getHeight() * 53) / 100);
        auto keyboardPanel = area.removeFromTop(keyboardPanelHeight).reduced(8, 8);
        auto keyboardHeader = keyboardPanel.removeFromTop(34);
        auto keyboardTitleRow = keyboardHeader.removeFromTop(16);
        playerOpenButton_.setBounds(keyboardTitleRow.removeFromRight(92));
        playerKeyboardLabel_.setBounds(keyboardTitleRow);
        keyboardHeader.removeFromTop(2);
        playerStatusLabel_.setBounds(keyboardHeader.removeFromTop(14));
        keyboardPanel.removeFromTop(4);
        playerKeyboardViewport_.setBounds(keyboardPanel);
        updatePlayerKeyboardSizing();

        area.removeFromTop(6);
        auto padsPanel = area.reduced(8, 8);
        playerPadsLabel_.setBounds(padsPanel.removeFromTop(16));
        padsPanel.removeFromTop(4);

        constexpr int padGap = 6;
        const int padCellWidth = juce::jmax(64, (padsPanel.getWidth() - (kPlayerPadCount - 1) * padGap) / kPlayerPadCount);
        const int padCellHeight = juce::jmax(34, padsPanel.getHeight());

        for (int i = 0; i < kPlayerPadCount; ++i)
        {
            auto cell = juce::Rectangle<int>(
                padsPanel.getX() + i * (padCellWidth + padGap),
                padsPanel.getY(),
                padCellWidth,
                padCellHeight);
            playerPadButtons_[static_cast<std::size_t>(i)].setBounds(cell);
            playerPadAssignButtons_[static_cast<std::size_t>(i)].setBounds({});
        }

        return;
    }

    playerKeyboardLabel_.setText("Piano", juce::dontSendNotification);
    playerStatusLabel_.setBounds({});
    playerPadsLabel_.setText("Drum Pads", juce::dontSendNotification);
    playerOpenButton_.setBounds({});

    auto keyboardPanel = area.removeFromTop(computePlayerKeyboardPanelHeight(area.getWidth()));
    keyboardPanel.reduce(10, 10);

    auto keyboardHeader = keyboardPanel.removeFromTop(26);
    playerKeyboardLabel_.setBounds(keyboardHeader);

    keyboardPanel.removeFromTop(6);
    playerKeyboardViewport_.setBounds(keyboardPanel);
    updatePlayerKeyboardSizing();

    area.removeFromTop(10);
    auto padsPanel = area.reduced(10, 10);
    playerPadsLabel_.setBounds(padsPanel.removeFromTop(22));
    padsPanel.removeFromTop(6);

    constexpr int kPadCols = 4;
    const int kPadRows = (kPlayerPadCount + kPadCols - 1) / kPadCols;
    const int padGap = 8;
    const int padCellWidth = juce::jmax(80, (padsPanel.getWidth() - (kPadCols - 1) * padGap) / kPadCols);
    constexpr int kPreferredPadHeight = 96;
    const int availablePadHeight = (padsPanel.getHeight() - (kPadRows - 1) * padGap) / kPadRows;
    const int padCellHeight = juce::jlimit(72, kPreferredPadHeight, availablePadHeight);

    for (int i = 0; i < kPlayerPadCount; ++i)
    {
        const int row = i / kPadCols;
        const int col = i % kPadCols;
        auto cell = juce::Rectangle<int>(
            padsPanel.getX() + col * (padCellWidth + padGap),
            padsPanel.getY() + row * (padCellHeight + padGap),
            padCellWidth,
            padCellHeight);

        playerPadButtons_[static_cast<std::size_t>(i)].setBounds(cell);

        constexpr int kAssignW = 28;
        constexpr int kAssignH = 20;
        constexpr int kAssignPad = 6;
        const auto assignBounds = juce::Rectangle<int>(
            cell.getRight() - kAssignW - kAssignPad,
            cell.getBottom() - kAssignH - kAssignPad,
            kAssignW,
            kAssignH);
        playerPadAssignButtons_[static_cast<std::size_t>(i)].setBounds(assignBounds);
        playerPadAssignButtons_[static_cast<std::size_t>(i)].toFront(false);
    }
}

void AudiocityAudioProcessorEditor::paintPlayerPane(juce::Graphics& g, juce::Rectangle<int> area, const bool compactLayout) const
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

    auto keyboardPanel = area.removeFromTop(computePlayerKeyboardPanelHeight(area.getWidth()));
    auto padsPanel = area.withTrimmedTop(10);

    paintPanel(keyboardPanel);
    paintPanel(padsPanel);
}

void AudiocityAudioProcessorEditor::paintAboutPane(juce::Graphics& g, juce::Rectangle<int> area) const
{
    area = area.reduced(14, 14);

    constexpr int kIconSize = 96;
    constexpr int kButtonHeight = 36;
    constexpr int kButtonBottomPadding = 22;
    constexpr int kFooterHeight = 26;
    constexpr int kFooterGap = 14;
    constexpr int kTableGap = 16;
    constexpr int kTableTitleHeight = 28;
    constexpr int kTableHeaderHeight = 28;
    constexpr int kFeatureLabelWidth = 108;
    constexpr int kShortcutLabelWidth = 90;

    struct AboutRow
    {
        const char* label;
        const char* detail;
    };

    constexpr std::array<AboutRow, 9> featureRows{{
        { "Loading", "Import WAV, AIFF, REX and RX2 samples" },
        { "Modulation", "Shape pitch, amp and filter motion with LFOs" },
        { "Effects", "Use reverb, delay, autopan and saturation" },
        { "Synthesis", "Sketch sine, saw, square, triangle and pulse waves" },
        { "Capture", "Record, cut, trim and normalize live audio" },
        { "Browser", "Search and preview the sample library quickly" },
        { "Performance", "Play from piano keys and assignable drum pads" },
        { "Presets", "Save, rename and delete reusable patches" },
        { "MIDI", "Map hardware controls with MIDI CC learn" }
    }};

    constexpr std::array<AboutRow, 9> shortcutRows{{
        { "1-7", "Switch between the seven top-level tabs" },
        { "Ctrl+O", "Open a sample file" },
        { "Ctrl+S", "Save the current state to disk" },
        { "Ctrl+Shift+S", "Save the current preset" },
        { "Ctrl+Alt+D", "Toggle the Sample-page diagnostics panel" },
        { "Ctrl+Z / Y", "Undo or redo sample, parameter, or mapping edits" },
        { "Space", "Play or stop generated waveform preview" },
        { "Enter / Esc", "Load selected browser row or panic audio" },
        { "Mapping", "Ctrl+N creates a zone, Ctrl+A selects all zones, Ctrl+D duplicates, Ctrl+Shift+D splits, Delete removes selected zones" }
    }};

    const int iconY = area.getY() + 20;
    const int iconX = area.getCentreX() - kIconSize / 2;

    if (aboutIconImage_.isValid())
        g.drawImage(aboutIconImage_,
                    juce::Rectangle<float>(static_cast<float>(iconX), static_cast<float>(iconY),
                                           static_cast<float>(kIconSize), static_cast<float>(kIconSize)),
                    juce::RectanglePlacement::centred);

    int textY = iconY + kIconSize + 16;

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(28.0f)).boldened());
    g.drawText("Audiocity", area.getX(), textY, area.getWidth(), 34, juce::Justification::centredTop);
    textY += 38;

    g.setColour(juce::Colour(0xffaab0cc));
    g.setFont(juce::Font(juce::FontOptions(15.0f)));
    g.drawText("A high-performance sampler instrument", area.getX(), textY, area.getWidth(), 22,
               juce::Justification::centredTop);
    textY += 28;

    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText("VST3 Plugin & Standalone Application", area.getX(), textY, area.getWidth(), 20,
               juce::Justification::centredTop);
    textY += 32;

    // Version
    g.setColour(juce::Colour(0xff61d9ff));
    g.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    g.drawText("Version " + juce::String(JucePlugin_VersionString),
               area.getX(), textY, area.getWidth(), 20,
               juce::Justification::centredTop);
    textY += 28;

    auto lowerArea = area.withTrimmedTop(textY - area.getY());
    lowerArea.removeFromBottom(kButtonHeight + kButtonBottomPadding);
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
        auto titleArea = content.removeFromTop(kTableTitleHeight);
        g.setColour(juce::Colour(0xffdfe6ff));
        g.setFont(juce::Font(juce::FontOptions(14.5f)).boldened());
        g.drawText(title, titleArea, juce::Justification::centred, false);

        content.removeFromTop(6);
        auto headerArea = content.removeFromTop(kTableHeaderHeight);
        g.setColour(juce::Colour(0xff23273a));
        g.fillRoundedRectangle(headerArea.toFloat(), 6.0f);

        auto headerLeft = headerArea.removeFromLeft(leftColumnWidth);
        headerArea.removeFromLeft(8);
        g.setColour(juce::Colour(0xff61d9ff));
        g.setFont(juce::Font(juce::FontOptions(11.5f)).boldened());
        g.drawText(leftHeader, headerLeft.reduced(8, 0), juce::Justification::centredLeft, false);
        g.drawText(rightHeader, headerArea.reduced(8, 0), juce::Justification::centredLeft, false);

        auto rowsArea = content;
        const int dividerX = rowsArea.getX() + leftColumnWidth;
        g.setColour(juce::Colour(0x30dfe6ff));
        g.drawLine(static_cast<float>(dividerX), static_cast<float>(headerLeft.getY()),
                   static_cast<float>(dividerX), static_cast<float>(rowsArea.getBottom()), 1.0f);

        const auto rowCount = static_cast<int>(rows.size());
        const int rowHeight = juce::jlimit(28, 40, rowsArea.getHeight() / juce::jmax(1, rowCount));

        for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex)
        {
            auto rowArea = rowsArea.removeFromTop(rowHeight);
            if (rowIndex > 0)
            {
                g.setColour(juce::Colour(0x203f4762));
                g.drawHorizontalLine(rowArea.getY(), static_cast<float>(rowArea.getX()),
                                     static_cast<float>(rowArea.getRight()));
            }

            auto rowLeft = rowArea.removeFromLeft(leftColumnWidth).reduced(8, 4);
            auto rowRight = rowArea.reduced(8, 4);

            g.setColour(juce::Colour(0xfff4f6ff));
            g.setFont(juce::Font(juce::FontOptions(11.5f)).boldened());
            g.drawFittedText(rows[static_cast<std::size_t>(rowIndex)].label, rowLeft,
                             juce::Justification::centredLeft, 1, 0.9f);

            g.setColour(juce::Colour(0xff9ba5c3));
            g.setFont(juce::Font(juce::FontOptions(11.5f)));
            g.drawFittedText(rows[static_cast<std::size_t>(rowIndex)].detail, rowRight,
                             juce::Justification::centredLeft, 2, 0.82f);
        }
    };

    auto featuresArea = lowerArea.removeFromLeft((lowerArea.getWidth() - kTableGap) / 2);
    lowerArea.removeFromLeft(kTableGap);
    auto shortcutsArea = lowerArea;

    drawTable(featuresArea, "Key Features", "Area", "Details", kFeatureLabelWidth, featureRows);
    drawTable(shortcutsArea, "Keyboard Shortcuts", "Keys", "Action", kShortcutLabelWidth, shortcutRows);

    g.setColour(juce::Colour(0xffdfe6ff));
    g.setFont(juce::Font(juce::FontOptions(14.0f)).boldened());
    g.drawFittedText("Copyright (c) 2026 Michael A. McCloskey | Released under the MIT License",
                     footerArea, juce::Justification::centred, 1, 0.82f);
}

// ─── Layout ────────────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::resized()
{
    constexpr int kMargin   = 14;
    constexpr int kDial     = 78;
    constexpr int kDialH    = 96;
    constexpr int kGrpPadH  = 12;
    constexpr int kGrpPadV  = 8;
    constexpr int kGrpHdr   = 22;
    constexpr int kGrpGap   = 10;
    constexpr int kDialGap  = 6;
    constexpr int kModeStackW = 104;
    constexpr int kStackLabelH = 16;
    constexpr int kStackButtonH = 22;
    constexpr int kStackGap = 2;
    constexpr int kStackColGap = kDialGap + 8;
    constexpr int kRowH     = kGrpHdr + kGrpPadV + kDialH + kGrpPadV;  // 134

    updateTabVisibility();

    auto content = getLocalBounds().reduced(kMargin);
    tabBar_.setBounds(content.removeFromTop(kEditorTabBarHeight));
    content.removeFromTop(kEditorTabBarGap);
    auto area = content;
    juce::Rectangle<int> browserRailArea;
    juce::Rectangle<int> performanceStripArea;
    const bool showBrowserRail = shouldShowPersistentBrowserRail();
    const bool showPerformanceStrip = shouldShowPersistentPerformanceStrip();

    if (showPerformanceStrip)
    {
        constexpr int kPerformanceStripGap = 10;
        const int kPerformanceStripHeight = computePersistentPerformanceStripHeight(area.getHeight());
        performanceStripArea = area.removeFromBottom(kPerformanceStripHeight);
        area.removeFromBottom(kPerformanceStripGap);
    }

    if (showBrowserRail)
    {
        constexpr int kBrowserRailGap = 10;
        const int kBrowserRailWidth = computePersistentBrowserRailWidth(area.getWidth());
        browserRailArea = area.removeFromLeft(kBrowserRailWidth);
        area.removeFromLeft(kBrowserRailGap);
    }

    groupBoxes_.clear();

    if (currentTabIndex_ == 1)
    {
        layoutSampleBrowserArea(area, false);
        if (showPerformanceStrip)
            layoutPlayerPerformanceArea(performanceStripArea, true);
        return;
    }

    if (showBrowserRail)
    {
        const bool showBrowserCancelButton = sampleScanInProgress_.load(std::memory_order_relaxed);
        sampleBrowserRootLabel_.setVisible(true);
        sampleBrowserChooseRootButton_.setVisible(true);
        sampleBrowserRefreshButton_.setVisible(true);
        sampleBrowserCancelButton_.setVisible(showBrowserCancelButton);
        sampleBrowserFilterEditor_.setVisible(true);
        sampleBrowserSortCombo_.setVisible(true);
        sampleBrowserFavoriteButton_.setVisible(true);
        sampleBrowserFavoritesOnlyToggle_.setVisible(true);
        sampleBrowserRecentOnlyToggle_.setVisible(true);
        sampleBrowserListBox_.setVisible(true);
        sampleBrowserCountLabel_.setVisible(true);
        sampleBrowserPreviewLabel_.setVisible(true);
        sampleBrowserListBox_.setRowHeight(54);
        layoutSampleBrowserArea(browserRailArea, true);
    }

    if (currentTabIndex_ == 2)
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

        auto header = mappingArea.removeFromTop(30);
        mappingRefreshButton_.setBounds(header.removeFromRight(86));
        header.removeFromRight(6);
        mappingDeleteZoneButton_.setBounds(header.removeFromRight(72));
        header.removeFromRight(6);
        mappingSplitZoneButton_.setBounds(header.removeFromRight(68));
        header.removeFromRight(6);
        mappingDuplicateZoneButton_.setBounds(header.removeFromRight(90));
        header.removeFromRight(6);
        mappingCreateZoneButton_.setBounds(header.removeFromRight(82));
        header.removeFromRight(8);
        mappingSummaryLabel_.setBounds(header);

        mappingArea.removeFromTop(4);
        const auto mappingOverviewHeight = juce::jlimit(
            kMappingOverviewMinimumHeight,
            kMappingOverviewPreferredHeight,
            juce::jmax(kMappingOverviewMinimumHeight, mappingArea.getHeight() - kMappingEditRequiredHeight - kMappingOverviewGap));
        mappingOverview_.setBounds(mappingArea.removeFromTop(mappingOverviewHeight));
        mappingArea.removeFromTop(kMappingOverviewGap);

        const auto detailWidth = juce::jlimit(220, 360, mappingArea.getWidth() / 3);
        auto details = mappingArea.removeFromRight(detailWidth);
        mappingArea.removeFromRight(10);
        mappingZoneListBox_.setBounds(mappingArea);

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

        layoutEditRow(editPanel, mappingEditKeyLowLabel_, mappingEditKeyLowSlider_);
        layoutEditRow(editPanel, mappingEditKeyHighLabel_, mappingEditKeyHighSlider_);
        layoutEditRow(editPanel, mappingEditVelocityLowLabel_, mappingEditVelocityLowSlider_);
        layoutEditRow(editPanel, mappingEditVelocityHighLabel_, mappingEditVelocityHighSlider_);
        layoutPairedEditRow(editPanel, mappingEditVelocityFadeInLabel_, mappingEditVelocityFadeInLowSlider_, mappingEditVelocityFadeInHighSlider_);
        layoutPairedEditRow(editPanel, mappingEditVelocityFadeOutLabel_, mappingEditVelocityFadeOutLowSlider_, mappingEditVelocityFadeOutHighSlider_);
        layoutEditRow(editPanel, mappingEditRootLabel_, mappingEditRootSlider_);
        layoutEditRow(editPanel, mappingEditSampleStartLabel_, mappingEditSampleStartSlider_);
        layoutEditRow(editPanel, mappingEditSampleEndLabel_, mappingEditSampleEndSlider_);
        layoutEditRow(editPanel, mappingEditLoopStartLabel_, mappingEditLoopStartSlider_);
        layoutEditRow(editPanel, mappingEditLoopEndLabel_, mappingEditLoopEndSlider_);
        layoutEditRow(editPanel, mappingEditGainLabel_, mappingEditGainSlider_);
        layoutEditRow(editPanel, mappingEditPanLabel_, mappingEditPanSlider_);
        layoutEditRow(editPanel, mappingEditRoundRobinGroupLabel_, mappingEditRoundRobinGroupSlider_);
        layoutEditRow(editPanel, mappingEditRoundRobinPositionLabel_, mappingEditRoundRobinPositionSlider_);
        layoutEditComboRow(editPanel, mappingEditRoundRobinModeLabel_, mappingEditRoundRobinModeCombo_);
        layoutEditRow(editPanel, mappingEditChokeLabel_, mappingEditChokeSlider_);
        layoutEditComboRow(editPanel, mappingEditTriggerLabel_, mappingEditTriggerCombo_);
        layoutEditComboRow(editPanel, mappingEditLoopLabel_, mappingEditLoopCombo_);

        auto applyRow = editPanel.removeFromTop(24);
        mappingEditApplyButton_.setBounds(applyRow.removeFromLeft(110));
        applyRow.removeFromLeft(8);
        mappingEditStatusLabel_.setBounds(applyRow);

        details.removeFromTop(4);
        mappingDetailsText_.setBounds(details);
        if (showPerformanceStrip)
            layoutPlayerPerformanceArea(performanceStripArea, true);
        return;
    }

    if (currentTabIndex_ == 3)
    {
        layoutPlayerPerformanceArea(area, false);
        return;
    }

    if (currentTabIndex_ == 4)
    {
        auto genArea = area.reduced(8, 6);

        auto waveformArea = genArea.removeFromTop(juce::jmax(200, genArea.getHeight() / 2));
        generateWaveformView_.setBounds(waveformArea);
        genArea.removeFromTop(12);

        auto waveButtons = genArea.removeFromTop(32);
        constexpr int kBtnW = 64;
        constexpr int kBtnGap = 6;
        generateLoadAsSampleButton_.setBounds(waveButtons.removeFromRight(140));
        waveButtons.removeFromRight(kBtnGap);
        generateSineButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generateRampButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generateSquareButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generateSawtoothButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generateTriangleButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generatePulseButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generateRandomButton_.setBounds(waveButtons.removeFromLeft(kBtnW));

        genArea.removeFromTop(10);
        auto settingsRow = genArea.removeFromTop(32);
        generateSamplesLabel_.setBounds(settingsRow.removeFromLeft(58));
        generateSamplesCombo_.setBounds(settingsRow.removeFromLeft(98));
        settingsRow.removeFromLeft(14);
        generateBitDepthLabel_.setBounds(settingsRow.removeFromLeft(34));
        generateBitDepthCombo_.setBounds(settingsRow.removeFromLeft(96));
        settingsRow.removeFromLeft(14);
        generateSketchSmoothingLabel_.setBounds(settingsRow.removeFromLeft(54));
        generateSketchSmoothingCombo_.setBounds(settingsRow.removeFromLeft(96));
        settingsRow.removeFromLeft(14);
        generatePulseWidthLabel_.setBounds(settingsRow.removeFromLeft(36));
        generatePulseWidthSlider_.setBounds(settingsRow);

        genArea.removeFromTop(10);
        auto actionsRow = genArea.removeFromTop(32);
        generatePreviewButton_.setBounds(actionsRow.removeFromLeft(96));
        actionsRow.removeFromLeft(12);
        generateFrequencyLabel_.setBounds(actionsRow.removeFromLeft(72));
        generateFrequencyCombo_.setBounds(actionsRow.removeFromLeft(190));

        if (showPerformanceStrip)
            layoutPlayerPerformanceArea(performanceStripArea, true);

        return;
    }

    if (currentTabIndex_ == 5)
    {
        auto captureArea = area.reduced(8, 6);
        auto waveformArea = captureArea.removeFromTop(juce::jmax(220, captureArea.getHeight() / 2));
        captureWaveformView_.setBounds(waveformArea);

        captureArea.removeFromTop(10);
        auto controlsRow = captureArea.removeFromTop(32);
        captureLoadAsSampleButton_.setBounds(controlsRow.removeFromRight(150));
        controlsRow.removeFromRight(8);
        captureRecordButton_.setBounds(controlsRow.removeFromLeft(96));
        controlsRow.removeFromLeft(8);
        captureClearButton_.setBounds(controlsRow.removeFromLeft(88));
        controlsRow.removeFromLeft(8);
        captureCutButton_.setBounds(controlsRow.removeFromLeft(126));
        controlsRow.removeFromLeft(8);
        captureTrimButton_.setBounds(controlsRow.removeFromLeft(132));
        controlsRow.removeFromLeft(8);
        capturePlayButton_.setBounds(controlsRow.removeFromLeft(120));
        controlsRow.removeFromLeft(8);
        captureNormalizeButton_.setBounds(controlsRow.removeFromLeft(100));

        captureArea.removeFromTop(10);
        auto sourceRow = captureArea.removeFromTop(20);
        captureSourceLabel_.setBounds(sourceRow);

        captureArea.removeFromTop(6);
        auto settingsRow = captureArea.removeFromTop(30);
        captureSampleRateLabel_.setBounds(settingsRow.removeFromLeft(28));
        captureSampleRateCombo_.setBounds(settingsRow.removeFromLeft(96));
        settingsRow.removeFromLeft(10);
        captureChannelLabel_.setBounds(settingsRow.removeFromLeft(28));
        captureChannelCombo_.setBounds(settingsRow.removeFromLeft(118));
        settingsRow.removeFromLeft(10);
        captureBitDepthLabel_.setBounds(settingsRow.removeFromLeft(34));
        captureBitDepthCombo_.setBounds(settingsRow.removeFromLeft(96));

        captureArea.removeFromTop(8);
        auto levelRow = captureArea.removeFromTop(40);
        captureRootNoteLabel_.setBounds(levelRow.removeFromLeft(72));
        captureRootNoteCombo_.setBounds(levelRow.removeFromLeft(160));
        levelRow.removeFromLeft(12);
        captureInputLevelLabel_.setBounds(levelRow.removeFromLeft(80));
        captureInputLevelSlider_.setBounds(levelRow.removeFromLeft(190));
        levelRow.removeFromLeft(12);
        captureInputVuMeter_.setBounds(levelRow.removeFromLeft(180));

        captureArea.removeFromTop(8);
        captureStatusLabel_.setBounds(captureArea.removeFromTop(22));
        if (showPerformanceStrip)
            layoutPlayerPerformanceArea(performanceStripArea, true);
        return;
    }

    if (currentTabIndex_ == 6)
    {
        auto aboutArea = area.reduced(8, 6);
        constexpr int kButtonW = 200;
        constexpr int kButtonH = 36;
        constexpr int kButtonGap = 16;
        constexpr int kButtonBottomPadding = 22;

        aboutArea.removeFromBottom(kButtonBottomPadding);
        auto buttonStrip = aboutArea.removeFromBottom(kButtonH);

        const int totalButtonsW = kButtonW * 2 + kButtonGap;
        const int buttonX = buttonStrip.getX() + (buttonStrip.getWidth() - totalButtonsW) / 2;
        aboutGitHubButton_.setBounds(buttonX, buttonStrip.getY(), kButtonW, kButtonH);
        aboutCoffeeButton_.setBounds(buttonX + kButtonW + kButtonGap, buttonStrip.getY(), kButtonW, kButtonH);
        return;
    }

    // ── Top bar: load + presets + diagnostics toggle ──
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

    area.removeFromTop(kGrpGap);

    sampleInspectorInfoBounds_ = {};
    sampleInspectorProgramMapBounds_ = {};
    sampleInspectorOutputBounds_ = {};
    sampleInspectorFilterModBounds_ = {};
    sampleInspectorEffectsBounds_ = {};

    const bool useSampleInspectorRail = shouldShowSampleInspectorRail();
    const bool useProgramMapInspector = shouldShowSampleProgramMapInspector();
    const bool useFilterModInspector = shouldShowSampleFilterModInspector();
    const bool useEffectsInspector = shouldShowSampleEffectsInspector();

    const auto reparentInspectorComponent = [this](juce::Component& component, juce::Component& target)
    {
        if (component.getParentComponent() != &target)
            target.addAndMakeVisible(component);
    };

    const auto countVisibleSampleInfoMetrics = [&]()
    {
        int count = 0;
        const auto countMetric = [&count](const juce::Label& keyLabel, const juce::Label& valueLabel)
        {
            if (keyLabel.isVisible() && valueLabel.isVisible())
                ++count;
        };

        countMetric(sampleInfoRateLabel_, sampleInfoRateValue_);
        countMetric(sampleInfoBitDepthLabel_, sampleInfoBitDepthValue_);
        countMetric(sampleInfoChannelsLabel_, sampleInfoChannelsValue_);
        countMetric(sampleInfoSamplesLabel_, sampleInfoSamplesValue_);
        countMetric(sampleInfoDurationLabel_, sampleInfoDurationValue_);
        countMetric(sampleInfoFileSizeLabel_, sampleInfoFileSizeValue_);
        countMetric(sampleInfoPlaybackLabel_, sampleInfoPlaybackValue_);
        countMetric(sampleInfoPlaybackDurationLabel_, sampleInfoPlaybackDurationValue_);
        countMetric(sampleInfoLoopLabel_, sampleInfoLoopValue_);
        countMetric(sampleInfoLoopDurationLabel_, sampleInfoLoopDurationValue_);
        countMetric(sampleInfoTempoLabel_, sampleInfoTempoValue_);
        countMetric(sampleInfoMetaRootLabel_, sampleInfoMetaRootValue_);
        return count;
    };

    const int visibleMetricCount = countVisibleSampleInfoMetrics();
    const int metricRows = juce::jmax(1, (visibleMetricCount + 1) / 2);
    const int infoCardHeight = 74 + metricRows * 34;
    const bool useOutputInspector = useSampleInspectorRail;

    const auto setSampleInspectorParenting = [&](const bool useInfoInspector,
                                                 const bool useProgramInspector,
                                                 const bool useOutputInspectorCard,
                                                 const bool useFilterModInspectorCard,
                                                 const bool useEffectsInspectorCard)
    {
        auto& infoTarget = useInfoInspector ? static_cast<juce::Component&>(*this)
                                            : static_cast<juce::Component&>(sampleControlsContent_);

        reparentInspectorComponent(sampleInfoSourceLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoSourceValue_, infoTarget);
        reparentInspectorComponent(sampleInfoRateLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoRateValue_, infoTarget);
        reparentInspectorComponent(sampleInfoBitDepthLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoBitDepthValue_, infoTarget);
        reparentInspectorComponent(sampleInfoChannelsLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoChannelsValue_, infoTarget);
        reparentInspectorComponent(sampleInfoDurationLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoDurationValue_, infoTarget);
        reparentInspectorComponent(sampleInfoFileSizeLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoFileSizeValue_, infoTarget);
        reparentInspectorComponent(sampleInfoSamplesLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoSamplesValue_, infoTarget);
        reparentInspectorComponent(sampleInfoPlaybackLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoPlaybackValue_, infoTarget);
        reparentInspectorComponent(sampleInfoPlaybackDurationLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoPlaybackDurationValue_, infoTarget);
        reparentInspectorComponent(sampleInfoLoopLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoLoopValue_, infoTarget);
        reparentInspectorComponent(sampleInfoLoopDurationLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoLoopDurationValue_, infoTarget);
        reparentInspectorComponent(sampleInfoTempoLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoTempoValue_, infoTarget);
        reparentInspectorComponent(sampleInfoMetaRootLabel_, infoTarget);
        reparentInspectorComponent(sampleInfoMetaRootValue_, infoTarget);
        reparentInspectorComponent(sampleInfoBadge_, infoTarget);

        auto& programMapTarget = useProgramInspector ? static_cast<juce::Component&>(*this)
                                                     : static_cast<juce::Component&>(sampleControlsContent_);
        reparentInspectorComponent(programMapText_, programMapTarget);

        auto& outputTarget = useOutputInspectorCard ? static_cast<juce::Component&>(*this)
                                                    : static_cast<juce::Component&>(sampleControlsContent_);
        reparentInspectorComponent(fadeInDial_, outputTarget);
        reparentInspectorComponent(fadeOutDial_, outputTarget);
        reparentInspectorComponent(preloadDial_, outputTarget);
        reparentInspectorComponent(masterVolumeDial_, outputTarget);
        reparentInspectorComponent(panDial_, outputTarget);
        reparentInspectorComponent(outputLevelMeter_, outputTarget);
        reparentInspectorComponent(qualityLabel_, outputTarget);
        reparentInspectorComponent(qualityCpuButton_, outputTarget);
        reparentInspectorComponent(qualityFidelityButton_, outputTarget);
        reparentInspectorComponent(qualityUltraButton_, outputTarget);

        auto& filterModTarget = useFilterModInspectorCard ? static_cast<juce::Component&>(*this)
                                  : static_cast<juce::Component&>(sampleControlsContent_);
        reparentInspectorComponent(filterAttackDial_, filterModTarget);
        reparentInspectorComponent(filterDecayDial_, filterModTarget);
        reparentInspectorComponent(filterSustainDial_, filterModTarget);
        reparentInspectorComponent(filterReleaseDial_, filterModTarget);
        reparentInspectorComponent(filterEnvelopeGraph_, filterModTarget);
        reparentInspectorComponent(filterKeytrackDial_, filterModTarget);
        reparentInspectorComponent(filterVelDial_, filterModTarget);
        reparentInspectorComponent(filterLfoRateDial_, filterModTarget);
        reparentInspectorComponent(filterLfoAmtDial_, filterModTarget);
        reparentInspectorComponent(filterLfoShapeLabel_, filterModTarget);
        reparentInspectorComponent(filterLfoShapeCombo_, filterModTarget);
        reparentInspectorComponent(filterLfoRetriggerToggle_, filterModTarget);
        reparentInspectorComponent(filterLfoTempoSyncToggle_, filterModTarget);
        reparentInspectorComponent(filterLfoDivisionLabel_, filterModTarget);
        reparentInspectorComponent(filterLfoDivisionCombo_, filterModTarget);

        auto& effectsTarget = useEffectsInspectorCard ? static_cast<juce::Component&>(*this)
                                                      : static_cast<juce::Component&>(sampleControlsContent_);
        reparentInspectorComponent(reverbMixDial_, effectsTarget);
        reparentInspectorComponent(delayTimeDial_, effectsTarget);
        reparentInspectorComponent(delayFeedbackDial_, effectsTarget);
        reparentInspectorComponent(delayMixDial_, effectsTarget);
        reparentInspectorComponent(delayTempoSyncToggle_, effectsTarget);
        reparentInspectorComponent(dcFilterEnabledToggle_, effectsTarget);
        reparentInspectorComponent(dcFilterCutoffDial_, effectsTarget);
        reparentInspectorComponent(autopanRateDial_, effectsTarget);
        reparentInspectorComponent(autopanDepthDial_, effectsTarget);
        reparentInspectorComponent(saturationDriveDial_, effectsTarget);
        reparentInspectorComponent(saturationModeCombo_, effectsTarget);
    };

    setSampleInspectorParenting(useSampleInspectorRail,
        useProgramMapInspector,
        useOutputInspector,
        useFilterModInspector,
        useEffectsInspector);

    const auto layoutSampleInfoInline = [&](juce::Rectangle<int> infoInner)
    {
        auto row1 = infoInner.removeFromTop(24);
        const auto badgeVisible = sampleInfoBadge_.isVisible() && sampleInfoBadge_.getText().isNotEmpty();
        const auto badgeWidth = badgeVisible ? (sampleInfoBadge_.getText() == "Apple Loop" ? 72 : 56) : 0;
        if (badgeVisible)
        {
            sampleInfoBadge_.setBounds(row1.removeFromRight(badgeWidth).withSizeKeepingCentre(badgeWidth, 18));
            row1.removeFromRight(8);
        }
        else
        {
            sampleInfoBadge_.setBounds({});
        }
        sampleInfoSourceLabel_.setBounds(row1.removeFromLeft(40));
        sampleInfoSourceValue_.setBounds(row1);
        infoInner.removeFromTop(4);
        auto row2 = infoInner.removeFromTop(22);
        auto layoutInlinePair = [](juce::Rectangle<int>& row,
                                   juce::Label& keyLabel,
                                   juce::Label& valueLabel,
                                   int keyWidth,
                                   int valueWidth)
        {
            keyLabel.setBounds(row.removeFromLeft(keyWidth));
            valueLabel.setBounds(row.removeFromLeft(valueWidth));
            row.removeFromLeft(10);
        };

        layoutInlinePair(row2, sampleInfoRateLabel_, sampleInfoRateValue_, 72, 98);
        layoutInlinePair(row2, sampleInfoBitDepthLabel_, sampleInfoBitDepthValue_, 60, 68);
        layoutInlinePair(row2, sampleInfoChannelsLabel_, sampleInfoChannelsValue_, 56, 30);
        layoutInlinePair(row2, sampleInfoSamplesLabel_, sampleInfoSamplesValue_, 66, 86);
        layoutInlinePair(row2, sampleInfoDurationLabel_, sampleInfoDurationValue_, 56, 72);
        layoutInlinePair(row2, sampleInfoFileSizeLabel_, sampleInfoFileSizeValue_, 48, 86);

        infoInner.removeFromTop(2);
        auto row3 = infoInner.removeFromTop(22);
        auto layoutInlinePairIfVisible = [&layoutInlinePair](juce::Rectangle<int>& row,
                                                             juce::Label& keyLabel,
                                                             juce::Label& valueLabel,
                                                             int keyWidth,
                                                             int valueWidth)
        {
            if (keyLabel.isVisible() && valueLabel.isVisible())
                layoutInlinePair(row, keyLabel, valueLabel, keyWidth, valueWidth);
            else
            {
                keyLabel.setBounds({});
                valueLabel.setBounds({});
            }
        };

        layoutInlinePairIfVisible(row3, sampleInfoPlaybackLabel_, sampleInfoPlaybackValue_, 120, 150);
        layoutInlinePairIfVisible(row3, sampleInfoPlaybackDurationLabel_, sampleInfoPlaybackDurationValue_, 124, 90);
        layoutInlinePairIfVisible(row3, sampleInfoLoopLabel_, sampleInfoLoopValue_, 74, 150);
        layoutInlinePairIfVisible(row3, sampleInfoLoopDurationLabel_, sampleInfoLoopDurationValue_, 94, 90);
        if (sampleInfoTempoLabel_.isVisible())
            layoutInlinePair(row3, sampleInfoTempoLabel_, sampleInfoTempoValue_, 52, 72);
        if (sampleInfoMetaRootLabel_.isVisible())
            layoutInlinePair(row3, sampleInfoMetaRootLabel_, sampleInfoMetaRootValue_, 68, 110);
    };

    const auto layoutSampleInfoInspector = [&](juce::Rectangle<int> inspectorBounds)
    {
        auto infoInner = inspectorBounds.withTrimmedTop(30).reduced(12, 10);
        const auto badgeVisible = sampleInfoBadge_.isVisible() && sampleInfoBadge_.getText().isNotEmpty();
        const auto badgeWidth = badgeVisible ? (sampleInfoBadge_.getText() == "Apple Loop" ? 72 : 56) : 0;

        auto sourceHeader = infoInner.removeFromTop(14);
        if (badgeVisible)
        {
            sampleInfoBadge_.setBounds(sourceHeader.removeFromRight(badgeWidth).withSizeKeepingCentre(badgeWidth, 18));
            sourceHeader.removeFromRight(8);
        }
        else
        {
            sampleInfoBadge_.setBounds({});
        }

        sampleInfoSourceLabel_.setBounds(sourceHeader.removeFromLeft(46));
        sampleInfoSourceValue_.setBounds(infoInner.removeFromTop(22));
        infoInner.removeFromTop(8);

        auto layoutMetricCell = [](juce::Rectangle<int> cell,
                                   juce::Label& keyLabel,
                                   juce::Label& valueLabel)
        {
            keyLabel.setBounds(cell.removeFromTop(11));
            cell.removeFromTop(2);
            valueLabel.setBounds(cell.removeFromTop(16));
        };

        std::vector<std::pair<juce::Label*, juce::Label*>> metrics;
        metrics.reserve(12);
        auto addMetric = [&metrics](juce::Label& keyLabel, juce::Label& valueLabel)
        {
            if (keyLabel.isVisible() && valueLabel.isVisible())
                metrics.push_back({ &keyLabel, &valueLabel });
            else
            {
                keyLabel.setBounds({});
                valueLabel.setBounds({});
            }
        };

        addMetric(sampleInfoRateLabel_, sampleInfoRateValue_);
        addMetric(sampleInfoBitDepthLabel_, sampleInfoBitDepthValue_);
        addMetric(sampleInfoChannelsLabel_, sampleInfoChannelsValue_);
        addMetric(sampleInfoSamplesLabel_, sampleInfoSamplesValue_);
        addMetric(sampleInfoDurationLabel_, sampleInfoDurationValue_);
        addMetric(sampleInfoFileSizeLabel_, sampleInfoFileSizeValue_);
        addMetric(sampleInfoPlaybackLabel_, sampleInfoPlaybackValue_);
        addMetric(sampleInfoPlaybackDurationLabel_, sampleInfoPlaybackDurationValue_);
        addMetric(sampleInfoLoopLabel_, sampleInfoLoopValue_);
        addMetric(sampleInfoLoopDurationLabel_, sampleInfoLoopDurationValue_);
        addMetric(sampleInfoTempoLabel_, sampleInfoTempoValue_);
        addMetric(sampleInfoMetaRootLabel_, sampleInfoMetaRootValue_);

        constexpr int kMetricGap = 8;
        const int columnWidth = (infoInner.getWidth() - kMetricGap) / 2;
        for (std::size_t index = 0; index < metrics.size(); index += 2)
        {
            auto row = infoInner.removeFromTop(30);
            auto leftCell = row.removeFromLeft(columnWidth);
            layoutMetricCell(leftCell, *metrics[index].first, *metrics[index].second);

            if (index + 1 < metrics.size())
            {
                row.removeFromLeft(kMetricGap);
                layoutMetricCell(row, *metrics[index + 1].first, *metrics[index + 1].second);
            }

            infoInner.removeFromTop(4);
        }
    };

    const auto layoutOutputInspector = [&](juce::Rectangle<int> inspectorBounds)
    {
        auto outputInner = inspectorBounds.withTrimmedTop(30).reduced(12, 10);
        constexpr int kInspectorDialGap = 6;
        constexpr int kCompactRowHeight = 52;
        constexpr int kSmallDialWidth = 52;

        auto row1 = outputInner.removeFromTop(kCompactRowHeight);
        fadeInDial_.setBounds(row1.removeFromLeft(kSmallDialWidth));
        row1.removeFromLeft(kInspectorDialGap);
        fadeOutDial_.setBounds(row1.removeFromLeft(kSmallDialWidth));
        row1.removeFromLeft(kInspectorDialGap);
        preloadDial_.setBounds(row1);

        outputInner.removeFromTop(6);
        auto row2 = outputInner.removeFromTop(kCompactRowHeight);
        masterVolumeDial_.setBounds(row2.removeFromLeft(kSmallDialWidth));
        row2.removeFromLeft(kInspectorDialGap);
        panDial_.setBounds(row2.removeFromLeft(kSmallDialWidth));
        row2.removeFromLeft(kInspectorDialGap);
        outputLevelMeter_.setBounds(row2.reduced(0, 6));

        outputInner.removeFromTop(6);
        qualityLabel_.setBounds({});
        auto qualityRow = outputInner.removeFromTop(20);
        const int qualityButtonWidth = (qualityRow.getWidth() - kInspectorDialGap * 2) / 3;
        qualityCpuButton_.setBounds(qualityRow.removeFromLeft(qualityButtonWidth));
        qualityRow.removeFromLeft(kInspectorDialGap);
        qualityFidelityButton_.setBounds(qualityRow.removeFromLeft(qualityButtonWidth));
        qualityRow.removeFromLeft(kInspectorDialGap);
        qualityUltraButton_.setBounds(qualityRow);
    };

    const auto layoutEffectsInspector = [&](juce::Rectangle<int> inspectorBounds)
    {
        auto effectsInner = inspectorBounds.withTrimmedTop(30).reduced(12, 10);
        constexpr int kInspectorGap = 6;

        auto topRow = effectsInner.removeFromTop(68);
        const int topCellWidth = (topRow.getWidth() - kInspectorGap * 3) / 4;
        reverbMixDial_.setBounds(topRow.removeFromLeft(topCellWidth));
        topRow.removeFromLeft(kInspectorGap);
        delayTimeDial_.setBounds(topRow.removeFromLeft(topCellWidth));
        topRow.removeFromLeft(kInspectorGap);
        delayFeedbackDial_.setBounds(topRow.removeFromLeft(topCellWidth));
        topRow.removeFromLeft(kInspectorGap);
        delayMixDial_.setBounds(topRow);

        effectsInner.removeFromTop(8);
        auto middleRow = effectsInner.removeFromTop(24);
        delayTempoSyncToggle_.setBounds(middleRow.removeFromLeft(132));
        middleRow.removeFromLeft(8);
        saturationModeCombo_.setBounds(middleRow.removeFromRight(96));

        effectsInner.removeFromTop(8);
        auto bottomRow = effectsInner.removeFromTop(68);
        const int bottomCellWidth = (bottomRow.getWidth() - kInspectorGap * 3) / 4;
        dcFilterCutoffDial_.setBounds(bottomRow.removeFromLeft(bottomCellWidth));
        bottomRow.removeFromLeft(kInspectorGap);
        autopanRateDial_.setBounds(bottomRow.removeFromLeft(bottomCellWidth));
        bottomRow.removeFromLeft(kInspectorGap);
        autopanDepthDial_.setBounds(bottomRow.removeFromLeft(bottomCellWidth));
        bottomRow.removeFromLeft(kInspectorGap);
        saturationDriveDial_.setBounds(bottomRow);

        effectsInner.removeFromTop(8);
        dcFilterEnabledToggle_.setBounds(effectsInner.removeFromTop(24));
    };

    const auto layoutFilterModInspector = [&](juce::Rectangle<int> inspectorBounds)
    {
        auto filterModInner = inspectorBounds.withTrimmedTop(30).reduced(12, 10);
        constexpr int kInspectorGap = 6;

        auto graphRow = filterModInner.removeFromTop(40);
        filterEnvelopeGraph_.setBounds(graphRow);

        filterModInner.removeFromTop(6);
        auto envRow = filterModInner.removeFromTop(54);
        const int envCellWidth = (envRow.getWidth() - kInspectorGap * 3) / 4;
        filterAttackDial_.setBounds(envRow.removeFromLeft(envCellWidth));
        envRow.removeFromLeft(kInspectorGap);
        filterDecayDial_.setBounds(envRow.removeFromLeft(envCellWidth));
        envRow.removeFromLeft(kInspectorGap);
        filterSustainDial_.setBounds(envRow.removeFromLeft(envCellWidth));
        envRow.removeFromLeft(kInspectorGap);
        filterReleaseDial_.setBounds(envRow);

        filterModInner.removeFromTop(6);
        auto modRow = filterModInner.removeFromTop(54);
        const int modCellWidth = (modRow.getWidth() - kInspectorGap * 3) / 4;
        filterKeytrackDial_.setBounds(modRow.removeFromLeft(modCellWidth));
        modRow.removeFromLeft(kInspectorGap);
        filterVelDial_.setBounds(modRow.removeFromLeft(modCellWidth));
        modRow.removeFromLeft(kInspectorGap);
        filterLfoRateDial_.setBounds(modRow.removeFromLeft(modCellWidth));
        modRow.removeFromLeft(kInspectorGap);
        filterLfoAmtDial_.setBounds(modRow);

        filterModInner.removeFromTop(6);
        auto comboRow = filterModInner.removeFromTop(40);
        auto shapeArea = comboRow.removeFromLeft((comboRow.getWidth() - 8) / 2);
        filterLfoShapeLabel_.setBounds(shapeArea.removeFromTop(11));
        shapeArea.removeFromTop(2);
        filterLfoShapeCombo_.setBounds(shapeArea.removeFromTop(24));
        comboRow.removeFromLeft(8);
        filterLfoDivisionLabel_.setBounds(comboRow.removeFromTop(11));
        comboRow.removeFromTop(2);
        filterLfoDivisionCombo_.setBounds(comboRow.removeFromTop(24));

        filterModInner.removeFromTop(6);
        auto toggleRow = filterModInner.removeFromTop(24);
        const int toggleWidth = (toggleRow.getWidth() - 8) / 2;
        filterLfoRetriggerToggle_.setBounds(toggleRow.removeFromLeft(toggleWidth));
        toggleRow.removeFromLeft(8);
        filterLfoTempoSyncToggle_.setBounds(toggleRow);
    };

    const auto clearEffectsInspectorControls = [&]()
    {
        reverbMixDial_.setBounds({});
        delayTimeDial_.setBounds({});
        delayFeedbackDial_.setBounds({});
        delayMixDial_.setBounds({});
        delayTempoSyncToggle_.setBounds({});
        dcFilterEnabledToggle_.setBounds({});
        dcFilterCutoffDial_.setBounds({});
        autopanRateDial_.setBounds({});
        autopanDepthDial_.setBounds({});
        saturationDriveDial_.setBounds({});
        saturationModeCombo_.setBounds({});
    };

    const auto clearOutputInspectorControls = [&]()
    {
        fadeInDial_.setBounds({});
        fadeOutDial_.setBounds({});
        preloadDial_.setBounds({});
        masterVolumeDial_.setBounds({});
        panDial_.setBounds({});
        outputLevelMeter_.setBounds({});
        qualityLabel_.setBounds({});
        qualityCpuButton_.setBounds({});
        qualityFidelityButton_.setBounds({});
        qualityUltraButton_.setBounds({});
    };

    const auto layoutOutputInspectorCard = [&](juce::Rectangle<int>& inspectorArea)
    {
        if (!useOutputInspector)
        {
            clearOutputInspectorControls();
            return;
        }

        inspectorArea.removeFromTop(kGrpGap);
        const int outputCardHeight = juce::jmin(kSampleOutputInspectorCardHeight, inspectorArea.getHeight());
        if (outputCardHeight <= 0)
        {
            clearOutputInspectorControls();
            return;
        }

        sampleInspectorOutputBounds_ = inspectorArea.removeFromTop(outputCardHeight);
        layoutOutputInspector(sampleInspectorOutputBounds_);
    };

    const auto clearFilterModInspectorControls = [&]()
    {
        filterAttackDial_.setBounds({});
        filterDecayDial_.setBounds({});
        filterSustainDial_.setBounds({});
        filterReleaseDial_.setBounds({});
        filterEnvelopeGraph_.setBounds({});
        filterKeytrackDial_.setBounds({});
        filterVelDial_.setBounds({});
        filterLfoRateDial_.setBounds({});
        filterLfoAmtDial_.setBounds({});
        filterLfoShapeLabel_.setBounds({});
        filterLfoShapeCombo_.setBounds({});
        filterLfoRetriggerToggle_.setBounds({});
        filterLfoTempoSyncToggle_.setBounds({});
        filterLfoDivisionLabel_.setBounds({});
        filterLfoDivisionCombo_.setBounds({});
    };

    auto workspaceArea = area;
    if (useSampleInspectorRail)
    {
        constexpr int kSampleInspectorGap = 10;
        const int inspectorWidth = juce::jlimit(240, 300, workspaceArea.getWidth() / 3);
        auto inspectorArea = workspaceArea.removeFromRight(inspectorWidth);
        workspaceArea.removeFromRight(kSampleInspectorGap);

        sampleInspectorInfoBounds_ = inspectorArea.removeFromTop(infoCardHeight);
        layoutSampleInfoInspector(sampleInspectorInfoBounds_);

        if (useProgramMapInspector)
        {
            inspectorArea.removeFromTop(kGrpGap);
            const int programMapHeight = useEffectsInspector
                ? juce::jmax(150, juce::jmin(188, inspectorArea.getHeight()))
                : juce::jmax(148, juce::jmin(214, inspectorArea.getHeight()));
            sampleInspectorProgramMapBounds_ = inspectorArea.removeFromTop(programMapHeight);
            programMapText_.setBounds(sampleInspectorProgramMapBounds_.withTrimmedTop(30).reduced(12, 10));
            layoutOutputInspectorCard(inspectorArea);

            if (useEffectsInspector)
            {
                inspectorArea.removeFromTop(kGrpGap);
                const int effectsCardHeight = juce::jmin(
                    getSampleInspectorCardHeight(sampleInspectorEffectsExpanded_, kExpandedSampleEffectsInspectorCardHeight),
                    inspectorArea.getHeight());
                sampleInspectorEffectsBounds_ = inspectorArea.removeFromTop(effectsCardHeight);
                if (sampleInspectorEffectsExpanded_)
                    layoutEffectsInspector(sampleInspectorEffectsBounds_);
                else
                    clearEffectsInspectorControls();
            }
            else
            {
                clearEffectsInspectorControls();
            }

            if (useFilterModInspector)
            {
                inspectorArea.removeFromTop(kGrpGap);
                const int filterModCardHeight = juce::jmin(
                    getSampleInspectorCardHeight(sampleInspectorFilterModExpanded_, kExpandedSampleFilterModInspectorCardHeight),
                    inspectorArea.getHeight());
                sampleInspectorFilterModBounds_ = inspectorArea.removeFromTop(filterModCardHeight);
                if (sampleInspectorFilterModExpanded_)
                    layoutFilterModInspector(sampleInspectorFilterModBounds_);
                else
                    clearFilterModInspectorControls();
            }
        }
        else
        {
            programMapText_.setBounds({});
            layoutOutputInspectorCard(inspectorArea);

            if (useEffectsInspector)
            {
                inspectorArea.removeFromTop(kGrpGap);
                const int effectsCardHeight = juce::jmin(
                    getSampleInspectorCardHeight(sampleInspectorEffectsExpanded_, kExpandedSampleEffectsInspectorCardHeight),
                    inspectorArea.getHeight());
                sampleInspectorEffectsBounds_ = inspectorArea.removeFromTop(effectsCardHeight);
                if (sampleInspectorEffectsExpanded_)
                    layoutEffectsInspector(sampleInspectorEffectsBounds_);
                else
                    clearEffectsInspectorControls();
            }
            else
            {
                clearEffectsInspectorControls();
            }

            if (useFilterModInspector)
            {
                inspectorArea.removeFromTop(kGrpGap);
                const int filterModCardHeight = juce::jmin(
                    getSampleInspectorCardHeight(sampleInspectorFilterModExpanded_, kExpandedSampleFilterModInspectorCardHeight),
                    inspectorArea.getHeight());
                sampleInspectorFilterModBounds_ = inspectorArea.removeFromTop(filterModCardHeight);
                if (sampleInspectorFilterModExpanded_)
                    layoutFilterModInspector(sampleInspectorFilterModBounds_);
                else
                    clearFilterModInspectorControls();
            }
        }
    }
    else
    {
        programMapText_.setBounds({});
        clearOutputInspectorControls();
    }

    const auto waveformHeight = juce::jlimit(180, 320, workspaceArea.getHeight() / 3);
    waveformView_.setBounds(workspaceArea.removeFromTop(waveformHeight));
    workspaceArea.removeFromTop(8);

    auto waveformInfoRow = workspaceArea.removeFromTop(26);
    waveformResetRangesButton_.setBounds(waveformInfoRow.removeFromRight(62));
    waveformInfoRow.removeFromRight(8);
    waveformInteractionSummaryLabel_.setBounds(waveformInfoRow);

    workspaceArea.removeFromTop(kGrpGap);
    sampleControlsViewport_.setBounds(workspaceArea);

    const auto viewportWidth = juce::jmax(200, sampleControlsViewport_.getWidth() - sampleControlsViewport_.getScrollBarThickness() - 2);
    int scrollY = 0;
    groupBoxes_.clear();

    auto makeGroup = [&](const juce::String& key,
                         const juce::String& title,
                         const int expandedHeight) -> juce::Rectangle<int>
    {
        const auto collapsible = isSampleGroupCollapsible(key);
        const auto expanded = !collapsible || isSampleGroupExpanded(key);
        const auto height = expanded ? expandedHeight : (kGrpHdr + 8);
        auto bounds = juce::Rectangle<int>(0, scrollY, viewportWidth, height);
        groupBoxes_.push_back({ key, title, bounds, expanded, collapsible });
        scrollY += height + kGrpGap;
        if (!expanded)
            return juce::Rectangle<int>();

        return bounds.withTrimmedTop(kGrpHdr).reduced(kGrpPadH, kGrpPadV);
    };

    auto layoutThreeButtonStack = [kStackLabelH, kStackButtonH, kStackGap](juce::Rectangle<int> area,
                                                                             juce::Label& label,
                                                                             juce::Button& topButton,
                                                                             juce::Button& middleButton,
                                                                             juce::Button& bottomButton)
    {
        label.setBounds(area.removeFromTop(kStackLabelH));
        area.removeFromTop(kStackGap);
        topButton.setBounds(area.removeFromTop(kStackButtonH));
        area.removeFromTop(kStackGap);
        middleButton.setBounds(area.removeFromTop(kStackButtonH));
        area.removeFromTop(kStackGap);
        bottomButton.setBounds(area.removeFromTop(kStackButtonH));
    };

    auto layoutTwoButtonStack = [kStackLabelH, kStackButtonH, kStackGap](juce::Rectangle<int> area,
                                                                           juce::Label& label,
                                                                           juce::Button& topButton,
                                                                           juce::Button& bottomButton)
    {
        label.setBounds(area.removeFromTop(kStackLabelH));
        area.removeFromTop(kStackGap);
        topButton.setBounds(area.removeFromTop(kStackButtonH));
        area.removeFromTop(kStackGap);
        bottomButton.setBounds(area.removeFromTop(kStackButtonH));
    };

    // ── Panel 1: Sample Information ──
    if (!useSampleInspectorRail)
    {
        constexpr int kSampleInfoPanelH = 108;
        auto infoInner = makeGroup("sampleInfo", "Sample Information", kSampleInfoPanelH);
        layoutSampleInfoInline(infoInner);
    }

    if (processor_.hasImportedProgram() && !useProgramMapInspector)
    {
        auto programInner = makeGroup("programMap", "Program Map", 164);
        if (!programInner.isEmpty())
            programMapText_.setBounds(programInner);
        else
            programMapText_.setBounds({});
    }
    else
    {
        programMapText_.setBounds({});
    }

    // ── Panel 2: Performance ──
    {
        auto perfInner = makeGroup("performance", "Performance", kRowH);
        auto modeArea = perfInner.removeFromRight(kModeStackW);
        layoutThreeButtonStack(modeArea,
                       playbackModeLabel_,
                       playbackModeGateButton_,
                       playbackModeOneShotButton_,
                       playbackModeLoopButton_);
        perfInner.removeFromRight(kStackColGap);

        auto toggleCol = perfInner.removeFromLeft(86);
        monoToggle_.setBounds(toggleCol.removeFromTop(28));
        toggleCol.removeFromTop(4);
        legatoToggle_.setBounds(toggleCol.removeFromTop(28));
        toggleCol.removeFromTop(4);
        reverseToggle_.setBounds(toggleCol.removeFromTop(28));
        perfInner.removeFromLeft(kDialGap);

        glideDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        polyphonyDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        tuneCoarseDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        tuneFineDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        pitchBendRangeDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        pitchLfoRateDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        pitchLfoDepthDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        auto rightStack = perfInner.removeFromLeft(136);
        rootNoteLabel_.setBounds(rightStack.removeFromTop(16));
        rightStack.removeFromTop(2);
        rootNoteCombo_.setBounds(rightStack.removeFromTop(28));
        rightStack.removeFromTop(6);
        velocityCurveLabel_.setBounds(rightStack.removeFromTop(16));
        rightStack.removeFromTop(2);
        velocityCurveCombo_.setBounds(rightStack.removeFromTop(24));
    }

    // ── Panel 3-4: Modulation ──
    modulationPanel_.setBounds(juce::Rectangle<int>(0, scrollY, viewportWidth, PlayerModulationPanel::preferredHeight()));
    scrollY += PlayerModulationPanel::preferredHeight() + kGrpGap;

    // ── Panel 5: Trim and Loop ──
    {
        auto trimLoopInner = makeGroup("trimLoop", "Trim and Loop", kRowH);

        playbackStartDial_.setBounds(trimLoopInner.removeFromLeft(kDial));
        trimLoopInner.removeFromLeft(kDialGap);
        playbackEndDial_.setBounds(trimLoopInner.removeFromLeft(kDial));
        trimLoopInner.removeFromLeft(kDialGap);
        loopStartDial_.setBounds(trimLoopInner.removeFromLeft(kDial));
        trimLoopInner.removeFromLeft(kDialGap);
        loopEndDial_.setBounds(trimLoopInner.removeFromLeft(kDial));
        trimLoopInner.removeFromLeft(kDialGap);
        loopCrossfadeDial_.setBounds(trimLoopInner.removeFromLeft(kDial));
    }

    // ── Panel 6: Amplitude Envelope ──
    {
        auto ampInner = makeGroup("ampEnv", "Amplitude Envelope", kRowH);
        ampAttackDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampDecayDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampSustainDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampReleaseDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampLfoRateDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampLfoDepthDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        auto ampLfoShapeArea = ampInner.removeFromLeft(kDial + 24);
        ampLfoShapeLabel_.setBounds(ampLfoShapeArea.removeFromTop(16));
        ampLfoShapeArea.removeFromTop(2);
        ampLfoShapeCombo_.setBounds(ampLfoShapeArea.removeFromTop(24));
        ampInner.removeFromLeft(10);
        ampEnvelopeGraph_.setBounds(ampInner.reduced(0, 8));
    }

    // ── Panel 7: Filter ──
    {
        auto filterInner = makeGroup("filter", "Filter", kRowH);
        filterCutoffDial_.setBounds(filterInner.removeFromLeft(kDial));
        filterInner.removeFromLeft(kDialGap);
        filterResDial_.setBounds(filterInner.removeFromLeft(kDial));
        filterInner.removeFromLeft(kDialGap);
        filterEnvAmtDial_.setBounds(filterInner.removeFromLeft(kDial));
        filterInner.removeFromLeft(kDialGap);
        auto filterTypeArea = filterInner.removeFromLeft(kDial + 24);
        filterTypeLabel_.setBounds(filterTypeArea.removeFromTop(16));
        filterTypeArea.removeFromTop(2);
        filterTypeCombo_.setBounds(filterTypeArea.removeFromTop(24));
        filterInner.removeFromLeft(10);
        filterResponseGraph_.setBounds(filterInner.reduced(0, 8));
    }

    // ── Panel 8: Filter Envelope + Mod ──
    if (!useFilterModInspector)
    {
        constexpr int kFilterModPanelH = 238;
        auto filterEnvInner = makeGroup("filterMod", "Filter Envelope + Mod", kFilterModPanelH);
        if (!filterEnvInner.isEmpty())
        {
            auto row1 = filterEnvInner.removeFromTop(kDialH);
            filterAttackDial_.setBounds(row1.removeFromLeft(kDial));
            row1.removeFromLeft(kDialGap);
            filterDecayDial_.setBounds(row1.removeFromLeft(kDial));
            row1.removeFromLeft(kDialGap);
            filterSustainDial_.setBounds(row1.removeFromLeft(kDial));
            row1.removeFromLeft(kDialGap);
            filterReleaseDial_.setBounds(row1.removeFromLeft(kDial));
            row1.removeFromLeft(10);
            filterEnvelopeGraph_.setBounds(row1.reduced(0, 8));

            filterEnvInner.removeFromTop(8);

            auto row2 = filterEnvInner.removeFromTop(kDialH);
            filterKeytrackDial_.setBounds(row2.removeFromLeft(kDial));
            row2.removeFromLeft(kDialGap);
            filterVelDial_.setBounds(row2.removeFromLeft(kDial));
            row2.removeFromLeft(kDialGap);
            filterLfoRateDial_.setBounds(row2.removeFromLeft(kDial));
            row2.removeFromLeft(kDialGap);
            filterLfoAmtDial_.setBounds(row2.removeFromLeft(kDial));
            row2.removeFromLeft(10);
            auto lfoShapeArea = row2.removeFromLeft(120);
            filterLfoShapeLabel_.setBounds(lfoShapeArea.removeFromTop(16));
            lfoShapeArea.removeFromTop(2);
            filterLfoShapeCombo_.setBounds(lfoShapeArea.removeFromTop(24));
            row2.removeFromLeft(10);
            auto divArea = row2.removeFromLeft(120);
            filterLfoDivisionLabel_.setBounds(divArea.removeFromTop(16));
            divArea.removeFromTop(2);
            filterLfoDivisionCombo_.setBounds(divArea.removeFromTop(24));

            row2.removeFromLeft(12);
            auto syncArea = row2.removeFromLeft(90);
            filterLfoRetriggerToggle_.setBounds(syncArea.removeFromTop(22));
            syncArea.removeFromTop(6);
            filterLfoTempoSyncToggle_.setBounds(syncArea.removeFromTop(22));
        }
    }

    // ── Panel 9: Effects ──
    if (!useEffectsInspector)
    {
        constexpr int kEffectsPanelH = kRowH + 44;
        auto fxInner = makeGroup("effects", "Effects", kEffectsPanelH);
        if (!fxInner.isEmpty())
        {
            auto fxDialRow = fxInner.removeFromTop(kDialH);

            reverbMixDial_.setBounds(fxDialRow.removeFromLeft(kDial));
            fxDialRow.removeFromLeft(kDialGap);

            const auto delayTimeBounds = fxDialRow.removeFromLeft(kDial);
            delayTimeDial_.setBounds(delayTimeBounds);
            fxDialRow.removeFromLeft(kDialGap);

            const auto delayFeedbackBounds = fxDialRow.removeFromLeft(kDial);
            delayFeedbackDial_.setBounds(delayFeedbackBounds);
            fxDialRow.removeFromLeft(kDialGap);

            const auto delayMixBounds = fxDialRow.removeFromLeft(kDial);
            delayMixDial_.setBounds(delayMixBounds);
            fxDialRow.removeFromLeft(kDialGap * 2);

            const auto dcFilterDialBounds = fxDialRow.removeFromLeft(kDial);
            dcFilterCutoffDial_.setBounds(dcFilterDialBounds);
            fxDialRow.removeFromLeft(kDialGap);

            autopanRateDial_.setBounds(fxDialRow.removeFromLeft(kDial));
            fxDialRow.removeFromLeft(kDialGap);
            const auto autopanDepthBounds = fxDialRow.removeFromLeft(kDial);
            autopanDepthDial_.setBounds(autopanDepthBounds);
            fxDialRow.removeFromLeft(kDialGap);
            const auto saturationDriveBounds = fxDialRow.removeFromLeft(kDial);
            saturationDriveDial_.setBounds(saturationDriveBounds);

            fxInner.removeFromTop(10);
            auto fxControlRow = fxInner.removeFromTop(24);

            constexpr int kDelaySyncW = 120;
            const auto delayClusterLeft = delayTimeBounds.getX();
            const auto delayClusterRight = delayMixBounds.getRight();
            const auto delaySyncX = delayClusterLeft + (delayClusterRight - delayClusterLeft - kDelaySyncW) / 2;
            delayTempoSyncToggle_.setBounds(delaySyncX, fxControlRow.getY(), kDelaySyncW, 24);

            constexpr int kDcFilterW = 108;
            const auto dcFilterX = dcFilterDialBounds.getX() + (dcFilterDialBounds.getWidth() - kDcFilterW) / 2;
            dcFilterEnabledToggle_.setBounds(dcFilterX, fxControlRow.getY(), kDcFilterW, 24);

            constexpr int kSatModeW = 92;
            const auto satModeX = saturationDriveBounds.getX() + (saturationDriveBounds.getWidth() - kSatModeW) / 2;
            saturationModeCombo_.setBounds(satModeX, fxControlRow.getY(), kSatModeW, 24);
        }
    }

    // ── Panel 10: Output ──
    if (!useOutputInspector)
    {
        auto outInner = makeGroup("output", "Output", kRowH);
        fadeInDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kDialGap);
        fadeOutDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kStackColGap);

        masterVolumeDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kDialGap);

        panDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kDialGap);

        preloadDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kStackColGap);

        auto qualArea = outInner.removeFromRight(kModeStackW);
        layoutThreeButtonStack(qualArea,
                 qualityLabel_,
                 qualityCpuButton_,
                 qualityFidelityButton_,
                 qualityUltraButton_);

        outInner.removeFromRight(kStackColGap);
        outputLevelMeter_.setBounds(outInner.reduced(0, 6));
    }

    if (showDiagnosticsPanel_)
    {
        auto diagInner = makeGroup("diagnostics", "Diagnostics", 56);
        diagnosticsLabel_.setBounds(diagInner.removeFromTop(22));
    }
    else
    {
        diagnosticsLabel_.setBounds({});
    }

    sampleControlsContent_.setSize(viewportWidth, juce::jmax(sampleControlsViewport_.getHeight(), scrollY + 4));

    if (showPerformanceStrip)
        layoutPlayerPerformanceArea(performanceStripArea, true);
}

void AudiocityAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(uiBackgroundColour());

    constexpr int kMargin = 14;

    // Soft top chrome: a thin raised band with a single 1px accent hairline.
    g.setColour(uiPanelRaisedColour());
    g.fillRect(0, 0, getWidth(), 6);
    g.setColour(uiAccentColour().withAlpha(0.32f));
    g.fillRect(0, 6, getWidth(), 1);

    auto content = getLocalBounds().reduced(kMargin);
    content.removeFromTop(kEditorTabBarHeight);
    content.removeFromTop(kEditorTabBarGap);
    juce::Rectangle<int> browserRailArea;
    juce::Rectangle<int> performanceStripArea;
    const bool showBrowserRail = shouldShowPersistentBrowserRail();
    const bool showPerformanceStrip = shouldShowPersistentPerformanceStrip();

    if (showPerformanceStrip)
    {
        constexpr int kPerformanceStripGap = 10;
        const int kPerformanceStripHeight = computePersistentPerformanceStripHeight(content.getHeight());
        performanceStripArea = content.removeFromBottom(kPerformanceStripHeight);
        content.removeFromBottom(kPerformanceStripGap);
    }

    if (showBrowserRail)
    {
        constexpr int kBrowserRailGap = 10;
        const int kBrowserRailWidth = computePersistentBrowserRailWidth(content.getWidth());
        browserRailArea = content.removeFromLeft(kBrowserRailWidth);
        content.removeFromLeft(kBrowserRailGap);
    }

    auto paintContentCard = [&g](juce::Rectangle<int> areaToPaint)
    {
        auto card = areaToPaint.reduced(6).toFloat();
        g.setColour(uiPanelColour());
        g.fillRoundedRectangle(card, 10.0f);
        g.setColour(uiBorderColour());
        g.drawRoundedRectangle(card.reduced(0.5f), 10.0f, 1.0f);
    };

    if (currentTabIndex_ == 1)
        paintSampleBrowserPane(g, content);
    else if (showBrowserRail)
        paintSampleBrowserPane(g, browserRailArea);

    if (currentTabIndex_ == 2)
        paintContentCard(content);
    else if (currentTabIndex_ == 3)
        paintPlayerPane(g, content, false);
    else if (currentTabIndex_ == 4 || currentTabIndex_ == 5)
        paintContentCard(content);
    else if (currentTabIndex_ == 6)
    {
        paintContentCard(content);
        paintAboutPane(g, content);
    }

    if (currentTabIndex_ == 0)
        paintSampleInspectorPane(g);

    if (showPerformanceStrip)
        paintPlayerPane(g, performanceStripArea, true);

    if (!isHoveringValidDrop_)
        return;

    // Drop overlay
    auto overlay = content.reduced(6);
    g.setColour(uiAccentColour().withAlpha(0.12f));
    g.fillRect(overlay);
    g.setColour(uiAccentColour().withAlpha(0.85f));
    g.drawRect(overlay, 2);
    g.setColour(uiTextStrongColour().withAlpha(0.95f));
    g.setFont(14.0f);
    const auto dropText = processor_.isRexRuntimeAvailable()
        ? juce::String("Drop .wav, .aiff, .sfz, .nki, .rex, or .rx2 to open")
        : juce::String("Drop .wav, .aiff, .sfz, or .nki to open");
    g.drawText(dropText, overlay, juce::Justification::centred);
}

bool AudiocityAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    const bool commandDown = modifiers.isCommandDown() || modifiers.isCtrlDown();
    const auto* focusedComponent = juce::Component::getCurrentlyFocusedComponent();
    const bool mappingShortcutContext = currentTabIndex_ == 2
        && focusedComponent != nullptr
        && (focusedComponent == &mappingZoneListBox_
            || mappingZoneListBox_.isParentOf(focusedComponent)
            || focusedComponent == &mappingOverview_
            || mappingOverview_.isParentOf(focusedComponent)
            || focusedComponent == &mappingCreateZoneButton_
            || focusedComponent == &mappingDuplicateZoneButton_
            || focusedComponent == &mappingSplitZoneButton_
            || focusedComponent == &mappingDeleteZoneButton_);

    if (mappingShortcutContext && commandDown && (key.getTextCharacter() == 'n' || key.getTextCharacter() == 'N'))
    {
        createMappingZone();
        return true;
    }

    if (mappingShortcutContext && commandDown && modifiers.isShiftDown()
        && (key.getTextCharacter() == 'd' || key.getTextCharacter() == 'D'))
    {
        splitSelectedMappingZone();
        return true;
    }

    if (mappingShortcutContext && commandDown && (key.getTextCharacter() == 'd' || key.getTextCharacter() == 'D'))
    {
        duplicateSelectedMappingZone();
        return true;
    }

    if (mappingShortcutContext && commandDown && (key.getTextCharacter() == 'a' || key.getTextCharacter() == 'A'))
    {
        selectAllMappingZones();
        return true;
    }

    if (mappingShortcutContext
        && (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey))
    {
        deleteSelectedMappingZone();
        return true;
    }

    if (commandDown && (key.getTextCharacter() == 'o' || key.getTextCharacter() == 'O'))
    {
        openSampleChooser();
        return true;
    }

    if (commandDown && key.getModifiers().isShiftDown()
        && (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S'))
    {
        promptSavePreset();
        return true;
    }

    if (commandDown && modifiers.isAltDown()
        && (key.getTextCharacter() == 'd' || key.getTextCharacter() == 'D'))
    {
        showDiagnosticsPanel_ = !showDiagnosticsPanel_;
        diagnosticsToggleButton_.setToggleState(showDiagnosticsPanel_, juce::dontSendNotification);
        resized();
        repaint();
        return true;
    }

    if (commandDown && (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S'))
    {
        saveStateToFile();
        return true;
    }

    if (commandDown && (key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z'))
    {
        syncImportedProgramMappingUndoContext();
        const auto currentSnapshot = captureSettingsSnapshot();
        const auto currentMappingState = captureImportedProgramMappingState();
        if (const auto previous = editorUndoHistory_.undo(currentSnapshot, currentMappingState); previous.has_value())
        {
            if (previous->kind == audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
            {
                if (applyImportedProgramMappingHistoryState(previous->mappingSnapshot, "Mapping undo"))
                    return true;

                mappingEditStatusLabel_.setText("Mapping undo failed", juce::dontSendNotification);
                return true;
            }

            applySettingsSnapshot(previous->settingsSnapshot);
            lastSettingsSnapshot_ = previous->settingsSnapshot;
        }
        return true;
    }

    if (commandDown && (key.getTextCharacter() == 'y' || key.getTextCharacter() == 'Y'))
    {
        syncImportedProgramMappingUndoContext();
        const auto currentSnapshot = captureSettingsSnapshot();
        const auto currentMappingState = captureImportedProgramMappingState();
        if (const auto next = editorUndoHistory_.redo(currentSnapshot, currentMappingState); next.has_value())
        {
            if (next->kind == audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
            {
                if (applyImportedProgramMappingHistoryState(next->mappingSnapshot, "Mapping redo"))
                    return true;

                mappingEditStatusLabel_.setText("Mapping redo failed", juce::dontSendNotification);
                return true;
            }

            applySettingsSnapshot(next->settingsSnapshot);
            lastSettingsSnapshot_ = next->settingsSnapshot;
        }
        return true;
    }

    if (!commandDown && !modifiers.isAltDown())
    {
        const auto character = key.getTextCharacter();
        if (character >= '1' && character <= '7')
        {
            const auto tabIndex = static_cast<int>(character - '1');
            tabBar_.setCurrentTabIndex(tabIndex);
            currentTabIndex_ = tabIndex;
            processor_.setEditorTabIndex(currentTabIndex_);
            updateTabVisibility();
            resized();
            repaint();
            return true;
        }
    }

    if (!commandDown && !modifiers.isAltDown()
        && key.getKeyCode() == juce::KeyPress::spaceKey
        && currentTabIndex_ == 4)
    {
        if (processor_.isGeneratedWaveformPreviewPlaying())
            processor_.stopGeneratedWaveformPreview();
        else
        {
            processor_.setGeneratedWaveformPreview(generatedWaveform_);
            processor_.setGeneratedWaveformPreviewMidiNote(getSelectedGenerateMidiNote());
            processor_.startGeneratedWaveformPreview();
        }

        updateGeneratePreviewButtonText();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::returnKey && (currentTabIndex_ == 1 || shouldShowPersistentBrowserRail()))
    {
        const auto selectedRow = sampleBrowserListBox_.getSelectedRow();
        if (selectedRow >= 0)
        {
            loadSampleFromBrowserRow(selectedRow);
            return true;
        }
    }

    if (key == juce::KeyPress::escapeKey)
    {
        if (processor_.isInputCaptureRecording())
        {
            processor_.stopInputCapture();
            refreshCaptureWaveform(true);
            updateCaptureUiState();
        }

        processor_.panicAllAudio();
        updateGeneratePreviewButtonText();
        return true;
    }

    return juce::AudioProcessorEditor::keyPressed(key);
}

void AudiocityAudioProcessorEditor::setSampleRailSnapshotState(const bool browserRailEnabled,
                                                              const bool inspectorRailEnabled)
{
    sampleBrowserRailEnabled_ = browserRailEnabled;
    sampleInspectorRailEnabled_ = inspectorRailEnabled;
    sampleBrowserRailToggleButton_.setToggleState(sampleBrowserRailEnabled_, juce::dontSendNotification);
    sampleInspectorRailToggleButton_.setToggleState(sampleInspectorRailEnabled_, juce::dontSendNotification);
    updateTabVisibility();
}

void AudiocityAudioProcessorEditor::setSnapshotTabIndex(const int tabIndex)
{
    const auto selectedTab = juce::jlimit(0, tabBar_.getNumTabs() - 1, tabIndex);
    tabBar_.setCurrentTabIndex(selectedTab);
    currentTabIndex_ = selectedTab;
    processor_.setEditorTabIndex(currentTabIndex_);
    updateTabVisibility();
    resized();
    repaint();

    if (currentTabIndex_ == 1)
        sampleBrowserListBox_.grabKeyboardFocus();
    else if (currentTabIndex_ == 2)
    {
        refreshMappingZoneRows();
        mappingZoneListBox_.grabKeyboardFocus();
    }
    else if (currentTabIndex_ == 5)
    {
        refreshCaptureWaveform(true);
        updateCaptureUiState();
    }
}

void AudiocityAudioProcessorEditor::setSampleInspectorCardSnapshotState(const bool filterModExpanded,
                                                                       const bool effectsExpanded)
{
    sampleInspectorFilterModExpanded_ = filterModExpanded;
    sampleInspectorEffectsExpanded_ = effectsExpanded;
}

void AudiocityAudioProcessorEditor::setPresetSearchSnapshotState(const juce::StringArray& presetNames,
                                                                 const juce::String& filterText)
{
    presetSnapshotNamesOverride_ = presetNames;
    currentPresetName_.clear();
    suppressPresetComboChange_ = true;
    presetFilterEditor_.setText(filterText, juce::dontSendNotification);
    presetCombo_.setSelectedId(0, juce::dontSendNotification);
    suppressPresetComboChange_ = false;
    refreshPresetList();
}

void AudiocityAudioProcessorEditor::saveStateToFile()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Save Audiocity State",
        juce::File{},
        "*.audiocitystate");

    auto chooserFlags = juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File{})
            return;

        if (!file.hasFileExtension(".audiocitystate"))
            file = file.withFileExtension(".audiocitystate");

        juce::MemoryBlock stateData;
        processor_.getStateInformation(stateData);
        if (stateData.getSize() == 0)
            return;

        file.replaceWithData(stateData.getData(), stateData.getSize());
    });
}

juce::File AudiocityAudioProcessorEditor::getPresetDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Audiocity")
        .getChildFile("Presets");
}

juce::Array<juce::File> AudiocityAudioProcessorEditor::getFactoryPresetDirectories() const
{
    juce::Array<juce::File> dirs;

    auto addIfExists = [&dirs](const juce::File& candidate)
    {
        if (candidate.isDirectory())
        {
            for (const auto& existing : dirs)
                if (existing.getFullPathName().equalsIgnoreCase(candidate.getFullPathName()))
                    return;
            dirs.add(candidate);
        }
    };

    const auto exeFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto exeDir = exeFile.getParentDirectory();
    for (int up = 0; up < 4 && exeDir.exists(); ++up)
    {
        addIfExists(exeDir.getChildFile("FactoryPresets"));
        exeDir = exeDir.getParentDirectory();
    }

    const auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    auto appDir = appFile.getParentDirectory();
    for (int up = 0; up < 6 && appDir.exists(); ++up)
    {
        addIfExists(appDir.getChildFile("FactoryPresets"));
        addIfExists(appDir.getChildFile("Contents").getChildFile("Resources").getChildFile("FactoryPresets"));
        appDir = appDir.getParentDirectory();
    }

#if defined(AUDIOCITY_SOURCE_DIR)
    addIfExists(juce::File(AUDIOCITY_SOURCE_DIR).getChildFile("assets").getChildFile("factory_presets"));
#endif

    return dirs;
}

bool AudiocityAudioProcessorEditor::isFactoryPresetFile(const juce::File& file) const
{
    if (!file.existsAsFile())
        return false;

    const auto candidate = file.getFullPathName();
    for (const auto& dir : getFactoryPresetDirectories())
    {
        if (candidate.startsWithIgnoreCase(dir.getFullPathName()))
            return true;
    }
    return false;
}

juce::String AudiocityAudioProcessorEditor::sanitizePresetName(const juce::String& rawName)
{
    auto name = rawName.trim();
    if (name.isEmpty())
        return {};

    static const juce::String kForbidden = "\\/:*?\"<>|";
    for (int i = 0; i < name.length(); ++i)
    {
        if (kForbidden.containsChar(name[i]))
            name = name.replaceCharacter(name[i], '_');
    }

    return name.trim();
}

juce::File AudiocityAudioProcessorEditor::presetFileForName(const juce::String& presetName) const
{
    return getPresetDirectory().getChildFile(presetName + kPresetFileExtension);
}

juce::File AudiocityAudioProcessorEditor::getSelectedPresetFile() const
{
    const auto selectedId = presetCombo_.getSelectedId();
    const auto index = selectedId - 1;
    if (index < 0 || index >= visiblePresetFiles_.size())
        return {};

    return visiblePresetFiles_.getReference(index);
}

void AudiocityAudioProcessorEditor::updatePresetActionButtons()
{
    const auto selectedFile = getSelectedPresetFile();
    const auto hasSelection = selectedFile.existsAsFile();
    const auto isFactory = hasSelection && isFactoryPresetFile(selectedFile);
    const auto hasLoadedSample = processor_.getLoadedSampleLength() > 0;

    presetSaveButton_.setEnabled(hasLoadedSample);
    presetRenameButton_.setEnabled(hasSelection && !isFactory);
    presetDeleteButton_.setEnabled(hasSelection && !isFactory);
}

void AudiocityAudioProcessorEditor::refreshPresetList(const juce::String& preferredPresetName)
{
    visiblePresetFiles_.clear();

    juce::StringArray allPresetNames;
    const auto filterText = presetFilterEditor_.getText().trim();
    const auto useSnapshotOverride = presetSnapshotNamesOverride_.size() > 0;

    if (useSnapshotOverride)
    {
        availablePresetFiles_.clear();
        allPresetNames.addArray(presetSnapshotNamesOverride_);
        allPresetNames.sort(true);
    }
    else
    {
        const auto presetDirectory = getPresetDirectory();
        if (!presetDirectory.exists())
            presetDirectory.createDirectory();

        juce::Array<juce::File> presetFiles;

        for (const auto& factoryDir : getFactoryPresetDirectories())
        {
            juce::Array<juce::File> factoryFiles;
            factoryDir.findChildFiles(factoryFiles, juce::File::TypesOfFileToFind::findFiles, true,
                juce::String("*") + kPresetFileExtension);
            for (const auto& f : factoryFiles)
                presetFiles.add(f);
        }

        juce::Array<juce::File> userFiles;
        presetDirectory.findChildFiles(userFiles, juce::File::TypesOfFileToFind::findFiles, false,
            juce::String("*") + kPresetFileExtension);

        juce::StringArray seenNames;
        for (const auto& f : presetFiles)
            seenNames.add(f.getFileNameWithoutExtension().toLowerCase());

        for (const auto& f : userFiles)
        {
            if (!seenNames.contains(f.getFileNameWithoutExtension().toLowerCase()))
                presetFiles.add(f);
        }

        std::sort(presetFiles.begin(), presetFiles.end(), [](const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension()) < 0;
        });

        availablePresetFiles_ = presetFiles;
        for (const auto& file : availablePresetFiles_)
            allPresetNames.add(file.getFileNameWithoutExtension());
    }

    suppressPresetComboChange_ = true;
    presetCombo_.clear(juce::dontSendNotification);

    juce::StringArray visiblePresetNames;
    for (int index = 0; index < allPresetNames.size(); ++index)
    {
        const auto& label = allPresetNames[index];
        if (filterText.isNotEmpty() && !label.containsIgnoreCase(filterText))
            continue;

        visiblePresetNames.add(label);
        presetCombo_.addItem(label, visiblePresetNames.size());

        if (!useSnapshotOverride && index < availablePresetFiles_.size())
            visiblePresetFiles_.add(availablePresetFiles_.getReference(index));
    }

    if (allPresetNames.isEmpty())
    {
        presetCountLabel_.setText("No presets", juce::dontSendNotification);
    }
    else if (filterText.isNotEmpty())
    {
        presetCountLabel_.setText(juce::String(visiblePresetNames.size()) + " of "
                + juce::String(allPresetNames.size()),
            juce::dontSendNotification);
    }
    else
    {
        presetCountLabel_.setText(juce::String(allPresetNames.size())
                + (allPresetNames.size() == 1 ? " preset" : " presets"),
            juce::dontSendNotification);
    }

    const auto selectionName = preferredPresetName.isNotEmpty() ? preferredPresetName : currentPresetName_;
    int selectedId = 0;
    for (int index = 0; index < visiblePresetNames.size(); ++index)
    {
        const auto& candidate = visiblePresetNames[index];
        if (candidate == selectionName)
        {
            selectedId = index + 1;
            break;
        }
    }

    if (selectedId == 0 && !useSnapshotOverride && filterText.isEmpty())
    {
        const auto currentPresetXml = processor_.createPlaybackPresetXml().trim();
        if (currentPresetXml.isNotEmpty())
        {
            for (int index = 0; index < visiblePresetFiles_.size(); ++index)
            {
                const auto candidateXml = visiblePresetFiles_.getReference(index).loadFileAsString().trim();
                if (candidateXml.isNotEmpty() && candidateXml == currentPresetXml)
                {
                    selectedId = index + 1;
                    break;
                }
            }
        }
    }

    if (selectedId > 0)
    {
        presetCombo_.setSelectedId(selectedId, juce::dontSendNotification);
        currentPresetName_ = presetCombo_.getText();
    }
    else
    {
        presetCombo_.setSelectedId(0, juce::dontSendNotification);
        if (allPresetNames.isEmpty() || useSnapshotOverride)
            currentPresetName_.clear();
    }

    suppressPresetComboChange_ = false;
    updatePresetActionButtons();
}

void AudiocityAudioProcessorEditor::savePreset(const juce::String& presetName)
{
    const auto sanitizedName = sanitizePresetName(presetName);
    if (sanitizedName.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Preset Save",
            "Preset name cannot be empty.");
        return;
    }

    const auto presetDirectory = getPresetDirectory();
    if (!presetDirectory.exists() && !presetDirectory.createDirectory())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Preset Save",
            "Unable to create preset directory.");
        return;
    }

    const auto presetXml = processor_.createPlaybackPresetXml();
    if (presetXml.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Preset Save",
            "Unable to build preset XML from current sample playback state.");
        return;
    }

    const auto file = presetFileForName(sanitizedName);
    if (!file.replaceWithText(presetXml))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Preset Save",
            "Failed to write preset file to disk.");
        return;
    }

    currentPresetName_ = sanitizedName;
    refreshPresetList(currentPresetName_);
}

void AudiocityAudioProcessorEditor::promptSavePreset()
{
    const auto defaultName = currentPresetName_.isNotEmpty() ? currentPresetName_ : juce::String("Preset");
    const auto initialFile = presetFileForName(defaultName);

    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Save Audiocity Preset",
        initialFile,
        juce::String("*") + kPresetFileExtension);

    auto chooserFlags = juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File{})
            return;

        if (!file.hasFileExtension(kPresetFileExtension))
            file = file.withFileExtension(kPresetFileExtension);

        const auto name = file.getFileName().upToLastOccurrenceOf(kPresetFileExtension, false, false);
        if (name.isNotEmpty())
            savePreset(name);
    });
}

void AudiocityAudioProcessorEditor::showPresetLoadErrorAndOfferDelete(const juce::File& presetFile,
    const juce::String& errorMessage)
{
    const auto presetName = presetFile.getFileName().upToLastOccurrenceOf(kPresetFileExtension, false, false);
    juce::String message;
    message << "There was an error loading preset '" << presetName << "'.\n\n";
    if (errorMessage.isNotEmpty())
        message << errorMessage << "\n\n";
    message << "Delete this preset from disk?";

    auto options = juce::MessageBoxOptions::makeOptionsYesNo(
        juce::MessageBoxIconType::WarningIcon,
        "Preset Load Error",
        message,
        "Delete Preset",
        "Keep Preset",
        this);

    juce::Component::SafePointer<AudiocityAudioProcessorEditor> safeThis(this);
    juce::NativeMessageBox::showAsync(options, [safeThis, presetFile](int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == 0)
        {
            const auto deletedName = presetFile.getFileName().upToLastOccurrenceOf(kPresetFileExtension, false, false);
            if (!presetFile.deleteFile())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Delete Preset",
                    "Failed to delete preset file.");
                return;
            }

            if (safeThis->currentPresetName_ == deletedName)
                safeThis->currentPresetName_.clear();

            safeThis->refreshPresetList();
        }
    });
}

void AudiocityAudioProcessorEditor::loadPresetFromSelection()
{
    const auto file = getSelectedPresetFile();
    if (file == juce::File{})
        return;

    if (!file.existsAsFile())
    {
        showPresetLoadErrorAndOfferDelete(file, "Preset file does not exist.");
        return;
    }

    const auto presetXml = file.loadFileAsString();
    if (presetXml.isEmpty())
    {
        showPresetLoadErrorAndOfferDelete(file, "Failed to read preset file.");
        return;
    }

    juce::String errorMessage;
    if (!processor_.loadPlaybackPresetXml(presetXml, errorMessage))
    {
        showPresetLoadErrorAndOfferDelete(file,
            errorMessage.isNotEmpty() ? errorMessage : "Failed to load preset.");
        return;
    }

    currentPresetName_ = presetCombo_.getText();
    refreshUI(true);
}

void AudiocityAudioProcessorEditor::renameSelectedPreset()
{
    const auto currentFile = getSelectedPresetFile();
    if (currentFile == juce::File{})
        return;
    const auto currentName = currentFile.getFileName().upToLastOccurrenceOf(kPresetFileExtension, false, false);

    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Rename Audiocity Preset",
        presetFileForName(currentName),
        juce::String("*") + kPresetFileExtension);

    auto chooserFlags = juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser_->launchAsync(chooserFlags, [this, currentFile, currentName](const juce::FileChooser& chooser)
    {
        auto selectedFile = chooser.getResult();
        if (selectedFile == juce::File{})
            return;

        if (!selectedFile.hasFileExtension(kPresetFileExtension))
            selectedFile = selectedFile.withFileExtension(kPresetFileExtension);

        const auto renamed = sanitizePresetName(
            selectedFile.getFileName().upToLastOccurrenceOf(kPresetFileExtension, false, false));
        if (renamed.isEmpty() || renamed == currentName)
            return;

        const auto renamedFile = presetFileForName(renamed);
        if (renamedFile.existsAsFile())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Rename Preset",
                "A preset with this name already exists.");
            return;
        }

        if (!currentFile.moveFileTo(renamedFile))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Rename Preset",
                "Failed to rename preset file.");
            return;
        }

        currentPresetName_ = renamed;
        refreshPresetList(currentPresetName_);
    });
}

void AudiocityAudioProcessorEditor::deleteSelectedPreset()
{
    const auto file = getSelectedPresetFile();
    if (file == juce::File{})
        return;
    const auto presetName = file.getFileName().upToLastOccurrenceOf(kPresetFileExtension, false, false);

    juce::String message;
    message << "Are you sure you want to delete preset '" << presetName << "'?";

    auto options = juce::MessageBoxOptions::makeOptionsYesNo(
        juce::MessageBoxIconType::WarningIcon,
        "Delete Preset",
        message,
        "Delete",
        "Cancel",
        this);

    juce::Component::SafePointer<AudiocityAudioProcessorEditor> safeThis(this);
    juce::NativeMessageBox::showAsync(options, [safeThis, file, presetName](int result)
    {
        if (safeThis == nullptr)
            return;

        if (result != 0)
            return;

        if (!file.deleteFile())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Delete Preset",
                "Failed to delete preset file.");
            return;
        }

        if (safeThis->currentPresetName_ == presetName)
            safeThis->currentPresetName_.clear();

        safeThis->refreshPresetList();
    });
}

void AudiocityAudioProcessorEditor::clearSelectedPresetAfterSourceLoad()
{
    currentPresetName_.clear();
    suppressPresetComboChange_ = true;
    presetCombo_.setSelectedId(0, juce::dontSendNotification);
    suppressPresetComboChange_ = false;
    updatePresetActionButtons();
}

void AudiocityAudioProcessorEditor::updateGeneratePreviewButtonText()
{
    generatePreviewButton_.setButtonText(processor_.isGeneratedWaveformPreviewPlaying() ? "Stop" : "Play");

    const auto hasCapturedAudio = processor_.getCapturedInputSamples() > 0;
    const auto samplePreviewPlaying = processor_.isSamplePreviewPlaying();
    capturePlayButton_.setEnabled(hasCapturedAudio || samplePreviewPlaying);
    capturePlayButton_.setButtonText(samplePreviewPlaying ? "Stop" : "Play");
}

void AudiocityAudioProcessorEditor::refreshCaptureWaveform(const bool force)
{
    const auto totalSamples = processor_.getCapturedInputSamples();
    const auto recording = processor_.isInputCaptureRecording();
    if (!force && !recording && totalSamples == captureLastSamples_ && recording == captureLastRecording_)
        return;

    captureLastSamples_ = totalSamples;
    captureLastRecording_ = recording;
    captureDisplayTotalSamples_ = totalSamples;

    const auto sampleRate = juce::jmax(1.0, processor_.getCapturedInputSampleRate());
    const auto followWindowSamples = static_cast<int>(std::round(sampleRate * 4.0));
    captureDisplayVisibleStart_ = 0;
    captureDisplayVisibleEnd_ = totalSamples;
    if (recording && totalSamples > followWindowSamples)
        captureDisplayVisibleStart_ = totalSamples - followWindowSamples;

    captureDisplayVisibleEnd_ = juce::jmax(captureDisplayVisibleStart_, totalSamples);

    captureSelectionStart_ = juce::jlimit(0, captureDisplayTotalSamples_, captureSelectionStart_);
    captureSelectionEnd_ = juce::jlimit(captureSelectionStart_, captureDisplayTotalSamples_, captureSelectionEnd_);

    const auto peakResolution = computeWaveformPeakResolution(captureWaveformView_.getWidth());
    const auto waveform = processor_.buildCapturedWaveformMinMax(
        peakResolution,
        captureDisplayVisibleStart_,
        captureDisplayVisibleEnd_);

    std::vector<CaptureWaveformView::MinMax> converted;
    converted.reserve(waveform.size());
    for (const auto& peak : waveform)
        converted.push_back({ peak.minValue, peak.maxValue });

    captureWaveformView_.setState(
        captureDisplayTotalSamples_,
        captureDisplayVisibleStart_,
        captureDisplayVisibleEnd_,
        std::move(converted),
        captureSelectionStart_,
        captureSelectionEnd_,
        processor_.getCapturedInputSampleRate(),
        recording);
}

void AudiocityAudioProcessorEditor::updateCaptureUiState()
{
    const auto recording = processor_.isInputCaptureRecording();
    const auto totalSamples = processor_.getCapturedInputSamples();
    const auto hasSelection = captureSelectionEnd_ > captureSelectionStart_;
    const auto hasAudio = totalSamples > 0;

    captureRecordButton_.setButtonText(recording ? "Stop" : "Record");
    captureCutButton_.setEnabled(hasSelection && !recording);
    captureTrimButton_.setEnabled(hasSelection && !recording);
    captureClearButton_.setEnabled(hasAudio && !recording);
    captureLoadAsSampleButton_.setEnabled((hasSelection || hasAudio) && !recording);

    const auto sampleRate = juce::jmax(1.0, processor_.getCapturedInputSampleRate());
    const auto totalMs = static_cast<int>(std::round((static_cast<double>(totalSamples) * 1000.0) / sampleRate));
    const auto sec = totalMs / 1000;
    const auto ms = totalMs % 1000;

    juce::String status = recording
        ? "Recording"
        : "Ready";
    status << "  |  Length: " << sec << "." << juce::String(ms).paddedLeft('0', 3) << " s";
    if (hasSelection)
    {
        const auto selectedSamples = captureSelectionEnd_ - captureSelectionStart_;
        const auto selectedMs = static_cast<int>(std::round((static_cast<double>(selectedSamples) * 1000.0) / sampleRate));
        status << "  |  Selection: "
            << captureSelectionStart_ << ".." << captureSelectionEnd_
            << " [" << selectedSamples << " samples]"
            << " (" << (selectedMs / 1000) << "." << juce::String(selectedMs % 1000).paddedLeft('0', 3) << " s)";
    }

    if (processor_.didInputCaptureOverflow())
        status << "  |  Max length reached";

    captureStatusLabel_.setText(status, juce::dontSendNotification);
}

std::vector<std::vector<AudiocityAudioProcessorEditor::WaveformView::MinMax>>
AudiocityAudioProcessorEditor::getLoadedSampleWaveformMinMaxByChannel(const int maxPeaks) const
{
    const auto source = processor_.getLoadedSampleMinMaxByChannel(maxPeaks);
    std::vector<std::vector<WaveformView::MinMax>> converted;
    converted.resize(source.size());

    for (std::size_t channel = 0; channel < source.size(); ++channel)
    {
        const auto& srcChannel = source[channel];
        auto& dstChannel = converted[channel];
        dstChannel.resize(srcChannel.size());
        for (std::size_t i = 0; i < srcChannel.size(); ++i)
        {
            dstChannel[i].min = srcChannel[i].minValue;
            dstChannel[i].max = srcChannel[i].maxValue;
        }
    }

    return converted;
}

int AudiocityAudioProcessorEditor::getSelectedGenerateSampleCount() const
{
    const auto selected = generateSamplesCombo_.getSelectedId();
    return juce::jlimit(16, 8192, selected > 0 ? selected : 1024);
}

int AudiocityAudioProcessorEditor::getSelectedGenerateBitDepth() const
{
    const auto selected = generateBitDepthCombo_.getSelectedId();
    return (selected == 8 || selected == 24) ? selected : 16;
}

int AudiocityAudioProcessorEditor::getSelectedGenerateMidiNote() const
{
    const auto selected = generateFrequencyCombo_.getSelectedId();
    return juce::jlimit(0, 127, selected - 1);
}

float AudiocityAudioProcessorEditor::quantizeWaveSample(const float value, const int bitDepth) const
{
    const auto clamped = juce::jlimit(-1.0f, 1.0f, value);
    if (bitDepth >= 24)
        return std::round(clamped * 8388607.0f) / 8388607.0f;
    if (bitDepth <= 8)
        return std::round(clamped * 127.0f) / 127.0f;
    return std::round(clamped * 32767.0f) / 32767.0f;
}

void AudiocityAudioProcessorEditor::enforceWaveBoundaryZeroCrossings(std::vector<float>& waveform) const
{
    if (waveform.empty())
        return;

    waveform.front() = 0.0f;
    waveform.back() = 0.0f;

    if (waveform.size() > 2)
    {
        waveform[1] *= 0.5f;
        waveform[waveform.size() - 2] *= 0.5f;
    }
}

void AudiocityAudioProcessorEditor::applySketchedWaveform(const std::vector<float>& sketchedWave)
{
    if (sketchedWave.empty())
        return;

    auto processed = sketchedWave;

    if (selectedSketchSmoothing_ == SketchedWaveSmoothing::curve && processed.size() >= 3)
    {
        auto smoothed = processed;
        for (std::size_t i = 1; i + 1 < processed.size(); ++i)
            smoothed[i] = (processed[i - 1] + processed[i] * 2.0f + processed[i + 1]) * 0.25f;
        processed.swap(smoothed);
    }

    enforceWaveBoundaryZeroCrossings(processed);

    const auto bitDepth = getSelectedGenerateBitDepth();
    for (auto& sample : processed)
        sample = quantizeWaveSample(sample, bitDepth);

    generatedWaveform_ = std::move(processed);
    generateWaveformView_.setWaveform(generatedWaveform_);
    processor_.setGeneratedWaveformPreview(generatedWaveform_);
}

void AudiocityAudioProcessorEditor::updateGeneratePulseWidthControlState()
{
    const auto enabled = selectedGeneratedWaveType_ == GeneratedWaveType::pulse;
    generatePulseWidthLabel_.setEnabled(enabled);
    generatePulseWidthSlider_.setEnabled(enabled);
}

void AudiocityAudioProcessorEditor::updateAmpEnvelopeGraphFromDials()
{
    ampEnvelopeGraph_.setEnvelope(static_cast<float>(ampAttackDial_.getValue()),
                                  static_cast<float>(ampDecayDial_.getValue()),
                                  static_cast<float>(ampSustainDial_.getValue()) / 100.0f,
                                  static_cast<float>(ampReleaseDial_.getValue()));
}

void AudiocityAudioProcessorEditor::updateFilterEnvelopeGraphFromDials()
{
    filterEnvelopeGraph_.setEnvelope(static_cast<float>(filterAttackDial_.getValue()),
                                     static_cast<float>(filterDecayDial_.getValue()),
                                     static_cast<float>(filterSustainDial_.getValue()) / 100.0f,
                                     static_cast<float>(filterReleaseDial_.getValue()));
}

void AudiocityAudioProcessorEditor::updateFilterResponseGraphFromControls()
{
    const auto modeId = filterTypeCombo_.getSelectedId();
    const auto cutoffHz = static_cast<float>(filterCutoffDial_.getValue());
    const auto resonance = static_cast<float>(filterResDial_.getValue()) / 100.0f;
    const auto envAmountHz = static_cast<float>(filterEnvAmtDial_.getValue());
    filterResponseGraph_.setState(modeId, cutoffHz, resonance, envAmountHz);
}

void AudiocityAudioProcessorEditor::regenerateWaveform()
{
    const auto sampleCount = getSelectedGenerateSampleCount();
    const auto bitDepth = getSelectedGenerateBitDepth();

    generatedWaveform_.assign(static_cast<std::size_t>(sampleCount), 0.0f);

    if (selectedGeneratedWaveType_ == GeneratedWaveType::random && sampleCount >= 2)
    {
        struct Anchor
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        struct CurveSegment
        {
            Anchor p0;
            Anchor p1;
            Anchor p2;
            Anchor p3;
        };

        juce::Random random(juce::Random::getSystemRandom().nextInt());
        const auto anchorCount = random.nextInt(juce::Range<int>(3, 10));

        std::vector<Anchor> anchors;
        anchors.resize(static_cast<std::size_t>(anchorCount));
        anchors.front() = { 0.0f, 0.0f };
        anchors.back() = { 1.0f, 0.0f };

        std::vector<float> segmentWeights;
        segmentWeights.resize(static_cast<std::size_t>(anchorCount - 1), 0.0f);
        float totalWeight = 0.0f;
        for (int i = 0; i < anchorCount - 1; ++i)
        {
            const auto w = 0.25f + random.nextFloat();
            segmentWeights[static_cast<std::size_t>(i)] = w;
            totalWeight += w;
        }

        float cumulative = 0.0f;
        for (int i = 1; i < anchorCount - 1; ++i)
        {
            cumulative += segmentWeights[static_cast<std::size_t>(i - 1)] / totalWeight;
            anchors[static_cast<std::size_t>(i)].x = juce::jlimit(0.0f, 1.0f, cumulative);
            anchors[static_cast<std::size_t>(i)].y = (random.nextFloat() * 2.0f) - 1.0f;
        }

        std::vector<CurveSegment> curves;
        curves.reserve(static_cast<std::size_t>(anchorCount - 1));
        for (int i = 0; i < anchorCount - 1; ++i)
        {
            const auto p0 = anchors[static_cast<std::size_t>(i)];
            const auto p3 = anchors[static_cast<std::size_t>(i + 1)];
            const auto dx = p3.x - p0.x;

            CurveSegment segment;
            segment.p0 = p0;
            segment.p3 = p3;
            segment.p1.x = p0.x + dx * (1.0f / 3.0f);
            segment.p2.x = p0.x + dx * (2.0f / 3.0f);
            segment.p1.y = juce::jlimit(-1.0f, 1.0f, p0.y + ((random.nextFloat() * 2.0f) - 1.0f));
            segment.p2.y = juce::jlimit(-1.0f, 1.0f, p3.y + ((random.nextFloat() * 2.0f) - 1.0f));
            curves.push_back(segment);
        }

        int segmentIndex = 0;
        const auto lastSample = juce::jmax(1, sampleCount - 1);
        for (int i = 0; i < sampleCount; ++i)
        {
            const auto x = static_cast<float>(i) / static_cast<float>(lastSample);
            while (segmentIndex < anchorCount - 2
                && x > anchors[static_cast<std::size_t>(segmentIndex + 1)].x)
            {
                ++segmentIndex;
            }

            const auto& left = anchors[static_cast<std::size_t>(segmentIndex)];
            const auto& right = anchors[static_cast<std::size_t>(segmentIndex + 1)];
            const auto span = juce::jmax(1.0e-6f, right.x - left.x);
            const auto t = juce::jlimit(0.0f, 1.0f, (x - left.x) / span);

            float value = 0.0f;
            const auto& curve = curves[static_cast<std::size_t>(segmentIndex)];
            const auto omt = 1.0f - t;
            const auto omt2 = omt * omt;
            const auto t2 = t * t;
            value = (omt2 * omt) * curve.p0.y
                + (3.0f * omt2 * t) * curve.p1.y
                + (3.0f * omt * t2) * curve.p2.y
                + (t2 * t) * curve.p3.y;

            generatedWaveform_[static_cast<std::size_t>(i)] = quantizeWaveSample(value, bitDepth);
        }

        enforceWaveBoundaryZeroCrossings(generatedWaveform_);
        generateWaveformView_.setWaveform(generatedWaveform_);
        processor_.setGeneratedWaveformPreview(generatedWaveform_);
        processor_.setGeneratedWaveformPreviewMidiNote(getSelectedGenerateMidiNote());
        return;
    }

    for (int i = 0; i < sampleCount; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(sampleCount);
        float value = 0.0f;

        switch (selectedGeneratedWaveType_)
        {
            case GeneratedWaveType::sine:
                value = std::sin(phase * 2.0f * juce::MathConstants<float>::pi);
                break;
            case GeneratedWaveType::ramp:
                value = (phase * 2.0f) - 1.0f;
                break;
            case GeneratedWaveType::square:
                value = phase < 0.5f ? 1.0f : -1.0f;
                break;
            case GeneratedWaveType::sawtooth:
                value = 1.0f - (phase * 2.0f);
                break;
            case GeneratedWaveType::triangle:
                value = 1.0f - 4.0f * std::abs(phase - 0.5f);
                break;
            case GeneratedWaveType::pulse:
            {
                const auto pulseWidth = static_cast<float>(generatePulseWidthSlider_.getValue() * 0.01);
                value = phase < pulseWidth ? 1.0f : -1.0f;
                break;
            }
            case GeneratedWaveType::random:
                value = 0.0f;
                break;
        }

        generatedWaveform_[static_cast<std::size_t>(i)] = quantizeWaveSample(value, bitDepth);
    }

    enforceWaveBoundaryZeroCrossings(generatedWaveform_);

    generateWaveformView_.setWaveform(generatedWaveform_);
    processor_.setGeneratedWaveformPreview(generatedWaveform_);
    processor_.setGeneratedWaveformPreviewMidiNote(getSelectedGenerateMidiNote());
}

// ─── Group box rendering ───────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::clearSampleInformationComponentBounds()
{
    sampleInfoSourceLabel_.setBounds({});
    sampleInfoSourceValue_.setBounds({});
    sampleInfoRateLabel_.setBounds({});
    sampleInfoRateValue_.setBounds({});
    sampleInfoBitDepthLabel_.setBounds({});
    sampleInfoBitDepthValue_.setBounds({});
    sampleInfoChannelsLabel_.setBounds({});
    sampleInfoChannelsValue_.setBounds({});
    sampleInfoDurationLabel_.setBounds({});
    sampleInfoDurationValue_.setBounds({});
    sampleInfoFileSizeLabel_.setBounds({});
    sampleInfoFileSizeValue_.setBounds({});
    sampleInfoSamplesLabel_.setBounds({});
    sampleInfoSamplesValue_.setBounds({});
    sampleInfoPlaybackLabel_.setBounds({});
    sampleInfoPlaybackValue_.setBounds({});
    sampleInfoPlaybackDurationLabel_.setBounds({});
    sampleInfoPlaybackDurationValue_.setBounds({});
    sampleInfoLoopLabel_.setBounds({});
    sampleInfoLoopValue_.setBounds({});
    sampleInfoLoopDurationLabel_.setBounds({});
    sampleInfoLoopDurationValue_.setBounds({});
    sampleInfoTempoLabel_.setBounds({});
    sampleInfoTempoValue_.setBounds({});
    sampleInfoMetaRootLabel_.setBounds({});
    sampleInfoMetaRootValue_.setBounds({});
    sampleInfoBadge_.setBounds({});
}

void AudiocityAudioProcessorEditor::paintSampleInspectorPane(juce::Graphics& g) const
{
    if (!sampleInspectorInfoBounds_.isEmpty())
        paintSectionCard(g, sampleInspectorInfoBounds_.toFloat(), "Sample Information");

    if (!sampleInspectorProgramMapBounds_.isEmpty())
        paintSectionCard(g, sampleInspectorProgramMapBounds_.toFloat(), "Program Map");

    if (!sampleInspectorOutputBounds_.isEmpty())
        paintSectionCard(g, sampleInspectorOutputBounds_.toFloat(), "Output");

    if (!sampleInspectorFilterModBounds_.isEmpty())
    {
        paintSectionCard(g, sampleInspectorFilterModBounds_.toFloat(), "Filter Envelope + Mod");
        paintSampleInspectorCardToggle(g, sampleInspectorFilterModBounds_, sampleInspectorFilterModExpanded_);
    }

    if (!sampleInspectorEffectsBounds_.isEmpty())
    {
        paintSectionCard(g, sampleInspectorEffectsBounds_.toFloat(), "Effects");
        paintSampleInspectorCardToggle(g, sampleInspectorEffectsBounds_, sampleInspectorEffectsExpanded_);
    }
}

void AudiocityAudioProcessorEditor::paintGroupBoxes(juce::Graphics& g) const
{
    for (const auto& group : groupBoxes_)
    {
        paintSectionCard(g, group.bounds.toFloat(), group.title);

        if (!group.collapsible)
            continue;

        g.setColour(group.expanded ? uiTextStrongColour() : uiTextMutedColour().brighter(0.08f));
        g.setFont(juce::Font(juce::FontOptions(10.5f)).boldened());
        g.drawText(group.expanded ? "- Hide" : "+ Show", getSectionCardToggleBounds(group.bounds), juce::Justification::centredRight, false);
    }
}

// ─── Tooltips ──────────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::setupTooltips()
{
    samplePathLabel_.setTooltip(
        "Shortcuts: Ctrl+O Load  |  Ctrl+Alt+D Toggle Tech Panel  |  Ctrl+Z Undo  |  Ctrl+Y Redo  |  Ctrl+S Save State  |  Ctrl+Shift+S Save Preset  |  Ctrl+D Duplicate Zone  |  Ctrl+Shift+D Split Zone  |  Delete Remove Zone  |  Space Play/Stop (Generate)  |  1-7 Switch Tabs");
    presetCombo_.setTooltip(
        "Preset Browser - Select a saved sample playback preset");
    presetSaveButton_.setTooltip(
        "Save Preset - Save current sample playback settings to XML (.acp)");
    presetRenameButton_.setTooltip(
        "Rename Preset - Rename selected preset file");
    presetDeleteButton_.setTooltip(
        "Delete Preset - Remove selected preset file");
    loadButton_.setTooltip(
        processor_.isRexRuntimeAvailable() ? "Load Sample, SFZ, or REX (Ctrl+O)" : "Load Sample (Ctrl+O)");
    diagnosticsToggleButton_.setTooltip(
        "Show or hide preload, stream, and voice diagnostics (Ctrl+Alt+D)");
    waveformResetRangesButton_.setTooltip(
        "Reset playback and loop ranges to the full sample");
    waveformInteractionSummaryLabel_.setTooltip(
        "Trim and loop are edited directly in the waveform: drag handles, Shift-drag loop handles to move playback too, use the mouse wheel to zoom, and middle-drag to pan");
    generatePreviewButton_.setTooltip(
        "Play/Stop Generate Preview (Space on Generate tab)");

    sampleBrowserChooseRootButton_.setTooltip(
        "Select Sample Folder...");
    sampleBrowserBookmarkCombo_.setTooltip(
        "Bookmarked Sample Folders - Select a saved library root to scan");
    sampleBrowserAddBookmarkButton_.setTooltip(
        "Bookmark Current Sample Folder");
    sampleBrowserRemoveBookmarkButton_.setTooltip(
        "Remove Selected Bookmarked Folder");
    sampleBrowserFilterEditor_.setTooltip(
        "Search Samples - Filter by sample name or relative path");
    sampleBrowserSortCombo_.setTooltip(
        "Sort Samples - Sort by name, path, or recent use");
    sampleBrowserFavoriteButton_.setTooltip(
        "Favorite Selected Item");
    sampleBrowserFavoritesOnlyToggle_.setTooltip(
        "Show Favorite Items Only");
    sampleBrowserRecentOnlyToggle_.setTooltip(
        "Show Recent Items Only");
    sampleBrowserTagFilterCombo_.setTooltip(
        "Tag Filter - Show items with a known tag");
    sampleBrowserTagsEditor_.setTooltip(
        "Tags for Selected Item");
    sampleBrowserApplyTagsButton_.setTooltip(
        "Apply Tags to Selected Item");
    mappingRefreshButton_.setTooltip(
        "Refresh Mapping View");
    mappingCreateZoneButton_.setTooltip(
        "Create a New Mapping Zone");
    mappingDuplicateZoneButton_.setTooltip(
        "Duplicate Selected Zone");
    mappingSplitZoneButton_.setTooltip(
        "Split Selected Zone Across the Key Range Midpoint");
    mappingDeleteZoneButton_.setTooltip(
        "Delete Selected Zone");
    mappingZoneListBox_.setTooltip(
        "Imported Program Zones");
    mappingDetailsText_.setTooltip(
        "Selected Zone Details");
    mappingEditKeyLowSlider_.setTooltip(
        "Selected Zone Key Low");
    mappingEditKeyHighSlider_.setTooltip(
        "Selected Zone Key High");
    mappingEditVelocityLowSlider_.setTooltip(
        "Selected Zone Velocity Low");
    mappingEditVelocityHighSlider_.setTooltip(
        "Selected Zone Velocity High");
    mappingEditVelocityFadeInLowSlider_.setTooltip(
        "Selected Zone Velocity Fade In Low Endpoint; set Off to disable");
    mappingEditVelocityFadeInHighSlider_.setTooltip(
        "Selected Zone Velocity Fade In High Endpoint; set Off to disable");
    mappingEditVelocityFadeOutLowSlider_.setTooltip(
        "Selected Zone Velocity Fade Out Low Endpoint; set Off to disable");
    mappingEditVelocityFadeOutHighSlider_.setTooltip(
        "Selected Zone Velocity Fade Out High Endpoint; set Off to disable");
    mappingEditRootSlider_.setTooltip(
        "Selected Zone Root Note");
    mappingEditSampleStartSlider_.setTooltip(
        "Selected Zone Sample Start");
    mappingEditSampleEndSlider_.setTooltip(
        "Selected Zone Sample End");
    mappingEditLoopStartSlider_.setTooltip(
        "Selected Zone Loop Start");
    mappingEditLoopEndSlider_.setTooltip(
        "Selected Zone Loop End");
    mappingEditGainSlider_.setTooltip(
        "Selected Zone Gain in dB");
    mappingEditPanSlider_.setTooltip(
        "Selected Zone Pan - Left to Right");
    mappingEditRoundRobinGroupSlider_.setTooltip(
        "Selected Zone Round Robin Group");
    mappingEditRoundRobinPositionSlider_.setTooltip(
        "Selected Zone Round Robin Position");
    mappingEditRoundRobinModeCombo_.setTooltip(
        "Selected Zone Round Robin Selection Mode");
    mappingEditChokeSlider_.setTooltip(
        "Selected Zone Choke Group");
    mappingEditTriggerCombo_.setTooltip(
        "Selected Zone Trigger Mode");
    mappingEditLoopCombo_.setTooltip(
        "Selected Zone Loop Mode; batch-enabled for multi-selection");
    mappingEditApplyButton_.setTooltip(
        "Apply zone mapping; with multiple selected rows, Apply uses edited batch controls for every selected zone");
    mappingZoneListBox_.setTooltip(
        "Imported Program Zones - Ctrl+N creates a zone; Ctrl+A selects all rows; Ctrl or Shift multi-select enables batch edit for velocity fades, gain, pan, RR group/mode, choke, trigger, and loop, or right-click for New, Duplicate, Split, Map Chromatically, Delete, and Clear Velocity Fades");
    captureRecordButton_.setTooltip(
        "Start/Stop Capture from Plugin Input");
    captureClearButton_.setTooltip(
        "Clear Captured Audio Buffer");
    captureCutButton_.setTooltip(
        "Cut Selected Region");
    captureTrimButton_.setTooltip(
        "Trim to Selected Region");
    captureLoadAsSampleButton_.setTooltip(
        "Load Capture (or Selection) as Current Sample");
    capturePlayButton_.setTooltip(
        "Play/Stop Captured Audio Preview");
    captureNormalizeButton_.setTooltip(
        "Normalize captured audio to 90% of maximum amplitude");
    captureRootNoteLabel_.setTooltip(
        "Capture Root Note - Applied when loading captured audio as sample");
    captureRootNoteCombo_.setTooltip(
        "Capture Root Note - Applied when loading captured audio as sample");
    captureInputLevelSlider_.setTooltip(
        "Capture Input Level - Scales recorded audio level from 0% to 200%");

    rootNoteLabel_.setTooltip(
        "Root Note - MIDI note number and pitch name for the sample's original pitch");
    rootNoteCombo_.setTooltip(
        "Root Note - MIDI note number and pitch name for the sample's original pitch");
    tuneCoarseDial_.setLabelTooltip(
        "Tune Coarse - Shift playback pitch in semitones (-24 to +24)");
    tuneFineDial_.setLabelTooltip(
        "Tune Fine - Shift playback pitch in cents (-100 to +100)");
    pitchBendRangeDial_.setLabelTooltip(
        "Pitch Bend Range - Maximum pitch wheel range in semitones (0 to 24)");
    pitchLfoRateDial_.setLabelTooltip(
        "Pitch LFO Rate - Vibrato speed in Hz");
    pitchLfoDepthDial_.setLabelTooltip(
        "Pitch LFO Depth - Vibrato amount in cents");
    modulationPanel_.setControlTooltips();
    playbackStartDial_.setLabelTooltip(
        "Playback Start - Sample position where playback begins");
    playbackEndDial_.setLabelTooltip(
        "Playback End - Sample position where playback ends");
    loopStartDial_.setLabelTooltip(
        "Loop Start - Sample position where the loop region begins");
    loopEndDial_.setLabelTooltip(
        "Loop End - Sample position where the loop region ends");
    loopCrossfadeDial_.setLabelTooltip(
        "Loop Crossfade - Crossfade length in samples at loop wrap point");
    glideDial_.setLabelTooltip(
        "Glide Time - Portamento time between notes in milliseconds");
    polyphonyDial_.setLabelTooltip(
        "Polyphony Limit - Maximum simultaneous voices (1 to 64)");
    ampAttackDial_.setLabelTooltip(
        "Attack - Amplitude envelope attack time in milliseconds");
    ampDecayDial_.setLabelTooltip(
        "Decay - Amplitude envelope decay time in milliseconds");
    ampSustainDial_.setLabelTooltip(
        "Sustain - Amplitude envelope sustain level (0% to 100%)");
    ampReleaseDial_.setLabelTooltip(
        "Release - Amplitude envelope release time in milliseconds");
    ampLfoRateDial_.setLabelTooltip(
        "Amp LFO Rate - Tremolo speed in Hz");
    ampLfoDepthDial_.setLabelTooltip(
        "Amp LFO Depth - Tremolo amount from 0% (off) to 100% (full)");
    ampLfoShapeLabel_.setTooltip(
        "Amp LFO Shape - Waveform used for tremolo modulation");
    ampLfoShapeCombo_.setTooltip(
        "Amp LFO Shape - Choose Sine, Triangle, Square, Saw Up, or Saw Down");
    filterCutoffDial_.setLabelTooltip(
        "Filter Cutoff - Low-pass filter frequency in Hz");
    filterResDial_.setLabelTooltip(
        "Resonance - Filter emphasis at the cutoff frequency");
    filterEnvAmtDial_.setLabelTooltip(
        "Envelope Amount - Bipolar filter envelope modulation depth in Hz");
    filterAttackDial_.setLabelTooltip(
        "Filter Attack - Filter envelope attack time in milliseconds");
    filterDecayDial_.setLabelTooltip(
        "Filter Decay - Filter envelope decay time in milliseconds");
    filterSustainDial_.setLabelTooltip(
        "Filter Sustain - Filter envelope sustain level (0% to 100%)");
    filterReleaseDial_.setLabelTooltip(
        "Filter Release - Filter envelope release time in milliseconds");
    filterKeytrackDial_.setLabelTooltip(
        "Filter Key Tracking - Scales cutoff by keyboard pitch (-100% to 200%)");
    filterVelDial_.setLabelTooltip(
        "Filter Velocity Amount - Bipolar cutoff offset driven by note velocity");
    filterLfoRateDial_.setLabelTooltip(
        "Filter LFO Rate - Modulation speed in Hz");
    filterLfoRateKeyDial_.setLabelTooltip(
        "Filter LFO Rate Keytracking - Scales LFO speed across keyboard (-100% to 200%)");
    filterLfoAmtDial_.setLabelTooltip(
        "Filter LFO Amount - Bipolar cutoff modulation depth in Hz");
    filterLfoAmtKeyDial_.setLabelTooltip(
        "Filter LFO Amount Keytracking - Scales LFO depth across keyboard (-100% to 200%)");
    filterLfoStartPhaseDial_.setLabelTooltip(
        "Filter LFO Start Phase - Retrigger start offset in degrees (0 to 360)");
    filterLfoStartRandDial_.setLabelTooltip(
        "Filter LFO Start Random - Adds deterministic bipolar random offset per note");
    filterLfoFadeInDial_.setLabelTooltip(
        "Filter LFO Fade In - Time to ramp LFO depth from 0 to full per note");
    filterLfoShapeLabel_.setTooltip(
        "Filter LFO Shape - Waveform used for filter modulation");
    filterLfoShapeCombo_.setTooltip(
        "Filter LFO Shape - Choose Sine, Triangle, Square, Saw Up, or Saw Down");
    filterLfoRetriggerToggle_.setTooltip(
        "Filter LFO Retrigger - Restart LFO phase at note-on when enabled");
    filterLfoTempoSyncToggle_.setTooltip(
        "Filter LFO Tempo Sync - Locks LFO rate to host tempo using musical divisions");
    filterLfoRateKeySyncToggle_.setTooltip(
        "Filter LFO Key Sync - Apply LFO rate keytracking while tempo sync is enabled");
    filterLfoKeytrackLinearToggle_.setTooltip(
        "Filter LFO Key Linear - Use linear keytracking curve for LFO rate and amount when enabled");
    filterLfoUnipolarToggle_.setTooltip(
        "Filter LFO Unipolar - Convert LFO from bipolar (-1..1) to unipolar (0..1)");
    filterLfoDivisionLabel_.setTooltip(
        "Filter LFO Division - Note length used when tempo sync is enabled");
    filterLfoDivisionCombo_.setTooltip(
        "Filter LFO Division - Select 1/16 through 2/1 sync rates");
    filterKeytrackSnapLabel_.setTooltip(
        "Key Snap - Quick preset values for key tracking");
    filterKeytrackSnapCombo_.setTooltip(
        "Key Snap - Quickly set key tracking to musical preset percentages");
    filterTypeCombo_.setTooltip(
        "Filter Type - Select mode/slope (LP/HP/BP/Notch)");
    filterTypeLabel_.setTooltip(
        "Filter Type - Select mode/slope (LP/HP/BP/Notch)");
    fadeInDial_.setLabelTooltip(
        "Fade In - Number of samples to fade in at playback start");
    fadeOutDial_.setLabelTooltip(
        "Fade Out - Number of samples to fade out at playback end");
    preloadDial_.setLabelTooltip(
        "Preload - Number of samples buffered before streaming begins");
    masterVolumeDial_.setLabelTooltip(
        "Master Volume - Final output gain after engine processing");
    panDial_.setLabelTooltip(
        "Pan - Stereo balance from -100L to +100R, applied pre-reverb");
    qualityCpuButton_.setTooltip(
        "Quality - Prioritize lower CPU usage");
    qualityFidelityButton_.setTooltip(
        "Quality - Prioritize highest playback fidelity");
    qualityUltraButton_.setTooltip(
        "Quality - High quality windowed-sinc interpolation");
    reverbMixDial_.setLabelTooltip(
        "Reverb Mix - Global wet amount");
    delayTimeDial_.setLabelTooltip(
        "Delay Time - Delay length in milliseconds (or snapped note value when Delay Sync is enabled)");
    delayFeedbackDial_.setLabelTooltip(
        "Delay Feedback - Amount of delayed signal fed back into the delay line");
    delayMixDial_.setLabelTooltip(
        "Delay Mix - Blend between dry signal and delayed signal");
    delayTempoSyncToggle_.setTooltip(
        "Delay Sync - Quantize delay time to host tempo divisions");
    generateRandomButton_.setTooltip(
        "Random - Generate a random waveform from Bezier-curve anchor segments");
    dcFilterEnabledToggle_.setTooltip(
        "DC Filter - Enable a subsonic high-pass filter to remove DC offset");
    dcFilterCutoffDial_.setLabelTooltip(
        "DC HPF - Subsonic high-pass cutoff in Hz (5 to 20)");
    autopanRateDial_.setLabelTooltip(
        "Autopan Rate - Stereo modulation speed in Hz");
    autopanDepthDial_.setLabelTooltip(
        "Autopan Depth - Stereo modulation amount from 0% to 100%");
    saturationDriveDial_.setLabelTooltip(
        "Drive - Amount of post-filter waveshaper saturation (adds harmonic character with minimal CPU)");
    saturationModeCombo_.setTooltip(
        "Type - Select saturation character: Soft Clip (smooth), Hard Clip (aggressive), Tape (rounded), Tube (warm odd/even harmonics)");
    velocityCurveCombo_.setTooltip(
        "Velocity Curve - Response curve for velocity to amplitude");
    velocityCurveLabel_.setTooltip(
        "Velocity Curve - Response curve for velocity to amplitude");

    monoToggle_.setTooltip(
        "Monophonic - Limit to a single voice at a time");
    legatoToggle_.setTooltip(
        "Legato - Keep the envelope running between overlapping notes");
    reverseToggle_.setTooltip(
        "Reverse - Play the sample backwards");

    for (int i = 0; i < kPlayerPadCount; ++i)
    {
        playerPadButtons_[static_cast<std::size_t>(i)].setTooltip(
            "Drum Pad - Click and hold to trigger the assigned MIDI note");
        playerPadAssignButtons_[static_cast<std::size_t>(i)].setTooltip(
            "Assign");
    }
}

// ─── Drag & Drop ───────────────────────────────────────────────────────────────

bool AudiocityAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    // IMPORTANT: Called from the OLE modal loop on every mouse-move.
    // juce::File() and getFileExtension() are pure string ops — safe here.
    // Do NOT use juce::URL (triggers COM), File::existsAsFile() (I/O), etc.
    DBG("[DnD] isInterestedInFileDrag called with " + juce::String(files.size()) + " file(s)");
    for (int i = 0; i < files.size(); ++i)
        DBG("[DnD]   file[" + juce::String(i) + "] = \"" + files[i] + "\"");

    for (const auto& rawPath : files)
    {
        auto path = rawPath.trim();
        if (path.isEmpty())
            continue;

        // Strip file:// URI scheme via string ops (no juce::URL)
        if (path.startsWithIgnoreCase("file:///"))
            path = path.substring(8).replace("/", "\\");
        else if (path.startsWithIgnoreCase("file://"))
            path = path.substring(7).replace("/", "\\");

        const auto ext = juce::File(path).getFileExtension().toLowerCase();
        DBG("[DnD]   normalized=\"" + path + "\"  ext=\"" + ext + "\"");
        if (ext == ".sfz" || ext == ".nki" || ext == ".wav" || ext == ".aiff" || ext == ".aif"
            || (processor_.isRexRuntimeAvailable() && (ext == ".rex" || ext == ".rx2")))
        {
            DBG("[DnD]   -> INTERESTED");
            return true;
        }
    }
    DBG("[DnD]   -> NOT interested");
    return false;
}

void AudiocityAudioProcessorEditor::fileDragEnter(const juce::StringArray& files, int, int)
{
    DBG("[DnD] fileDragEnter");
    isHoveringValidDrop_ = isInterestedInFileDrag(files);
    repaint();
}

void AudiocityAudioProcessorEditor::fileDragMove(const juce::StringArray& files, int x, int y)
{
    fileDragEnter(files, x, y);
}

void AudiocityAudioProcessorEditor::fileDragExit(const juce::StringArray&)
{
    DBG("[DnD] fileDragExit");
    isHoveringValidDrop_ = false;
    repaint();
}

void AudiocityAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    // IMPORTANT: Called during the OLE modal loop.
    // Must NOT do file I/O, juce::URL, callAsync, or any COM call.
    // Just stash the raw paths and let timerCallback handle the load.
    DBG("[DnD] filesDropped called with " + juce::String(files.size()) + " file(s)");
    for (int i = 0; i < files.size(); ++i)
        DBG("[DnD]   dropped[" + juce::String(i) + "] = \"" + files[i] + "\"");
    isHoveringValidDrop_ = false;
    pendingDropFiles_ = files;
    hasPendingDrop_ = true;
    repaint();
    DBG("[DnD] filesDropped finished, hasPendingDrop_=true");
}

// ─── File Chooser ──────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::openSampleChooser()
{
    const auto wildcard = processor_.isRexRuntimeAvailable()
        ? juce::String("*.wav;*.aiff;*.aif;*.sfz;*.nki;*.rex;*.rx2")
        : juce::String("*.wav;*.aiff;*.aif;*.sfz;*.nki");
    fileChooser_ = std::make_unique<juce::FileChooser>(
        processor_.isRexRuntimeAvailable() ? "Open sample, SFZ, NKI, or REX" : "Open sample, SFZ, or NKI",
        juce::File{},
        wildcard);

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        const auto selected = chooser.getResult();
        if (selected == juce::File{})
            return;

        loadFileAsInstrument(selected, [this](const bool loaded)
        {
            if (!loaded)
                return;

            clearSelectedPresetAfterSourceLoad();
            refreshUI(true);
        });
    });
}

void AudiocityAudioProcessorEditor::updatePlayerKeyboardSizing()
{
    const auto whiteKeyCount = countWhiteKeysInRange(kPlayerKeyboardMinMidiNote, kPlayerKeyboardMaxMidiNote);
    constexpr float kWhiteKeyLengthRatio = 6.4f;
    const auto viewportBounds = playerKeyboardViewport_.getLocalBounds();
    if (viewportBounds.isEmpty())
        return;

    const auto whiteKeyWidth = juce::jmax(6.0f,
        juce::jmin(18.0f, static_cast<float>(viewportBounds.getWidth()) / static_cast<float>(whiteKeyCount)));

    const auto preferredKeyLength = static_cast<int>(std::round(whiteKeyWidth * kWhiteKeyLengthRatio));
    const auto minKeyboardHeight = juce::jmin(64, juce::jmax(40, viewportBounds.getHeight()));
    const auto keyboardHeight = juce::jlimit(minKeyboardHeight, viewportBounds.getHeight(), preferredKeyLength);

    playerKeyboard_.setKeyWidth(whiteKeyWidth);
    playerKeyboard_.setSize(static_cast<int>(std::ceil(whiteKeyWidth * static_cast<float>(whiteKeyCount))),
                            keyboardHeight);
    playerKeyboardViewport_.setViewPosition(0, 0);
}

void AudiocityAudioProcessorEditor::refreshPlayerPadButtons()
{
    const bool compactLayout = shouldShowPersistentPerformanceStrip();

    for (int i = 0; i < kPlayerPadCount; ++i)
    {
        const auto& assignment = playerPadAssignments_[static_cast<std::size_t>(i)];
        playerPadButtons_[static_cast<std::size_t>(i)].setButtonText(
            formatPlayerPadButtonLabel(i, assignment, compactLayout));
        playerPadButtons_[static_cast<std::size_t>(i)].setTooltip(
            "Pad " + juce::String(i + 1)
            + " -> " + formatMidiNoteName(assignment.noteNumber)
            + ", velocity " + juce::String(assignment.velocity));
        playerPadAssignButtons_[static_cast<std::size_t>(i)].setTooltip(
            "Assign MIDI note and velocity for Pad " + juce::String(i + 1));
    }
}

void AudiocityAudioProcessorEditor::showPadAssignmentDialog(const int padIndex)
{
    if (padIndex < 0 || padIndex >= kPlayerPadCount)
        return;

    const auto current = playerPadAssignments_[static_cast<std::size_t>(padIndex)];

    auto content = std::make_unique<PadAssignmentDialogContent>(current.noteNumber,
                                                                current.velocity,
                                                                [this, padIndex](const int note, const int vel)
                                                                {
                                                                    const auto noteNumber = juce::jlimit(0, 127, note);
                                                                    const auto velocity = juce::jlimit(1, 127, vel);
                                                                    processor_.setPlayerPadAssignment(padIndex, noteNumber, velocity);
                                                                    playerPadAssignments_[static_cast<std::size_t>(padIndex)] = { noteNumber, velocity };
                                                                    refreshPlayerPadButtons();
                                                                });
    content->setSize(420, 140);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Assign Drum Pad " + juce::String(padIndex + 1);
    options.content.setOwned(content.release());
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour(0xff252538);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

// ─── Refresh UI ────────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::refreshUI(const bool forceWaveformReset)
{
    const auto persistedDisplayMode = processor_.getWaveformDisplayMode();
    waveformView_.setDisplayMode(persistedDisplayMode == 2
        ? WaveformView::DisplayMode::symmetricEnvelope
        : WaveformView::DisplayMode::signedWaveform);

    const auto hasImportedProgram = processor_.hasImportedProgram();
    const auto importedProgramPath = processor_.getImportedProgramPath();
    const auto importedProgramName = processor_.getImportedProgramName();
    const auto importedProgramBadge = audiocity::plugin::importedProgramFormatBadge(processor_.getImportedProgramFormat());
    const auto path = processor_.getLoadedSamplePath();
    const auto sampleIdentity = hasImportedProgram
        ? (juce::String("program:") + importedProgramPath)
        : path.isNotEmpty()
        ? (juce::String("file:") + path)
        : (processor_.isGeneratedWaveformLoaded()
            ? juce::String("generated")
            : (processor_.isCapturedAudioLoaded() ? juce::String("captured") : juce::String("none")));
    const auto sampleLabel = hasImportedProgram
        ? (importedProgramPath.isNotEmpty()
            ? (importedProgramBadge + ": " + importedProgramPath)
            : (importedProgramBadge + ": " + (importedProgramName.isNotEmpty() ? importedProgramName : juce::String("Imported Program"))))
        : path.isNotEmpty()
        ? path
        : (processor_.isGeneratedWaveformLoaded()
            ? juce::String("Generated Waveform")
            : (processor_.isCapturedAudioLoaded() ? juce::String("Captured Audio") : juce::String("No sample loaded")));
    samplePathLabel_.setText(sampleLabel, juce::dontSendNotification);
    programMapText_.setText(processor_.getImportedProgramMapSummary(), juce::dontSendNotification);
    const auto programMapShouldBeVisible = currentTabIndex_ == 0 && hasImportedProgram;
    if (programMapText_.isVisible() != programMapShouldBeVisible)
    {
        programMapText_.setVisible(programMapShouldBeVisible);
        resized();
    }
    updateSampleInformationDisplay();
    presetSaveButton_.setEnabled(processor_.getLoadedSampleLength() > 0);
    const auto isNewLoadedSample = sampleIdentity != lastWaveformSamplePath_;
    if (isNewLoadedSample || currentTabIndex_ == 2)
        refreshMappingZoneRows();

    const bool isEditingRootNote = rootNoteCombo_.hasKeyboardFocus(true) || rootNoteCombo_.isPopupActive();
    if (!isEditingRootNote)
        rootNoteCombo_.setSelectedId(processor_.getRootMidiNote() + 1, juce::dontSendNotification);
    tuneCoarseDial_.setValue(processor_.getCoarseTuneSemitones(), juce::dontSendNotification);
    tuneFineDial_.setValue(processor_.getFineTuneCents(), juce::dontSendNotification);
    pitchBendRangeDial_.setValue(processor_.getPitchBendRangeSemitones(), juce::dontSendNotification);
    const auto pitchLfo = processor_.getPitchLfoSettings();
    pitchLfoRateDial_.setValue(pitchLfo.rateHz, juce::dontSendNotification);
    pitchLfoDepthDial_.setValue(pitchLfo.depthCents, juce::dontSendNotification);
    modulationPanel_.syncFromProcessor();

    playerPadAssignments_ = processor_.getAllPlayerPadAssignments();
    refreshPlayerPadButtons();

    const auto sampleLength = processor_.getLoadedSampleLength();
    const auto maxSampleIndex = juce::jmax(1, sampleLength - 1);

    playbackStartDial_.setRange(0.0, static_cast<double>(maxSampleIndex), 1.0);
    playbackEndDial_.setRange(0.0, static_cast<double>(maxSampleIndex), 1.0);
    loopStartDial_.setRange(0.0, static_cast<double>(maxSampleIndex), 1.0);
    loopEndDial_.setRange(0.0, static_cast<double>(maxSampleIndex), 1.0);

    const auto targetPeakResolution = computeWaveformPeakResolution(waveformView_.getWidth());
    const auto shouldRefreshPeaks = sampleLength <= 0
        ? false
        : (isNewLoadedSample
            || forceWaveformReset
            || cachedWaveformMinMaxByChannel_.empty()
            || cachedWaveformPeakResolution_ != targetPeakResolution);

    if (sampleLength <= 0)
    {
        cachedWaveformMinMaxByChannel_.clear();
        cachedWaveformPeakResolution_ = 0;
    }
    else if (shouldRefreshPeaks)
    {
        cachedWaveformMinMaxByChannel_ = getLoadedSampleWaveformMinMaxByChannel(targetPeakResolution);
        cachedWaveformPeakResolution_ = targetPeakResolution;
    }

    waveformView_.setAutoSliceEnabled(!hasImportedProgram && path.isNotEmpty());
    waveformView_.setSliceMarkers(processor_.getImportedProgramSliceMarkerSamples());
    const auto waveformBadge = hasImportedProgram
        ? importedProgramBadge
        : processor_.getLoadedSampleLoopFormatBadge();
    waveformView_.setState(sampleLength, cachedWaveformMinMaxByChannel_,
        processor_.getSampleWindowStart(), processor_.getSampleWindowEnd(),
        processor_.getLoopStart(), processor_.getLoopEnd(),
        waveformBadge);

    if (forceWaveformReset || isNewLoadedSample)
    {
        waveformView_.resetView();
        processor_.setWaveformViewRange(waveformView_.getViewStartSample(),
                                        waveformView_.getViewSampleCount());
    }
    else
    {
        const auto [viewStart, viewCount] = processor_.getWaveformViewRange();
        waveformView_.setViewRange(viewStart, viewCount);
    }

    lastWaveformSamplePath_ = sampleIdentity;

    // Playback mode
    const auto playbackMode = processor_.getPlaybackMode();
    playbackModeGateButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::gate,
        juce::dontSendNotification);
    playbackModeOneShotButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::oneShot,
        juce::dontSendNotification);
    playbackModeLoopButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::loop,
        juce::dontSendNotification);

    // Playback window
    playbackStartDial_.setValue(processor_.getSampleWindowStart(), juce::dontSendNotification);
    playbackEndDial_.setValue(processor_.getSampleWindowEnd(), juce::dontSendNotification);

    // Loop points
    loopStartDial_.setValue(processor_.getLoopStart(), juce::dontSendNotification);
    loopEndDial_.setValue(processor_.getLoopEnd(), juce::dontSendNotification);
    loopCrossfadeDial_.setValue(processor_.getLoopCrossfadeSamples(), juce::dontSendNotification);

    // Performance
    monoToggle_.setToggleState(processor_.getMonoMode(), juce::dontSendNotification);
    legatoToggle_.setToggleState(processor_.getLegatoMode(), juce::dontSendNotification);
    legatoToggle_.setEnabled(processor_.getMonoMode());
    glideDial_.setValue(processor_.getGlideSeconds() * 1000.0f);
    polyphonyDial_.setValue(static_cast<double>(processor_.getPolyphonyLimit()));

    // Amp ADSR
    const auto amp = processor_.getAmpEnvelope();
    ampAttackDial_.setValue(amp.attackSeconds * 1000.0f);
    ampDecayDial_.setValue(amp.decaySeconds * 1000.0f);
    ampSustainDial_.setValue(amp.sustainLevel * 100.0f);
    ampReleaseDial_.setValue(amp.releaseSeconds * 1000.0f);
    const auto ampLfo = processor_.getAmpLfoSettings();
    ampLfoRateDial_.setValue(ampLfo.rateHz);
    ampLfoDepthDial_.setValue(ampLfo.depth * 100.0f);
    ampLfoShapeCombo_.setSelectedId(static_cast<int>(ampLfo.shape) + 1, juce::dontSendNotification);
    updateAmpEnvelopeGraphFromDials();

    // Filter
    const auto filter = processor_.getFilterSettings();
    filterCutoffDial_.setValue(filter.baseCutoffHz);
    filterResDial_.setValue(static_cast<double>(filter.resonance) * 100.0);
    filterEnvAmtDial_.setValue(filter.envAmountHz);
    filterTypeCombo_.setSelectedId(filterModeToComboId(filter.mode), juce::dontSendNotification);
    updateFilterResponseGraphFromControls();
    filterKeytrackDial_.setValue(filter.keyTracking * 100.0f);
    filterVelDial_.setValue(filter.velocityAmountHz);
    filterLfoRateDial_.setValue(filter.lfoRateHz);
    filterLfoRateKeyDial_.setValue(filter.lfoRateKeyTracking * 100.0f);
    filterLfoAmtDial_.setValue(filter.lfoAmountHz);
    filterLfoAmtKeyDial_.setValue(filter.lfoAmountKeyTracking * 100.0f);
    filterLfoStartPhaseDial_.setValue(filter.lfoStartPhaseDegrees);
    filterLfoStartRandDial_.setValue(filter.lfoStartPhaseRandomDegrees);
    filterLfoFadeInDial_.setValue(filter.lfoFadeInMs);
    filterLfoShapeCombo_.setSelectedId(lfoShapeToComboId(filter.lfoShape), juce::dontSendNotification);
    filterLfoRetriggerToggle_.setToggleState(filter.lfoRetrigger, juce::dontSendNotification);
    filterLfoTempoSyncToggle_.setToggleState(filter.lfoTempoSync, juce::dontSendNotification);
    filterLfoRateKeySyncToggle_.setToggleState(filter.lfoRateKeytrackInTempoSync, juce::dontSendNotification);
    filterLfoKeytrackLinearToggle_.setToggleState(filter.lfoKeytrackLinear, juce::dontSendNotification);
    filterLfoUnipolarToggle_.setToggleState(filter.lfoUnipolar, juce::dontSendNotification);
    filterLfoDivisionCombo_.setSelectedId(filter.lfoSyncDivision + 1, juce::dontSendNotification);
    filterLfoRateDial_.setEnabled(!filter.lfoTempoSync);
    filterLfoRateKeySyncToggle_.setEnabled(filter.lfoTempoSync);
    filterLfoDivisionLabel_.setEnabled(filter.lfoTempoSync);
    filterLfoDivisionCombo_.setEnabled(filter.lfoTempoSync);

    const auto filterEnv = processor_.getFilterEnvelope();
    filterAttackDial_.setValue(filterEnv.attackSeconds * 1000.0f);
    filterDecayDial_.setValue(filterEnv.decaySeconds * 1000.0f);
    filterSustainDial_.setValue(filterEnv.sustainLevel * 100.0f);
    filterReleaseDial_.setValue(filterEnv.releaseSeconds * 1000.0f);
    updateFilterEnvelopeGraphFromDials();

    // Quality / Preload
    qualityCpuButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::cpu,
        juce::dontSendNotification);
    qualityFidelityButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::fidelity,
        juce::dontSendNotification);
    qualityUltraButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::ultra,
        juce::dontSendNotification);
    preloadDial_.setValue(processor_.getPreloadSamples());
    masterVolumeDial_.setValue(processor_.getMasterVolume() * 100.0f);
    panDial_.setValue(processor_.getPan() * 100.0f);
    reverbMixDial_.setValue(processor_.getReverbMix() * 100.0f);
    const auto delay = processor_.getDelaySettings();
    delayTimeDial_.setValue(delay.timeMs);
    delayFeedbackDial_.setValue(delay.feedback * 100.0f);
    delayMixDial_.setValue(delay.mix * 100.0f);
    delayTempoSyncToggle_.setToggleState(delay.tempoSync, juce::dontSendNotification);
    const auto dcFilter = processor_.getDcFilterSettings();
    dcFilterEnabledToggle_.setToggleState(dcFilter.enabled, juce::dontSendNotification);
    dcFilterCutoffDial_.setValue(dcFilter.cutoffHz, juce::dontSendNotification);
    const auto autopan = processor_.getAutopanSettings();
    autopanRateDial_.setValue(autopan.rateHz, juce::dontSendNotification);
    autopanDepthDial_.setValue(autopan.depth * 100.0f, juce::dontSendNotification);
    const auto saturation = processor_.getSaturationSettings();
    saturationDriveDial_.setValue(saturation.drive * 100.0f, juce::dontSendNotification);
    saturationModeCombo_.setSelectedId(static_cast<int>(saturation.mode) + 1, juce::dontSendNotification);

    const auto velCurve = processor_.getVelocityCurve();
    const bool isEditingVelocityCurve = velocityCurveCombo_.hasKeyboardFocus(true) || velocityCurveCombo_.isPopupActive();
    if (!isEditingVelocityCurve)
    {
        velocityCurveCombo_.setSelectedId(
            velCurve == AudiocityAudioProcessor::VelocityCurve::soft ? 2
                : (velCurve == AudiocityAudioProcessor::VelocityCurve::hard ? 3 : 1),
            juce::dontSendNotification);
    }

    // Reverse / Fade
    reverseToggle_.setToggleState(processor_.getReversePlayback(), juce::dontSendNotification);
    fadeInDial_.setValue(processor_.getFadeInSamples());
    fadeOutDial_.setValue(processor_.getFadeOutSamples());

    const auto captureRate = processor_.getCaptureTargetSampleRate();
    captureSampleRateCombo_.setSelectedId(captureRate <= 0 ? 1 : captureRate, juce::dontSendNotification);
    captureChannelCombo_.setSelectedId(processor_.getCaptureChannelMode() + 1, juce::dontSendNotification);
    captureBitDepthCombo_.setSelectedId(processor_.getCaptureBitDepth(), juce::dontSendNotification);
    captureInputLevelSlider_.setValue(processor_.getCaptureInputGain() * 100.0f, juce::dontSendNotification);

    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::updateSampleInformationDisplay()
{
    const auto hasImportedProgram = processor_.hasImportedProgram();
    const auto importedProgramPath = processor_.getImportedProgramPath();
    const auto importedProgramName = processor_.getImportedProgramName();
    const auto samplePath = processor_.getLoadedSamplePath();
    const auto sampleLength = processor_.getLoadedSampleLength();
    const auto channels = juce::jmax(0, processor_.getLoadedSampleChannels());
    const auto sampleRate = processor_.getLoadedSampleRateHz();
    const auto loopBadge = hasImportedProgram
        ? audiocity::plugin::importedProgramFormatBadge(processor_.getImportedProgramFormat())
        : processor_.getLoadedSampleLoopFormatBadge();
    const auto sliceMarkers = loopBadge == "SLICE" ? processor_.getImportedProgramSliceMarkerSamples() : std::vector<int>{};
    const auto sliceCount = static_cast<int>(sliceMarkers.size()) >= 2 ? static_cast<int>(sliceMarkers.size()) - 1 : 0;

    juce::String sourceText;
    if (hasImportedProgram)
        sourceText = importedProgramName.isNotEmpty() ? importedProgramName : importedProgramPath;
    else if (samplePath.isNotEmpty())
        sourceText = samplePath;
    else if (processor_.isGeneratedWaveformLoaded())
        sourceText = "Generated";
    else if (processor_.isCapturedAudioLoaded())
        sourceText = "Captured";
    else
        sourceText = "None";

    sampleInfoSourceValue_.setText(sourceText, juce::dontSendNotification);
    sampleInfoSourceValue_.setTooltip(hasImportedProgram ? importedProgramPath : samplePath);

    sampleInfoRateValue_.setText(formatHzNoDecimals(sampleRate), juce::dontSendNotification);

    juce::String bitDepthText = "Unknown";
    const auto loadedBitDepth = processor_.getLoadedSampleBitDepth();
    if (loadedBitDepth > 0)
        bitDepthText = juce::String(loadedBitDepth) + "-bit";
    else if (processor_.isGeneratedWaveformLoaded())
        bitDepthText = juce::String(processor_.getGenerateBitDepth()) + "-bit";
    else if (processor_.isCapturedAudioLoaded())
        bitDepthText = juce::String(processor_.getCaptureBitDepth()) + "-bit";
    sampleInfoBitDepthValue_.setText(bitDepthText, juce::dontSendNotification);

    juce::String channelsText = "-";
    if (channels > 0)
        channelsText = juce::String(channels);
    sampleInfoChannelsValue_.setText(channelsText, juce::dontSendNotification);

    sampleInfoSamplesValue_.setText(sampleLength > 0 ? juce::String(sampleLength) : "-", juce::dontSendNotification);
    sampleInfoDurationValue_.setText(formatDurationFromSamples(sampleLength, sampleRate), juce::dontSendNotification);

    juce::String fileSizeText = "-";
    if (hasImportedProgram && importedProgramPath.isNotEmpty())
    {
        const juce::File sourceFile(importedProgramPath);
        if (sourceFile.existsAsFile())
            fileSizeText = formatFileSizeString(sourceFile.getSize());
    }
    else if (samplePath.isNotEmpty())
    {
        const juce::File sourceFile(samplePath);
        if (sourceFile.existsAsFile())
            fileSizeText = formatFileSizeString(sourceFile.getSize());
    }
    sampleInfoFileSizeValue_.setText(fileSizeText, juce::dontSendNotification);

    const auto playbackStart = processor_.getSampleWindowStart();
    const auto playbackEnd = processor_.getSampleWindowEnd();
    const auto loopStart = processor_.getLoopStart();
    const auto loopEnd = processor_.getLoopEnd();
    const auto sampleLastIndex = juce::jmax(0, sampleLength - 1);
    const auto playbackDurationSamples = juce::jmax(0, playbackEnd - playbackStart + 1);
    const auto loopDurationSamples = juce::jmax(0, loopEnd - loopStart + 1);
    const auto playbackIsFullRange = sampleLength <= 0
        || (playbackStart <= 0 && playbackEnd >= sampleLastIndex);
    const auto loopIsFullRange = sampleLength <= 0
        || (loopStart <= 0 && loopEnd >= sampleLastIndex);

    sampleInfoPlaybackValue_.setText(sampleLength > 0
            ? (juce::String(playbackStart) + "-" + juce::String(playbackEnd))
            : juce::String("-"),
        juce::dontSendNotification);
    sampleInfoLoopValue_.setText(sampleLength > 0
            ? (juce::String(loopStart) + "-" + juce::String(loopEnd))
            : juce::String("-"),
        juce::dontSendNotification);

    sampleInfoPlaybackDurationLabel_.setVisible(sampleLength > 0 && !playbackIsFullRange);
    sampleInfoPlaybackDurationValue_.setVisible(sampleLength > 0 && !playbackIsFullRange);
    sampleInfoPlaybackDurationValue_.setText(
        (sampleLength > 0 && !playbackIsFullRange)
            ? formatDurationFromSamples(playbackDurationSamples, sampleRate)
            : juce::String("-"),
        juce::dontSendNotification);

    sampleInfoLoopDurationLabel_.setVisible(sampleLength > 0 && !loopIsFullRange);
    sampleInfoLoopDurationValue_.setVisible(sampleLength > 0 && !loopIsFullRange);
    sampleInfoLoopDurationValue_.setText(
        (sampleLength > 0 && !loopIsFullRange)
            ? formatDurationFromSamples(loopDurationSamples, sampleRate)
            : juce::String("-"),
        juce::dontSendNotification);

    const auto metadataTempo = processor_.getLoadedMetadataTempoBpm();
    const auto metadataRoot = processor_.getLoadedMetadataRootMidiNote();
    sampleInfoTempoValue_.setText(metadataTempo > 0.0 ? (juce::String(metadataTempo, 1) + " BPM") : "-", juce::dontSendNotification);
    sampleInfoMetaRootValue_.setText(metadataRoot >= 0 ? formatMidiNoteName(metadataRoot) : "-", juce::dontSendNotification);
    sampleInfoTempoLabel_.setVisible(metadataTempo > 0.0);
    sampleInfoTempoValue_.setVisible(metadataTempo > 0.0);
    sampleInfoMetaRootLabel_.setVisible(metadataRoot >= 0);
    sampleInfoMetaRootValue_.setVisible(metadataRoot >= 0);

    if (loopBadge.isNotEmpty())
    {
        juce::Colour badgeColour(0xff9ba7b9);
        if (loopBadge == "Apple Loop")
            badgeColour = juce::Colour(0xffa593e8);
        else if (loopBadge == "Acidized")
            badgeColour = juce::Colour(0xff8fbf5e);
        else if (loopBadge == "REX")
            badgeColour = juce::Colour(0xff7eb6e0);
        else if (loopBadge == "SFZ")
            badgeColour = juce::Colour(0xffb3a5ff);
        else if (loopBadge == "SLICE")
            badgeColour = juce::Colour(0xff78d7ff);
        sampleInfoBadge_.setBadge(loopBadge, badgeColour);
    }
    else
    {
        sampleInfoBadge_.setBadge({}, juce::Colour(0xff3a3a52));
    }

    waveformResetRangesButton_.setEnabled(sampleLength > 0);

    juce::String waveformSummaryText;
    if (sampleLength > 0)
    {
        waveformSummaryText = "Trim " + sampleInfoPlaybackValue_.getText();
        if (!playbackIsFullRange)
            waveformSummaryText += " (" + sampleInfoPlaybackDurationValue_.getText() + ")";

        waveformSummaryText += "  |  Loop " + sampleInfoLoopValue_.getText();
        if (!loopIsFullRange)
            waveformSummaryText += " (" + sampleInfoLoopDurationValue_.getText() + ")";

        if (sliceCount > 0)
            waveformSummaryText += "  |  Slices " + juce::String(sliceCount) + ". Hover to inspect, double-click to split, right-click a boundary to merge or re-slice.";

        waveformSummaryText += "  |  Drag waveform handles. Shift-drag loop handles to move playback too. Wheel zoom, middle-drag pan.";
    }
    else
    {
        waveformSummaryText = "Load a sample, then drag waveform handles to trim or loop. Wheel zoom, middle-drag pan.";
    }

    waveformInteractionSummaryLabel_.setText(waveformSummaryText, juce::dontSendNotification);
    waveformInteractionSummaryLabel_.setTooltip(sliceCount > 0
        ? "Slice view: hover a slice to inspect it, double-click the waveform to split at that point, and right-click near a boundary to merge adjacent slices or re-run transient slicing"
        : "Trim and loop are edited directly in the waveform: drag handles, Shift-drag loop handles to move playback too, use the mouse wheel to zoom, and middle-drag to pan");
}

void AudiocityAudioProcessorEditor::updateDiagnosticsStatusText()
{
    auto text = "Preload: " + juce::String(processor_.getLoadedPreloadSamples())
            + " | Stream: " + juce::String(processor_.getLoadedStreamSamples())
            + " | Rebuilds: " + juce::String(processor_.getSegmentRebuildCount())
            + " | Prime R/H/M/S: " + juce::String(processor_.getStreamPrimeRequestCount())
            + "/" + juce::String(processor_.getStreamPrimeCacheHitCount())
            + "/" + juce::String(processor_.getStreamPrimeCacheMissCount())
            + "/" + juce::String(processor_.getStreamPrimeServiceCount())
            + " | Voices: " + juce::String(processor_.getActiveVoiceCount())
            + "/" + juce::String(processor_.getPolyphonyLimit())
            + " | Root: " + juce::String(processor_.getRootMidiNote())
            + " | Length: " + juce::String(processor_.getLoadedSampleLength());

    if (processor_.hasImportedProgram())
        text += " | Program zones: " + juce::String(processor_.getImportedProgramZoneCount());

    const auto importSummary = processor_.getLastImportDiagnosticSummary();
    if (importSummary.isNotEmpty())
        text += " | " + importSummary;

    diagnosticsLabel_.setText(text, juce::dontSendNotification);
}

// ─── CC sync ───────────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::syncCcMappingsFromProcessor()
{
    const auto mappings = processor_.getAllCcMappings();
    for (auto& [dial, paramId] : allDials_)
    {
        const auto cc = processor_.getCcForParam(paramId);
        if (cc >= 0)
            dial->assignCc(cc);
        else
            dial->clearCc();
    }
}

// ─── Apply helpers ─────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::pushPlaybackWindow()
{
    const auto ps = juce::jmax(0, static_cast<int>(playbackStartDial_.getValue()));
    const auto pe = juce::jmax(ps + 1, static_cast<int>(playbackEndDial_.getValue()));
    processor_.setSampleWindow(ps, pe);

    // Update waveform view to show new playback bounds
    const auto sampleLength = processor_.getLoadedSampleLength();
    const auto waveformBadge = processor_.hasImportedProgram()
        ? audiocity::plugin::importedProgramFormatBadge(processor_.getImportedProgramFormat())
        : processor_.getLoadedSampleLoopFormatBadge();
    waveformView_.setState(sampleLength, getLoadedSampleWaveformMinMaxByChannel(cachedWaveformPeakResolution_ > 0 ? cachedWaveformPeakResolution_ : 2048),
        ps, pe, processor_.getLoopStart(), processor_.getLoopEnd(),
        waveformBadge);

    updateSampleInformationDisplay();
}

void AudiocityAudioProcessorEditor::enforcePlaybackLoopConstraints()
{
    auto pbStart  = static_cast<int>(playbackStartDial_.getValue());
    auto pbEnd    = static_cast<int>(playbackEndDial_.getValue());
    const auto ls = static_cast<int>(loopStartDial_.getValue());
    const auto le = static_cast<int>(loopEndDial_.getValue());

    // Playback start must stay at or before loop start
    if (pbStart > ls)
        playbackStartDial_.setValue(ls);

    // Playback end must stay at or after loop end
    if (pbEnd < le)
        playbackEndDial_.setValue(le);
}

void AudiocityAudioProcessorEditor::applyLoopPoints()
{
    const auto ls = juce::jmax(0, static_cast<int>(loopStartDial_.getValue()));
    const auto le = juce::jmax(ls + 1, static_cast<int>(loopEndDial_.getValue()));
    processor_.setLoopPoints(ls, le);

    const auto appliedLoopStart = processor_.getLoopStart();
    const auto appliedLoopEnd = processor_.getLoopEnd();
    loopStartDial_.setValue(appliedLoopStart, juce::dontSendNotification);
    loopEndDial_.setValue(appliedLoopEnd, juce::dontSendNotification);

    // Auto-switch to Loop mode when applying loop points
    if (processor_.getPlaybackMode() != AudiocityAudioProcessor::PlaybackMode::loop)
    {
        processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::loop);
        playbackModeLoopButton_.setToggleState(true, juce::dontSendNotification);
    }

    const auto sampleLength = processor_.getLoadedSampleLength();
    const auto waveformBadge = processor_.hasImportedProgram()
        ? audiocity::plugin::importedProgramFormatBadge(processor_.getImportedProgramFormat())
        : processor_.getLoadedSampleLoopFormatBadge();
    waveformView_.setState(sampleLength, getLoadedSampleWaveformMinMaxByChannel(cachedWaveformPeakResolution_ > 0 ? cachedWaveformPeakResolution_ : 2048),
        processor_.getSampleWindowStart(), processor_.getSampleWindowEnd(),
        appliedLoopStart, appliedLoopEnd,
        waveformBadge);

    updateSampleInformationDisplay();
}

void AudiocityAudioProcessorEditor::pushAmpEnvelope()
{
    AudiocityAudioProcessor::AdsrSettings adsr;
    adsr.attackSeconds = juce::jmax(0.0001f, static_cast<float>(ampAttackDial_.getValue()) / 1000.0f);
    adsr.decaySeconds = juce::jmax(0.0001f, static_cast<float>(ampDecayDial_.getValue()) / 1000.0f);
    adsr.sustainLevel = juce::jlimit(0.0f, 1.0f, static_cast<float>(ampSustainDial_.getValue()) / 100.0f);
    adsr.releaseSeconds = juce::jmax(0.0001f, static_cast<float>(ampReleaseDial_.getValue()) / 1000.0f);
    processor_.setAmpEnvelope(adsr);
}

void AudiocityAudioProcessorEditor::pushAmpLfoSettings()
{
    AudiocityAudioProcessor::AmpLfoSettings settings;
    settings.rateHz = juce::jlimit(0.0f, 40.0f, static_cast<float>(ampLfoRateDial_.getValue()));
    settings.depth = juce::jlimit(0.0f, 1.0f, static_cast<float>(ampLfoDepthDial_.getValue()) / 100.0f);
    settings.shape = comboIdToLfoShape(ampLfoShapeCombo_.getSelectedId());
    processor_.setAmpLfoSettings(settings);
}

void AudiocityAudioProcessorEditor::pushDelaySettings()
{
    AudiocityAudioProcessor::DelaySettings settings;
    settings.timeMs = juce::jlimit(1.0f, 2000.0f, static_cast<float>(delayTimeDial_.getValue()));
    settings.feedback = juce::jlimit(0.0f, 0.95f, static_cast<float>(delayFeedbackDial_.getValue()) / 100.0f);
    settings.mix = juce::jlimit(0.0f, 1.0f, static_cast<float>(delayMixDial_.getValue()) / 100.0f);
    settings.tempoSync = delayTempoSyncToggle_.getToggleState();
    processor_.setDelaySettings(settings);
}

void AudiocityAudioProcessorEditor::pushDcFilterSettings()
{
    AudiocityAudioProcessor::DcFilterSettings settings;
    settings.enabled = dcFilterEnabledToggle_.getToggleState();
    settings.cutoffHz = juce::jlimit(5.0f, 20.0f, static_cast<float>(dcFilterCutoffDial_.getValue()));
    processor_.setDcFilterSettings(settings);
}

void AudiocityAudioProcessorEditor::pushAutopanSettings()
{
    AudiocityAudioProcessor::AutopanSettings settings;
    settings.rateHz = juce::jlimit(0.01f, 20.0f, static_cast<float>(autopanRateDial_.getValue()));
    settings.depth = juce::jlimit(0.0f, 1.0f, static_cast<float>(autopanDepthDial_.getValue()) / 100.0f);
    processor_.setAutopanSettings(settings);
}

void AudiocityAudioProcessorEditor::pushSaturationSettings()
{
    AudiocityAudioProcessor::SaturationSettings settings;
    settings.drive = juce::jlimit(0.0f, 1.0f, static_cast<float>(saturationDriveDial_.getValue()) / 100.0f);
    settings.mode = static_cast<AudiocityAudioProcessor::SaturationSettings::Mode>(juce::jlimit(0, 3,
        saturationModeCombo_.getSelectedId() - 1));
    processor_.setSaturationSettings(settings);
}

void AudiocityAudioProcessorEditor::pushPitchLfoSettings()
{
    AudiocityAudioProcessor::PitchLfoSettings settings;
    settings.rateHz = juce::jlimit(0.0f, 40.0f, static_cast<float>(pitchLfoRateDial_.getValue()));
    settings.depthCents = juce::jlimit(0.0f, 100.0f, static_cast<float>(pitchLfoDepthDial_.getValue()));
    processor_.setPitchLfoSettings(settings);
}

void AudiocityAudioProcessorEditor::pushFilterSettings()
{
    AudiocityAudioProcessor::FilterSettings settings;
    settings.baseCutoffHz = juce::jmax(20.0f, static_cast<float>(filterCutoffDial_.getValue()));
    settings.resonance = juce::jlimit(0.0f, 1.0f, static_cast<float>(filterResDial_.getValue()) / 100.0f);
    settings.envAmountHz = juce::jlimit(-12000.0f, 12000.0f, static_cast<float>(filterEnvAmtDial_.getValue()));
    settings.mode = comboIdToFilterMode(filterTypeCombo_.getSelectedId());
    settings.keyTracking = juce::jlimit(-1.0f, 2.0f, static_cast<float>(filterKeytrackDial_.getValue()) / 100.0f);
    settings.velocityAmountHz = juce::jlimit(-12000.0f, 12000.0f, static_cast<float>(filterVelDial_.getValue()));
    settings.lfoRateHz = juce::jlimit(0.0f, 40.0f, static_cast<float>(filterLfoRateDial_.getValue()));
    settings.lfoRateKeyTracking = juce::jlimit(-1.0f, 2.0f, static_cast<float>(filterLfoRateKeyDial_.getValue()) / 100.0f);
    settings.lfoAmountHz = juce::jlimit(-20000.0f, 20000.0f, static_cast<float>(filterLfoAmtDial_.getValue()));
    settings.lfoAmountKeyTracking = juce::jlimit(-1.0f, 2.0f, static_cast<float>(filterLfoAmtKeyDial_.getValue()) / 100.0f);
    settings.lfoStartPhaseDegrees = juce::jlimit(0.0f, 360.0f, static_cast<float>(filterLfoStartPhaseDial_.getValue()));
    settings.lfoStartPhaseRandomDegrees = juce::jlimit(0.0f, 180.0f, static_cast<float>(filterLfoStartRandDial_.getValue()));
    settings.lfoFadeInMs = juce::jlimit(0.0f, 5000.0f, static_cast<float>(filterLfoFadeInDial_.getValue()));
    settings.lfoShape = comboIdToLfoShape(filterLfoShapeCombo_.getSelectedId());
    settings.lfoRetrigger = filterLfoRetriggerToggle_.getToggleState();
    settings.lfoTempoSync = filterLfoTempoSyncToggle_.getToggleState();
    settings.lfoRateKeytrackInTempoSync = filterLfoRateKeySyncToggle_.getToggleState();
    settings.lfoKeytrackLinear = filterLfoKeytrackLinearToggle_.getToggleState();
    settings.lfoUnipolar = filterLfoUnipolarToggle_.getToggleState();
    settings.lfoSyncDivision = juce::jlimit(0, 11, filterLfoDivisionCombo_.getSelectedId() - 1);
    processor_.setFilterSettings(settings);
    updateFilterResponseGraphFromControls();
}

void AudiocityAudioProcessorEditor::pushFilterEnvelope()
{
    AudiocityAudioProcessor::AdsrSettings adsr;
    adsr.attackSeconds = juce::jmax(0.0001f, static_cast<float>(filterAttackDial_.getValue()) / 1000.0f);
    adsr.decaySeconds = juce::jmax(0.0001f, static_cast<float>(filterDecayDial_.getValue()) / 1000.0f);
    adsr.sustainLevel = juce::jlimit(0.0f, 1.0f, static_cast<float>(filterSustainDial_.getValue()) / 100.0f);
    adsr.releaseSeconds = juce::jmax(0.0001f, static_cast<float>(filterReleaseDial_.getValue()) / 1000.0f);
    processor_.setFilterEnvelope(adsr);
}

void AudiocityAudioProcessorEditor::pushPerformanceControls()
{
    const auto mono = monoToggle_.getToggleState();
    processor_.setMonoMode(mono);
    legatoToggle_.setEnabled(mono);

    if (!mono && legatoToggle_.getToggleState())
    {
        legatoToggle_.setToggleState(false, juce::dontSendNotification);
        processor_.setLegatoMode(false);
    }
    else
    {
        processor_.setLegatoMode(legatoToggle_.getToggleState());
    }
}

audiocity::engine::SettingsSnapshot AudiocityAudioProcessorEditor::captureSettingsSnapshot() const
{
    int playbackModeIndex = 0;
    if (processor_.getPlaybackMode() == AudiocityAudioProcessor::PlaybackMode::oneShot)
        playbackModeIndex = 1;
    else if (processor_.getPlaybackMode() == AudiocityAudioProcessor::PlaybackMode::loop)
        playbackModeIndex = 2;

    int qualityTierIndex = 1;
    if (processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::cpu)
        qualityTierIndex = 0;
    else if (processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::ultra)
        qualityTierIndex = 2;

    return {
        processor_.getPreloadSamples(),
        qualityTierIndex,
        playbackModeIndex,
        processor_.getCoarseTuneSemitones(),
        processor_.getFineTuneCents(),
        processor_.getPitchBendRangeSemitones(),
        processor_.getMonoMode(),
        processor_.getLegatoMode(),
        processor_.getGlideSeconds(),
        processor_.getPolyphonyLimit(),
        processor_.getSampleWindowStart(),
        processor_.getSampleWindowEnd(),
        processor_.getLoopStart(),
        processor_.getLoopEnd(),
        processor_.getFadeInSamples(),
        processor_.getFadeOutSamples(),
        processor_.getReversePlayback(),
        processor_.getCaptureTargetSampleRate(),
        processor_.getCaptureChannelMode(),
        processor_.getCaptureBitDepth(),
        processor_.getCaptureInputGain()
    };
}

void AudiocityAudioProcessorEditor::applySettingsSnapshot(const audiocity::engine::SettingsSnapshot& snapshot)
{
    processor_.setPreloadSamples(snapshot.preloadSamples);
    processor_.setCoarseTuneSemitones(snapshot.coarseTuneSemitones);
    processor_.setFineTuneCents(snapshot.fineTuneCents);
    processor_.setPitchBendRangeSemitones(snapshot.pitchBendRangeSemitones);
    processor_.setQualityTier(snapshot.qualityTierIndex == 0
        ? AudiocityAudioProcessor::QualityTier::cpu
        : (snapshot.qualityTierIndex == 2
            ? AudiocityAudioProcessor::QualityTier::ultra
            : AudiocityAudioProcessor::QualityTier::fidelity));
    processor_.setPlaybackMode(snapshot.playbackModeIndex == 1
        ? AudiocityAudioProcessor::PlaybackMode::oneShot
        : (snapshot.playbackModeIndex == 2
            ? AudiocityAudioProcessor::PlaybackMode::loop
            : AudiocityAudioProcessor::PlaybackMode::gate));
    processor_.setMonoMode(snapshot.monoEnabled);
    processor_.setLegatoMode(snapshot.legatoEnabled);
    processor_.setGlideSeconds(snapshot.glideSeconds);
    processor_.setPolyphonyLimit(snapshot.polyphonyLimit);
    processor_.setSampleWindow(snapshot.sampleWindowStart, snapshot.sampleWindowEnd);
    processor_.setLoopPoints(snapshot.loopStart, snapshot.loopEnd);
    processor_.setFadeSamples(snapshot.fadeInSamples, snapshot.fadeOutSamples);
    processor_.setReversePlayback(snapshot.reversePlayback);
    processor_.setCaptureTargetSampleRate(snapshot.captureTargetSampleRate);
    processor_.setCaptureChannelMode(snapshot.captureChannelMode);
    processor_.setCaptureBitDepth(snapshot.captureBitDepth);
    processor_.setCaptureInputGain(snapshot.captureInputGain);

    captureSampleRateCombo_.setSelectedId(snapshot.captureTargetSampleRate <= 0 ? 1 : snapshot.captureTargetSampleRate,
        juce::dontSendNotification);
    captureChannelCombo_.setSelectedId(snapshot.captureChannelMode + 1, juce::dontSendNotification);
    captureBitDepthCombo_.setSelectedId(snapshot.captureBitDepth, juce::dontSendNotification);
    captureInputLevelSlider_.setValue(snapshot.captureInputGain * 100.0f, juce::dontSendNotification);

    refreshUI();
    updateCaptureUiState();
}

