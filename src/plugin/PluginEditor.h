#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <atomic>
#include <memory>
#include <optional>
#include <array>
#include <string>
#include <vector>

#include "../engine/SettingsUndoHistory.h"
#include "CcLearnDial.h"
#include "DialLookAndFeel.h"
#include "PlayerPadState.h"
#include "../engine/VoicePool.h"

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
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void handleNoteOn(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& event) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void returnKeyPressed(int lastRowSelected) override;

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

            auto area = getLocalBounds().toFloat();
            g.setColour(colour_);
            g.fillRoundedRectangle(area, 4.0f);
            g.setColour(juce::Colour(0xffdfe6ff));
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
        std::vector<std::vector<MinMax>> waveformByChannel_;
        int playbackStart_ = 0;
        int playbackEnd_ = 0;
        int loopStart_ = 0;
        int loopEnd_ = 0;
        juce::String loopFormatBadge_;
        VoicePlaybackPositions voicePlaybackPositions_{};

        int viewStartSample_ = 0;
        int viewSampleCount_ = 0;

        DragMode dragMode_ = DragMode::none;
        int dragAnchorViewStart_ = 0;
        bool linkedPlaybackDuringLoopDrag_ = false;
        DisplayMode displayMode_ = DisplayMode::signedWaveform;
    };

    AudiocityAudioProcessor& processor_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow_;
    audiocity::engine::SettingsUndoHistory settingsUndoHistory_;
    bool isHoveringValidDrop_ = false;
    bool isResizingSampleList_ = false;
    int sampleListColumnWidth_ = 360;
    std::atomic<int> sampleScanGeneration_{ 0 };
    std::atomic<bool> sampleScanInProgress_{ false };
    juce::TabbedComponent tabBar_{ juce::TabbedButtonBar::TabsAtTop };
    juce::Component tabSamplePage_;
    juce::Component tabLibraryPage_;
    juce::Component tabPlayerPage_;
    juce::Component tabGeneratePage_;
    juce::Component tabCapturePage_;
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
    int lastPreviewedBrowserSourceIndex_ = -1;
    juce::String lastWaveformSamplePath_;
    std::vector<std::vector<WaveformView::MinMax>> cachedWaveformMinMaxByChannel_;
    int cachedWaveformPeakResolution_ = 0;

    // ── Sample Browser ──
    juce::Label sampleBrowserRootLabel_{ {}, "< Select Folder >" };
    juce::TextButton sampleBrowserChooseRootButton_{ "..." };
    juce::TextEditor sampleBrowserFilterEditor_;
    juce::ComboBox sampleBrowserSortCombo_;
    juce::ListBox sampleBrowserListBox_;
    juce::Label sampleBrowserCountLabel_;
    juce::Label sampleBrowserPreviewLabel_{ {}, "" };

    // ── Player ──
    juce::Label playerKeyboardLabel_{ {}, "Piano" };
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
    juce::ComboBox presetCombo_;
    juce::TextButton presetSaveButton_{ "Save" };
    juce::TextButton presetRenameButton_{ "Rename" };
    juce::TextButton presetDeleteButton_{ "Delete" };
    juce::TextButton loadButton_{ "..." };
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
    juce::Label rootNoteLabel_{ {}, "Root Note" };
    juce::ComboBox rootNoteCombo_;
    CcLearnDial tuneCoarseDial_{ "Coarse", -24, 24, 1, "st", 0 };
    CcLearnDial tuneFineDial_{ "Fine", -100, 100, 1, "ct", 0 };
    CcLearnDial pitchBendRangeDial_{ "PB Range", 0, 24, 1, "st", 2 };
    CcLearnDial pitchLfoRateDial_{ "P LFO Hz", 0, 40, 0.001, "Hz", 0 };
    CcLearnDial pitchLfoDepthDial_{ "P LFO D", 0, 100, 1, "ct", 0 };

    // ── Waveform ──
    WaveformView waveformView_;
    juce::Viewport sampleControlsViewport_;
    PaintCallbackComponent sampleControlsContent_;

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
        void setEnvelope(float attackMs, float decayMs, float sustain, float releaseMs)
        {
            attackMs_ = juce::jmax(0.1f, attackMs);
            decayMs_ = juce::jmax(0.1f, decayMs);
            sustain_ = juce::jlimit(0.0f, 1.0f, sustain);
            releaseMs_ = juce::jmax(0.1f, releaseMs);
            repaint();
        }

        void paint(juce::Graphics& g) override;

    private:
        float attackMs_ = 5.0f;
        float decayMs_ = 150.0f;
        float sustain_ = 0.85f;
        float releaseMs_ = 150.0f;
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
    juce::Label generateSamplesLabel_{ {}, "Samples" };
    juce::ComboBox generateSamplesCombo_;
    juce::Label generateBitDepthLabel_{ {}, "Bit Depth" };
    juce::ComboBox generateBitDepthCombo_;
    juce::Label generateSketchSmoothingLabel_{ {}, "Sketch" };
    juce::ComboBox generateSketchSmoothingCombo_;
    juce::Label generatePulseWidthLabel_{ {}, "Pulse Width" };
    juce::Slider generatePulseWidthSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton generatePreviewButton_{ "Play" };
    juce::Label generateFrequencyLabel_{ {}, "Frequency" };
    juce::ComboBox generateFrequencyCombo_;
    juce::TextButton generateLoadAsSampleButton_{ "Load as Sample" };
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
    juce::TextButton capturePlayButton_{ "Play Capture" };
    juce::TextButton captureNormalizeButton_{ "Normalize" };
    juce::TextButton captureLoadAsSampleButton_{ "Load as Sample" };
    juce::Label captureSourceLabel_{ {}, "Source: Plugin Input (host-routed)" };
    juce::Label captureSampleRateLabel_{ {}, "Sample Rate" };
    juce::ComboBox captureSampleRateCombo_;
    juce::Label captureChannelLabel_{ {}, "Channel" };
    juce::ComboBox captureChannelCombo_;
    juce::Label captureBitDepthLabel_{ {}, "Bit Depth" };
    juce::ComboBox captureBitDepthCombo_;
    juce::Label captureRootNoteLabel_{ {}, "Root Note" };
    juce::ComboBox captureRootNoteCombo_;
    juce::Label captureInputLevelLabel_{ {}, "Input Level" };
    juce::Slider captureInputLevelSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    StereoPeakMeter captureInputVuMeter_;
    juce::Label captureStatusLabel_{ {}, "No capture" };
    int captureSelectionStart_ = 0;
    int captureSelectionEnd_ = 0;
    int captureDisplayTotalSamples_ = 0;
    int captureDisplayVisibleStart_ = 0;
    int captureDisplayVisibleEnd_ = 0;
    int captureLastSamples_ = -1;
    bool captureLastRecording_ = false;
    juce::Array<juce::File> availablePresetFiles_;
    bool suppressPresetComboChange_ = false;
    juce::String currentPresetName_;

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
    CcLearnDial ampSustainDial_{ "Sustain", 0, 1.0, 0.01, {}, 1.0 };
    CcLearnDial ampReleaseDial_{ "Release", 0.1, 5000, 0.1, "ms", 5 };
    CcLearnDial ampLfoRateDial_{ "A LFO Hz", 0, 40, 0.001, "Hz", 0 };
    CcLearnDial ampLfoDepthDial_{ "A LFO D", 0, 100, 1, "%", 0 };
    juce::Label ampLfoShapeLabel_{ {}, "A LFO Shape" };
    juce::ComboBox ampLfoShapeCombo_;
    AmpEnvelopeGraph ampEnvelopeGraph_;

    // ── Filter ──
    CcLearnDial filterCutoffDial_{ "Cutoff", 20, 20000, 1, "Hz", 18000 };
    CcLearnDial filterResDial_{ "Res", 0, 100, 1, "%", 0 };
    CcLearnDial filterEnvAmtDial_{ "Env", 0, 20000, 1, "Hz", 0 };
    juce::Label filterTypeLabel_{ {}, "Type" };
    juce::ComboBox filterTypeCombo_;
    FilterResponseGraph filterResponseGraph_;
    CcLearnDial filterAttackDial_{ "F Attack", 0.1, 5000, 0.1, "ms", 0.1 };
    CcLearnDial filterDecayDial_{ "F Decay", 0.1, 5000, 0.1, "ms", 1 };
    CcLearnDial filterSustainDial_{ "F Sustain", 0, 1.0, 0.01, {}, 1.0 };
    CcLearnDial filterReleaseDial_{ "F Release", 0.1, 5000, 0.1, "ms", 5 };
    AmpEnvelopeGraph filterEnvelopeGraph_;
    CcLearnDial filterKeytrackDial_{ "Key %", -100, 200, 1, "%", 0 };
    CcLearnDial filterVelDial_{ "Vel Hz", 0, 12000, 1, "Hz", 0 };
    juce::Label filterKeytrackSnapLabel_{ {}, "Key Snap" };
    juce::ComboBox filterKeytrackSnapCombo_;
    CcLearnDial filterLfoRateDial_{ "LFO Hz", 0, 40, 0.001, "Hz", 0 };
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
    void pushAmpLfoSettings();
    void pushPitchLfoSettings();
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
    [[nodiscard]] static juce::String sanitizePresetName(const juce::String& rawName);
    [[nodiscard]] juce::File presetFileForName(const juce::String& presetName) const;
    void refreshPresetList(const juce::String& preferredPresetName = {});
    void savePreset(const juce::String& presetName);
    void promptSavePreset();
    void showPresetLoadErrorAndOfferDelete(const juce::File& presetFile, const juce::String& errorMessage);
    void loadPresetFromSelection();
    void renameSelectedPreset();
    void deleteSelectedPreset();
    void clearSelectedPresetAfterSourceLoad();
    void chooseSampleRootFolder();
    void scanSampleRootFolder(const juce::File& rootFolder);
    void rebuildVisibleSampleList();
    void loadSampleFromBrowserRow(int row);
    void previewSampleFromBrowserRow(int row, bool forceRestart = true);
    void updatePlayerKeyboardSizing();
    void refreshPlayerPadButtons();
    void showPadAssignmentDialog(int padIndex);
    void syncAutomatedControlsFromProcessor();
    void paintPlayerPane(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintSampleBrowserPane(juce::Graphics& g, juce::Rectangle<int> browserArea) const;
    void updateTabVisibility();
    void updateGeneratePreviewButtonText();
    void refreshCaptureWaveform(bool force = false);
    void updateCaptureUiState();
    void updateGeneratePulseWidthControlState();
    void updateDiagnosticsStatusText();
    void updateSampleInformationDisplay();
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

    std::optional<audiocity::engine::SettingsSnapshot> lastSettingsSnapshot_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudiocityAudioProcessorEditor)
};

