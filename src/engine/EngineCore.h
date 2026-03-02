#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

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
        fidelity,
        ultra
    };

    enum class VelocityCurve
    {
        linear,
        soft,
        hard
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
        enum class Mode
        {
            lowPass12,
            lowPass24,
            highPass12,
            highPass24,
            bandPass12,
            notch12
        };

        enum class LfoShape
        {
            sine,
            triangle,
            square,
            sawUp,
            sawDown
        };

        float baseCutoffHz = 1200.0f;
        float envAmountHz = 2400.0f;
        float resonance = 0.0f;  // 0..1, mapped to Q internally
        Mode mode = Mode::lowPass12;
        float keyTracking = 0.0f;      // -1..2 (-100%..200%)
        float velocityAmountHz = 0.0f; // Hz at velocity=1.0
        float lfoRateHz = 0.0f;        // 0 disables LFO
        float lfoRateKeyTracking = 0.0f; // -1..2 (-100%..200%)
        float lfoAmountHz = 0.0f;      // bipolar modulation depth
        float lfoAmountKeyTracking = 0.0f; // -1..2 (-100%..200%)
        float lfoStartPhaseDegrees = 0.0f;
        float lfoStartPhaseRandomDegrees = 0.0f;
        float lfoFadeInMs = 0.0f;
        bool lfoKeytrackLinear = false;
        bool lfoUnipolar = false;
        LfoShape lfoShape = LfoShape::sine;
        bool lfoRetrigger = true;
        bool lfoTempoSync = false;
        bool lfoRateKeytrackInTempoSync = true;
        int lfoSyncDivision = 6;
    };

    struct AmpLfoSettings
    {
        float rateHz = 0.0f;
        float depth = 0.0f;
        FilterSettings::LfoShape shape = FilterSettings::LfoShape::sine;
    };

    struct PitchLfoSettings
    {
        float rateHz = 0.0f;
        float depthCents = 0.0f;
    };

    struct DelaySettings
    {
        float timeMs = 320.0f;
        float feedback = 0.35f;
        float mix = 0.0f;
        bool tempoSync = false;
    };

    struct DcFilterSettings
    {
        bool enabled = true;
        float cutoffHz = 10.0f;
    };

    struct AutopanSettings
    {
        float rateHz = 0.5f;
        float depth = 0.0f;
    };

    struct SaturationSettings
    {
        enum class Mode
        {
            softClip,
            hardClip,
            tape,
            tube
        };

        float drive = 0.0f;
        Mode mode = Mode::softClip;
    };

    void prepare(double sampleRate, int maxSamplesPerBlock, int outputChannels) noexcept;
    void release() noexcept;

    struct DisplayMinMax
    {
        float minValue = 0.0f;
        float maxValue = 0.0f;
    };

    struct VoicePlaybackState
    {
        bool active = false;
        int sampleIndex = -1;
    };

    using VoicePlaybackStates = std::array<VoicePlaybackState, VoicePool::maxVoices>;

    bool loadSampleFromFile(const juce::File& file);
    void setSampleData(const juce::AudioBuffer<float>& sampleData, double sampleRate, int rootNote) noexcept;
    void setPreloadSamples(int preloadSamples) noexcept;
    [[nodiscard]] int getPreloadSamples() const noexcept { return preloadSamples_; }
    [[nodiscard]] int getLoadedPreloadSamples() const noexcept;
    [[nodiscard]] int getLoadedStreamSamples() const noexcept;
    [[nodiscard]] int getLoadedSampleLength() const noexcept;
    [[nodiscard]] int getLoadedSampleChannels() const noexcept;
    [[nodiscard]] juce::String getLoadedSampleLoopFormatBadge() const noexcept { return loadedSampleLoopFormatBadge_; }
    [[nodiscard]] std::vector<float> buildDisplayPeaks(int maxPeaks) const noexcept;
    [[nodiscard]] std::vector<std::vector<float>> buildDisplayPeaksByChannel(int maxPeaks) const noexcept;
    [[nodiscard]] std::vector<std::vector<DisplayMinMax>> buildDisplayMinMaxByChannel(int maxPeaks) const noexcept;
    [[nodiscard]] int getSegmentRebuildCount() const noexcept { return segmentRebuildCount_; }
    void setQualityTier(QualityTier tier) noexcept { qualityTier_ = tier; }
    [[nodiscard]] QualityTier getQualityTier() const noexcept { return qualityTier_; }
    void setVelocityCurve(VelocityCurve curve) noexcept { velocityCurve_ = curve; }
    [[nodiscard]] VelocityCurve getVelocityCurve() const noexcept { return velocityCurve_; }
    void setReverbMix(float mix) noexcept;
    [[nodiscard]] float getReverbMix() const noexcept { return reverbMix_; }
    void setDelaySettings(const DelaySettings& settings) noexcept;
    [[nodiscard]] DelaySettings getDelaySettings() const noexcept { return delaySettings_; }
    void setDcFilterSettings(const DcFilterSettings& settings) noexcept;
    [[nodiscard]] DcFilterSettings getDcFilterSettings() const noexcept { return dcFilterSettings_; }
    void setAutopanSettings(const AutopanSettings& settings) noexcept;
    [[nodiscard]] AutopanSettings getAutopanSettings() const noexcept { return autopanSettings_; }
    void setSaturationSettings(const SaturationSettings& settings) noexcept;
    [[nodiscard]] SaturationSettings getSaturationSettings() const noexcept { return saturationSettings_; }
    void setHostTempoBpm(float bpm) noexcept { hostTempoBpm_ = juce::jmax(1.0f, bpm); }
    void setPan(float pan) noexcept { pan_ = juce::jlimit(-1.0f, 1.0f, pan); }
    [[nodiscard]] float getPan() const noexcept { return pan_; }
    void setMasterVolume(float volume) noexcept { masterVolume_ = juce::jlimit(0.0f, 1.0f, volume); }
    [[nodiscard]] float getMasterVolume() const noexcept { return masterVolume_; }

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
    void pitchBend(int pitchWheelValue, int sampleOffsetInBlock) noexcept;

    void setPitchBendRangeSemitones(float semitones) noexcept { pitchBendRangeSemitones_ = juce::jlimit(0.0f, 24.0f, semitones); }
    [[nodiscard]] float getPitchBendRangeSemitones() const noexcept { return pitchBendRangeSemitones_; }

    void render(float** outputs, int numChannels, int numSamples) noexcept;
    void render(juce::AudioBuffer<float>& audioBuffer, const juce::MidiBuffer& midiBuffer) noexcept;
    void panic() noexcept;

    void setAmpEnvelope(const AdsrSettings& settings) noexcept;
    void setAmpLfoSettings(const AmpLfoSettings& settings) noexcept;
    void setPitchLfoSettings(const PitchLfoSettings& settings) noexcept;
    void setFilterEnvelope(const AdsrSettings& settings) noexcept;
    void setFilterSettings(const FilterSettings& settings) noexcept;

    [[nodiscard]] AdsrSettings getAmpEnvelope() const noexcept { return ampEnvelopeSettings_; }
    [[nodiscard]] AmpLfoSettings getAmpLfoSettings() const noexcept { return ampLfoSettings_; }
    [[nodiscard]] PitchLfoSettings getPitchLfoSettings() const noexcept { return pitchLfoSettings_; }
    [[nodiscard]] AdsrSettings getFilterEnvelope() const noexcept { return filterEnvelopeSettings_; }
    [[nodiscard]] FilterSettings getFilterSettings() const noexcept { return filterSettings_; }

    [[nodiscard]] juce::String getSamplePath() const { return samplePath_; }
    void clearSamplePath() noexcept { samplePath_.clear(); }
    [[nodiscard]] int getRootMidiNote() const noexcept { return rootMidiNote_; }
    void setRootMidiNote(int rootMidiNote) noexcept;
    void setCoarseTuneSemitones(float semitones) noexcept { coarseTuneSemitones_ = juce::jlimit(-24.0f, 24.0f, semitones); }
    [[nodiscard]] float getCoarseTuneSemitones() const noexcept { return coarseTuneSemitones_; }
    void setFineTuneCents(float cents) noexcept { fineTuneCents_ = juce::jlimit(-100.0f, 100.0f, cents); }
    [[nodiscard]] float getFineTuneCents() const noexcept { return fineTuneCents_; }

    void setPlaybackMode(PlaybackMode mode) noexcept { playbackMode_ = mode; }
    [[nodiscard]] PlaybackMode getPlaybackMode() const noexcept { return playbackMode_; }

    void setMonoMode(bool enabled) noexcept { monoMode_ = enabled; }
    [[nodiscard]] bool getMonoMode() const noexcept { return monoMode_; }

    void setLegatoMode(bool enabled) noexcept { legatoMode_ = enabled; }
    [[nodiscard]] bool getLegatoMode() const noexcept { return legatoMode_; }

    void setGlideSeconds(float seconds) noexcept { glideSeconds_ = juce::jmax(0.0f, seconds); }
    [[nodiscard]] float getGlideSeconds() const noexcept { return glideSeconds_; }
    void setPolyphonyLimit(int voices) noexcept;
    [[nodiscard]] int getPolyphonyLimit() const noexcept { return voicePool_.getVoiceLimit(); }

    void setLoopPoints(int loopStart, int loopEnd) noexcept;
    [[nodiscard]] int getLoopStart() const noexcept { return loopStartSample_; }
    [[nodiscard]] int getLoopEnd() const noexcept { return loopEndSample_; }
    void setLoopCrossfadeSamples(int crossfadeSamples) noexcept;
    [[nodiscard]] int getLoopCrossfadeSamples() const noexcept { return loopCrossfadeSamples_; }

    [[nodiscard]] int activeVoiceCount() const noexcept;
    [[nodiscard]] int stealCount() const noexcept;
    void resetStealCount() noexcept;
    [[nodiscard]] bool isNoteActive(int noteNumber) const noexcept;
    [[nodiscard]] VoicePlaybackStates getVoicePlaybackStates() const noexcept;

private:
    struct SampleSegments;

    struct VoiceState
    {
        float samplePosition = 0.0f;
        float sampleIncrement = 1.0f;
        float velocity = 0.0f;
        bool noteHeld = false;
        float lastAmpLevel = 0.0f;
        juce::dsp::StateVariableTPTFilter<float> filterA;
        juce::dsp::StateVariableTPTFilter<float> filterB;
        float filterLfoPhase = 0.0f;
        int filterLfoFadeSamplesTotal = 0;
        int filterLfoFadeSamplesRemaining = 0;
        float glideTargetIncrement = 1.0f;
        int glideSamplesRemaining = 0;
        juce::ADSR ampEnvelope;
        juce::ADSR filterEnvelope;
    };

    enum class EventType
    {
        noteOn,
        noteOff,
        pitchBend
    };

    struct PendingEvent
    {
        EventType type = EventType::noteOn;
        int noteNumber = 0;
        float velocity = 0.0f;
        int offset = 0;
    };

    bool enqueuePendingEvent(EventType type, int noteNumber, float velocity, int sampleOffsetInBlock) noexcept;

    void generateFallbackSample() noexcept;
    void flushPendingEventsAtOffset(int offset) noexcept;
    void startVoice(int voiceIndex, int noteNumber, float velocity) noexcept;
    void retargetVoiceLegato(int voiceIndex, int noteNumber, float velocity) noexcept;
    void stopAllVoicesImmediate() noexcept;
    void stopVoicesForNoteImmediate(int noteNumber) noexcept;
    void releaseVoicesForNote(int noteNumber) noexcept;
    void applyEnvelopeParamsToVoices() noexcept;
    void applyFilterParamsToVoices() noexcept;
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
    [[nodiscard]] float readSampleCubic(const SampleSegments& segments, float position) const noexcept;
    [[nodiscard]] float computeFilterSample(float inputSample,
                                            float envValue,
                                            float lfoValue,
                                            int noteNumber,
                                            float velocity,
                                            VoiceState& voice) const noexcept;
    void processDelay(float** outputs, int numChannels, int numSamples) noexcept;
    void processDcFilter(float** outputs, int numChannels, int numSamples) noexcept;
    [[nodiscard]] float processSaturationSample(float sample) const noexcept;
    [[nodiscard]] float computeSyncedDelayTimeMs(float rawTimeMs) const noexcept;
    [[nodiscard]] float mapVelocity(float velocity) const noexcept;
    void updateReverbParameters() noexcept;

    VoicePool voicePool_;
    std::array<VoiceState, VoicePool::maxVoices> voices_{};
    std::array<PendingEvent, 1024> pendingEvents_{};
    int pendingEventCount_ = 0;

    std::atomic<std::shared_ptr<const SampleSegments>> sampleSegments_{};
    juce::AudioBuffer<float> displaySampleData_;
    juce::String loadedSampleLoopFormatBadge_;
    juce::String samplePath_;
    double sampleDataRate_ = 44100.0;
    int rootMidiNote_ = 60;
    float coarseTuneSemitones_ = 0.0f;
    float fineTuneCents_ = 0.0f;
    float pitchBendRangeSemitones_ = 2.0f;
    float currentPitchBendSemitones_ = 0.0f;
    int preloadSamples_ = 32768;
    int segmentRebuildCount_ = 0;

    AdsrSettings ampEnvelopeSettings_{};
    AmpLfoSettings ampLfoSettings_{};
    PitchLfoSettings pitchLfoSettings_{};
    AdsrSettings filterEnvelopeSettings_{ 0.001f, 0.120f, 0.0f, 0.100f };
    FilterSettings filterSettings_{};
    PlaybackMode playbackMode_ = PlaybackMode::gate;
    bool monoMode_ = false;
    bool legatoMode_ = false;
    float glideSeconds_ = 0.0f;
    float globalFilterLfoPhase_ = 0.0f;
    float globalAmpLfoPhase_ = 0.0f;
    float globalPitchLfoPhase_ = 0.0f;
    QualityTier qualityTier_ = QualityTier::fidelity;
    VelocityCurve velocityCurve_ = VelocityCurve::linear;
    float reverbMix_ = 0.0f;
    DelaySettings delaySettings_{};
    DcFilterSettings dcFilterSettings_{};
    AutopanSettings autopanSettings_{};
    SaturationSettings saturationSettings_{};
    float autopanPhase_ = 0.0f;
    float hostTempoBpm_ = 120.0f;
    float pan_ = 0.0f;
    float masterVolume_ = 1.0f;
    juce::Reverb reverb_;
    juce::AudioBuffer<float> delayBuffer_;
    int delayWritePos_ = 0;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> dcBlockFilters_{};

    int loopStartSample_ = 0;
    int loopEndSample_ = 0;
    int loopCrossfadeSamples_ = 0;
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
