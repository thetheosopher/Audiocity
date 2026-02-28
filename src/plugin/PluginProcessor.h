#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "../engine/EngineCore.h"

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
    [[nodiscard]] juce::String getLoadedSamplePath() const;

    using PlaybackMode = audiocity::engine::EngineCore::PlaybackMode;
    void setPlaybackMode(PlaybackMode mode) noexcept;
    [[nodiscard]] PlaybackMode getPlaybackMode() const noexcept;

    using QualityTier = audiocity::engine::EngineCore::QualityTier;
    void setQualityTier(QualityTier tier) noexcept { engine_.setQualityTier(tier); }
    [[nodiscard]] QualityTier getQualityTier() const noexcept { return engine_.getQualityTier(); }

    void setPreloadSamples(int preloadSamples) noexcept { engine_.setPreloadSamples(preloadSamples); }
    [[nodiscard]] int getPreloadSamples() const noexcept { return engine_.getPreloadSamples(); }
    [[nodiscard]] int getLoadedPreloadSamples() const noexcept { return engine_.getLoadedPreloadSamples(); }
    [[nodiscard]] int getLoadedStreamSamples() const noexcept { return engine_.getLoadedStreamSamples(); }
    [[nodiscard]] int getSegmentRebuildCount() const noexcept { return engine_.getSegmentRebuildCount(); }

    void setMonoMode(bool enabled) noexcept { engine_.setMonoMode(enabled); }
    [[nodiscard]] bool getMonoMode() const noexcept { return engine_.getMonoMode(); }

    void setLegatoMode(bool enabled) noexcept { engine_.setLegatoMode(enabled); }
    [[nodiscard]] bool getLegatoMode() const noexcept { return engine_.getLegatoMode(); }

    void setGlideSeconds(float seconds) noexcept { engine_.setGlideSeconds(seconds); }
    [[nodiscard]] float getGlideSeconds() const noexcept { return engine_.getGlideSeconds(); }

    void setChokeGroup(int chokeGroup) noexcept { engine_.setChokeGroup(chokeGroup); }
    [[nodiscard]] int getChokeGroup() const noexcept { return engine_.getChokeGroup(); }

    void setSampleWindow(int startSample, int endSample) noexcept { engine_.setSampleWindow(startSample, endSample); }
    [[nodiscard]] int getSampleWindowStart() const noexcept { return engine_.getSampleWindowStart(); }
    [[nodiscard]] int getSampleWindowEnd() const noexcept { return engine_.getSampleWindowEnd(); }

    void setLoopPoints(int loopStart, int loopEnd) noexcept { engine_.setLoopPoints(loopStart, loopEnd); }
    [[nodiscard]] int getLoopStart() const noexcept { return engine_.getLoopStart(); }
    [[nodiscard]] int getLoopEnd() const noexcept { return engine_.getLoopEnd(); }

    void setFadeSamples(int fadeInSamples, int fadeOutSamples) noexcept { engine_.setFadeSamples(fadeInSamples, fadeOutSamples); }
    [[nodiscard]] int getFadeInSamples() const noexcept { return engine_.getFadeInSamples(); }
    [[nodiscard]] int getFadeOutSamples() const noexcept { return engine_.getFadeOutSamples(); }

    void setReversePlayback(bool enabled) noexcept { engine_.setReversePlayback(enabled); }
    [[nodiscard]] bool getReversePlayback() const noexcept { return engine_.getReversePlayback(); }
    [[nodiscard]] int getLoadedSampleLength() const noexcept { return engine_.getLoadedSampleLength(); }
    [[nodiscard]] int getLoadedSampleChannels() const noexcept { return engine_.getLoadedSampleChannels(); }
    [[nodiscard]] juce::String getLoadedSampleLoopFormatBadge() const noexcept { return engine_.getLoadedSampleLoopFormatBadge(); }
    [[nodiscard]] std::vector<float> getLoadedSamplePeaks(int maxPeaks = 2048) const noexcept { return engine_.buildDisplayPeaks(maxPeaks); }
    [[nodiscard]] std::vector<std::vector<float>> getLoadedSamplePeaksByChannel(int maxPeaks = 2048) const noexcept { return engine_.buildDisplayPeaksByChannel(maxPeaks); }

    [[nodiscard]] int getRootMidiNote() const noexcept { return engine_.getRootMidiNote(); }
    void setRootMidiNote(int rootNote) noexcept { engine_.setRootMidiNote(rootNote); }

    using AdsrSettings = audiocity::engine::EngineCore::AdsrSettings;
    void setAmpEnvelope(const AdsrSettings& settings) noexcept { engine_.setAmpEnvelope(settings); }
    [[nodiscard]] AdsrSettings getAmpEnvelope() const noexcept { return engine_.getAmpEnvelope(); }
    void setFilterEnvelope(const AdsrSettings& settings) noexcept { engine_.setFilterEnvelope(settings); }
    [[nodiscard]] AdsrSettings getFilterEnvelope() const noexcept { return engine_.getFilterEnvelope(); }

    using FilterSettings = audiocity::engine::EngineCore::FilterSettings;
    void setFilterSettings(const FilterSettings& settings) noexcept { engine_.setFilterSettings(settings); }
    [[nodiscard]] FilterSettings getFilterSettings() const noexcept { return engine_.getFilterSettings(); }

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
    audiocity::engine::EngineCore engine_;

    // Ring buffer for CC events
    std::array<CcEvent, kCcFifoSize> ccFifo_{};
    std::atomic<int> ccFifoWritePos_{ 0 };
    std::atomic<int> ccFifoReadPos_{ 0 };
    void pushCcEvent(int ccNumber, int value);

    // CC mapping storage
    mutable std::mutex ccMappingMutex_;
    std::map<int, juce::String> ccToParam_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudiocityAudioProcessor)
};
