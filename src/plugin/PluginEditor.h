#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
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

class AudiocityAudioProcessor;

class AudiocityAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            public juce::FileDragAndDropTarget,
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
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
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
    std::vector<GroupBox> groupBoxes_;
    void paintGroupBoxes(juce::Graphics& g) const;

    class WaveformView final : public juce::Component
    {
    public:
        void setState(int totalSamples, std::vector<std::vector<float>> peaksByChannel,
                      int playbackStart, int playbackEnd,
                      int loopStart, int loopEnd);
        void resetView();

        std::function<void(int, int)> onLoopPreview;
        std::function<void(int, int)> onLoopCommitted;
        std::function<void(int, int)> onPlaybackPreview;
        std::function<void(int, int)> onPlaybackCommitted;

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

        int viewStartSample_ = 0;
        int viewSampleCount_ = 0;

        DragMode dragMode_ = DragMode::none;
        int dragAnchorViewStart_ = 0;
    };

    AudiocityAudioProcessor& processor_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    audiocity::engine::SettingsUndoHistory settingsUndoHistory_;
    bool isHoveringValidDrop_ = false;
    bool isResizingSampleList_ = false;
    int sampleListColumnWidth_ = 360;
    std::atomic<int> sampleScanGeneration_{ 0 };
    juce::TabbedComponent tabBar_{ juce::TabbedButtonBar::TabsAtTop };
    juce::Component tabSamplePage_;
    juce::Component tabLibraryPage_;
    int currentTabIndex_ = 0;

    struct SampleListEntry
    {
        juce::File file;
        juce::String relativePath;
        juce::String fileName;
        juce::String fileNameLower;
        juce::String relativePathLower;
        juce::String metadataLine;
        std::vector<float> previewPeaks;
    };
    std::vector<SampleListEntry> allSampleEntries_;
    std::vector<int> visibleSampleEntryIndices_;
    juce::String sampleRootFolderPath_;

    // ── Sample Browser ──
    juce::Label sampleBrowserRootLabel_{ {}, "Source Folder" };
    juce::TextButton sampleBrowserChooseRootButton_{ "Choose Root Folder" };
    juce::TextEditor sampleBrowserFilterEditor_;
    juce::ComboBox sampleBrowserSortCombo_;
    juce::ListBox sampleBrowserListBox_;
    juce::Label sampleBrowserCountLabel_;

    // ── Sample ──
    juce::Label samplePathLabel_;
    juce::TextButton loadButton_{ "..." };
    juce::Label rootNoteLabel_{ {}, "Root Note" };
    juce::ComboBox rootNoteCombo_;

    // ── Waveform ──
    WaveformView waveformView_;

    // ── Playback ──
    juce::Label playbackModeLabel_{ {}, "Mode" };
    juce::ComboBox playbackModeCombo_;
    juce::ToggleButton reverseToggle_{ "Reverse" };

    // ── Trim ──
    CcLearnDial playbackStartDial_{ "Start", 0, 1000000, 1 };
    CcLearnDial playbackEndDial_{ "End", 0, 1000000, 1 };

    // ── Loop ──
    CcLearnDial loopStartDial_{ "Start", 0, 1000000, 1 };
    CcLearnDial loopEndDial_{ "End", 0, 1000000, 1 };

    // ── Performance ──
    juce::ToggleButton monoToggle_{ "Mono" };
    juce::ToggleButton legatoToggle_{ "Legato" };
    CcLearnDial glideDial_{ "Glide", 0, 2000, 0.1, "ms" };
    CcLearnDial chokeGroupDial_{ "Choke", 0, 16, 1 };

    // ── Amp Envelope ──
    CcLearnDial ampAttackDial_{ "Attack", 0.1, 5000, 0.1, "ms", 0.1 };
    CcLearnDial ampDecayDial_{ "Decay", 0.1, 5000, 0.1, "ms", 1 };
    CcLearnDial ampSustainDial_{ "Sustain", 0, 1.0, 0.01, {}, 1.0 };
    CcLearnDial ampReleaseDial_{ "Release", 0.1, 5000, 0.1, "ms", 5 };

    // ── Filter ──
    CcLearnDial filterCutoffDial_{ "Cutoff", 20, 20000, 1, "Hz", 18000 };
    CcLearnDial filterResDial_{ "Res", 0, 100, 1, "%", 0 };
    CcLearnDial filterEnvAmtDial_{ "Env", 0, 20000, 1, "Hz", 0 };

    // ── Output ──
    CcLearnDial fadeInDial_{ "Fade In", 0, 10000, 1 };
    CcLearnDial fadeOutDial_{ "Fade Out", 0, 10000, 1 };
    juce::Label qualityLabel_{ {}, "Quality" };
    juce::ComboBox qualityCombo_;
    CcLearnDial preloadDial_{ "Preload", 256, 131072, 1, {}, 32768 };

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
    void refreshUI();
    void pushPlaybackWindow();
    void applyLoopPoints();
    void enforcePlaybackLoopConstraints();
    void pushAmpEnvelope();
    void pushFilterSettings();
    void pushPerformanceControls();
    void syncCcMappingsFromProcessor();
    void setupTooltips();
    void chooseSampleRootFolder();
    void scanSampleRootFolder(const juce::File& rootFolder);
    void rebuildVisibleSampleList();
    void loadSampleFromBrowserRow(int row);
    void paintSampleBrowserPane(juce::Graphics& g, juce::Rectangle<int> browserArea) const;
    void updateTabVisibility();
    [[nodiscard]] bool isSupportedSampleFile(const juce::File& file) const;
    [[nodiscard]] audiocity::engine::SettingsSnapshot captureSettingsSnapshot() const;
    void applySettingsSnapshot(const audiocity::engine::SettingsSnapshot& snapshot);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudiocityAudioProcessorEditor)
};

