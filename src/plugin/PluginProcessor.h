#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "../engine/EngineCore.h"
#include "PlayerPadState.h"

#include <array>
#include <atomic>
#include <map>
#include <mutex>

class AudiocityAudioProcessor final : public juce::AudioProcessor
{
public:
    AudiocityAudioProcessor();
    ~AudiocityAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override { juce::ignoreUnused(index); }
    const juce::String getProgramName(int index) override
    {
        juce::ignoreUnused(index);
        return {};
    }
    void changeProgramName(int index, const juce::String& newName) override
    {
        juce::ignoreUnused(index, newName);
    }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool loadSampleFromFile(const juce::File& file);
    void loadGeneratedWaveformAsSample(const std::vector<float>& waveform, int rootMidiNote = 60);
    [[nodiscard]] juce::String getLoadedSamplePath() const;
    [[nodiscard]] bool isGeneratedWaveformLoaded() const noexcept
    {
        return generatedWaveformLoaded_.load(std::memory_order_relaxed);
    }
    void setSampleBrowserRootFolder(const juce::String& folderPath) { sampleBrowserRootFolderPath_ = folderPath; }
    [[nodiscard]] juce::String getSampleBrowserRootFolder() const { return sampleBrowserRootFolderPath_; }

    using PlayerPadAssignment = audiocity::plugin::PlayerPadAssignment;
    static constexpr int kPlayerPadCount = audiocity::plugin::kPlayerPadCount;
    void setPlayerPadAssignment(int padIndex, int noteNumber, int velocity) noexcept;
    [[nodiscard]] PlayerPadAssignment getPlayerPadAssignment(int padIndex) const noexcept;
    [[nodiscard]] std::array<PlayerPadAssignment, kPlayerPadCount> getAllPlayerPadAssignments() const noexcept
    {
        return playerPadAssignments_;
    }

    void enqueueUiMidiNoteOn(int noteNumber, int velocity) noexcept;
    void enqueueUiMidiNoteOff(int noteNumber) noexcept;
    void setGeneratedWaveformPreview(const std::vector<float>& waveform) noexcept;
    void setGeneratedWaveformPreviewMidiNote(int midiNote) noexcept;
    void startGeneratedWaveformPreview() noexcept;
    void stopGeneratedWaveformPreview() noexcept;
    bool previewSampleFromFile(const juce::File& file);
    void panicAllAudio() noexcept;
    [[nodiscard]] bool isGeneratedWaveformPreviewPlaying() const noexcept
    {
        return previewWavePlaying_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool isSamplePreviewPlaying() const noexcept
    {
        return samplePreviewPlaying_.load(std::memory_order_relaxed);
    }

    using PlaybackMode = audiocity::engine::EngineCore::PlaybackMode;
    void setPlaybackMode(PlaybackMode mode) noexcept;
    [[nodiscard]] PlaybackMode getPlaybackMode() const noexcept;

    using QualityTier = audiocity::engine::EngineCore::QualityTier;
    void setQualityTier(QualityTier tier) noexcept;
    [[nodiscard]] QualityTier getQualityTier() const noexcept { return engine_.getQualityTier(); }

    using VelocityCurve = audiocity::engine::EngineCore::VelocityCurve;
    void setVelocityCurve(VelocityCurve curve) noexcept;
    [[nodiscard]] VelocityCurve getVelocityCurve() const noexcept { return engine_.getVelocityCurve(); }

    void setReverbMix(float mix) noexcept;
    [[nodiscard]] float getReverbMix() const noexcept { return engine_.getReverbMix(); }
    void setPan(float pan) noexcept;
    [[nodiscard]] float getPan() const noexcept { return engine_.getPan(); }
    void setMasterVolume(float volume) noexcept;
    [[nodiscard]] float getMasterVolume() const noexcept { return engine_.getMasterVolume(); }

    struct OutputPeakLevels
    {
        float left = 0.0f;
        float right = 0.0f;
    };
    [[nodiscard]] OutputPeakLevels consumeOutputPeakLevels() noexcept;

    void setPreloadSamples(int preloadSamples) noexcept { engine_.setPreloadSamples(preloadSamples); }
    [[nodiscard]] int getPreloadSamples() const noexcept { return engine_.getPreloadSamples(); }
    [[nodiscard]] int getLoadedPreloadSamples() const noexcept { return engine_.getLoadedPreloadSamples(); }
    [[nodiscard]] int getLoadedStreamSamples() const noexcept { return engine_.getLoadedStreamSamples(); }
    [[nodiscard]] int getSegmentRebuildCount() const noexcept { return engine_.getSegmentRebuildCount(); }
    [[nodiscard]] int getActiveVoiceCount() const noexcept { return engine_.activeVoiceCount(); }

    void setMonoMode(bool enabled) noexcept;
    [[nodiscard]] bool getMonoMode() const noexcept { return engine_.getMonoMode(); }

    void setLegatoMode(bool enabled) noexcept;
    [[nodiscard]] bool getLegatoMode() const noexcept { return engine_.getLegatoMode(); }

    void setGlideSeconds(float seconds) noexcept;
    [[nodiscard]] float getGlideSeconds() const noexcept { return engine_.getGlideSeconds(); }
    void setPolyphonyLimit(int voices) noexcept;
    [[nodiscard]] int getPolyphonyLimit() const noexcept { return engine_.getPolyphonyLimit(); }

    void setSampleWindow(int startSample, int endSample) noexcept;
    [[nodiscard]] int getSampleWindowStart() const noexcept { return engine_.getSampleWindowStart(); }
    [[nodiscard]] int getSampleWindowEnd() const noexcept { return engine_.getSampleWindowEnd(); }

    void setLoopPoints(int loopStart, int loopEnd) noexcept;
    [[nodiscard]] int getLoopStart() const noexcept { return engine_.getLoopStart(); }
    [[nodiscard]] int getLoopEnd() const noexcept { return engine_.getLoopEnd(); }
    void setLoopCrossfadeSamples(int crossfadeSamples) noexcept;
    [[nodiscard]] int getLoopCrossfadeSamples() const noexcept { return engine_.getLoopCrossfadeSamples(); }

    void setFadeSamples(int fadeInSamples, int fadeOutSamples) noexcept;
    [[nodiscard]] int getFadeInSamples() const noexcept { return engine_.getFadeInSamples(); }
    [[nodiscard]] int getFadeOutSamples() const noexcept { return engine_.getFadeOutSamples(); }

    void setReversePlayback(bool enabled) noexcept;
    [[nodiscard]] bool getReversePlayback() const noexcept { return engine_.getReversePlayback(); }
    [[nodiscard]] int getLoadedSampleLength() const noexcept { return engine_.getLoadedSampleLength(); }
    [[nodiscard]] int getLoadedSampleChannels() const noexcept { return engine_.getLoadedSampleChannels(); }
    [[nodiscard]] juce::String getLoadedSampleLoopFormatBadge() const noexcept { return engine_.getLoadedSampleLoopFormatBadge(); }
    [[nodiscard]] std::vector<float> getLoadedSamplePeaks(int maxPeaks = 2048) const noexcept { return engine_.buildDisplayPeaks(maxPeaks); }
    [[nodiscard]] std::vector<std::vector<float>> getLoadedSamplePeaksByChannel(int maxPeaks = 2048) const noexcept { return engine_.buildDisplayPeaksByChannel(maxPeaks); }
    [[nodiscard]] std::vector<std::vector<audiocity::engine::EngineCore::DisplayMinMax>>
        getLoadedSampleMinMaxByChannel(int maxPeaks = 2048) const noexcept
    {
        return engine_.buildDisplayMinMaxByChannel(maxPeaks);
    }

    [[nodiscard]] int getRootMidiNote() const noexcept { return engine_.getRootMidiNote(); }
    void setRootMidiNote(int rootNote) noexcept;
    void setCoarseTuneSemitones(float semitones) noexcept;
    [[nodiscard]] float getCoarseTuneSemitones() const noexcept { return engine_.getCoarseTuneSemitones(); }
    void setFineTuneCents(float cents) noexcept;
    [[nodiscard]] float getFineTuneCents() const noexcept { return engine_.getFineTuneCents(); }
    void setPitchBendRangeSemitones(float semitones) noexcept;
    [[nodiscard]] float getPitchBendRangeSemitones() const noexcept { return engine_.getPitchBendRangeSemitones(); }

    using AdsrSettings = audiocity::engine::EngineCore::AdsrSettings;
    void setAmpEnvelope(const AdsrSettings& settings) noexcept;
    [[nodiscard]] AdsrSettings getAmpEnvelope() const noexcept { return engine_.getAmpEnvelope(); }
    void setFilterEnvelope(const AdsrSettings& settings) noexcept;
    [[nodiscard]] AdsrSettings getFilterEnvelope() const noexcept { return engine_.getFilterEnvelope(); }

    using FilterSettings = audiocity::engine::EngineCore::FilterSettings;
    void setFilterSettings(const FilterSettings& settings) noexcept;
    [[nodiscard]] FilterSettings getFilterSettings() const noexcept { return engine_.getFilterSettings(); }

    [[nodiscard]] juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts_; }
    [[nodiscard]] const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept { return apvts_; }

    // ── MIDI CC mapping ──────────────────────────────────────────────────────
    struct CcEvent
    {
        int ccNumber = 0;
        int value = 0;
    };

    // Lock-free FIFO: processBlock writes, editor reads via timer.
    static constexpr int kCcFifoSize = 256;
    [[nodiscard]] bool popCcEvent(CcEvent& out);

    // Persistent CC → paramId mappings (thread-safe, called from message thread)
    void setCcMapping(int ccNumber, const juce::String& paramId);
    void clearCcMapping(int ccNumber);
    void clearCcMappingByParam(const juce::String& paramId);
    [[nodiscard]] int getCcForParam(const juce::String& paramId) const;
    [[nodiscard]] juce::String getParamForCc(int ccNumber) const;
    [[nodiscard]] std::map<int, juce::String> getAllCcMappings() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void syncEngineFromAutomatableParameters() noexcept;
    void updateParameterFromPlainValue(const juce::String& parameterId, float plainValue) noexcept;
    void updateHostTempoFromPlayHead() noexcept;
    [[nodiscard]] float lfoRateHzFromTempoSync(int divisionIndex) const noexcept;
    void syncSampleDerivedParametersFromEngine() noexcept;
    void renderGeneratedWavePreview(juce::AudioBuffer<float>& buffer) noexcept;
    void renderSampleFilePreview(juce::AudioBuffer<float>& buffer) noexcept;
    void updateOutputPeakLevels(const juce::AudioBuffer<float>& buffer) noexcept;

    struct UiMidiEvent
    {
        int noteNumber = 0;
        int velocity = 0;
        bool isNoteOn = false;
    };

    static constexpr int kUiMidiFifoSize = 256;

    audiocity::engine::EngineCore engine_;
    juce::AudioProcessorValueTreeState apvts_;

    // Ring buffer for CC events
    std::array<CcEvent, kCcFifoSize> ccFifo_{};
    std::atomic<int> ccFifoWritePos_{ 0 };
    std::atomic<int> ccFifoReadPos_{ 0 };
    void pushCcEvent(int ccNumber, int value);

    // CC mapping storage
    mutable std::mutex ccMappingMutex_;
    std::map<int, juce::String> ccToParam_;
    juce::String sampleBrowserRootFolderPath_;
    std::array<PlayerPadAssignment, kPlayerPadCount> playerPadAssignments_{};

    std::array<UiMidiEvent, kUiMidiFifoSize> uiMidiFifo_{};
    std::atomic<int> uiMidiWritePos_{ 0 };
    std::atomic<int> uiMidiReadPos_{ 0 };
    std::atomic<int> suspendParamSyncBlocks_{ 0 };
    std::atomic<float> hostBpm_{ 120.0f };
    std::atomic<bool> generatedWaveformLoaded_{ false };
    std::atomic<float> outputPeakLeft_{ 0.0f };
    std::atomic<float> outputPeakRight_{ 0.0f };
    static constexpr int kPreviewWaveMaxSamples = 2048;
    std::array<float, kPreviewWaveMaxSamples> previewWaveData_{};
    std::atomic<int> previewWaveSamples_{ 0 };
    std::atomic<int> previewWaveMidiNote_{ 60 };
    std::atomic<bool> previewWavePlaying_{ false };
    float previewWavePhase_ = 0.0f;

    static constexpr int kSamplePreviewMaxSamples = 30 * 48000;
    std::array<float, kSamplePreviewMaxSamples> samplePreviewData_{};
    std::atomic<int> samplePreviewSamples_{ 0 };
    std::atomic<double> samplePreviewSourceRate_{ 44100.0 };
    std::atomic<bool> samplePreviewPlaying_{ false };
    float samplePreviewReadPos_ = 0.0f;
    void pushUiMidiEvent(int noteNumber, int velocity, bool isNoteOn) noexcept;
    [[nodiscard]] bool popUiMidiEvent(UiMidiEvent& out) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudiocityAudioProcessor)
};
