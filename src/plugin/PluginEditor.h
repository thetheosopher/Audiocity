#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <array>
#include <string>
#include <vector>

#include "../engine/SettingsUndoHistory.h"
#include "CcLearnDial.h"
#include "DialLookAndFeel.h"
#include "ImportedProgramState.h"
#include "LibraryFileIndex.h"
#include "OwnedJobWorker.h"
#include "PluginAboutPage.h"
#include "PluginCapturePage.h"
#include "PluginGeneratePage.h"
#include "PluginLibraryPage.h"
#include "PluginMappingPage.h"
#include "PluginPlayerPage.h"
#include "PluginSampleHeaderPage.h"
#include "PluginSampleInfoMetricsLayout.h"
#include "PluginSampleInspectorCardLayout.h"
#include "PlayerPadState.h"
#include "ProgramMappingModel.h"
#include "ProgramMappingUndoHistory.h"
#include "../engine/VoicePool.h"

class AudiocityAudioProcessor;

namespace audiocity::plugin
{
enum class ImportedProgramFormat;
}

class PlayerModulationPanel final : public juce::Component
{
public:
    explicit PlayerModulationPanel(AudiocityAudioProcessor& processor);

    static constexpr int preferredHeight() noexcept { return 318; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void syncFromProcessor();
    void setControlTooltips();
    void forEachDial(const std::function<void(CcLearnDial&, const juce::String&)>& visitor);
    CcLearnDial& macroControl(int index) { return index == 0 ? macro1ValueDial_ : macro2ValueDial_; }

private:
    enum class RouteSlot
    {
        modWheel,
        aftertouch,
        velocity,
        macro1,
        macro2
    };

    struct RouteDialSet
    {
        RouteSlot slot;
        CcLearnDial* pitchDial = nullptr;
        CcLearnDial* filterDial = nullptr;
        CcLearnDial* ampDial = nullptr;
        const char* pitchParamId = nullptr;
        const char* filterParamId = nullptr;
        const char* ampParamId = nullptr;
        const char* pitchTooltip = nullptr;
        const char* filterTooltip = nullptr;
        const char* ampTooltip = nullptr;
    };

    struct MacroDialSet
    {
        std::size_t index = 0;
        CcLearnDial* valueDial = nullptr;
        const char* valueParamId = nullptr;
        const char* valueTooltip = nullptr;
    };

    [[nodiscard]] std::array<RouteDialSet, 5> routeDialSets() noexcept;
    [[nodiscard]] std::array<MacroDialSet, 2> macroDialSets() noexcept;
    void pushToProcessor();
    void paintGroupBox(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title) const;

    AudiocityAudioProcessor& processor_;
    CcLearnDial modWheelPitchDial_{ "MW Pitch", -1200, 1200, 1, "ct", 0 };
    CcLearnDial modWheelFilterDial_{ "MW Filt", -20000, 20000, 1, "Hz", 0 };
    CcLearnDial modWheelAmpDial_{ "MW Amp", -100, 100, 1, "%", 0 };
    CcLearnDial aftertouchPitchDial_{ "AT Pitch", -1200, 1200, 1, "ct", 0 };
    CcLearnDial aftertouchFilterDial_{ "AT Filt", -20000, 20000, 1, "Hz", 0 };
    CcLearnDial aftertouchAmpDial_{ "AT Amp", -100, 100, 1, "%", 0 };
    CcLearnDial velocityPitchDial_{ "Vel Pitch", -1200, 1200, 1, "ct", 0 };
    CcLearnDial velocityFilterDial_{ "Vel Filt", -20000, 20000, 1, "Hz", 0 };
    CcLearnDial velocityAmpDial_{ "Vel Amp", -100, 100, 1, "%", 0 };
    CcLearnDial macro1ValueDial_{ "Macro 1", 0, 100, 1, "%", 0 };
    CcLearnDial macro1PitchDial_{ "M1 Pitch", -1200, 1200, 1, "ct", 0 };
    CcLearnDial macro1FilterDial_{ "M1 Filt", -20000, 20000, 1, "Hz", 0 };
    CcLearnDial macro1AmpDial_{ "M1 Amp", -100, 100, 1, "%", 0 };
    CcLearnDial macro2ValueDial_{ "Macro 2", 0, 100, 1, "%", 0 };
    CcLearnDial macro2PitchDial_{ "M2 Pitch", -1200, 1200, 1, "ct", 0 };
    CcLearnDial macro2FilterDial_{ "M2 Filt", -20000, 20000, 1, "Hz", 0 };
    CcLearnDial macro2AmpDial_{ "M2 Amp", -100, 100, 1, "%", 0 };
};

class MappingOverviewComponent final : public juce::Component,
                                      public juce::DragAndDropTarget
{
public:
    void setRows(std::vector<audiocity::plugin::ProgramZoneListRow> rows);
    void setSelectedZoneIndex(int zoneIndex);
    void setHighlightedMidiNotes(std::bitset<128> highlightedMidiNotes);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDragEnter(const SourceDetails& dragSourceDetails) override;
    void itemDragMove(const SourceDetails& dragSourceDetails) override;
    void itemDragExit(const SourceDetails& dragSourceDetails) override;
    void itemDropped(const SourceDetails& dragSourceDetails) override;

    std::function<void(int)> onZoneSelected;
    std::function<void(const audiocity::plugin::ProgramZoneEdit&)> onZoneEditCommitted;
    std::function<void(const juce::File&, int)> onSampleDroppedOnZone;
    std::function<void(const juce::File&, int)> onSampleDroppedOnKeyboard;

private:
    struct OverviewLayout
    {
        juce::Rectangle<int> title;
        juce::Rectangle<int> plot;
        juce::Rectangle<int> keyboard;
    };

    struct DragState
    {
        audiocity::plugin::ProgramZoneListRow originalRow;
        audiocity::plugin::ProgramZoneEdit previewEdit;
        juce::Point<int> startPosition;
        int startNoteValue = 0;
        int startVelocityValue = 0;
        audiocity::plugin::ProgramZoneOverviewDragMode mode = audiocity::plugin::ProgramZoneOverviewDragMode::none;
    };

    [[nodiscard]] OverviewLayout getOverviewLayout() const;
    [[nodiscard]] juce::Rectangle<int> getZoneBounds(const audiocity::plugin::ProgramZoneListRow& row,
                                                     juce::Rectangle<int> plot) const;
    [[nodiscard]] int findZoneIndexAt(juce::Point<int> position) const;
    [[nodiscard]] const audiocity::plugin::ProgramZoneListRow* findRowByZoneIndex(int zoneIndex) const;
    [[nodiscard]] audiocity::plugin::ProgramZoneOverviewDragMode getDragModeForPosition(
        const audiocity::plugin::ProgramZoneListRow& row,
        juce::Point<int> position,
        juce::Rectangle<int> plot) const;
    [[nodiscard]] int findKeyboardNoteAt(juce::Point<int> position) const;
    [[nodiscard]] int positionToNoteValue(int x, juce::Rectangle<int> plot) const;
    [[nodiscard]] int positionToVelocityValue(int y, juce::Rectangle<int> plot) const;
    void updateDragPreview(juce::Point<int> position);
    void updateSampleDropPreview(juce::Point<int> position);
    void clearSampleDropPreview();

    std::vector<audiocity::plugin::ProgramZoneListRow> rows_;
    int selectedZoneIndex_ = -1;
    std::bitset<128> highlightedMidiNotes_;
    std::optional<DragState> dragState_;
    int sampleDropZoneIndex_ = -1;
    int sampleDropKeyboardNote_ = -1;
};

class PerformanceStripStatusDisplay final : public juce::Component
{
public:
    void setCompactHeaderOverlayEnabled(const bool enabled)
    {
        if (compactHeaderOverlayEnabled_ == enabled)
            return;

        compactHeaderOverlayEnabled_ = enabled;
        repaint();
    }

    void setState(juce::String stateText, int activeVoices, float leftLevel, float rightLevel)
    {
        constexpr float kDecayPerTick = 0.92f;

        stateText_ = std::move(stateText);
        activeVoices_ = juce::jlimit(0, 99, activeVoices);
        leftLevel_ = juce::jmax(juce::jlimit(0.0f, 1.0f, leftLevel), leftLevel_ * kDecayPerTick);
        rightLevel_ = juce::jmax(juce::jlimit(0.0f, 1.0f, rightLevel), rightLevel_ * kDecayPerTick);
        repaint();
    }

    void paint(juce::Graphics& g) override;

private:
    bool compactHeaderOverlayEnabled_ = false;
    juce::String stateText_{ "Ready" };
    int activeVoices_ = 0;
    float leftLevel_ = 0.0f;
    float rightLevel_ = 0.0f;
};

class AudiocityAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            public juce::FileDragAndDropTarget,
                                            public juce::DragAndDropContainer,
                                            private juce::MidiKeyboardStateListener,
                                            private juce::ListBoxModel,
                                            private juce::Timer
{
public:
    explicit AudiocityAudioProcessorEditor(AudiocityAudioProcessor& processor);
    ~AudiocityAudioProcessorEditor() override;

    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Deferred drop handling (processed in timerCallback, outside OLE modal loop)
    juce::StringArray pendingDropFiles_;
    bool hasPendingDrop_ = false;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void setSnapshotTabIndex(int tabIndex);
    void setSampleRailSnapshotState(bool browserRailEnabled, bool inspectorRailEnabled);
    void setSampleInspectorCardSnapshotState(bool filterModExpanded, bool effectsExpanded);
    void setPresetSearchSnapshotState(const juce::StringArray& presetNames, const juce::String& filterText);
    void setSnapshotActiveMidiNotes(const std::vector<int>& noteNumbers);
    void setWorkspaceSnapshotState(bool modulation, bool details, bool browser, int scope = 1, int audition = 1);
    friend struct AudiocityWorkspaceTestAccess;

private:
    void handleNoteOn(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& event) override;
    void selectedRowsChanged(int lastRowSelected) override;
    juce::String getTooltipForRow(int row) override;
    void returnKeyPressed(int lastRowSelected) override;

    void timerCallback() override;
    void promptForPendingImportedAssetRelink();
    void consumeExternalMidiDisplayEvents();
    void syncExternalMidiDisplayNotes(const std::bitset<128>& nextNotes, float noteOnVelocity = 1.0f);
    void paintSampleInspectorPane(juce::Graphics& g) const;
    void clearSampleInformationComponentBounds();

    // ── Custom theme ──
    DialLookAndFeel dialLaf_;

    // ── Group box painting helper ──
    struct GroupBox
    {
        juce::String key;
        juce::String title;
        juce::Rectangle<int> bounds;
        bool expanded = true;
        bool collapsible = false;
    };

    class PaintCallbackComponent final : public juce::Component
    {
    public:
        std::function<void(juce::Graphics&)> onPaint;
        std::function<void(const juce::MouseEvent&)> onMouseDown;

        void paint(juce::Graphics& g) override
        {
            if (onPaint)
                onPaint(g);
        }

        void mouseDown(const juce::MouseEvent& event) override
        {
            if (onMouseDown)
                onMouseDown(event);
        }
    };

    class PlayerPadButton final : public juce::TextButton
    {
    public:
        std::function<void(bool)> onEngagementChanged;
        std::function<void()> onAssignmentRequested;

        void mouseDown(const juce::MouseEvent& event) override
        {
            if (event.mods.isPopupMenu()) { if (onAssignmentRequested) onAssignmentRequested(); return; }
            juce::TextButton::mouseDown(event);
            updateEngagement(true, isEventInside(event));
        }

        void mouseDrag(const juce::MouseEvent& event) override
        {
            juce::TextButton::mouseDrag(event);
            updateEngagement(event.mods.isLeftButtonDown() || event.mods.isRightButtonDown() || event.mods.isMiddleButtonDown(),
                isEventInside(event));
        }

        void mouseUp(const juce::MouseEvent& event) override
        {
            juce::TextButton::mouseUp(event);
            updateEngagement(false, false);
        }

        void mouseExit(const juce::MouseEvent& event) override
        {
            juce::TextButton::mouseExit(event);
            if (event.mods.isAnyMouseButtonDown())
                updateEngagement(true, false);
        }

    private:
        [[nodiscard]] bool isEventInside(const juce::MouseEvent& event) const
        {
            return getLocalBounds().contains(event.getPosition());
        }

        void updateEngagement(const bool mouseDown, const bool inside)
        {
            const auto next = mouseDown && inside;
            if (next == engaged_)
                return;

            engaged_ = next;
            if (onEngagementChanged)
                onEngagementChanged(engaged_);
        }

        bool engaged_ = false;
    };

    class InfoBadge final : public juce::Component
    {
    public:
        void setBadge(const juce::String& text, juce::Colour colour)
        {
            text_ = text;
            colour_ = colour;
            setVisible(text_.isNotEmpty());
            repaint();
        }

        [[nodiscard]] juce::String getText() const { return text_; }

        void paint(juce::Graphics& g) override
        {
            if (text_.isEmpty())
                return;

            auto area = getLocalBounds().toFloat().reduced(0.5f);
            const auto fill = colour_.withAlpha(0.16f);
            g.setColour(fill);
            g.fillRoundedRectangle(area, 4.0f);
            g.setColour(colour_.withAlpha(0.55f));
            g.drawRoundedRectangle(area, 4.0f, 1.0f);
            g.setColour(colour_.brighter(0.45f));
            g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
            g.drawText(text_, getLocalBounds(), juce::Justification::centred, false);
        }

    private:
        juce::String text_;
        juce::Colour colour_{ 0xff3a3a52 };
    };

    std::vector<GroupBox> groupBoxes_;
    void paintGroupBoxes(juce::Graphics& g) const;

    class WaveformView final : public juce::Component
    {
    public:
        enum class DisplayMode
        {
            signedWaveform,
            symmetricEnvelope
        };

        struct MinMax
        {
            float min = 0.0f;
            float max = 0.0f;
        };

        void setState(int totalSamples, std::vector<std::vector<MinMax>> waveformByChannel,
                      int playbackStart, int playbackEnd,
                      int loopStart, int loopEnd,
                      juce::String loopFormatBadge);
        using VoicePlaybackPositions = std::array<int, audiocity::engine::VoicePool::maxVoices>;
        void setSliceMarkers(std::vector<int> sliceMarkers)
        {
            sliceMarkers_ = std::move(sliceMarkers);
            hoveredSliceBoundarySample_ = -1;
            hoveredSliceRegionIndex_ = -1;
            repaint();
        }
        void setAutoSliceEnabled(const bool enabled) noexcept { autoSliceEnabled_ = enabled; }
        void setVoicePlaybackPositions(const VoicePlaybackPositions& positions);
        void resetView();
        void setViewRange(int viewStartSample, int viewSampleCount);
        [[nodiscard]] int getViewStartSample() const noexcept { return viewStartSample_; }
        [[nodiscard]] int getViewSampleCount() const noexcept { return viewSampleCount_; }
        void setDisplayMode(DisplayMode mode)
        {
            displayMode_ = mode;
            repaint();
        }

        std::function<void(int, int)> onLoopPreview;
        std::function<void(int, int)> onLoopCommitted;
        std::function<void(int, int)> onPlaybackPreview;
        std::function<void(int, int)> onPlaybackCommitted;
        std::function<void()> onResetRangesRequested;
        std::function<void(DisplayMode)> onDisplayModeSelected;
        std::function<void(int)> onMergeSliceRequested;
        std::function<void(int)> onSplitSliceRequested;
        std::function<void()> onAutoSliceRequested;

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseMove(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;
        void mouseExit(const juce::MouseEvent& event) override;
        void mouseDoubleClick(const juce::MouseEvent& event) override;
        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    private:
        enum class DragMode { none, dragLoopStart, dragLoopEnd,
                              dragPlaybackStart, dragPlaybackEnd, pan };

        [[nodiscard]] bool isSliceViewActive() const noexcept;
        [[nodiscard]] int sampleFromX(float x) const noexcept;
        [[nodiscard]] float xFromSample(int sample) const noexcept;
        [[nodiscard]] int findNearestSliceBoundarySample(int sampleIndex, int toleranceSamples) const noexcept;
        [[nodiscard]] int findSliceRegionIndexForSample(int sampleIndex) const noexcept;
        [[nodiscard]] juce::Range<int> getSliceRegionBounds(int sliceRegionIndex) const noexcept;
        void updateSliceHoverState(juce::Point<float> position);
        void clampView();
        void zoomAround(float anchorX, float zoomFactor);
        void panByPixels(float deltaX);

        int totalSamples_ = 0;
        std::vector<std::vector<MinMax>> waveformByChannel_;
        int playbackStart_ = 0;
        int playbackEnd_ = 0;
        int loopStart_ = 0;
        int loopEnd_ = 0;
        std::vector<int> sliceMarkers_;
        juce::String loopFormatBadge_;
        VoicePlaybackPositions voicePlaybackPositions_{};

        int viewStartSample_ = 0;
        int viewSampleCount_ = 0;
        int hoveredSliceBoundarySample_ = -1;
        int hoveredSliceRegionIndex_ = -1;

        DragMode dragMode_ = DragMode::none;
        int dragAnchorViewStart_ = 0;
        bool linkedPlaybackDuringLoopDrag_ = false;
        bool autoSliceEnabled_ = false;
        DisplayMode displayMode_ = DisplayMode::signedWaveform;
    };

    AudiocityAudioProcessor& processor_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    bool missingAssetResolverPrompted_ = false;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow_;
    audiocity::plugin::EditorUndoHistory editorUndoHistory_;
    bool isHoveringValidDrop_ = false;
    bool isResizingSampleList_ = false;
    int sampleListColumnWidth_ = 360;
    std::atomic<int> sampleScanGeneration_{ 0 };
    std::atomic<bool> sampleScanInProgress_{ false };
    audiocity::plugin::OwnedJobWorker sampleScanWorker_;
    std::atomic<int> samplePreviewGeneration_{ 0 };
    std::atomic<bool> samplePreviewBuildInProgress_{ false };
    audiocity::plugin::OwnedJobWorker samplePreviewWorker_;
    std::atomic<int> backgroundImportGeneration_{ 0 };
    std::atomic<bool> backgroundImportInProgress_{ false };
    audiocity::plugin::OwnedJobWorker backgroundImportWorker_;
    juce::File backgroundImportFile_;
    std::function<void(bool)> backgroundImportCompletion_;
    juce::TabbedComponent tabBar_{ juce::TabbedButtonBar::TabsAtTop };
    juce::Component tabSamplePage_;
    juce::Component tabLibraryPage_;
    juce::Component tabMappingPage_;
    juce::Component tabPlayerPage_;
    juce::Component tabGeneratePage_;
    juce::Component tabCapturePage_;
    juce::Component tabAboutPage_;
    PluginAboutPage aboutPage_;
    int currentTabIndex_ = 0;
    bool showDiagnosticsPanel_ = false;

    // The legacy page numbers remain a state-compatibility detail. Only Sound and
    // Modulation are primary destinations; the other pages are contextual tools.
    void initialiseInstrumentWorkspace();
    void showWorkspace(int legacyPage, bool modulation = false, bool details = false);
    void showWorkspaceMenu();
    void layoutInstrumentBrowser(juce::Rectangle<int> area);
    void updateSoundIdentity();
    void markSoundSaved();
    void recordPendingSettingsChange();
    void rememberSoundBeforeReplacement();
    void captureSoundRecovery(juce::MemoryBlock& destination);
    void restorePreviousSound();
    bool savePresetToFile(const juce::File& file);
    void loadPresetFile(const juce::File& file);
    juce::Result replaceSoundFromPreset(const juce::File& file);
    bool workspaceReady_ = false;
    bool modulationView_ = false;
    bool soundDetailsView_ = false;
    bool browserOpen_ = false;
    bool browserDocked_ = false;
    bool soundEdited_ = false;
    juce::ComboBox auditionModeCombo_, browserScopeCombo_;
    juce::TextButton soundViewButton_{ "Sound" }, modulationViewButton_{ "Modulation" };
    juce::TextButton sourceMenuButton_{ "Source..." }, workspaceMenuButton_{ "Menu" };
    juce::TextButton closeToolButton_{ "Back to Sound" }, panicButton_{ "All Notes Off" };
    juce::TextButton detailsButton_{ "Details" }, sliceButton_{ "Slice..." };
    juce::TextButton browserLoadButton_{ "Load sound" }, browserStopButton_{ "Stop preview" };
    juce::TextButton browserCloseButton_{ "Close" }, browserDockButton_{ "Dock" };
    juce::Label browserHelpLabel_;
    juce::ToggleButton browserAutoPreview_{ "Auto preview" };
    juce::Slider browserPreviewLevel_;
    juce::Label soundIdentityLabel_, sourceIdentityLabel_, workspaceStatusLabel_;
    juce::MemoryBlock previousSoundState_;
    juce::MemoryBlock pendingReplacementState_;
    juce::String pendingReplacementName_;
    juce::String previousSoundName_;
    juce::File currentSoundFile_;
    std::optional<audiocity::engine::SettingsSnapshot> savedSettingsSnapshot_;
    std::vector<float> savedParameterValues_;
    juce::uint32 lastSettingsEditTime_ = 0;
    int settingsCoalesceKey_ = 1;
    int lastChangedParameter_ = -1;
    juce::TextButton editZonesButton_{ "Edit zones" };
    juce::Label zoneScopeLabel_;
    juce::Rectangle<int> workspaceBrowserBounds_;
    PaintCallbackComponent browserBackdrop_;
    PaintCallbackComponent mappingControlsContent_;
    juce::Viewport mappingControlsViewport_;
    struct PresetBrowserModel final : juce::ListBoxModel
    {
        std::function<int()> count;
        std::function<void(int, juce::Graphics&, int, int, bool)> paintRow;
        std::function<void(int)> load;
        std::function<void()> selectionChanged;
        int getNumRows() override { return count ? count() : 0; }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
        { if (paintRow) paintRow(row, g, w, h, selected); }
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { if (load) load(row); }
        void returnKeyPressed(int row) override { if (load) load(row); }
        void selectedRowsChanged(int) override { if (selectionChanged) selectionChanged(); }
    } presetBrowserModel_;
    juce::ListBox presetBrowserList_{ "Sounds", &presetBrowserModel_ };

    struct SampleListEntry
    {
        enum class PreviewState
        {
            notApplicable,
            missing,
            queued,
            ready,
            failed
        };

        juce::File file;
        juce::String relativePath;
        juce::String fileName;
        juce::String fileNameLower;
        juce::String relativePathLower;
        juce::String loopFormatBadge;
        juce::String metadataLine;
        juce::String loopMetadataLine;
        juce::StringArray tags;
        juce::String tagsLower;
        std::string previewCacheKey;
        std::vector<float> previewPeaks;
        juce::int64 fileSizeBytes = 0;
        juce::int64 modificationTimeMs = 0;
        PreviewState previewState = PreviewState::notApplicable;
        bool isInstrument = false;
        bool isFavorite = false;
        bool isRecent = false;
        int recentRank = -1;
    };

    struct SampleScanDeliveryState
    {
        std::mutex mutex;
        std::deque<SampleListEntry> pendingEntries;
        std::vector<SampleListEntry> replacementEntries;
        std::vector<SampleListEntry> cancellationRollbackEntries;
        std::size_t replacementReadIndex = 0;
        bool replacementReady = false;
        bool cancellationRollbackReady = false;
        bool workerComplete = false;
        bool entryLimitReached = false;
        bool hadWarmIndex = false;
        audiocity::plugin::LibraryFileIndexScanResult::IncompleteReason incompleteReason =
            audiocity::plugin::LibraryFileIndexScanResult::IncompleteReason::none;
    };

    class SampleBrowserListBox final : public juce::ListBox
    {
    public:
        explicit SampleBrowserListBox(AudiocityAudioProcessorEditor& owner) : owner_(owner) {}

        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;

    private:
        AudiocityAudioProcessorEditor& owner_;
        int dragCandidateRow_ = -1;
        bool dragInProgress_ = false;
    };

    class MappingZoneListModel final : public juce::ListBoxModel
    {
    public:
        explicit MappingZoneListModel(AudiocityAudioProcessorEditor& owner) : owner_(owner) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent& event) override;
        void selectedRowsChanged(int lastRowSelected) override;
        void returnKeyPressed(int lastRowSelected) override;

    private:
        AudiocityAudioProcessorEditor& owner_;
    };

    class MappingZoneListBox final : public juce::ListBox,
                                     public juce::DragAndDropTarget
    {
    public:
        explicit MappingZoneListBox(AudiocityAudioProcessorEditor& owner) : owner_(owner) {}

        bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
        void itemDragEnter(const SourceDetails& dragSourceDetails) override;
        void itemDragMove(const SourceDetails& dragSourceDetails) override;
        void itemDragExit(const SourceDetails& dragSourceDetails) override;
        void itemDropped(const SourceDetails& dragSourceDetails) override;
        void paintOverChildren(juce::Graphics& g) override;

    private:
        void updateDropRow(juce::Point<int> position);

        AudiocityAudioProcessorEditor& owner_;
        int dropRow_ = -1;
    };

    std::vector<SampleListEntry> allSampleEntries_;
    std::vector<int> visibleSampleEntryIndices_;
    std::vector<int> sampleBrowserSortOrder_;
    bool sampleBrowserSortOrderDirty_ = true;
    std::vector<SampleListEntry> sampleScanReplacementStaging_;
    std::shared_ptr<SampleScanDeliveryState> sampleScanDelivery_;
    juce::String sampleRootFolderPath_;
    int lastPreviewedBrowserSourceIndex_ = -1;
    juce::String lastWaveformSamplePath_;
    std::vector<std::vector<WaveformView::MinMax>> cachedWaveformMinMaxByChannel_;
    int cachedWaveformPeakResolution_ = 0;

    // ── Sample Browser ──
    juce::Label sampleBrowserRootLabel_{ {}, "< Select Folder >" };
    juce::TextButton sampleBrowserChooseRootButton_{ "..." };
    juce::TextButton sampleBrowserRefreshButton_{ "Refresh" };
    juce::TextButton sampleBrowserCancelButton_{ "Cancel" };
    juce::ComboBox sampleBrowserBookmarkCombo_;
    juce::TextButton sampleBrowserAddBookmarkButton_{ "Bookmark" };
    juce::TextButton sampleBrowserRemoveBookmarkButton_{ "Remove" };
    juce::TextEditor sampleBrowserFilterEditor_;
    juce::ComboBox sampleBrowserSortCombo_;
    juce::ToggleButton sampleBrowserFavoriteButton_{ "Favorite" };
    juce::ToggleButton sampleBrowserFavoritesOnlyToggle_{ "Favorites" };
    juce::ToggleButton sampleBrowserRecentOnlyToggle_{ "Recent" };
    juce::ComboBox sampleBrowserTagFilterCombo_;
    juce::TextEditor sampleBrowserTagsEditor_;
    juce::TextButton sampleBrowserApplyTagsButton_{ "Apply Tags" };
    SampleBrowserListBox sampleBrowserListBox_{ *this };
    juce::Label sampleBrowserCountLabel_;
    juce::Label sampleBrowserPreviewLabel_{ {}, "" };
    PluginLibraryPage libraryPage_;

    // ── Mapping ──
    std::vector<audiocity::plugin::ProgramZoneListRow> mappingZoneRows_;
    MappingZoneListModel mappingZoneListModel_;
    juce::Label mappingSummaryLabel_{ {}, "No imported program" };
    juce::TextButton mappingNewLibraryButton_{ "New Library" };
    juce::TextButton mappingAddSampleButton_{ "Add Sample" };
    juce::TextButton mappingSaveLibraryButton_{ "Save Library" };
    juce::TextButton mappingRefreshButton_{ "Refresh" };
    juce::TextButton mappingCreateZoneButton_{ "New Zone" };
    juce::TextButton mappingDuplicateZoneButton_{ "Duplicate" };
    juce::TextButton mappingSplitZoneButton_{ "Split" };
    juce::TextButton mappingDeleteZoneButton_{ "Delete" };
    MappingOverviewComponent mappingOverview_;
    MappingZoneListBox mappingZoneListBox_{ *this };
    juce::TextEditor mappingDetailsText_;
    juce::Label mappingEditKeyLowLabel_{ {}, "Key Low" };
    juce::Slider mappingEditKeyLowSlider_;
    juce::Label mappingEditKeyHighLabel_{ {}, "Key High" };
    juce::Slider mappingEditKeyHighSlider_;
    juce::Label mappingEditVelocityLowLabel_{ {}, "Vel Low" };
    juce::Slider mappingEditVelocityLowSlider_;
    juce::Label mappingEditVelocityHighLabel_{ {}, "Vel High" };
    juce::Slider mappingEditVelocityHighSlider_;
    juce::Label mappingEditVelocityFadeInLabel_{ {}, "Vel In" };
    juce::Slider mappingEditVelocityFadeInLowSlider_;
    juce::Slider mappingEditVelocityFadeInHighSlider_;
    juce::Label mappingEditVelocityFadeOutLabel_{ {}, "Vel Out" };
    juce::Slider mappingEditVelocityFadeOutLowSlider_;
    juce::Slider mappingEditVelocityFadeOutHighSlider_;
    juce::Label mappingEditRootLabel_{ {}, "Root" };
    juce::Slider mappingEditRootSlider_;
    juce::Label mappingEditSampleStartLabel_{ {}, "Start" };
    juce::Slider mappingEditSampleStartSlider_;
    juce::Label mappingEditSampleEndLabel_{ {}, "End" };
    juce::Slider mappingEditSampleEndSlider_;
    juce::Label mappingEditLoopStartLabel_{ {}, "Loop In" };
    juce::Slider mappingEditLoopStartSlider_;
    juce::Label mappingEditLoopEndLabel_{ {}, "Loop Out" };
    juce::Slider mappingEditLoopEndSlider_;
    juce::Label mappingEditGainLabel_{ {}, "Gain" };
    juce::Slider mappingEditGainSlider_;
    juce::Label mappingEditPanLabel_{ {}, "Pan" };
    juce::Slider mappingEditPanSlider_;
    juce::Label mappingEditRoundRobinGroupLabel_{ {}, "RR Group" };
    juce::Slider mappingEditRoundRobinGroupSlider_;
    juce::Label mappingEditRoundRobinPositionLabel_{ {}, "RR Pos" };
    juce::Slider mappingEditRoundRobinPositionSlider_;
    juce::Label mappingEditRoundRobinModeLabel_{ {}, "RR Mode" };
    juce::ComboBox mappingEditRoundRobinModeCombo_;
    juce::Label mappingEditChokeLabel_{ {}, "Choke" };
    juce::Slider mappingEditChokeSlider_;
    juce::Label mappingEditTriggerLabel_{ {}, "Trigger" };
    juce::ComboBox mappingEditTriggerCombo_;
    juce::Label mappingEditLoopLabel_{ {}, "Loop" };
    juce::ComboBox mappingEditLoopCombo_;
    juce::TextButton mappingEditApplyButton_{ "Apply Zone" };
    juce::Label mappingEditStatusLabel_{ {}, "" };
    PluginMappingPage mappingPage_;

    // ── Player ──
    juce::Label playerKeyboardLabel_{ {}, "Piano" };
    PerformanceStripStatusDisplay playerStatusDisplay_;
    juce::TextButton playerOpenButton_{ "Open Player" };
    juce::Viewport playerKeyboardViewport_;
    juce::MidiKeyboardState playerKeyboardState_;
    juce::MidiKeyboardComponent playerKeyboard_{ playerKeyboardState_, juce::MidiKeyboardComponent::horizontalKeyboard };
    static constexpr int kExternalKeyboardDisplayMidiChannel = 16;
    std::array<int, 128> externalMidiDisplayNoteCounts_{};
    std::bitset<128> externalMidiDisplayNotes_;
    juce::Label playerPadsLabel_{ {}, "Drum Pads" };
    static constexpr int kPlayerPadCount = audiocity::plugin::kPlayerPadCount;
    std::array<PlayerPadButton, kPlayerPadCount> playerPadButtons_;
    std::array<juce::TextButton, kPlayerPadCount> playerPadAssignButtons_;
    PluginPlayerPage playerPage_;
    std::array<bool, kPlayerPadCount> playerPadHeld_{};
    std::array<audiocity::plugin::PlayerPadAssignment, kPlayerPadCount> playerPadAssignments_{};

    // ── Sample ──
    juce::Label samplePathLabel_;
    juce::TextEditor presetFilterEditor_;
    juce::Label presetCountLabel_{ {}, "No presets" };
    juce::ComboBox presetCombo_;
    juce::TextButton presetSaveButton_{ "Save" };
    juce::TextButton presetRenameButton_{ "Rename" };
    juce::TextButton presetDeleteButton_{ "Delete" };
    juce::TextButton loadButton_{ "Load" };
    juce::TextButton sampleBrowserRailToggleButton_{ "Browse" };
    juce::TextButton sampleInspectorRailToggleButton_{ "Inspect" };
    juce::TextButton diagnosticsToggleButton_{ "Tech" };
    PluginSampleHeaderPage sampleHeaderPage_;
    PluginSampleInfoMetricsLayout sampleInfoMetricsLayout_;
    PluginSampleInspectorCardLayout sampleInspectorCardLayout_;
    juce::TextButton waveformResetRangesButton_{ "Reset" };
    juce::Label waveformInteractionSummaryLabel_{ {}, "" };
    juce::Label sampleInfoSourceLabel_{ {}, "Source" };
    juce::Label sampleInfoSourceValue_{ {}, "None" };
    juce::Label sampleInfoRateLabel_{ {}, "Sample Rate" };
    juce::Label sampleInfoRateValue_{ {}, "-" };
    juce::Label sampleInfoBitDepthLabel_{ {}, "Bit Depth" };
    juce::Label sampleInfoBitDepthValue_{ {}, "-" };
    juce::Label sampleInfoChannelsLabel_{ {}, "Channels" };
    juce::Label sampleInfoChannelsValue_{ {}, "-" };
    juce::Label sampleInfoDurationLabel_{ {}, "Duration" };
    juce::Label sampleInfoDurationValue_{ {}, "-" };
    juce::Label sampleInfoFileSizeLabel_{ {}, "File Size" };
    juce::Label sampleInfoFileSizeValue_{ {}, "-" };
    juce::Label sampleInfoSamplesLabel_{ {}, "Samples" };
    juce::Label sampleInfoSamplesValue_{ {}, "-" };
    juce::Label sampleInfoPlaybackLabel_{ {}, "Playback Position" };
    juce::Label sampleInfoPlaybackValue_{ {}, "-" };
    juce::Label sampleInfoPlaybackDurationLabel_{ {}, "Playback Duration" };
    juce::Label sampleInfoPlaybackDurationValue_{ {}, "-" };
    juce::Label sampleInfoLoopLabel_{ {}, "Loop Points" };
    juce::Label sampleInfoLoopValue_{ {}, "-" };
    juce::Label sampleInfoLoopDurationLabel_{ {}, "Loop Duration" };
    juce::Label sampleInfoLoopDurationValue_{ {}, "-" };
    juce::Label sampleInfoTempoLabel_{ {}, "Tempo" };
    juce::Label sampleInfoTempoValue_{ {}, "-" };
    juce::Label sampleInfoMetaRootLabel_{ {}, "Root Note" };
    juce::Label sampleInfoMetaRootValue_{ {}, "-" };
    InfoBadge sampleInfoBadge_;
    juce::TextEditor programMapText_;
    juce::Label rootNoteLabel_{ {}, "Root Note" };
    juce::ComboBox rootNoteCombo_;
    CcLearnDial tuneCoarseDial_{ "Coarse", -24, 24, 1, "st", 0 };
    CcLearnDial tuneFineDial_{ "Fine", -100, 100, 1, "ct", 0 };
    CcLearnDial pitchBendRangeDial_{ "PB Range", 0, 24, 1, "st", 2 };
    CcLearnDial pitchLfoRateDial_{ "P LFO Hz", 0, 40, 0.001, "Hz", 0 };
    CcLearnDial pitchLfoDepthDial_{ "P LFO D", 0, 100, 1, "ct", 0 };
    PlayerModulationPanel modulationPanel_;

    // ── Waveform ──
    WaveformView waveformView_;
    juce::Viewport sampleControlsViewport_;
    PaintCallbackComponent sampleControlsContent_;
    juce::Rectangle<int> sampleInspectorInfoBounds_;
    juce::Rectangle<int> sampleInspectorProgramMapBounds_;
    juce::Rectangle<int> sampleInspectorOutputBounds_;
    juce::Rectangle<int> sampleInspectorFilterModBounds_;
    juce::Rectangle<int> sampleInspectorEffectsBounds_;

    // ── Generate ──
    class GeneratedWaveformView final : public juce::Component
    {
    public:
        using WaveChangedCallback = std::function<void(const std::vector<float>&)>;

        void setWaveform(const std::vector<float>& waveform)
        {
            waveform_ = waveform;
            repaint();
        }

        void setWaveChangedCallback(WaveChangedCallback callback)
        {
            onWaveChanged_ = std::move(callback);
        }

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;

    private:
        void applyPoint(const juce::Point<float>& position, bool interpolateFromLast);
        [[nodiscard]] int sampleIndexFromX(float x) const;
        [[nodiscard]] float sampleValueFromY(float y) const;

        std::vector<float> waveform_;
        WaveChangedCallback onWaveChanged_;
        bool drawing_ = false;
        int lastDrawIndex_ = -1;
        float lastDrawValue_ = 0.0f;
    };

    class AmpEnvelopeGraph final : public juce::Component
    {
    public:
        std::function<void(float, float, float, float)> onEnvelopeEdited;

        void setEnvelope(float attackMs, float decayMs, float sustain, float releaseMs)
        {
            attackMs_ = juce::jmax(0.1f, attackMs);
            decayMs_ = juce::jmax(0.1f, decayMs);
            sustain_ = juce::jlimit(0.0f, 1.0f, sustain);
            releaseMs_ = juce::jmax(0.1f, releaseMs);
            repaint();
        }

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;

    private:
        enum class DragHandle
        {
            none,
            attack,
            decaySustain,
            release
        };

        struct Geometry
        {
            juce::Rectangle<float> area;
            juce::Point<float> attackPoint;
            juce::Point<float> decayPoint;
            juce::Point<float> releasePoint;
        };

        [[nodiscard]] Geometry getGeometry() const;
        [[nodiscard]] float levelFromY(float y) const;
        [[nodiscard]] static float holdMsFor(float attackMs, float decayMs, float releaseMs) noexcept;
        [[nodiscard]] static float totalMsFor(float attackMs, float decayMs, float releaseMs) noexcept;
        [[nodiscard]] static float solveAttackMsForX(float proportion, float decayMs, float releaseMs) noexcept;
        [[nodiscard]] static float solveDecayMsForX(float proportion, float attackMs, float releaseMs) noexcept;
        [[nodiscard]] static float solveReleaseMsForX(float proportion, float attackMs, float decayMs) noexcept;

        float attackMs_ = 5.0f;
        float decayMs_ = 150.0f;
        float sustain_ = 0.85f;
        float releaseMs_ = 150.0f;
        DragHandle dragHandle_ = DragHandle::none;
    };

    class FilterResponseGraph final : public juce::Component
    {
    public:
        void setState(int modeId,
                      float cutoffHz,
                      float resonance,
                      float envAmountHz)
        {
            modeId_ = juce::jlimit(1, 6, modeId);
            cutoffHz_ = juce::jlimit(20.0f, 20000.0f, cutoffHz);
            resonance_ = juce::jlimit(0.0f, 1.0f, resonance);
            envAmountHz_ = juce::jmax(0.0f, envAmountHz);
            repaint();
        }

        void paint(juce::Graphics& g) override;

    private:
        int modeId_ = 1;
        float cutoffHz_ = 1200.0f;
        float resonance_ = 0.0f;
        float envAmountHz_ = 0.0f;
    };

    class StereoPeakMeter final : public juce::Component
    {
    public:
        void setClipZoneEnabled(bool enabled)
        {
            clipZoneEnabled_ = enabled;
            repaint();
        }

        void pushLevels(float left, float right)
        {
            constexpr float kDecayPerTick = 0.92f;
            leftLevel_ = juce::jmax(juce::jlimit(0.0f, 1.0f, left), leftLevel_ * kDecayPerTick);
            rightLevel_ = juce::jmax(juce::jlimit(0.0f, 1.0f, right), rightLevel_ * kDecayPerTick);
            repaint();
        }

        void paint(juce::Graphics& g) override;

    private:
        float leftLevel_ = 0.0f;
        float rightLevel_ = 0.0f;
        bool clipZoneEnabled_ = false;
    };

    GeneratedWaveformView generateWaveformView_;
    juce::TextButton generateSineButton_{ "Sine" };
    juce::TextButton generateRampButton_{ "Ramp" };
    juce::TextButton generateSquareButton_{ "Square" };
    juce::TextButton generateSawtoothButton_{ "Sawtooth" };
    juce::TextButton generateTriangleButton_{ "Triangle" };
    juce::TextButton generatePulseButton_{ "Pulse" };
    juce::TextButton generateRandomButton_{ "Random" };
    juce::Label generateSamplesLabel_{ {}, "Length" };
    juce::ComboBox generateSamplesCombo_;
    juce::Label generateBitDepthLabel_{ {}, "Bits" };
    juce::ComboBox generateBitDepthCombo_;
    juce::Label generateSketchSmoothingLabel_{ {}, "Sketch" };
    juce::ComboBox generateSketchSmoothingCombo_;
    juce::Label generatePulseWidthLabel_{ {}, "PW" };
    juce::Slider generatePulseWidthSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton generatePreviewButton_{ "Play" };
    juce::Label generateFrequencyLabel_{ {}, "Frequency" };
    juce::ComboBox generateFrequencyCombo_;
    juce::TextButton generateLoadAsSampleButton_{ "Load as Sample" };
    PluginGeneratePage generatePage_;
    std::vector<float> generatedWaveform_;
    enum class GeneratedWaveType
    {
        sine,
        ramp,
        square,
        sawtooth,
        triangle,
        pulse,
        random
    };
    enum class SketchedWaveSmoothing
    {
        line,
        curve
    };
    GeneratedWaveType selectedGeneratedWaveType_ = GeneratedWaveType::sine;
    SketchedWaveSmoothing selectedSketchSmoothing_ = SketchedWaveSmoothing::line;

    class CaptureWaveformView final : public juce::Component
    {
    public:
        struct MinMax
        {
            float min = 0.0f;
            float max = 0.0f;
        };

        void setState(int totalSamples,
            int visibleStart,
            int visibleEnd,
            std::vector<MinMax> waveform,
            int selectionStart,
            int selectionEnd,
            double sampleRate,
            bool recording);

        std::function<void(int, int)> onSelectionChanged;

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;

    private:
        [[nodiscard]] int sampleFromX(float x) const noexcept;
        [[nodiscard]] float xFromSample(int sample) const noexcept;
        void updateSelectionFromDrag(float x);

        int totalSamples_ = 0;
        int visibleStart_ = 0;
        int visibleEnd_ = 0;
        std::vector<MinMax> waveform_;
        int selectionStart_ = 0;
        int selectionEnd_ = 0;
        double sampleRate_ = 44100.0;
        bool recording_ = false;
        bool dragging_ = false;
        int dragAnchorSample_ = 0;
    };

    CaptureWaveformView captureWaveformView_;
    juce::TextButton captureRecordButton_{ "Record" };
    juce::TextButton captureClearButton_{ "Clear" };
    juce::TextButton captureCutButton_{ "Cut Selection" };
    juce::TextButton captureTrimButton_{ "Trim Selection" };
    juce::TextButton capturePlayButton_{ "Play" };
    juce::TextButton captureNormalizeButton_{ "Normalize" };
    juce::TextButton captureLoadAsSampleButton_{ "Load as Sample" };
    juce::Label captureSourceLabel_{ {}, "Source: Plugin Input (host-routed)" };
    juce::Label captureSampleRateLabel_{ {}, "SR" };
    juce::ComboBox captureSampleRateCombo_;
    juce::Label captureChannelLabel_{ {}, "Ch" };
    juce::ComboBox captureChannelCombo_;
    juce::Label captureBitDepthLabel_{ {}, "Bits" };
    juce::ComboBox captureBitDepthCombo_;
    juce::Label captureRootNoteLabel_{ {}, "Root Note" };
    juce::ComboBox captureRootNoteCombo_;
    juce::Label captureInputLevelLabel_{ {}, "Input Level" };
    juce::Slider captureInputLevelSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    StereoPeakMeter captureInputVuMeter_;
    juce::Label captureStatusLabel_{ {}, "No capture" };
    PluginCapturePage capturePage_;
    int captureSelectionStart_ = 0;
    int captureSelectionEnd_ = 0;
    int captureDisplayTotalSamples_ = 0;
    int captureDisplayVisibleStart_ = 0;
    int captureDisplayVisibleEnd_ = 0;
    int captureLastSamples_ = -1;
    bool captureLastRecording_ = false;
    juce::Array<juce::File> availablePresetFiles_;
    juce::Array<juce::File> visiblePresetFiles_;
    bool suppressPresetComboChange_ = false;
    juce::String currentPresetName_;
    juce::StringArray presetSnapshotNamesOverride_;

    // ── Playback ──
    juce::Label playbackModeLabel_{ {}, "Mode" };
    juce::ToggleButton playbackModeGateButton_{ "Gate" };
    juce::ToggleButton playbackModeOneShotButton_{ "One-shot" };
    juce::ToggleButton playbackModeLoopButton_{ "Loop" };
    juce::ToggleButton reverseToggle_{ "Reverse" };

    // ── Trim ──
    CcLearnDial playbackStartDial_{ "Trim Start", 0, 16000000, 1 };
    CcLearnDial playbackEndDial_{ "Trim End", 0, 16000000, 1 };

    // ── Loop ──
    CcLearnDial loopStartDial_{ "Loop Start", 0, 16000000, 1 };
    CcLearnDial loopEndDial_{ "Loop End", 0, 16000000, 1 };
    CcLearnDial loopCrossfadeDial_{ "XFade", 0, 5000, 1 };

    // ── Performance ──
    juce::ToggleButton monoToggle_{ "Mono" };
    juce::ToggleButton legatoToggle_{ "Legato" };
    juce::Label velocityCurveLabel_{ {}, "Velocity" };
    juce::ComboBox velocityCurveCombo_;
    CcLearnDial glideDial_{ "Glide", 0, 2000, 0.1, "ms" };
    CcLearnDial polyphonyDial_{ "Poly", 1, 64, 1, {}, 64 };

    // ── Amp Envelope ──
    CcLearnDial ampAttackDial_{ "Attack", 0.1, 5000, 0.1, "ms", 0.1 };
    CcLearnDial ampDecayDial_{ "Decay", 0.1, 5000, 0.1, "ms", 1 };
    CcLearnDial ampSustainDial_{ "Sustain", 0, 100.0, 1.0, "%", 100.0 };
    CcLearnDial ampReleaseDial_{ "Release", 0.1, 5000, 0.1, "ms", 5 };
    CcLearnDial ampLfoRateDial_{ "A LFO Hz", 0, 40, 0.001, "Hz", 0 };
    CcLearnDial ampLfoDepthDial_{ "A LFO D", 0, 100, 1, "%", 0 };
    juce::Label ampLfoShapeLabel_{ {}, "A LFO Shape" };
    juce::ComboBox ampLfoShapeCombo_;
    AmpEnvelopeGraph ampEnvelopeGraph_;

    // ── Filter ──
    CcLearnDial filterCutoffDial_{ "Cutoff", 20, 20000, 1, "Hz", 18000 };
    CcLearnDial filterResDial_{ "Res", 0, 100, 1, "%", 0 };
    CcLearnDial filterEnvAmtDial_{ "Env", -12000, 12000, 1, "Hz", 0 };
    juce::Label filterTypeLabel_{ {}, "Type" };
    juce::ComboBox filterTypeCombo_;
    FilterResponseGraph filterResponseGraph_;
    CcLearnDial filterAttackDial_{ "F Attack", 0.1, 5000, 0.1, "ms", 0.1 };
    CcLearnDial filterDecayDial_{ "F Decay", 0.1, 5000, 0.1, "ms", 1 };
    CcLearnDial filterSustainDial_{ "F Sustain", 0, 100.0, 1.0, "%", 100.0 };
    CcLearnDial filterReleaseDial_{ "F Release", 0.1, 5000, 0.1, "ms", 5 };
    AmpEnvelopeGraph filterEnvelopeGraph_;
    CcLearnDial filterKeytrackDial_{ "Key %", -100, 200, 1, "%", 0 };
    CcLearnDial filterVelDial_{ "Vel Hz", -12000, 12000, 1, "Hz", 0 };
    juce::Label filterKeytrackSnapLabel_{ {}, "Key Snap" };
    juce::ComboBox filterKeytrackSnapCombo_;
    CcLearnDial filterLfoRateDial_{ "LFO Hz", 0, 40, 0.001, "Hz", 0 };
    CcLearnDial filterLfoRateKeyDial_{ "LFO Rate %", -100, 200, 1, "%", 0 };
    CcLearnDial filterLfoAmtDial_{ "LFO Amt", -20000, 20000, 1, "Hz", 0 };
    CcLearnDial filterLfoAmtKeyDial_{ "LFO Key %", -100, 200, 1, "%", 0 };
        CcLearnDial filterLfoStartPhaseDial_{ "LFO Phase", 0, 360, 1, "deg", 0 };
        CcLearnDial filterLfoStartRandDial_{ "LFO Rand", 0, 180, 1, "deg", 0 };
    CcLearnDial filterLfoFadeInDial_{ "LFO Fade", 0, 5000, 1, "ms", 0 };
    juce::Label filterLfoShapeLabel_{ {}, "LFO Shape" };
    juce::ComboBox filterLfoShapeCombo_;
    juce::ToggleButton filterLfoRetriggerToggle_{ "Retrig" };
    juce::ToggleButton filterLfoTempoSyncToggle_{ "Sync" };
    juce::ToggleButton filterLfoRateKeySyncToggle_{ "Key Sync" };
    juce::ToggleButton filterLfoKeytrackLinearToggle_{ "Key Lin" };
    juce::ToggleButton filterLfoUnipolarToggle_{ "Uni" };
    juce::Label filterLfoDivisionLabel_{ {}, "LFO Div" };
    juce::ComboBox filterLfoDivisionCombo_;

    // ── Output ──
    CcLearnDial fadeInDial_{ "Fade In", 0, 10000, 1 };
    CcLearnDial fadeOutDial_{ "Fade Out", 0, 10000, 1 };
    juce::Label qualityLabel_{ {}, "Quality" };
    juce::ToggleButton qualityCpuButton_{ "CPU" };
    juce::ToggleButton qualityFidelityButton_{ "Fidelity" };
    juce::ToggleButton qualityUltraButton_{ "Ultra" };
    CcLearnDial preloadDial_{ "Preload", 256, 131072, 1, {}, 32768 };
    int pendingPreloadSamples_ = 32768;
    bool preloadDragInProgress_ = false;
    CcLearnDial masterVolumeDial_{ "Master", 0, 100, 1, "%", 100 };
    CcLearnDial panDial_{ "Pan", -100, 100, 1, {}, 0 };
    StereoPeakMeter outputLevelMeter_;
    CcLearnDial reverbMixDial_{ "Reverb", 0, 100, 1, "%", 0 };
    CcLearnDial delayTimeDial_{ "Delay Time", 1, 2000, 1, "ms", 320 };
    CcLearnDial delayFeedbackDial_{ "Feedback", 0, 95, 1, "%", 35 };
    CcLearnDial delayMixDial_{ "Delay Mix", 0, 100, 1, "%", 0 };
    juce::ToggleButton delayTempoSyncToggle_{ "Delay Sync" };
    juce::ToggleButton dcFilterEnabledToggle_{ "DC Filter" };
    CcLearnDial dcFilterCutoffDial_{ "DC HPF", 5, 20, 0.1, "Hz", 10 };
    CcLearnDial autopanRateDial_{ "Autopan Rate", 0.01, 20, 0.01, "Hz", 0.5 };
    CcLearnDial autopanDepthDial_{ "Depth", 0, 100, 1, "%", 0 };
    CcLearnDial saturationDriveDial_{ "Drive", 0, 100, 1, "%", 0 };
    juce::Label saturationModeLabel_{ {}, "Sat Mode" };
    juce::ComboBox saturationModeCombo_;

    // ── Diagnostics ──
    juce::Label diagnosticsLabel_;
    juce::TextButton copyImportDiagnosticsButton_{ "Copy Log" };
    static constexpr int kImportDiagnosticLogCapacity = 8;
    std::array<juce::String, kImportDiagnosticLogCapacity> importDiagnosticLogEntries_{};
    int importDiagnosticLogCount_ = 0;
    int nextImportDiagnosticLogIndex_ = 0;

    // ── CC routing ──
    struct DialMapping
    {
        CcLearnDial* dial = nullptr;
        juce::String paramId;
    };
    std::vector<DialMapping> allDials_;

    void openSampleChooser();
    void refreshUI(bool forceWaveformReset = false);
    void commitPendingPreloadChange();
    void pushPlaybackWindow();
    void applyLoopPoints();
    void enforcePlaybackLoopConstraints();
    void pushAmpEnvelope();
    void pushAmpLfoSettings();
    void pushPitchLfoSettings();
    void resetMappingBatchEditTracking(const std::vector<int>& selectedRows = {});
    [[nodiscard]] bool hasPendingMappingBatchEdit() const noexcept;
    void pushFilterEnvelope();
    void pushFilterSettings();
    void pushDelaySettings();
    void pushDcFilterSettings();
    void pushAutopanSettings();
    void pushSaturationSettings();
    void pushPerformanceControls();
    void syncCcMappingsFromProcessor();
    void setupTooltips();
    void saveStateToFile();
    [[nodiscard]] juce::File getPresetDirectory() const;
    [[nodiscard]] juce::Array<juce::File> getFactoryPresetDirectories() const;
    [[nodiscard]] bool isFactoryPresetFile(const juce::File& file) const;
    [[nodiscard]] static juce::String sanitizePresetName(const juce::String& rawName);
    [[nodiscard]] juce::File presetFileForName(const juce::String& presetName) const;
    [[nodiscard]] juce::File getSelectedPresetFile() const;
    void updatePresetActionButtons();
    void refreshPresetList(const juce::String& preferredPresetName = {});
    void savePreset(const juce::String& presetName);
    void promptSavePreset();
    void showPresetLoadErrorAndOfferDelete(const juce::File& presetFile, const juce::String& errorMessage);
    void loadPresetFromSelection();
    void renameSelectedPreset();
    void deleteSelectedPreset();
    void clearSelectedPresetAfterSourceLoad();
    void chooseSampleRootFolder();
    void cancelSampleRootScan();
    void scanSampleRootFolder(const juce::File& rootFolder);
    void promptForNkiSampleFolder(const juce::File& nkiFile);
    void refreshSampleBrowserBookmarks();
    void refreshSampleBrowserTagFilter();
    void scanSelectedSampleBrowserBookmark();
    void addCurrentSampleRootBookmark();
    void removeSelectedSampleBrowserBookmark();
    void syncImportedProgramMappingUndoContext();
    void refreshMappingZoneRows();
    std::vector<int> getSelectedMappingRowIndices() const;
    std::vector<int> getSelectedMappingZoneIndices() const;
    [[nodiscard]] audiocity::plugin::ProgramMappingStateSnapshot captureImportedProgramMappingState() const;
    void recordImportedProgramMappingChange(const audiocity::plugin::ProgramMappingStateSnapshot& beforeState,
                                           const juce::String& label);
    bool applyImportedProgramMappingHistoryState(const audiocity::plugin::ProgramMappingStateSnapshot& mappingState,
                                                 const juce::String& statusText);
    void selectMappingZoneIndices(const std::vector<int>& zoneIndices);
    void selectAllMappingZones();
    bool selectMappingZoneByIndex(int zoneIndex);
    void selectClosestMappingRow(int preferredRow);
    void showMappingZoneContextMenu(int row, juce::Point<int> screenPosition);
    void updateMappingDetails();
    void updateMappingEditControls();
    void applySelectedMappingZoneEdit();
    void beginSampleBrowserDrag(int row, const juce::MouseEvent& event);
    void createMappingZone();
    void createMappingZoneForSampleAsset(int sampleAssetIndex, int seedZoneIndex);
    void createMappingZoneForSampleAssetAtNote(int sampleAssetIndex, int midiNote);
    void duplicateSelectedMappingZone();
    void splitSelectedMappingZone();
    void deleteSelectedMappingZone();
    void clearSelectedMappingVelocityFades();
    void remapSelectedMappingZonesChromatically();
    void mapSelectedMappingZonesToRootNotes();
    void spreadSelectedMappingZonesAcrossKeyRange();
    void deriveSelectedMappingZoneRootsFromKeyRange();
    void createNewLibraryFromScratch();
    void addSampleToCurrentLibrary();
    void saveCurrentLibraryAs();
    bool commitMappingZoneEdit(const audiocity::plugin::ProgramZoneEdit& edit, const juce::String& statusText);
    [[nodiscard]] bool isSampleBrowserDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) const;
    [[nodiscard]] juce::File sampleFileFromDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) const;
    bool ensureDraggedSampleAvailable(const juce::File& sampleFile, int& sampleAssetIndexOut);
    bool assignDraggedSampleToMappingZone(const juce::File& sampleFile, int zoneIndex);
    bool createMappingZoneFromDraggedSample(const juce::File& sampleFile, int midiNote);
    void handleMappingZoneListSampleDrop(int row, const juce::DragAndDropTarget::SourceDetails& dragSourceDetails);
    void paintMappingListRow(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected);
    void refreshBrowserEntryLibraryFlags();
    void updateBrowserLibraryControls();
    void toggleSelectedBrowserFavorite();
    void applySelectedBrowserTags();
    void drainSampleScanResults();
    void rebuildVisibleSampleList();
    void queueVisibleSamplePreviews();
    void restartVisibleSamplePreviews();
    void showInstrumentLoadErrorDialog(const juce::File& file,
                                       const juce::String& diagnosticSummary,
                                       std::function<void()> onDismissed = {});
    void recordImportDiagnosticEntry(const juce::File& file, bool loaded, bool cancelledByUser);
    [[nodiscard]] juce::File getImportDiagnosticLogFile() const;
    void appendImportDiagnosticEntryToDisk(const juce::String& entry) const;
    [[nodiscard]] juce::String buildImportDiagnosticLogText() const;
    void copyImportDiagnosticLogToClipboard();
    [[nodiscard]] bool canImportInstrumentInBackground(audiocity::plugin::ImportedProgramFormat format) const noexcept;
    bool startBackgroundInstrumentLoad(const juce::File& file,
                                       audiocity::plugin::ImportedProgramFormat format,
                                       int selectedChoiceIndex,
                                       std::function<void(bool)> completion,
                                       const juce::File& searchFolder = {});
    void cancelBackgroundInstrumentLoad();
    void setBackgroundImportUiActive(bool active,
                                     const juce::File& file = {},
                                     const juce::String& statusText = {});
    bool loadFileAsInstrument(const juce::File& file,
                              std::function<void(bool)> completion = {});
    bool importInstrumentFileByFormat(const juce::File& file,
                                      audiocity::plugin::ImportedProgramFormat format,
                                      int selectedChoiceIndex);
    void completeInstrumentLoad(const juce::File& file,
                                bool loaded,
                                bool cancelledByUser,
                                const std::function<void(bool)>& completion);
    void autoSliceLoadedSample();
    void mergeLoadedSliceAtBoundary(int boundarySample);
    void splitLoadedSliceAtSample(int sampleIndex);
    void loadSampleFromBrowserRow(int row);
    void previewSampleFromBrowserRow(int row, bool forceRestart = true);
    void updatePlayerKeyboardSizing();
    void refreshPlayerPadButtons();
    void updatePerformanceStripStatus(float outputLeftPeak, float outputRightPeak);
    [[nodiscard]] bool shouldShowPersistentBrowserRail() const noexcept;
    [[nodiscard]] bool shouldShowPersistentPerformanceStrip() const noexcept;
    [[nodiscard]] bool shouldShowSampleInspectorRail() const noexcept;
    [[nodiscard]] bool shouldShowWideSampleInspectorMode() const noexcept;
    [[nodiscard]] bool shouldShowSampleProgramMapInspector() const noexcept;
    [[nodiscard]] int getAvailableSampleAdvancedInspectorHeight(bool reserveProgramMap) const noexcept;
    [[nodiscard]] bool shouldShowSampleFilterModInspector() const noexcept;
    [[nodiscard]] bool shouldShowSampleEffectsInspector() const noexcept;
    void layoutSampleBrowserArea(juce::Rectangle<int> area, bool compactLayout);
    std::vector<int> mappingBatchTrackedSelectionRows_;
    bool suppressMappingBatchEditTracking_ = false;
    bool mappingBatchVelocityFadeEdited_ = false;
    bool mappingBatchGainEdited_ = false;
    bool mappingBatchPanEdited_ = false;
    bool mappingBatchRoundRobinGroupEdited_ = false;
    bool mappingBatchRoundRobinModeEdited_ = false;
    bool mappingBatchChokeEdited_ = false;
    bool mappingBatchTriggerEdited_ = false;
    bool mappingBatchLoopEdited_ = false;
    void showPadAssignmentDialog(int padIndex);
    void syncAutomatedControlsFromProcessor();
    void paintSampleBrowserPane(juce::Graphics& g, juce::Rectangle<int> browserArea) const;
    void handleSampleControlsMouseDown(const juce::MouseEvent& event);
    void updateTabVisibility();
    void updateGeneratePreviewButtonText();
    void refreshCaptureWaveform(bool force = false);
    void updateCaptureUiState();
    void updateGeneratePulseWidthControlState();
    void updateDiagnosticsStatusText();
    void updateSampleInformationDisplay();
    [[nodiscard]] bool isSampleGroupExpanded(const juce::String& key) const;
    [[nodiscard]] bool isSampleGroupCollapsible(const juce::String& key) const;
    void toggleSampleGroupExpanded(const juce::String& key);
    void updateAmpEnvelopeGraphFromDials();
    void updateFilterEnvelopeGraphFromDials();
    void updateFilterResponseGraphFromControls();
    [[nodiscard]] std::vector<std::vector<WaveformView::MinMax>>
        getLoadedSampleWaveformMinMaxByChannel(int maxPeaks = 2048) const;
    void regenerateWaveform();
    [[nodiscard]] int getSelectedGenerateSampleCount() const;
    [[nodiscard]] int getSelectedGenerateBitDepth() const;
    [[nodiscard]] int getSelectedGenerateMidiNote() const;
    [[nodiscard]] float quantizeWaveSample(float value, int bitDepth) const;
    void applySketchedWaveform(const std::vector<float>& sketchedWave);
    void enforceWaveBoundaryZeroCrossings(std::vector<float>& waveform) const;
    [[nodiscard]] bool isSupportedSampleFile(const juce::File& file) const;
    [[nodiscard]] audiocity::engine::SettingsSnapshot captureSettingsSnapshot() const;
    void applySettingsSnapshot(const audiocity::engine::SettingsSnapshot& snapshot);

    juce::String mappingUndoProgramPath_;
    std::optional<audiocity::engine::SettingsSnapshot> lastSettingsSnapshot_;
    bool sampleProgramMapExpanded_ = false;
    bool sampleModulationExpanded_ = false;
    bool sampleFilterModExpanded_ = false;
    bool sampleEffectsExpanded_ = false;
    bool sampleBrowserRailEnabled_ = true;
    bool sampleInspectorRailEnabled_ = true;
    bool sampleInspectorFilterModExpanded_ = true;
    bool sampleInspectorEffectsExpanded_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudiocityAudioProcessorEditor)
};

