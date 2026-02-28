#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <atomic>

#include "../engine/EngineCore.h"
#include "../engine/ZoneSelector.h"
#include "../engine/sfz/SfzModel.h"
#include "BrowserIndex.h"

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

    bool importSfzFile(const juce::File& file);
    [[nodiscard]] const std::vector<audiocity::engine::sfz::Zone>& getImportedZones() const noexcept;
    [[nodiscard]] const std::vector<audiocity::engine::sfz::Diagnostic>& getImportDiagnostics() const noexcept;
    bool updateImportedZoneLoopPoints(int zoneIndex, int loopStart, int loopEnd);

    using RoundRobinMode = audiocity::engine::ZoneSelector::RoundRobinMode;
    void setRoundRobinMode(RoundRobinMode mode) noexcept;
    [[nodiscard]] RoundRobinMode getRoundRobinMode() const noexcept;

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

    void setEditorLoopPoints(int loopStart, int loopEnd) noexcept { engine_.setLoopPoints(loopStart, loopEnd); }
    [[nodiscard]] int getEditorLoopStart() const noexcept { return engine_.getLoopStart(); }
    [[nodiscard]] int getEditorLoopEnd() const noexcept { return engine_.getLoopEnd(); }

    void setFadeSamples(int fadeInSamples, int fadeOutSamples) noexcept { engine_.setFadeSamples(fadeInSamples, fadeOutSamples); }
    [[nodiscard]] int getFadeInSamples() const noexcept { return engine_.getFadeInSamples(); }
    [[nodiscard]] int getFadeOutSamples() const noexcept { return engine_.getFadeOutSamples(); }

    void setReversePlayback(bool enabled) noexcept { engine_.setReversePlayback(enabled); }
    [[nodiscard]] bool getReversePlayback() const noexcept { return engine_.getReversePlayback(); }
    [[nodiscard]] int getLoadedSampleLength() const noexcept { return engine_.getLoadedSampleLength(); }

    BrowserIndex& browserIndex() noexcept { return browserIndex_; }
    const BrowserIndex& browserIndex() const noexcept { return browserIndex_; }

    bool startPreviewFromPath(const juce::String& path);
    void stopPreview();
    [[nodiscard]] bool isPreviewPlaying() const noexcept { return previewPlaying_.load(); }

private:
    audiocity::engine::EngineCore engine_;
    audiocity::engine::sfz::Program sfzProgram_;
    audiocity::engine::ZoneSelector zoneSelector_;
    BrowserIndex browserIndex_;

    std::atomic<bool> previewStartRequested_{ false };
    std::atomic<bool> previewStopRequested_{ false };
    std::atomic<bool> previewPlaying_{ false };
    static constexpr int previewMidiNote_ = 72;
    juce::String importedSfzPath_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudiocityAudioProcessor)
};
