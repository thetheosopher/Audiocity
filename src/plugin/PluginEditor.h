#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CommandStack.h"
#include "EditorViewModel.h"
#include "SelectionModel.h"
#include "ZoneModel.h"
#include "../engine/SettingsUndoHistory.h"
#include "../engine/sfz/SfzModel.h"
#include "BrowserIndex.h"

class AudiocityAudioProcessor;

class AudiocityAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudiocityAudioProcessorEditor(AudiocityAudioProcessor& processor);
    ~AudiocityAudioProcessorEditor() override;

    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class BrowserPanel final : public juce::Component,
                               private juce::Timer
    {
    public:
        class WaveformView final : public juce::Component
        {
        public:
            void setPeaks(std::vector<float> peaks);
            void setPlayheadProgress(float progress);
            void paint(juce::Graphics& g) override;

        private:
            std::vector<float> peaks_;
            float playheadProgress_ = 0.0f;
        };

        BrowserPanel();

        std::function<void(const juce::String&)> onLoadSample;
        std::function<void()> onImportSfz;
        std::function<void()> onAddWatchedFolder;
        std::function<void()> onRescan;
        std::function<void(const juce::String&)> onSearchChanged;
        std::function<void(const juce::String&)> onSelectionChanged;
        std::function<void(const juce::String&)> onToggleFavorite;
        std::function<void(const juce::String&, bool)> onPreviewToggle;

        void setSamplePath(const juce::String& path);
        void setWatchedFolders(const juce::StringArray& folders);
        void setSearchText(const juce::String& text);
        void setSearchResults(const std::vector<BrowserIndex::EntrySnapshot>& results);
        void setFavorites(const std::vector<BrowserIndex::EntrySnapshot>& favorites);
        void setRecent(const std::vector<BrowserIndex::EntrySnapshot>& recent);
        void setSelectedPath(const juce::String& path);
        void setWaveformPeaks(std::vector<float> peaks);
        void setPreviewState(bool isPlaying, double durationSeconds);
        [[nodiscard]] juce::String getSelectedPath() const;

        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        void timerCallback() override;
        void refreshResultCombo();
        static juce::String listToText(const std::vector<BrowserIndex::EntrySnapshot>& entries);

        juce::TextButton addFolderButton_{ "Add Watched Folder" };
        juce::TextButton rescanButton_{ "Rescan" };
        juce::TextButton loadButton_{ "Load Sample (WAV/AIFF)" };
        juce::TextButton importSfzButton_{ "Import SFZ" };
        juce::TextButton favoriteButton_{ "Toggle Favorite" };
        juce::TextButton previewButton_{ "Preview" };

        juce::Label searchLabel_{ {}, "Search" };
        juce::TextEditor searchEditor_;

        juce::Label resultsLabel_{ {}, "Results" };
        juce::ComboBox resultCombo_;

        juce::Label watchedLabel_{ {}, "Watched Folders" };
        juce::TextEditor watchedView_;

        juce::Label favoritesLabel_{ {}, "Favorites" };
        juce::TextEditor favoritesView_;

        juce::Label recentLabel_{ {}, "Recent" };
        juce::TextEditor recentView_;

        WaveformView waveformView_;

        juce::Label samplePathLabel_;
        std::vector<BrowserIndex::EntrySnapshot> results_;
        juce::String selectedPath_;
        bool previewPlaying_ = false;
        double previewDurationSeconds_ = 0.0;
        double previewStartedSeconds_ = 0.0;
    };

    class MappingPanel final : public juce::Component,
                               private juce::TableListBoxModel
    {
    public:
        MappingPanel();
        std::function<void(int)> onRrModeChanged;
        std::function<void(int)> onPlaybackModeChanged;
        std::function<void(int, int, int)> onLoopPointsApply;
        std::function<void(int)> onZoneSelectionChanged;
        std::function<void(bool)> onMonoModeChanged;
        std::function<void(bool)> onLegatoModeChanged;
        std::function<void(float)> onGlideSecondsChanged;
        std::function<void(int)> onChokeGroupChanged;
        void setZones(const std::vector<audiocity::engine::sfz::Zone>& zones);
        void setRrMode(int modeIndex);
        void setPlaybackMode(int modeIndex);
        void setPerformanceControls(bool monoEnabled, bool legatoEnabled, float glideSeconds, int chokeGroup);
        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        int getNumRows() override;
        void paintRowBackground(juce::Graphics& g,
            int rowNumber,
            int width,
            int height,
            bool rowIsSelected) override;
        void paintCell(juce::Graphics& g,
            int rowNumber,
            int columnId,
            int width,
            int height,
            bool rowIsSelected) override;
        void selectedRowsChanged(int lastRowSelected) override;

        void updateLoopEditorsFromSelection();
        void updatePerformanceControlAvailability();

        juce::TableListBox table_{ "zones", this };
        juce::Label rrModeLabel_{ {}, "RR Mode" };
        juce::ComboBox rrModeCombo_;
        juce::Label playbackModeLabel_{ {}, "Playback" };
        juce::ComboBox playbackModeCombo_;
        juce::ToggleButton monoToggle_{ "Mono" };
        juce::ToggleButton legatoToggle_{ "Legato" };
        juce::Label glideLabel_{ {}, "Glide (ms)" };
        juce::TextEditor glideEditor_;
        juce::Label chokeGroupLabel_{ {}, "Choke Group" };
        juce::TextEditor chokeGroupEditor_;
        juce::Label zoneSummaryLabel_{ {}, "No zone selected" };
        juce::Label loopStartLabel_{ {}, "Loop Start" };
        juce::TextEditor loopStartEditor_;
        juce::Label loopEndLabel_{ {}, "Loop End" };
        juce::TextEditor loopEndEditor_;
        juce::TextButton applyLoopButton_{ "Apply" };
        std::vector<audiocity::engine::sfz::Zone> zones_;
        int selectedRow_ = -1;
    };

    class DiagnosticsPanel final : public juce::Component
    {
    public:
        DiagnosticsPanel();
        void setDiagnostics(
            const std::vector<audiocity::engine::sfz::Diagnostic>& diagnostics,
            const juce::String& streamingInfo,
            const juce::String& streamingTooltip);
        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::TextEditor text_;
    };

    class SettingsPanel final : public juce::Component
    {
    public:
        SettingsPanel();
        std::function<void(int)> onPreloadSamplesChanged;
        std::function<void(int)> onQualityTierChanged;
        std::function<void()> onCopyDiagnostics;
        std::function<void()> onUndoSettings;
        std::function<void()> onRedoSettings;
        void setPreloadSamples(int samples);
        void setQualityTier(int qualityTierIndex);
        void setPreloadSplit(int preloadSamples, int streamSamples);
        void setUndoRedoEnabled(bool canUndo, bool canRedo);
        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::Label preloadLabel_{ {}, "Preload Samples" };
        juce::TextEditor preloadEditor_;
        juce::TextButton applyButton_{ "Apply" };
        juce::Label qualityTierLabel_{ {}, "Quality Tier" };
        juce::ComboBox qualityTierCombo_;
        juce::Label qualityDescriptionLabel_;
        juce::TextButton copyDiagnosticsButton_{ "Copy Diagnostics" };
        juce::TextButton undoButton_{ "Undo" };
        juce::TextButton redoButton_{ "Redo" };
        juce::Label splitInfoLabel_;
    };

    class EditorPanel final : public juce::Component
    {
    public:
        EditorPanel();
        std::function<void(int, int)> onApplyZoneLoopPoints;
        std::function<void()> onUndo;
        std::function<void()> onRedo;
        void setSelectedZoneLoopState(int selectedZoneIndex, int loopStart, int loopEnd, bool hasSelection);
        void setUndoRedoStatus(bool canUndo, bool canRedo, const juce::String& undoLabel, const juce::String& redoLabel);
        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::Label selectedZoneLabel_{ {}, "Selected Zone" };
        juce::Label selectedZoneValue_;
        juce::Label undoRedoStatusLabel_{ {}, "Undo: (none) | Redo: (none)" };
        juce::Label loopStartLabel_{ {}, "Loop Start" };
        juce::TextEditor loopStartEditor_;
        juce::Label loopEndLabel_{ {}, "Loop End" };
        juce::TextEditor loopEndEditor_;
        juce::TextButton undoButton_{ "Undo" };
        juce::TextButton redoButton_{ "Redo" };
        juce::TextButton applyButton_{ "Apply" };
    };

    class PlaceholderPanel final : public juce::Component
    {
    public:
        explicit PlaceholderPanel(juce::String title) : title_(std::move(title)) {}

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black.withAlpha(0.9f));
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.drawRect(getLocalBounds().reduced(8), 1);
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.setFont(18.0f);
            g.drawText(title_ + " panel (stub)", getLocalBounds(), juce::Justification::centred);
        }

    private:
        juce::String title_;
    };

    AudiocityAudioProcessor& processor_;
    SelectionModel selectionModel_{};
    ProcessorZoneModel zoneModel_;
    CommandStack commandStack_{};
    EditorViewModel editorViewModel_;
    juce::TabbedComponent tabs_{ juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::FileChooser> fileChooser_;

    BrowserPanel browserPanel_{};
    MappingPanel mappingPanel_{};
    EditorPanel editorPanel_{};
    SettingsPanel settingsPanel_{};
    DiagnosticsPanel diagnosticsPanel_{};

    void openSampleChooser();
    void openSfzChooser();
    void openWatchedFolderChooser();
    void refreshImportedSfzViews();
    void refreshBrowserPanel();
    void refreshEditorPanel();
    void refreshSettingsPanel();
    [[nodiscard]] juce::String buildStreamingDiagnosticsLine(bool includeFullSamplePath) const;
    [[nodiscard]] juce::String buildStreamingDiagnosticsTooltip() const;
    [[nodiscard]] audiocity::engine::SettingsSnapshot captureSettingsSnapshot() const;
    void applySettingsSnapshot(
        const audiocity::engine::SettingsSnapshot& snapshot,
        bool recordHistory,
        int coalesceKey = -1,
        const std::string& changeLabel = {});
    void updateSettingsUndoRedoAvailability();
    void performSettingsUndo();
    void performSettingsRedo();
    void refreshDiagnosticsTabTitle();

    audiocity::engine::SettingsUndoHistory settingsUndoHistory_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudiocityAudioProcessorEditor)
};
