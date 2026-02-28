#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>

#include "VoicePool.h"

namespace audiocity::engine
{
class EngineCore
{
public:
    enum class PlaybackMode
    {
        gate,
        oneShot,
        loop
    };

    enum class QualityTier
    {
        cpu,
        fidelity
    };

    struct AdsrSettings
    {
        float attackSeconds = 0.005f;
        float decaySeconds = 0.150f;
        float sustainLevel = 0.85f;
        float releaseSeconds = 0.150f;
    };

    struct FilterSettings
    {
        float baseCutoffHz = 1200.0f;
        float envAmountHz = 2400.0f;
        float resonance = 0.0f;  // 0..1, mapped to Q internally
    };

    void prepare(double sampleRate, int maxSamplesPerBlock, int outputChannels) noexcept;
    void release() noexcept;

    bool loadSampleFromFile(const juce::File& file);
    void setSampleData(const juce::AudioBuffer<float>& sampleData, double sampleRate, int rootNote) noexcept;
    void setPreloadSamples(int preloadSamples) noexcept;
    [[nodiscard]] int getPreloadSamples() const noexcept { return preloadSamples_; }
    [[nodiscard]] int getLoadedPreloadSamples() const noexcept;
    [[nodiscard]] int getLoadedStreamSamples() const noexcept;
    [[nodiscard]] int getLoadedSampleLength() const noexcept;
    [[nodiscard]] std::vector<float> buildDisplayPeaks(int maxPeaks) const noexcept;
    [[nodiscard]] int getSegmentRebuildCount() const noexcept { return segmentRebuildCount_; }
    void setQualityTier(QualityTier tier) noexcept { qualityTier_ = tier; }
    [[nodiscard]] QualityTier getQualityTier() const noexcept { return qualityTier_; }

    void setSampleWindow(int startSample, int endSample) noexcept;
    [[nodiscard]] int getSampleWindowStart() const noexcept { return sampleWindowStart_; }
    [[nodiscard]] int getSampleWindowEnd() const noexcept { return sampleWindowEnd_; }

    void setFadeSamples(int fadeInSamples, int fadeOutSamples) noexcept;
    [[nodiscard]] int getFadeInSamples() const noexcept { return fadeInSamples_; }
    [[nodiscard]] int getFadeOutSamples() const noexcept { return fadeOutSamples_; }

    void setReversePlayback(bool enabled) noexcept { reversePlayback_ = enabled; }
    [[nodiscard]] bool getReversePlayback() const noexcept { return reversePlayback_; }

    void noteOn(int noteNumber, float velocity, int sampleOffsetInBlock) noexcept;
    void noteOff(int noteNumber, int sampleOffsetInBlock) noexcept;

    void render(float** outputs, int numChannels, int numSamples) noexcept;
    void render(juce::AudioBuffer<float>& audioBuffer, const juce::MidiBuffer& midiBuffer) noexcept;

    void setAmpEnvelope(const AdsrSettings& settings) noexcept;
    void setFilterEnvelope(const AdsrSettings& settings) noexcept;
    void setFilterSettings(const FilterSettings& settings) noexcept;

    [[nodiscard]] AdsrSettings getAmpEnvelope() const noexcept { return ampEnvelopeSettings_; }
    [[nodiscard]] AdsrSettings getFilterEnvelope() const noexcept { return filterEnvelopeSettings_; }
    [[nodiscard]] FilterSettings getFilterSettings() const noexcept { return filterSettings_; }

    [[nodiscard]] juce::String getSamplePath() const { return samplePath_; }
    [[nodiscard]] int getRootMidiNote() const noexcept { return rootMidiNote_; }
    void setRootMidiNote(int rootMidiNote) noexcept;

    void setPlaybackMode(PlaybackMode mode) noexcept { playbackMode_ = mode; }
    [[nodiscard]] PlaybackMode getPlaybackMode() const noexcept { return playbackMode_; }

    void setMonoMode(bool enabled) noexcept { monoMode_ = enabled; }
    [[nodiscard]] bool getMonoMode() const noexcept { return monoMode_; }

    void setLegatoMode(bool enabled) noexcept { legatoMode_ = enabled; }
    [[nodiscard]] bool getLegatoMode() const noexcept { return legatoMode_; }

    void setGlideSeconds(float seconds) noexcept { glideSeconds_ = juce::jmax(0.0f, seconds); }
    [[nodiscard]] float getGlideSeconds() const noexcept { return glideSeconds_; }

    void setChokeGroup(int chokeGroup) noexcept { chokeGroup_ = juce::jmax(0, chokeGroup); }
    [[nodiscard]] int getChokeGroup() const noexcept { return chokeGroup_; }

    void setLoopPoints(int loopStart, int loopEnd) noexcept;
    [[nodiscard]] int getLoopStart() const noexcept { return loopStartSample_; }
    [[nodiscard]] int getLoopEnd() const noexcept { return loopEndSample_; }

    [[nodiscard]] int activeVoiceCount() const noexcept;
    [[nodiscard]] int stealCount() const noexcept;
    void resetStealCount() noexcept;
    [[nodiscard]] bool isNoteActive(int noteNumber) const noexcept;

private:
    struct SampleSegments;

    struct VoiceState
    {
        float samplePosition = 0.0f;
        float sampleIncrement = 1.0f;
        float velocity = 0.0f;
        bool noteHeld = false;
        float lastAmpLevel = 0.0f;
        float lpfState = 0.0f;
        float glideTargetIncrement = 1.0f;
        int glideSamplesRemaining = 0;
        juce::ADSR ampEnvelope;
        juce::ADSR filterEnvelope;
    };

    enum class EventType
    {
        noteOn,
        noteOff
    };

    struct PendingEvent
    {
        EventType type = EventType::noteOn;
        int noteNumber = 0;
        float velocity = 0.0f;
        int offset = 0;
    };

    void generateFallbackSample() noexcept;
    void flushPendingEventsAtOffset(int offset) noexcept;
    void startVoice(int voiceIndex, int noteNumber, float velocity) noexcept;
    void retargetVoiceLegato(int voiceIndex, int noteNumber, float velocity) noexcept;
    void stopAllVoicesImmediate() noexcept;
    void releaseVoicesForNote(int noteNumber) noexcept;
    void applyEnvelopeParamsToVoices() noexcept;
    [[nodiscard]] float computeSampleIncrementForNote(int noteNumber) const noexcept;
    void rebuildSampleSegments(const juce::AudioBuffer<float>& monoSampleData) noexcept;
    [[nodiscard]] std::shared_ptr<const SampleSegments> getSampleSegmentsSnapshot() const noexcept;
    [[nodiscard]] static std::shared_ptr<const SampleSegments> buildSampleSegments(const juce::AudioBuffer<float>& monoSampleData,
        int preloadSamples) noexcept;
    [[nodiscard]] int getTotalSampleLength(const SampleSegments& segments) const noexcept;
    [[nodiscard]] int getTotalSampleLength() const noexcept;
    [[nodiscard]] int getEffectivePlaybackLength(const SampleSegments& segments) const noexcept;
    [[nodiscard]] int getEffectivePlaybackLength() const noexcept;
    [[nodiscard]] int mapPlaybackIndexToSampleIndex(const SampleSegments& segments, int playbackIndex) const noexcept;
    [[nodiscard]] float computeEditGain(float playbackPosition, int playbackLength) const noexcept;
    [[nodiscard]] float readSampleAt(const SampleSegments& segments, int index) const noexcept;
    [[nodiscard]] float readSampleAt(int index) const noexcept;

    [[nodiscard]] float readSampleLinear(const SampleSegments& segments, float position) const noexcept;
    [[nodiscard]] float readSampleLinear(float position) const noexcept;
    [[nodiscard]] float computeLpf(float inputSample, float envValue, float& state) const noexcept;

    VoicePool voicePool_;
    std::array<VoiceState, VoicePool::maxVoices> voices_{};
    std::array<PendingEvent, 1024> pendingEvents_{};
    int pendingEventCount_ = 0;

    std::atomic<std::shared_ptr<const SampleSegments>> sampleSegments_{};
    juce::String samplePath_;
    double sampleDataRate_ = 44100.0;
    int rootMidiNote_ = 60;
    int preloadSamples_ = 32768;
    int segmentRebuildCount_ = 0;

    AdsrSettings ampEnvelopeSettings_{};
    AdsrSettings filterEnvelopeSettings_{ 0.001f, 0.120f, 0.0f, 0.100f };
    FilterSettings filterSettings_{};
    PlaybackMode playbackMode_ = PlaybackMode::gate;
    bool monoMode_ = false;
    bool legatoMode_ = false;
    float glideSeconds_ = 0.0f;
    int chokeGroup_ = 0;
    QualityTier qualityTier_ = QualityTier::fidelity;

    int loopStartSample_ = 0;
    int loopEndSample_ = 0;
    int sampleWindowStart_ = 0;
    int sampleWindowEnd_ = 0;
    int fadeInSamples_ = 0;
    int fadeOutSamples_ = 0;
    bool reversePlayback_ = false;

    double sampleRate_ = 44100.0;
    int maxSamplesPerBlock_ = 0;
    int outputChannels_ = 2;
};
}
