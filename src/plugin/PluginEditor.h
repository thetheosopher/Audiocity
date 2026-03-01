#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "../engine/SettingsUndoHistory.h"
#include "CcLearnDial.h"
#include "DialLookAndFeel.h"
#include "PlayerPadState.h"

class AudiocityAudioProcessor;

class AudiocityAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            public juce::FileDragAndDropTarget,
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
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void handleNoteOn(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;

    void timerCallback() override;

    // ── Custom theme ──
    DialLookAndFeel dialLaf_;

    // ── Group box painting helper ──
    struct GroupBox
    {
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    class PaintCallbackComponent final : public juce::Component
    {
    public:
        std::function<void(juce::Graphics&)> onPaint;

        void paint(juce::Graphics& g) override
        {
            if (onPaint)
                onPaint(g);
        }
    };

    std::vector<GroupBox> groupBoxes_;
    void paintGroupBoxes(juce::Graphics& g) const;

    class WaveformView final : public juce::Component
    {
    public:
        void setState(int totalSamples, std::vector<std::vector<float>> peaksByChannel,
                      int playbackStart, int playbackEnd,
                      int loopStart, int loopEnd,
                      juce::String loopFormatBadge);
        void resetView();

        std::function<void(int, int)> onLoopPreview;
        std::function<void(int, int)> onLoopCommitted;
        std::function<void(int, int)> onPlaybackPreview;
        std::function<void(int, int)> onPlaybackCommitted;
        std::function<void()> onResetRangesRequested;

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;
        void mouseDoubleClick(const juce::MouseEvent& event) override;
        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    private:
        enum class DragMode { none, dragLoopStart, dragLoopEnd,
                              dragPlaybackStart, dragPlaybackEnd, pan };

        [[nodiscard]] int sampleFromX(float x) const noexcept;
        [[nodiscard]] float xFromSample(int sample) const noexcept;
        void clampView();
        void zoomAround(float anchorX, float zoomFactor);
        void panByPixels(float deltaX);

        int totalSamples_ = 0;
        std::vector<std::vector<float>> peaksByChannel_;
        int playbackStart_ = 0;
        int playbackEnd_ = 0;
        int loopStart_ = 0;
        int loopEnd_ = 0;
        juce::String loopFormatBadge_;

        int viewStartSample_ = 0;
        int viewSampleCount_ = 0;

        DragMode dragMode_ = DragMode::none;
        int dragAnchorViewStart_ = 0;
        bool linkedPlaybackDuringLoopDrag_ = false;
    };

    AudiocityAudioProcessor& processor_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow_;
    audiocity::engine::SettingsUndoHistory settingsUndoHistory_;
    bool isHoveringValidDrop_ = false;
    bool isResizingSampleList_ = false;
    int sampleListColumnWidth_ = 360;
    std::atomic<int> sampleScanGeneration_{ 0 };
    juce::TabbedComponent tabBar_{ juce::TabbedButtonBar::TabsAtTop };
    juce::Component tabSamplePage_;
    juce::Component tabLibraryPage_;
    juce::Component tabPlayerPage_;
    int currentTabIndex_ = 0;

    struct SampleListEntry
    {
        juce::File file;
        juce::String relativePath;
        juce::String fileName;
        juce::String fileNameLower;
        juce::String relativePathLower;
        juce::String loopFormatBadge;
        juce::String metadataLine;
        juce::String loopMetadataLine;
        std::vector<float> previewPeaks;
    };
    std::vector<SampleListEntry> allSampleEntries_;
    std::vector<int> visibleSampleEntryIndices_;
    juce::String sampleRootFolderPath_;
    juce::String lastWaveformSamplePath_;
    std::vector<std::vector<float>> cachedWaveformPeaksByChannel_;
    int cachedWaveformPeakResolution_ = 0;

    // ── Sample Browser ──
    juce::Label sampleBrowserRootLabel_{ {}, "< Select Folder >" };
    juce::TextButton sampleBrowserChooseRootButton_{ "..." };
    juce::TextEditor sampleBrowserFilterEditor_;
    juce::ComboBox sampleBrowserSortCombo_;
    juce::ListBox sampleBrowserListBox_;
    juce::Label sampleBrowserCountLabel_;

    // ── Player ──
    juce::Label playerKeyboardLabel_{ {}, "Piano" };
    juce::TextButton playerKeyboardScrollLeft_{ "<" };
    juce::TextButton playerKeyboardScrollRight_{ ">" };
    juce::Viewport playerKeyboardViewport_;
    juce::MidiKeyboardState playerKeyboardState_;
    juce::MidiKeyboardComponent playerKeyboard_{ playerKeyboardState_, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::Label playerPadsLabel_{ {}, "Drum Pads" };
    static constexpr int kPlayerPadCount = audiocity::plugin::kPlayerPadCount;
    std::array<juce::TextButton, kPlayerPadCount> playerPadButtons_;
    std::array<juce::TextButton, kPlayerPadCount> playerPadAssignButtons_;
    std::array<int, kPlayerPadCount> playerPadPendingOffTicks_{};
    std::array<int, kPlayerPadCount> playerPadPendingOffNotes_{};
    std::array<audiocity::plugin::PlayerPadAssignment, kPlayerPadCount> playerPadAssignments_{};

    // ── Sample ──
    juce::Label samplePathLabel_;
    juce::TextButton loadButton_{ "..." };
    juce::Label rootNoteLabel_{ {}, "Root Note" };
    juce::ComboBox rootNoteCombo_;

    // ── Waveform ──
    WaveformView waveformView_;
    juce::Viewport sampleControlsViewport_;
    PaintCallbackComponent sampleControlsContent_;

    // ── Playback ──
    juce::Label playbackModeLabel_{ {}, "Mode" };
    juce::ToggleButton playbackModeGateButton_{ "Gate" };
    juce::ToggleButton playbackModeOneShotButton_{ "One-shot" };
    juce::ToggleButton playbackModeLoopButton_{ "Loop" };
    juce::ToggleButton reverseToggle_{ "Reverse" };

    // ── Trim ──
    CcLearnDial playbackStartDial_{ "Start", 0, 16000000, 1 };
    CcLearnDial playbackEndDial_{ "End", 0, 16000000, 1 };

    // ── Loop ──
    CcLearnDial loopStartDial_{ "Start", 0, 16000000, 1 };
    CcLearnDial loopEndDial_{ "End", 0, 16000000, 1 };
    CcLearnDial loopCrossfadeDial_{ "XFade", 0, 5000, 1 };

    // ── Performance ──
    juce::ToggleButton monoToggle_{ "Mono" };
    juce::ToggleButton legatoToggle_{ "Legato" };
    juce::Label velocityCurveLabel_{ {}, "Velocity" };
    juce::ComboBox velocityCurveCombo_;
    CcLearnDial glideDial_{ "Glide", 0, 2000, 0.1, "ms" };

    // ── Amp Envelope ──
    CcLearnDial ampAttackDial_{ "Attack", 0.1, 5000, 0.1, "ms", 0.1 };
    CcLearnDial ampDecayDial_{ "Decay", 0.1, 5000, 0.1, "ms", 1 };
    CcLearnDial ampSustainDial_{ "Sustain", 0, 1.0, 0.01, {}, 1.0 };
    CcLearnDial ampReleaseDial_{ "Release", 0.1, 5000, 0.1, "ms", 5 };

    // ── Filter ──
    CcLearnDial filterCutoffDial_{ "Cutoff", 20, 20000, 1, "Hz", 18000 };
    CcLearnDial filterResDial_{ "Res", 0, 100, 1, "%", 0 };
    CcLearnDial filterEnvAmtDial_{ "Env", 0, 20000, 1, "Hz", 0 };
    juce::Label filterTypeLabel_{ {}, "Type" };
    juce::ComboBox filterTypeCombo_;
    CcLearnDial filterAttackDial_{ "F Attack", 0.1, 5000, 0.1, "ms", 0.1 };
    CcLearnDial filterDecayDial_{ "F Decay", 0.1, 5000, 0.1, "ms", 1 };
    CcLearnDial filterSustainDial_{ "F Sustain", 0, 1.0, 0.01, {}, 1.0 };
    CcLearnDial filterReleaseDial_{ "F Release", 0.1, 5000, 0.1, "ms", 5 };
    CcLearnDial filterKeytrackDial_{ "Key %", -100, 200, 1, "%", 0 };
    CcLearnDial filterVelDial_{ "Vel Hz", 0, 12000, 1, "Hz", 0 };
    juce::Label filterKeytrackSnapLabel_{ {}, "Key Snap" };
    juce::ComboBox filterKeytrackSnapCombo_;
    CcLearnDial filterLfoRateDial_{ "LFO Hz", 0, 40, 0.01, "Hz", 0 };
    CcLearnDial filterLfoRateKeyDial_{ "LFO Rate %", -100, 200, 1, "%", 0 };
    CcLearnDial filterLfoAmtDial_{ "LFO Amt", -20000, 20000, 1, "Hz", 0 };
    CcLearnDial filterLfoAmtKeyDial_{ "LFO Key %", -100, 200, 1, "%", 0 };
    CcLearnDial filterLfoStartPhaseDial_{ "LFO Phase", 0, 360, 1, "\u00b0", 0 };
    CcLearnDial filterLfoStartRandDial_{ "LFO Rand", 0, 180, 1, "\u00b0", 0 };
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
    CcLearnDial reverbMixDial_{ "Reverb", 0, 100, 1, "%", 0 };

    // ── Diagnostics ──
    juce::Label diagnosticsLabel_;

    // ── CC routing ──
    struct DialMapping
    {
        CcLearnDial* dial = nullptr;
        juce::String paramId;
    };
    std::vector<DialMapping> allDials_;

    void openSampleChooser();
    void refreshUI(bool forceWaveformReset = false);
    void pushPlaybackWindow();
    void applyLoopPoints();
    void enforcePlaybackLoopConstraints();
    void pushAmpEnvelope();
    void pushFilterEnvelope();
    void pushFilterSettings();
    void pushPerformanceControls();
    void syncCcMappingsFromProcessor();
    void setupTooltips();
    void chooseSampleRootFolder();
    void scanSampleRootFolder(const juce::File& rootFolder);
    void rebuildVisibleSampleList();
    void loadSampleFromBrowserRow(int row);
    void updatePlayerKeyboardSizing();
    void refreshPlayerPadButtons();
    void showPadAssignmentDialog(int padIndex);
    void syncAutomatedControlsFromProcessor();
    void paintPlayerPane(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintSampleBrowserPane(juce::Graphics& g, juce::Rectangle<int> browserArea) const;
    void updateTabVisibility();
    [[nodiscard]] bool isSupportedSampleFile(const juce::File& file) const;
    [[nodiscard]] audiocity::engine::SettingsSnapshot captureSettingsSnapshot() const;
    void applySettingsSnapshot(const audiocity::engine::SettingsSnapshot& snapshot);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudiocityAudioProcessorEditor)
};

