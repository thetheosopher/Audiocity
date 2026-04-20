#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "PresetJson.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
constexpr auto kPatchRoot = "AudiocityPatch";
constexpr auto kSamplePath = "samplePath";
constexpr auto kGeneratedWaveformData = "generatedWaveformData";
constexpr auto kCapturedSampleData = "capturedSampleData";
constexpr auto kCapturedSampleRate = "capturedSampleRate";
constexpr auto kSampleBrowserRootFolder = "sampleBrowserRootFolder";
constexpr auto kRootMidiNote = "rootMidiNote";
constexpr auto kCoarseTuneSemitones = "coarseTuneSemitones";
constexpr auto kFineTuneCents = "fineTuneCents";
constexpr auto kPitchBendRangeSemitones = "pitchBendRangeSemitones";
constexpr auto kPitchLfoRate = "pitchLfoRate";
constexpr auto kPitchLfoDepth = "pitchLfoDepth";

constexpr auto kAmpAttack = "ampAttack";
constexpr auto kAmpDecay = "ampDecay";
constexpr auto kAmpSustain = "ampSustain";
constexpr auto kAmpRelease = "ampRelease";
constexpr auto kAmpLfoRate = "ampLfoRate";
constexpr auto kAmpLfoDepth = "ampLfoDepth";
constexpr auto kAmpLfoShape = "ampLfoShape";

constexpr auto kFilterAttack = "filterAttack";
constexpr auto kFilterDecay = "filterDecay";
constexpr auto kFilterSustain = "filterSustain";
constexpr auto kFilterRelease = "filterRelease";
constexpr auto kFilterBaseCutoff = "filterBaseCutoff";
constexpr auto kFilterEnvAmount = "filterEnvAmount";
constexpr auto kFilterResonance = "filterResonance";
constexpr auto kFilterMode = "filterMode";
constexpr auto kFilterKeyTracking = "filterKeyTracking";
constexpr auto kFilterVelocityAmount = "filterVelocityAmount";
constexpr auto kFilterLfoRate = "filterLfoRate";
constexpr auto kFilterLfoRateKeytrack = "filterLfoRateKeytrack";
constexpr auto kFilterLfoAmount = "filterLfoAmount";
constexpr auto kFilterLfoAmountKeytrack = "filterLfoAmountKeytrack";
constexpr auto kFilterLfoStartPhase = "filterLfoStartPhase";
constexpr auto kFilterLfoStartPhaseRandom = "filterLfoStartPhaseRandom";
constexpr auto kFilterLfoFadeIn = "filterLfoFadeIn";
constexpr auto kFilterLfoShape = "filterLfoShape";
constexpr auto kFilterLfoRetrigger = "filterLfoRetrigger";
constexpr auto kFilterLfoTempoSync = "filterLfoTempoSync";
constexpr auto kFilterLfoRateKeytrackInTempoSync = "filterLfoRateKeytrackInTempoSync";
constexpr auto kFilterLfoKeytrackLinear = "filterLfoKeytrackLinear";
constexpr auto kFilterLfoUnipolar = "filterLfoUnipolar";
constexpr auto kFilterLfoSyncDivision = "filterLfoSyncDivision";
constexpr auto kPlaybackMode = "playbackMode";
constexpr auto kQualityTier = "qualityTier";
constexpr auto kVelocityCurve = "velocityCurve";
constexpr auto kReverbMix = "reverbMix";
constexpr auto kDelayTimeMs = "delayTimeMs";
constexpr auto kDelayFeedback = "delayFeedback";
constexpr auto kDelayMix = "delayMix";
constexpr auto kDelayTempoSync = "delayTempoSync";
constexpr auto kDcFilterEnabled = "dcFilterEnabled";
constexpr auto kDcFilterCutoffHz = "dcFilterCutoffHz";
constexpr auto kAutopanRateHz = "autopanRateHz";
constexpr auto kAutopanDepth = "autopanDepth";
constexpr auto kSaturationDrive = "saturationDrive";
constexpr auto kSaturationMode = "saturationMode";
constexpr auto kPan = "pan";
constexpr auto kMasterVolume = "masterVolume";
constexpr auto kPreloadSamples = "preloadSamples";
constexpr auto kMonoMode = "monoMode";
constexpr auto kLegatoMode = "legatoMode";
constexpr auto kGlideSeconds = "glideSeconds";
constexpr auto kPolyphonyLimit = "polyphonyLimit";
constexpr auto kSampleWindowStart = "sampleWindowStart";
constexpr auto kSampleWindowEnd = "sampleWindowEnd";
constexpr auto kWaveformViewStartSample = "waveformViewStartSample";
constexpr auto kWaveformViewSampleCount = "waveformViewSampleCount";
constexpr auto kEditorTabIndex = "editorTabIndex";
constexpr auto kWaveformDisplayMode = "waveformDisplayMode";
constexpr auto kGenerateWaveType = "generateWaveType";
constexpr auto kGenerateSampleCount = "generateSampleCount";
constexpr auto kGenerateBitDepth = "generateBitDepth";
constexpr auto kGeneratePulseWidth = "generatePulseWidth";
constexpr auto kGenerateFrequencyMidiNote = "generateFrequencyMidiNote";
constexpr auto kGenerateSketchSmoothing = "generateSketchSmoothing";
constexpr auto kCaptureTargetSampleRate = "captureTargetSampleRate";
constexpr auto kCaptureChannelMode = "captureChannelMode";
constexpr auto kCaptureBitDepth = "captureBitDepth";
constexpr auto kCaptureInputGain = "captureInputGain";
constexpr auto kLoopStart = "loopStart";
constexpr auto kLoopEnd = "loopEnd";
constexpr auto kLoopCrossfadeSamples = "loopCrossfadeSamples";
constexpr auto kFadeInSamples = "fadeInSamples";
constexpr auto kFadeOutSamples = "fadeOutSamples";
constexpr auto kReversePlayback = "reversePlayback";
constexpr auto kCcMappings = "ccMappings";
constexpr auto kCcEntry = "cc";
constexpr auto kCcNumber = "ccNum";
constexpr auto kCcParam = "ccParam";

constexpr auto kParamFilterCutoff = "p_filterCutoff";
constexpr auto kParamFilterRes = "p_filterRes";
constexpr auto kParamFilterEnvAmt = "p_filterEnvAmt";
constexpr auto kParamFilterMode = "p_filterMode";
constexpr auto kParamFilterAttack = "p_filterAttack";
constexpr auto kParamFilterDecay = "p_filterDecay";
constexpr auto kParamFilterSustain = "p_filterSustain";
constexpr auto kParamFilterRelease = "p_filterRelease";
constexpr auto kParamFilterKeytrack = "p_filterKeytrack";
constexpr auto kParamFilterVel = "p_filterVel";
constexpr auto kParamFilterLfoRate = "p_filterLfoRate";
constexpr auto kParamFilterLfoRateKeytrack = "p_filterLfoRateKeytrack";
constexpr auto kParamFilterLfoAmount = "p_filterLfoAmount";
constexpr auto kParamFilterLfoAmountKeytrack = "p_filterLfoAmountKeytrack";
constexpr auto kParamFilterLfoStartPhase = "p_filterLfoStartPhase";
constexpr auto kParamFilterLfoStartPhaseRandom = "p_filterLfoStartPhaseRandom";
constexpr auto kParamFilterLfoFadeIn = "p_filterLfoFadeIn";
constexpr auto kParamFilterLfoShape = "p_filterLfoShape";
constexpr auto kParamFilterLfoRetrigger = "p_filterLfoRetrigger";
constexpr auto kParamFilterLfoTempoSync = "p_filterLfoTempoSync";
constexpr auto kParamFilterLfoRateKeytrackInTempoSync = "p_filterLfoRateKeySync";
constexpr auto kParamFilterLfoKeytrackLinear = "p_filterLfoKeytrackLinear";
constexpr auto kParamFilterLfoUnipolar = "p_filterLfoUnipolar";
constexpr auto kParamFilterLfoSyncDivision = "p_filterLfoSyncDivision";
constexpr auto kParamAmpAttack = "p_ampAttack";
constexpr auto kParamAmpDecay = "p_ampDecay";
constexpr auto kParamAmpSustain = "p_ampSustain";
constexpr auto kParamAmpRelease = "p_ampRelease";
constexpr auto kParamAmpLfoRate = "p_ampLfoRate";
constexpr auto kParamAmpLfoDepth = "p_ampLfoDepth";
constexpr auto kParamAmpLfoShape = "p_ampLfoShape";
constexpr auto kParamPlaybackMode = "p_playbackMode";
constexpr auto kParamMonoMode = "p_mono";
constexpr auto kParamLegatoMode = "p_legato";
constexpr auto kParamGlideSeconds = "p_glideSeconds";
constexpr auto kParamPolyphonyLimit = "p_polyphonyLimit";
constexpr auto kParamFadeIn = "p_fadeIn";
constexpr auto kParamFadeOut = "p_fadeOut";
constexpr auto kParamReversePlayback = "p_reverse";
constexpr auto kParamRootMidiNote = "p_rootMidiNote";
constexpr auto kParamTuneCoarse = "p_tuneCoarse";
constexpr auto kParamTuneFine = "p_tuneFine";
constexpr auto kParamPitchBendRange = "p_pitchBendRange";
constexpr auto kParamPitchLfoRate = "p_pitchLfoRate";
constexpr auto kParamPitchLfoDepth = "p_pitchLfoDepth";
constexpr auto kParamPlaybackStart = "p_playbackStart";
constexpr auto kParamPlaybackEnd = "p_playbackEnd";
constexpr auto kParamLoopStart = "p_loopStart";
constexpr auto kParamLoopEnd = "p_loopEnd";
constexpr auto kParamLoopCrossfade = "p_loopCrossfade";
constexpr auto kParamVelocityCurve = "p_velocityCurve";
constexpr auto kParamQualityTier = "p_qualityTier";
constexpr auto kParamReverbMix = "p_reverbMix";
constexpr auto kParamDelayTime = "p_delayTime";
constexpr auto kParamDelayFeedback = "p_delayFeedback";
constexpr auto kParamDelayMix = "p_delayMix";
constexpr auto kParamDelayTempoSync = "p_delayTempoSync";
constexpr auto kParamDcFilterEnabled = "p_dcFilterEnabled";
constexpr auto kParamDcFilterCutoff = "p_dcFilterCutoff";
constexpr auto kParamAutopanRate = "p_autopanRate";
constexpr auto kParamAutopanDepth = "p_autopanDepth";
constexpr auto kParamSaturationDrive = "p_saturationDrive";
constexpr auto kParamSaturationMode = "p_saturationMode";
constexpr auto kParamPan = "p_pan";
constexpr auto kParamMasterVolume = "p_masterVolume";
constexpr float kMaxSamplePositionParam = 16000000.0f;

bool isPlaybackPresetExcludedProperty(const juce::Identifier& property)
{
    const auto propertyName = property.toString();
    return propertyName == kSampleBrowserRootFolder
        || propertyName == kWaveformViewStartSample
        || propertyName == kWaveformViewSampleCount
        || propertyName == kEditorTabIndex
        || propertyName == kWaveformDisplayMode
        || propertyName == kGenerateWaveType
        || propertyName == kGenerateSampleCount
        || propertyName == kGenerateBitDepth
        || propertyName == kGeneratePulseWidth
        || propertyName == kGenerateFrequencyMidiNote
        || propertyName == kGenerateSketchSmoothing
        || propertyName == kCaptureTargetSampleRate
        || propertyName == kCaptureChannelMode
        || propertyName == kCaptureBitDepth
        || propertyName == kCaptureInputGain;
}

juce::ValueTree buildPlaybackPresetStateTree(const juce::ValueTree& fullState)
{
    auto presetState = fullState.createCopy();

    for (int propertyIndex = presetState.getNumProperties() - 1; propertyIndex >= 0; --propertyIndex)
    {
        const auto propertyName = presetState.getPropertyName(propertyIndex);
        if (isPlaybackPresetExcludedProperty(propertyName))
            presetState.removeProperty(propertyName, nullptr);
    }

    return presetState;
}
}

AudiocityAudioProcessor::AudiocityAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts_(*this, nullptr, "AutomatableParams", createParameterLayout())
{
    clearVoicePlaybackPositions();
    playerPadAssignments_ = audiocity::plugin::defaultPlayerPadAssignments();

    setFilterSettings(engine_.getFilterSettings());
    setAmpEnvelope(engine_.getAmpEnvelope());
    setAmpLfoSettings(engine_.getAmpLfoSettings());
    setFilterEnvelope(engine_.getFilterEnvelope());
    setPlaybackMode(engine_.getPlaybackMode());
    setMonoMode(engine_.getMonoMode());
    setLegatoMode(engine_.getLegatoMode());
    setGlideSeconds(engine_.getGlideSeconds());
    setPolyphonyLimit(engine_.getPolyphonyLimit());
    setFadeSamples(engine_.getFadeInSamples(), engine_.getFadeOutSamples());
    setReversePlayback(engine_.getReversePlayback());
    setRootMidiNote(engine_.getRootMidiNote());
    setCoarseTuneSemitones(engine_.getCoarseTuneSemitones());
    setFineTuneCents(engine_.getFineTuneCents());
    setPitchBendRangeSemitones(engine_.getPitchBendRangeSemitones());
    setPitchLfoSettings(engine_.getPitchLfoSettings());
    setSampleWindow(engine_.getSampleWindowStart(), engine_.getSampleWindowEnd());
    setLoopPoints(engine_.getLoopStart(), engine_.getLoopEnd());
    setLoopCrossfadeSamples(engine_.getLoopCrossfadeSamples());
    setQualityTier(engine_.getQualityTier());
    setVelocityCurve(engine_.getVelocityCurve());
    setReverbMix(engine_.getReverbMix());
    setDelaySettings(engine_.getDelaySettings());
    setDcFilterSettings(engine_.getDcFilterSettings());
    setAutopanSettings(engine_.getAutopanSettings());
    setSaturationSettings(engine_.getSaturationSettings());
    setPan(engine_.getPan());
    setMasterVolume(engine_.getMasterVolume());
}

juce::AudioProcessorValueTreeState::ParameterLayout AudiocityAudioProcessor::createParameterLayout()
{
    using Mode = AudiocityAudioProcessor::FilterSettings::Mode;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterCutoff, "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.01f, 0.35f), 1200.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterRes, "Filter Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterEnvAmt, "Filter Env Amount",
        juce::NormalisableRange<float>(0.0f, 20000.0f, 0.01f, 0.35f), 2400.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamFilterMode, "Filter Mode",
        juce::StringArray{ "LP12", "LP24", "HP12", "HP24", "BP12", "Notch" }, static_cast<int>(Mode::lowPass12)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterAttack, "Filter Attack",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.001f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterDecay, "Filter Decay",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.120f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterSustain, "Filter Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterRelease, "Filter Release",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.100f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterKeytrack, "Filter Key Tracking",
        juce::NormalisableRange<float>(-1.0f, 2.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterVel, "Filter Velocity Amount",
        juce::NormalisableRange<float>(0.0f, 12000.0f, 0.01f, 0.5f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterLfoRate, "Filter LFO Rate",
        juce::NormalisableRange<float>(0.0f, 40.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterLfoRateKeytrack, "Filter LFO Rate Keytracking",
        juce::NormalisableRange<float>(-1.0f, 2.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterLfoAmount, "Filter LFO Amount",
        juce::NormalisableRange<float>(-20000.0f, 20000.0f, 0.01f, 0.35f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterLfoAmountKeytrack, "Filter LFO Amount Keytracking",
        juce::NormalisableRange<float>(-1.0f, 2.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterLfoStartPhase, "Filter LFO Start Phase",
        juce::NormalisableRange<float>(0.0f, 360.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterLfoStartPhaseRandom, "Filter LFO Start Rand",
        juce::NormalisableRange<float>(0.0f, 180.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterLfoFadeIn, "Filter LFO Fade In",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamFilterLfoShape, "Filter LFO Shape",
        juce::StringArray{ "Sine", "Triangle", "Square", "Saw Up", "Saw Down" },
        static_cast<int>(FilterSettings::LfoShape::sine)));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamFilterLfoRetrigger, "Filter LFO Retrigger", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamFilterLfoTempoSync, "Filter LFO Tempo Sync", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamFilterLfoRateKeytrackInTempoSync,
        "Filter LFO Rate Keytrack In Sync", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamFilterLfoKeytrackLinear,
        "Filter LFO Keytrack Linear", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamFilterLfoUnipolar,
        "Filter LFO Unipolar", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamFilterLfoSyncDivision, "Filter LFO Sync Division",
        juce::StringArray{ "1/16", "1/16T", "1/16.", "1/8", "1/8T", "1/8.",
            "1/4", "1/4T", "1/4.", "1/2", "1/1", "2/1" }, 6));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAmpAttack, "Amp Attack",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.005f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAmpDecay, "Amp Decay",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.150f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAmpSustain, "Amp Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.85f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAmpRelease, "Amp Release",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.150f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAmpLfoRate, "Amp LFO Rate",
        juce::NormalisableRange<float>(0.0f, 40.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAmpLfoDepth, "Amp LFO Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamAmpLfoShape, "Amp LFO Shape",
        juce::StringArray{ "Sine", "Triangle", "Square", "Saw Up", "Saw Down" },
        static_cast<int>(FilterSettings::LfoShape::sine)));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamPlaybackMode, "Playback Mode",
        juce::StringArray{ "Gate", "One-shot", "Loop" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamMonoMode, "Mono", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamLegatoMode, "Legato", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamGlideSeconds, "Glide Seconds",
        juce::NormalisableRange<float>(0.0f, 2.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamPolyphonyLimit, "Polyphony Limit",
        juce::NormalisableRange<float>(1.0f, static_cast<float>(audiocity::engine::VoicePool::maxVoices), 1.0f),
        static_cast<float>(audiocity::engine::VoicePool::maxVoices)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFadeIn, "Fade In",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFadeOut, "Fade Out",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamReversePlayback, "Reverse", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamRootMidiNote, "Root MIDI Note",
        juce::NormalisableRange<float>(0.0f, 127.0f, 1.0f), 60.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamTuneCoarse, "Tune Coarse",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamTuneFine, "Tune Fine",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamPitchBendRange, "Pitch Bend Range",
        juce::NormalisableRange<float>(0.0f, 24.0f, 1.0f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamPitchLfoRate, "Pitch LFO Rate",
        juce::NormalisableRange<float>(0.0f, 40.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamPitchLfoDepth, "Pitch LFO Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamPlaybackStart, "Playback Start",
        juce::NormalisableRange<float>(0.0f, kMaxSamplePositionParam, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamPlaybackEnd, "Playback End",
        juce::NormalisableRange<float>(0.0f, kMaxSamplePositionParam, 1.0f), kMaxSamplePositionParam));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamLoopStart, "Loop Start",
        juce::NormalisableRange<float>(0.0f, kMaxSamplePositionParam, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamLoopEnd, "Loop End",
        juce::NormalisableRange<float>(0.0f, kMaxSamplePositionParam, 1.0f), kMaxSamplePositionParam));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamLoopCrossfade, "Loop Crossfade",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamVelocityCurve, "Velocity Curve",
        juce::StringArray{ "Linear", "Soft", "Hard" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamQualityTier, "Quality",
        juce::StringArray{ "CPU", "Fidelity", "Ultra" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamReverbMix, "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamDelayTime, "Delay Time",
        juce::NormalisableRange<float>(1.0f, 2000.0f, 1.0f), 320.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamDelayFeedback, "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f), 0.35f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamDelayMix, "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamDelayTempoSync, "Delay Tempo Sync", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamDcFilterEnabled, "DC Filter Enabled", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamDcFilterCutoff, "DC Filter Cutoff",
        juce::NormalisableRange<float>(5.0f, 20.0f), 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAutopanRate, "Autopan Rate",
        juce::NormalisableRange<float>(0.01f, 20.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAutopanDepth, "Autopan Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamSaturationDrive, "Saturation Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamSaturationMode, "Saturation Mode",
        juce::StringArray{ "Soft Clip", "Hard Clip", "Tape", "Tube" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamPan, "Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamMasterVolume, "Master Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    return { params.begin(), params.end() };
}

void AudiocityAudioProcessor::updateParameterFromPlainValue(const juce::String& parameterId, const float plainValue) noexcept
{
    if (auto* parameter = apvts_.getParameter(parameterId))
    {
        const auto normalised = parameter->convertTo0to1(plainValue);
        parameter->setValueNotifyingHost(normalised);

        const auto current = suspendParamSyncBlocks_.load(std::memory_order_relaxed);
        if (current < 2)
            suspendParamSyncBlocks_.store(2, std::memory_order_relaxed);
    }
}

void AudiocityAudioProcessor::syncEngineFromAutomatableParameters() noexcept
{
    engine_.setHostTempoBpm(hostBpm_.load(std::memory_order_relaxed));

    auto amp = engine_.getAmpEnvelope();
    amp.attackSeconds = apvts_.getRawParameterValue(kParamAmpAttack)->load();
    amp.decaySeconds = apvts_.getRawParameterValue(kParamAmpDecay)->load();
    amp.sustainLevel = apvts_.getRawParameterValue(kParamAmpSustain)->load();
    amp.releaseSeconds = apvts_.getRawParameterValue(kParamAmpRelease)->load();
    engine_.setAmpEnvelope(amp);

    auto ampLfo = engine_.getAmpLfoSettings();
    ampLfo.rateHz = apvts_.getRawParameterValue(kParamAmpLfoRate)->load();
    ampLfo.depth = apvts_.getRawParameterValue(kParamAmpLfoDepth)->load();
    ampLfo.shape = static_cast<FilterSettings::LfoShape>(juce::jlimit(0, 4,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamAmpLfoShape)->load()))));
    engine_.setAmpLfoSettings(ampLfo);

    auto filter = engine_.getFilterSettings();
    filter.baseCutoffHz = apvts_.getRawParameterValue(kParamFilterCutoff)->load();
    filter.resonance = apvts_.getRawParameterValue(kParamFilterRes)->load();
    filter.envAmountHz = apvts_.getRawParameterValue(kParamFilterEnvAmt)->load();
    filter.mode = static_cast<FilterSettings::Mode>(juce::jlimit(0, 5,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamFilterMode)->load()))));
    filter.keyTracking = apvts_.getRawParameterValue(kParamFilterKeytrack)->load();
    filter.velocityAmountHz = apvts_.getRawParameterValue(kParamFilterVel)->load();
    filter.lfoRateHz = apvts_.getRawParameterValue(kParamFilterLfoRate)->load();
    filter.lfoRateKeyTracking = apvts_.getRawParameterValue(kParamFilterLfoRateKeytrack)->load();
    filter.lfoAmountHz = apvts_.getRawParameterValue(kParamFilterLfoAmount)->load();
    filter.lfoAmountKeyTracking = apvts_.getRawParameterValue(kParamFilterLfoAmountKeytrack)->load();
    filter.lfoStartPhaseDegrees = apvts_.getRawParameterValue(kParamFilterLfoStartPhase)->load();
    filter.lfoStartPhaseRandomDegrees = apvts_.getRawParameterValue(kParamFilterLfoStartPhaseRandom)->load();
    filter.lfoFadeInMs = apvts_.getRawParameterValue(kParamFilterLfoFadeIn)->load();
    filter.lfoShape = static_cast<FilterSettings::LfoShape>(juce::jlimit(0, 4,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamFilterLfoShape)->load()))));
    filter.lfoRetrigger = apvts_.getRawParameterValue(kParamFilterLfoRetrigger)->load() >= 0.5f;
    filter.lfoTempoSync = apvts_.getRawParameterValue(kParamFilterLfoTempoSync)->load() >= 0.5f;
    filter.lfoRateKeytrackInTempoSync = apvts_.getRawParameterValue(kParamFilterLfoRateKeytrackInTempoSync)->load() >= 0.5f;
    filter.lfoKeytrackLinear = apvts_.getRawParameterValue(kParamFilterLfoKeytrackLinear)->load() >= 0.5f;
    filter.lfoUnipolar = apvts_.getRawParameterValue(kParamFilterLfoUnipolar)->load() >= 0.5f;
    filter.lfoSyncDivision = juce::jlimit(0, 11,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamFilterLfoSyncDivision)->load())));
    if (filter.lfoTempoSync)
        filter.lfoRateHz = lfoRateHzFromTempoSync(filter.lfoSyncDivision);
    engine_.setFilterSettings(filter);

    auto filterEnv = engine_.getFilterEnvelope();
    filterEnv.attackSeconds = apvts_.getRawParameterValue(kParamFilterAttack)->load();
    filterEnv.decaySeconds = apvts_.getRawParameterValue(kParamFilterDecay)->load();
    filterEnv.sustainLevel = apvts_.getRawParameterValue(kParamFilterSustain)->load();
    filterEnv.releaseSeconds = apvts_.getRawParameterValue(kParamFilterRelease)->load();
    engine_.setFilterEnvelope(filterEnv);

    engine_.setPlaybackMode(static_cast<PlaybackMode>(juce::jlimit(0, 2,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamPlaybackMode)->load())))));
    engine_.setMonoMode(apvts_.getRawParameterValue(kParamMonoMode)->load() >= 0.5f);
    engine_.setLegatoMode(apvts_.getRawParameterValue(kParamLegatoMode)->load() >= 0.5f);
    engine_.setGlideSeconds(apvts_.getRawParameterValue(kParamGlideSeconds)->load());
    engine_.setPolyphonyLimit(static_cast<int>(std::round(apvts_.getRawParameterValue(kParamPolyphonyLimit)->load())));
    engine_.setFadeSamples(
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamFadeIn)->load())),
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamFadeOut)->load())));
    engine_.setReversePlayback(apvts_.getRawParameterValue(kParamReversePlayback)->load() >= 0.5f);
    engine_.setRootMidiNote(static_cast<int>(std::round(apvts_.getRawParameterValue(kParamRootMidiNote)->load())));
    engine_.setCoarseTuneSemitones(apvts_.getRawParameterValue(kParamTuneCoarse)->load());
    engine_.setFineTuneCents(apvts_.getRawParameterValue(kParamTuneFine)->load());
    engine_.setPitchBendRangeSemitones(apvts_.getRawParameterValue(kParamPitchBendRange)->load());
    auto pitchLfo = engine_.getPitchLfoSettings();
    pitchLfo.rateHz = apvts_.getRawParameterValue(kParamPitchLfoRate)->load();
    pitchLfo.depthCents = apvts_.getRawParameterValue(kParamPitchLfoDepth)->load();
    engine_.setPitchLfoSettings(pitchLfo);
    engine_.setSampleWindow(
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamPlaybackStart)->load())),
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamPlaybackEnd)->load())));
    engine_.setLoopPoints(
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamLoopStart)->load())),
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamLoopEnd)->load())));
    engine_.setLoopCrossfadeSamples(
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamLoopCrossfade)->load())));

    engine_.setVelocityCurve(static_cast<VelocityCurve>(juce::jlimit(0, 2,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamVelocityCurve)->load())))));
    engine_.setQualityTier(static_cast<QualityTier>(juce::jlimit(0, 2,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamQualityTier)->load())))));
    engine_.setReverbMix(apvts_.getRawParameterValue(kParamReverbMix)->load());

    DelaySettings delay;
    delay.timeMs = apvts_.getRawParameterValue(kParamDelayTime)->load();
    delay.feedback = apvts_.getRawParameterValue(kParamDelayFeedback)->load();
    delay.mix = apvts_.getRawParameterValue(kParamDelayMix)->load();
    delay.tempoSync = apvts_.getRawParameterValue(kParamDelayTempoSync)->load() >= 0.5f;
    engine_.setDelaySettings(delay);

    DcFilterSettings dcFilter;
    dcFilter.enabled = apvts_.getRawParameterValue(kParamDcFilterEnabled)->load() >= 0.5f;
    dcFilter.cutoffHz = apvts_.getRawParameterValue(kParamDcFilterCutoff)->load();
    engine_.setDcFilterSettings(dcFilter);

    AutopanSettings autopan;
    autopan.rateHz = apvts_.getRawParameterValue(kParamAutopanRate)->load();
    autopan.depth = apvts_.getRawParameterValue(kParamAutopanDepth)->load();
    engine_.setAutopanSettings(autopan);

    SaturationSettings saturation;
    saturation.drive = apvts_.getRawParameterValue(kParamSaturationDrive)->load();
    saturation.mode = static_cast<SaturationSettings::Mode>(juce::jlimit(0, 3,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamSaturationMode)->load()))));
    engine_.setSaturationSettings(saturation);

    engine_.setPan(apvts_.getRawParameterValue(kParamPan)->load());
    engine_.setMasterVolume(apvts_.getRawParameterValue(kParamMasterVolume)->load());
}

void AudiocityAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    engine_.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    captureInputSampleRate_.store(juce::jmax(1.0, sampleRate), std::memory_order_relaxed);
    outputBoundaryLastSample_.fill(0.0f);
    outputBoundaryHasLastSample_ = false;
}

void AudiocityAudioProcessor::releaseResources()
{
    engine_.release();
    outputBoundaryLastSample_.fill(0.0f);
    outputBoundaryHasLastSample_ = false;
}

bool AudiocityAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    const auto outputSupported = mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
    if (!outputSupported)
        return false;

    if (mainIn.isDisabled())
        return true;

    const auto inputSupported = mainIn == juce::AudioChannelSet::mono() || mainIn == juce::AudioChannelSet::stereo();
    return inputSupported && (mainIn == mainOut);
}

void AudiocityAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputChannels = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();
    const auto captureRecording = captureRecording_.load(std::memory_order_acquire);
    const auto monitorCaptureInput = captureRecording
        || getEditorTabIndex() == 4;

    for (auto channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    if (monitorCaptureInput && numInputChannels > 0)
        updateCaptureInputMonitorLevels(buffer, numInputChannels);

    bool capturedThisBlock = false;
    if (captureRecording && numInputChannels > 0)
        capturedThisBlock = captureInputAudio(buffer, numInputChannels);

    updateHostTempoFromPlayHead();
    const auto suspendedBlocks = suspendParamSyncBlocks_.load(std::memory_order_relaxed);
    if (suspendedBlocks > 0)
        suspendParamSyncBlocks_.store(suspendedBlocks - 1, std::memory_order_relaxed);
    else
        syncEngineFromAutomatableParameters();

    // Extract CC messages and push to FIFO for the editor to consume
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isController())
            pushCcEvent(msg.getControllerNumber(), msg.getControllerValue());
    }

    if (previewWavePlaying_.load(std::memory_order_relaxed)
        && previewWaveSamples_.load(std::memory_order_relaxed) > 0)
    {
        renderGeneratedWavePreview(buffer);
        applyOutputBoundarySmoothing(buffer);
        if (monitorCaptureInput && numInputChannels <= 0 && numOutputChannels > 0)
            updateCaptureInputMonitorLevels(buffer, numOutputChannels);
        if (captureRecording && !capturedThisBlock)
            capturedThisBlock = captureInputAudio(buffer, numOutputChannels);
        clearVoicePlaybackPositions();
        updateOutputPeakLevels(buffer);
        return;
    }

    if (samplePreviewPlaying_.load(std::memory_order_relaxed)
        && samplePreviewSamples_.load(std::memory_order_relaxed) > 0)
    {
        renderSampleFilePreview(buffer);
        applyOutputBoundarySmoothing(buffer);
        if (monitorCaptureInput && numInputChannels <= 0 && numOutputChannels > 0)
            updateCaptureInputMonitorLevels(buffer, numOutputChannels);
        if (captureRecording && !capturedThisBlock)
            capturedThisBlock = captureInputAudio(buffer, numOutputChannels);
        clearVoicePlaybackPositions();
        updateOutputPeakLevels(buffer);
        return;
    }

    UiMidiEvent uiEvent{};
    while (popUiMidiEvent(uiEvent))
    {
        if (uiEvent.isNoteOn)
            engine_.noteOn(uiEvent.noteNumber, static_cast<float>(uiEvent.velocity) / 127.0f, 0);
        else
            engine_.noteOff(uiEvent.noteNumber, 0);
    }

    engine_.render(buffer, midiMessages);
    applyOutputBoundarySmoothing(buffer);
    if (monitorCaptureInput && numInputChannels <= 0 && numOutputChannels > 0)
        updateCaptureInputMonitorLevels(buffer, numOutputChannels);
    if (captureRecording && !capturedThisBlock)
        capturedThisBlock = captureInputAudio(buffer, numOutputChannels);
    updateVoicePlaybackPositionsFromEngine();
    updateOutputPeakLevels(buffer);
}

void AudiocityAudioProcessor::setQualityTier(const QualityTier tier) noexcept
{
    engine_.setQualityTier(tier);
    updateParameterFromPlainValue(kParamQualityTier, static_cast<float>(tier));
}

void AudiocityAudioProcessor::setVelocityCurve(const VelocityCurve curve) noexcept
{
    engine_.setVelocityCurve(curve);
    updateParameterFromPlainValue(kParamVelocityCurve, static_cast<float>(curve));
}

void AudiocityAudioProcessor::setReverbMix(const float mix) noexcept
{
    engine_.setReverbMix(mix);
    updateParameterFromPlainValue(kParamReverbMix, mix);
}

void AudiocityAudioProcessor::setDelaySettings(const DelaySettings& settings) noexcept
{
    engine_.setDelaySettings(settings);
    const auto applied = engine_.getDelaySettings();
    updateParameterFromPlainValue(kParamDelayTime, applied.timeMs);
    updateParameterFromPlainValue(kParamDelayFeedback, applied.feedback);
    updateParameterFromPlainValue(kParamDelayMix, applied.mix);
    updateParameterFromPlainValue(kParamDelayTempoSync, applied.tempoSync ? 1.0f : 0.0f);
}

void AudiocityAudioProcessor::setDcFilterSettings(const DcFilterSettings& settings) noexcept
{
    engine_.setDcFilterSettings(settings);
    const auto applied = engine_.getDcFilterSettings();
    updateParameterFromPlainValue(kParamDcFilterEnabled, applied.enabled ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamDcFilterCutoff, applied.cutoffHz);
}

void AudiocityAudioProcessor::setAutopanSettings(const AutopanSettings& settings) noexcept
{
    engine_.setAutopanSettings(settings);
    const auto applied = engine_.getAutopanSettings();
    updateParameterFromPlainValue(kParamAutopanRate, applied.rateHz);
    updateParameterFromPlainValue(kParamAutopanDepth, applied.depth);
}

void AudiocityAudioProcessor::setSaturationSettings(const SaturationSettings& settings) noexcept
{
    engine_.setSaturationSettings(settings);
    const auto applied = engine_.getSaturationSettings();
    updateParameterFromPlainValue(kParamSaturationDrive, applied.drive);
    updateParameterFromPlainValue(kParamSaturationMode, static_cast<float>(applied.mode));
}

void AudiocityAudioProcessor::setPan(const float pan) noexcept
{
    engine_.setPan(pan);
    updateParameterFromPlainValue(kParamPan, engine_.getPan());
}

void AudiocityAudioProcessor::setMasterVolume(const float volume) noexcept
{
    engine_.setMasterVolume(volume);
    updateParameterFromPlainValue(kParamMasterVolume, engine_.getMasterVolume());
}

AudiocityAudioProcessor::OutputPeakLevels AudiocityAudioProcessor::consumeOutputPeakLevels() noexcept
{
    OutputPeakLevels levels;
    levels.left = outputPeakLeft_.exchange(0.0f, std::memory_order_acq_rel);
    levels.right = outputPeakRight_.exchange(0.0f, std::memory_order_acq_rel);
    return levels;
}

AudiocityAudioProcessor::VoicePlaybackPositions AudiocityAudioProcessor::getVoicePlaybackPositions() const noexcept
{
    VoicePlaybackPositions positions{};
    for (std::size_t index = 0; index < positions.size(); ++index)
        positions[index] = voicePlaybackPositions_[index].load(std::memory_order_relaxed);

    return positions;
}

void AudiocityAudioProcessor::setFilterEnvelope(const AdsrSettings& settings) noexcept
{
    engine_.setFilterEnvelope(settings);
    updateParameterFromPlainValue(kParamFilterAttack, settings.attackSeconds);
    updateParameterFromPlainValue(kParamFilterDecay, settings.decaySeconds);
    updateParameterFromPlainValue(kParamFilterSustain, settings.sustainLevel);
    updateParameterFromPlainValue(kParamFilterRelease, settings.releaseSeconds);
}

void AudiocityAudioProcessor::setFilterSettings(const FilterSettings& settings) noexcept
{
    engine_.setFilterSettings(settings);

    const auto applied = engine_.getFilterSettings();
    updateParameterFromPlainValue(kParamFilterCutoff, applied.baseCutoffHz);
    updateParameterFromPlainValue(kParamFilterRes, applied.resonance);
    updateParameterFromPlainValue(kParamFilterEnvAmt, applied.envAmountHz);
    updateParameterFromPlainValue(kParamFilterMode, static_cast<float>(applied.mode));
    updateParameterFromPlainValue(kParamFilterKeytrack, applied.keyTracking);
    updateParameterFromPlainValue(kParamFilterVel, applied.velocityAmountHz);
    updateParameterFromPlainValue(kParamFilterLfoRate, applied.lfoRateHz);
    updateParameterFromPlainValue(kParamFilterLfoRateKeytrack, applied.lfoRateKeyTracking);
    updateParameterFromPlainValue(kParamFilterLfoAmount, applied.lfoAmountHz);
    updateParameterFromPlainValue(kParamFilterLfoAmountKeytrack, applied.lfoAmountKeyTracking);
    updateParameterFromPlainValue(kParamFilterLfoStartPhase, applied.lfoStartPhaseDegrees);
    updateParameterFromPlainValue(kParamFilterLfoStartPhaseRandom, applied.lfoStartPhaseRandomDegrees);
    updateParameterFromPlainValue(kParamFilterLfoFadeIn, applied.lfoFadeInMs);
    updateParameterFromPlainValue(kParamFilterLfoShape, static_cast<float>(applied.lfoShape));
    updateParameterFromPlainValue(kParamFilterLfoRetrigger, applied.lfoRetrigger ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamFilterLfoTempoSync, applied.lfoTempoSync ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamFilterLfoRateKeytrackInTempoSync, applied.lfoRateKeytrackInTempoSync ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamFilterLfoKeytrackLinear, applied.lfoKeytrackLinear ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamFilterLfoUnipolar, applied.lfoUnipolar ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamFilterLfoSyncDivision, static_cast<float>(applied.lfoSyncDivision));
}

juce::AudioProcessorEditor* AudiocityAudioProcessor::createEditor()
{
    return new AudiocityAudioProcessorEditor(*this);
}

void AudiocityAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = juce::ValueTree(kPatchRoot);

    state.setProperty(kSamplePath, engine_.getSamplePath(), nullptr);
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        if (generatedWaveformLoaded_.load(std::memory_order_relaxed) && !generatedWaveformState_.empty())
        {
            juce::MemoryBlock generatedBytes(generatedWaveformState_.size() * sizeof(float));
            std::memcpy(generatedBytes.getData(), generatedWaveformState_.data(), generatedBytes.getSize());
            state.setProperty(kGeneratedWaveformData, juce::var(generatedBytes), nullptr);
        }

        if (capturedAudioLoaded_.load(std::memory_order_relaxed) && !capturedSampleState_.empty())
        {
            juce::MemoryBlock capturedBytes(capturedSampleState_.size() * sizeof(float));
            std::memcpy(capturedBytes.getData(), capturedSampleState_.data(), capturedBytes.getSize());
            state.setProperty(kCapturedSampleData, juce::var(capturedBytes), nullptr);
            state.setProperty(kCapturedSampleRate, capturedSampleRateState_, nullptr);
        }
    }
    state.setProperty(kSampleBrowserRootFolder, sampleBrowserRootFolderPath_, nullptr);
    state.setProperty(kRootMidiNote, engine_.getRootMidiNote(), nullptr);
    state.setProperty(kCoarseTuneSemitones, engine_.getCoarseTuneSemitones(), nullptr);
    state.setProperty(kFineTuneCents, engine_.getFineTuneCents(), nullptr);
    state.setProperty(kPitchBendRangeSemitones, engine_.getPitchBendRangeSemitones(), nullptr);
    const auto pitchLfo = engine_.getPitchLfoSettings();
    state.setProperty(kPitchLfoRate, pitchLfo.rateHz, nullptr);
    state.setProperty(kPitchLfoDepth, pitchLfo.depthCents, nullptr);

    const auto amp = engine_.getAmpEnvelope();
    state.setProperty(kAmpAttack, amp.attackSeconds, nullptr);
    state.setProperty(kAmpDecay, amp.decaySeconds, nullptr);
    state.setProperty(kAmpSustain, amp.sustainLevel, nullptr);
    state.setProperty(kAmpRelease, amp.releaseSeconds, nullptr);
    const auto ampLfo = engine_.getAmpLfoSettings();
    state.setProperty(kAmpLfoRate, ampLfo.rateHz, nullptr);
    state.setProperty(kAmpLfoDepth, ampLfo.depth, nullptr);
    state.setProperty(kAmpLfoShape, static_cast<int>(ampLfo.shape), nullptr);

    const auto filterAdsr = engine_.getFilterEnvelope();
    state.setProperty(kFilterAttack, filterAdsr.attackSeconds, nullptr);
    state.setProperty(kFilterDecay, filterAdsr.decaySeconds, nullptr);
    state.setProperty(kFilterSustain, filterAdsr.sustainLevel, nullptr);
    state.setProperty(kFilterRelease, filterAdsr.releaseSeconds, nullptr);

    const auto filter = engine_.getFilterSettings();
    state.setProperty(kFilterBaseCutoff, filter.baseCutoffHz, nullptr);
    state.setProperty(kFilterEnvAmount, filter.envAmountHz, nullptr);
    state.setProperty(kFilterResonance, filter.resonance, nullptr);
    state.setProperty(kFilterMode, static_cast<int>(filter.mode), nullptr);
    state.setProperty(kFilterKeyTracking, filter.keyTracking, nullptr);
    state.setProperty(kFilterVelocityAmount, filter.velocityAmountHz, nullptr);
    state.setProperty(kFilterLfoRate, filter.lfoRateHz, nullptr);
    state.setProperty(kFilterLfoRateKeytrack, filter.lfoRateKeyTracking, nullptr);
    state.setProperty(kFilterLfoAmount, filter.lfoAmountHz, nullptr);
    state.setProperty(kFilterLfoAmountKeytrack, filter.lfoAmountKeyTracking, nullptr);
    state.setProperty(kFilterLfoStartPhase, filter.lfoStartPhaseDegrees, nullptr);
    state.setProperty(kFilterLfoStartPhaseRandom, filter.lfoStartPhaseRandomDegrees, nullptr);
    state.setProperty(kFilterLfoFadeIn, filter.lfoFadeInMs, nullptr);
    state.setProperty(kFilterLfoShape, static_cast<int>(filter.lfoShape), nullptr);
    state.setProperty(kFilterLfoRetrigger, filter.lfoRetrigger ? 1 : 0, nullptr);
    state.setProperty(kFilterLfoTempoSync, filter.lfoTempoSync ? 1 : 0, nullptr);
    state.setProperty(kFilterLfoRateKeytrackInTempoSync, filter.lfoRateKeytrackInTempoSync ? 1 : 0, nullptr);
    state.setProperty(kFilterLfoKeytrackLinear, filter.lfoKeytrackLinear ? 1 : 0, nullptr);
    state.setProperty(kFilterLfoUnipolar, filter.lfoUnipolar ? 1 : 0, nullptr);
    state.setProperty(kFilterLfoSyncDivision, filter.lfoSyncDivision, nullptr);
    state.setProperty(kPlaybackMode,
        getPlaybackMode() == PlaybackMode::oneShot ? 1 : (getPlaybackMode() == PlaybackMode::loop ? 2 : 0),
        nullptr);
    int qualityTierIndex = 1;
    if (getQualityTier() == QualityTier::cpu)
        qualityTierIndex = 0;
    else if (getQualityTier() == QualityTier::ultra)
        qualityTierIndex = 2;

    state.setProperty(kQualityTier, qualityTierIndex, nullptr);
    state.setProperty(kVelocityCurve, static_cast<int>(getVelocityCurve()), nullptr);
    state.setProperty(kReverbMix, getReverbMix(), nullptr);
    const auto delay = getDelaySettings();
    state.setProperty(kDelayTimeMs, delay.timeMs, nullptr);
    state.setProperty(kDelayFeedback, delay.feedback, nullptr);
    state.setProperty(kDelayMix, delay.mix, nullptr);
    state.setProperty(kDelayTempoSync, delay.tempoSync ? 1 : 0, nullptr);
    const auto dcFilter = getDcFilterSettings();
    state.setProperty(kDcFilterEnabled, dcFilter.enabled ? 1 : 0, nullptr);
    state.setProperty(kDcFilterCutoffHz, dcFilter.cutoffHz, nullptr);
    const auto autopan = getAutopanSettings();
    state.setProperty(kAutopanRateHz, autopan.rateHz, nullptr);
    state.setProperty(kAutopanDepth, autopan.depth, nullptr);
    const auto saturation = getSaturationSettings();
    state.setProperty(kSaturationDrive, saturation.drive, nullptr);
    state.setProperty(kSaturationMode, static_cast<int>(saturation.mode), nullptr);
    state.setProperty(kPan, getPan(), nullptr);
    state.setProperty(kMasterVolume, getMasterVolume(), nullptr);
    state.setProperty(kPreloadSamples, getPreloadSamples(), nullptr);
    state.setProperty(kMonoMode, getMonoMode() ? 1 : 0, nullptr);
    state.setProperty(kLegatoMode, getLegatoMode() ? 1 : 0, nullptr);
    state.setProperty(kGlideSeconds, getGlideSeconds(), nullptr);
    state.setProperty(kPolyphonyLimit, getPolyphonyLimit(), nullptr);
    state.setProperty(kSampleWindowStart, getSampleWindowStart(), nullptr);
    state.setProperty(kSampleWindowEnd, getSampleWindowEnd(), nullptr);
    state.setProperty(kWaveformViewStartSample, waveformViewStartSample_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kWaveformViewSampleCount, waveformViewSampleCount_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kEditorTabIndex, editorTabIndex_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kWaveformDisplayMode, waveformDisplayMode_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kGenerateWaveType, generateWaveType_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kGenerateSampleCount, generateSampleCount_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kGenerateBitDepth, generateBitDepth_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kGeneratePulseWidth, generatePulseWidth_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kGenerateFrequencyMidiNote, generateFrequencyMidiNote_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kGenerateSketchSmoothing, generateSketchSmoothing_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kCaptureTargetSampleRate, captureTargetSampleRate_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kCaptureChannelMode, captureChannelMode_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kCaptureBitDepth, captureBitDepth_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kCaptureInputGain, captureInputGain_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(kLoopStart, getLoopStart(), nullptr);
    state.setProperty(kLoopEnd, getLoopEnd(), nullptr);
    state.setProperty(kLoopCrossfadeSamples, getLoopCrossfadeSamples(), nullptr);
    state.setProperty(kFadeInSamples, getFadeInSamples(), nullptr);
    state.setProperty(kFadeOutSamples, getFadeOutSamples(), nullptr);
    state.setProperty(kReversePlayback, getReversePlayback() ? 1 : 0, nullptr);

    {
        auto padsNode = juce::ValueTree(audiocity::plugin::kPlayerPads);
        for (int i = 0; i < kPlayerPadCount; ++i)
        {
            const auto assignment = audiocity::plugin::sanitizePlayerPadAssignment(
                playerPadAssignments_[static_cast<std::size_t>(i)]);

            auto entry = juce::ValueTree(audiocity::plugin::kPlayerPad);
            entry.setProperty(audiocity::plugin::kPlayerPadIndex, i, nullptr);
            entry.setProperty(audiocity::plugin::kPlayerPadNote, assignment.noteNumber, nullptr);
            entry.setProperty(audiocity::plugin::kPlayerPadVelocity, assignment.velocity, nullptr);
            padsNode.appendChild(entry, nullptr);
        }
        state.appendChild(padsNode, nullptr);
    }

    // Save CC mappings
    {
        auto mappingsNode = juce::ValueTree(kCcMappings);
        const auto mappings = getAllCcMappings();
        for (const auto& [ccNum, paramId] : mappings)
        {
            auto entry = juce::ValueTree(kCcEntry);
            entry.setProperty(kCcNumber, ccNum, nullptr);
            entry.setProperty(kCcParam, paramId, nullptr);
            mappingsNode.appendChild(entry, nullptr);
        }
        state.appendChild(mappingsNode, nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void AudiocityAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid() || !state.hasType(kPatchRoot))
        return;

    const auto samplePath = state.getProperty(kSamplePath).toString();
    const auto storedRootMidiNote = static_cast<int>(state.getProperty(kRootMidiNote, engine_.getRootMidiNote()));

    bool restoredSample = false;
    int restoredSampleSource = 0;
    if (samplePath.isNotEmpty())
    {
        restoredSample = loadSampleFromFile(juce::File(samplePath));
        if (restoredSample)
            restoredSampleSource = 1;
    }

    if (!restoredSample)
    {
        if (const auto* generatedData = state.getProperty(kGeneratedWaveformData).getBinaryData(); generatedData != nullptr)
        {
            const auto totalBytes = generatedData->getSize();
            if (totalBytes >= sizeof(float) && (totalBytes % sizeof(float)) == 0)
            {
                const auto sampleCount = static_cast<std::size_t>(totalBytes / sizeof(float));
                std::vector<float> waveform(sampleCount, 0.0f);
                std::memcpy(waveform.data(), generatedData->getData(), totalBytes);
                loadGeneratedWaveformAsSample(waveform, storedRootMidiNote);
                restoredSample = true;
                restoredSampleSource = 2;
            }
        }
    }

    if (!restoredSample)
    {
        if (const auto* capturedData = state.getProperty(kCapturedSampleData).getBinaryData(); capturedData != nullptr)
        {
            const auto totalBytes = capturedData->getSize();
            if (totalBytes >= sizeof(float) && (totalBytes % sizeof(float)) == 0)
            {
                const auto sampleCount = static_cast<int>(totalBytes / sizeof(float));
                const auto storedSampleRate = static_cast<double>(state.getProperty(kCapturedSampleRate, 44100.0));
                const auto restoredSampleRate = juce::jmax(1.0, storedSampleRate);

                juce::AudioBuffer<float> restoredBuffer(1, sampleCount);
                std::memcpy(restoredBuffer.getWritePointer(0), capturedData->getData(), totalBytes);

                engine_.setSampleData(restoredBuffer, restoredSampleRate, storedRootMidiNote);
                engine_.setRootMidiNote(storedRootMidiNote);
                engine_.clearSamplePath();

                generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
                capturedAudioLoaded_.store(true, std::memory_order_relaxed);
                {
                    std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
                    generatedWaveformState_.clear();
                    capturedSampleState_.resize(static_cast<std::size_t>(sampleCount));
                    std::memcpy(capturedSampleState_.data(), capturedData->getData(), totalBytes);
                    capturedSampleRateState_ = restoredSampleRate;
                }

                suspendParamSyncBlocks_.store(8, std::memory_order_relaxed);
                syncSampleDerivedParametersFromEngine();
                restoredSample = true;
                restoredSampleSource = 3;
            }
        }
    }

    if (!restoredSample)
    {
        generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
        capturedAudioLoaded_.store(false, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_.clear();
        capturedSampleState_.clear();
        capturedSampleRateState_ = 44100.0;
    }

    lastStateRestoreSource_.store(restoredSampleSource, std::memory_order_relaxed);

    sampleBrowserRootFolderPath_ = state.getProperty(kSampleBrowserRootFolder, {}).toString();

    setRootMidiNote(static_cast<int>(state.getProperty(kRootMidiNote, engine_.getRootMidiNote())));
    setCoarseTuneSemitones(static_cast<float>(state.getProperty(kCoarseTuneSemitones, engine_.getCoarseTuneSemitones())));
    setFineTuneCents(static_cast<float>(state.getProperty(kFineTuneCents, engine_.getFineTuneCents())));
    setPitchBendRangeSemitones(static_cast<float>(state.getProperty(kPitchBendRangeSemitones, engine_.getPitchBendRangeSemitones())));
    auto pitchLfo = engine_.getPitchLfoSettings();
    pitchLfo.rateHz = static_cast<float>(state.getProperty(kPitchLfoRate, pitchLfo.rateHz));
    pitchLfo.depthCents = static_cast<float>(state.getProperty(kPitchLfoDepth, pitchLfo.depthCents));
    setPitchLfoSettings(pitchLfo);

    auto amp = engine_.getAmpEnvelope();
    amp.attackSeconds = static_cast<float>(state.getProperty(kAmpAttack, amp.attackSeconds));
    amp.decaySeconds = static_cast<float>(state.getProperty(kAmpDecay, amp.decaySeconds));
    amp.sustainLevel = static_cast<float>(state.getProperty(kAmpSustain, amp.sustainLevel));
    amp.releaseSeconds = static_cast<float>(state.getProperty(kAmpRelease, amp.releaseSeconds));
    setAmpEnvelope(amp);

    auto ampLfo = engine_.getAmpLfoSettings();
    ampLfo.rateHz = static_cast<float>(state.getProperty(kAmpLfoRate, ampLfo.rateHz));
    ampLfo.depth = static_cast<float>(state.getProperty(kAmpLfoDepth, ampLfo.depth));
    ampLfo.shape = static_cast<FilterSettings::LfoShape>(juce::jlimit(0, 4,
        static_cast<int>(state.getProperty(kAmpLfoShape, static_cast<int>(ampLfo.shape)))));
    setAmpLfoSettings(ampLfo);

    auto filterAdsr = engine_.getFilterEnvelope();
    filterAdsr.attackSeconds = static_cast<float>(state.getProperty(kFilterAttack, filterAdsr.attackSeconds));
    filterAdsr.decaySeconds = static_cast<float>(state.getProperty(kFilterDecay, filterAdsr.decaySeconds));
    filterAdsr.sustainLevel = static_cast<float>(state.getProperty(kFilterSustain, filterAdsr.sustainLevel));
    filterAdsr.releaseSeconds = static_cast<float>(state.getProperty(kFilterRelease, filterAdsr.releaseSeconds));
    setFilterEnvelope(filterAdsr);

    auto filter = engine_.getFilterSettings();
    filter.baseCutoffHz = static_cast<float>(state.getProperty(kFilterBaseCutoff, filter.baseCutoffHz));
    filter.envAmountHz = static_cast<float>(state.getProperty(kFilterEnvAmount, filter.envAmountHz));
    filter.resonance = static_cast<float>(state.getProperty(kFilterResonance, filter.resonance));
    filter.mode = static_cast<AudiocityAudioProcessor::FilterSettings::Mode>(
        static_cast<int>(state.getProperty(kFilterMode, static_cast<int>(filter.mode))));
    filter.keyTracking = static_cast<float>(state.getProperty(kFilterKeyTracking, filter.keyTracking));
    filter.velocityAmountHz = static_cast<float>(state.getProperty(kFilterVelocityAmount, filter.velocityAmountHz));
    filter.lfoRateHz = static_cast<float>(state.getProperty(kFilterLfoRate, filter.lfoRateHz));
    filter.lfoRateKeyTracking = static_cast<float>(state.getProperty(kFilterLfoRateKeytrack, filter.lfoRateKeyTracking));
    filter.lfoAmountHz = static_cast<float>(state.getProperty(kFilterLfoAmount, filter.lfoAmountHz));
    filter.lfoAmountKeyTracking = static_cast<float>(state.getProperty(kFilterLfoAmountKeytrack, filter.lfoAmountKeyTracking));
    filter.lfoStartPhaseDegrees = static_cast<float>(state.getProperty(kFilterLfoStartPhase, filter.lfoStartPhaseDegrees));
    filter.lfoStartPhaseRandomDegrees = static_cast<float>(state.getProperty(kFilterLfoStartPhaseRandom, filter.lfoStartPhaseRandomDegrees));
    filter.lfoFadeInMs = static_cast<float>(state.getProperty(kFilterLfoFadeIn, filter.lfoFadeInMs));
    filter.lfoShape = static_cast<FilterSettings::LfoShape>(juce::jlimit(0, 4,
        static_cast<int>(state.getProperty(kFilterLfoShape, static_cast<int>(filter.lfoShape)))));
    filter.lfoRetrigger = static_cast<int>(state.getProperty(kFilterLfoRetrigger, filter.lfoRetrigger ? 1 : 0)) == 1;
    filter.lfoTempoSync = static_cast<int>(state.getProperty(kFilterLfoTempoSync, filter.lfoTempoSync ? 1 : 0)) == 1;
    filter.lfoRateKeytrackInTempoSync = static_cast<int>(state.getProperty(kFilterLfoRateKeytrackInTempoSync,
        filter.lfoRateKeytrackInTempoSync ? 1 : 0)) == 1;
    filter.lfoKeytrackLinear = static_cast<int>(state.getProperty(kFilterLfoKeytrackLinear,
        filter.lfoKeytrackLinear ? 1 : 0)) == 1;
    filter.lfoUnipolar = static_cast<int>(state.getProperty(kFilterLfoUnipolar,
        filter.lfoUnipolar ? 1 : 0)) == 1;
    filter.lfoSyncDivision = juce::jlimit(0, 11,
        static_cast<int>(state.getProperty(kFilterLfoSyncDivision, filter.lfoSyncDivision)));
    setFilterSettings(filter);

    const auto playbackMode = static_cast<int>(state.getProperty(kPlaybackMode, 0));
    setPlaybackMode(playbackMode == 1 ? PlaybackMode::oneShot : (playbackMode == 2 ? PlaybackMode::loop : PlaybackMode::gate));
    const auto qualityTier = static_cast<int>(state.getProperty(kQualityTier, 1));
    setQualityTier(qualityTier == 0 ? QualityTier::cpu : (qualityTier == 2 ? QualityTier::ultra : QualityTier::fidelity));
    setVelocityCurve(static_cast<VelocityCurve>(static_cast<int>(state.getProperty(kVelocityCurve,
        static_cast<int>(getVelocityCurve())))));
    setReverbMix(static_cast<float>(state.getProperty(kReverbMix, getReverbMix())));
    DelaySettings delay = getDelaySettings();
    delay.timeMs = static_cast<float>(state.getProperty(kDelayTimeMs, delay.timeMs));
    delay.feedback = static_cast<float>(state.getProperty(kDelayFeedback, delay.feedback));
    delay.mix = static_cast<float>(state.getProperty(kDelayMix, delay.mix));
    delay.tempoSync = static_cast<int>(state.getProperty(kDelayTempoSync, delay.tempoSync ? 1 : 0)) == 1;
    setDelaySettings(delay);
    DcFilterSettings dcFilter = getDcFilterSettings();
    dcFilter.enabled = static_cast<int>(state.getProperty(kDcFilterEnabled, dcFilter.enabled ? 1 : 0)) == 1;
    dcFilter.cutoffHz = static_cast<float>(state.getProperty(kDcFilterCutoffHz, dcFilter.cutoffHz));
    setDcFilterSettings(dcFilter);
    AutopanSettings autopan = getAutopanSettings();
    autopan.rateHz = static_cast<float>(state.getProperty(kAutopanRateHz, autopan.rateHz));
    autopan.depth = static_cast<float>(state.getProperty(kAutopanDepth, autopan.depth));
    setAutopanSettings(autopan);
    SaturationSettings saturation = getSaturationSettings();
    saturation.drive = static_cast<float>(state.getProperty(kSaturationDrive, saturation.drive));
    saturation.mode = static_cast<SaturationSettings::Mode>(juce::jlimit(0, 3,
        static_cast<int>(state.getProperty(kSaturationMode, static_cast<int>(saturation.mode)))));
    setSaturationSettings(saturation);
    setPan(static_cast<float>(state.getProperty(kPan, getPan())));
    setMasterVolume(static_cast<float>(state.getProperty(kMasterVolume, getMasterVolume())));
    setPreloadSamples(static_cast<int>(state.getProperty(kPreloadSamples, getPreloadSamples())));
    setMonoMode(static_cast<int>(state.getProperty(kMonoMode, getMonoMode() ? 1 : 0)) == 1);
    setLegatoMode(static_cast<int>(state.getProperty(kLegatoMode, getLegatoMode() ? 1 : 0)) == 1);
    setGlideSeconds(static_cast<float>(state.getProperty(kGlideSeconds, getGlideSeconds())));
    setPolyphonyLimit(static_cast<int>(state.getProperty(kPolyphonyLimit, getPolyphonyLimit())));
    setSampleWindow(
        static_cast<int>(state.getProperty(kSampleWindowStart, getSampleWindowStart())),
        static_cast<int>(state.getProperty(kSampleWindowEnd, getSampleWindowEnd())));
    setWaveformViewRange(
        static_cast<int>(state.getProperty(kWaveformViewStartSample, waveformViewStartSample_.load(std::memory_order_relaxed))),
        static_cast<int>(state.getProperty(kWaveformViewSampleCount, waveformViewSampleCount_.load(std::memory_order_relaxed))));
    setEditorTabIndex(0);
    setWaveformDisplayMode(static_cast<int>(state.getProperty(kWaveformDisplayMode, waveformDisplayMode_.load(std::memory_order_relaxed))));
    setGenerateWaveType(static_cast<int>(state.getProperty(kGenerateWaveType, generateWaveType_.load(std::memory_order_relaxed))));
    setGenerateSampleCount(static_cast<int>(state.getProperty(kGenerateSampleCount, 1024)));
    setGenerateBitDepth(static_cast<int>(state.getProperty(kGenerateBitDepth, generateBitDepth_.load(std::memory_order_relaxed))));
    setGeneratePulseWidth(static_cast<float>(state.getProperty(kGeneratePulseWidth, 5.0f)));
    setGenerateFrequencyMidiNote(static_cast<int>(state.getProperty(kGenerateFrequencyMidiNote, generateFrequencyMidiNote_.load(std::memory_order_relaxed))));
    setGenerateSketchSmoothing(static_cast<int>(state.getProperty(kGenerateSketchSmoothing, generateSketchSmoothing_.load(std::memory_order_relaxed))));
    setCaptureTargetSampleRate(static_cast<int>(state.getProperty(kCaptureTargetSampleRate,
        captureTargetSampleRate_.load(std::memory_order_relaxed))));
    setCaptureChannelMode(static_cast<int>(state.getProperty(kCaptureChannelMode,
        captureChannelMode_.load(std::memory_order_relaxed))));
    setCaptureBitDepth(static_cast<int>(state.getProperty(kCaptureBitDepth,
        captureBitDepth_.load(std::memory_order_relaxed))));
    setCaptureInputGain(static_cast<float>(state.getProperty(kCaptureInputGain,
        captureInputGain_.load(std::memory_order_relaxed))));
    clearInputCapture();
    setLoopPoints(
        static_cast<int>(state.getProperty(kLoopStart, getLoopStart())),
        static_cast<int>(state.getProperty(kLoopEnd, getLoopEnd())));
    setLoopCrossfadeSamples(
        static_cast<int>(state.getProperty(kLoopCrossfadeSamples, getLoopCrossfadeSamples())));
    setFadeSamples(
        static_cast<int>(state.getProperty(kFadeInSamples, getFadeInSamples())),
        static_cast<int>(state.getProperty(kFadeOutSamples, getFadeOutSamples())));
    setReversePlayback(static_cast<int>(state.getProperty(kReversePlayback, getReversePlayback() ? 1 : 0)) == 1);

    playerPadAssignments_ = audiocity::plugin::defaultPlayerPadAssignments();
    {
        const auto padsNode = state.getChildWithName(audiocity::plugin::kPlayerPads);
        for (int i = 0; i < padsNode.getNumChildren(); ++i)
        {
            const auto entry = padsNode.getChild(i);
            if (!entry.hasType(audiocity::plugin::kPlayerPad))
                continue;

            const auto index = static_cast<int>(entry.getProperty(audiocity::plugin::kPlayerPadIndex, -1));
            if (index < 0 || index >= kPlayerPadCount)
                continue;

            const auto note = static_cast<int>(entry.getProperty(audiocity::plugin::kPlayerPadNote,
                playerPadAssignments_[static_cast<std::size_t>(index)].noteNumber));
            const auto velocity = static_cast<int>(entry.getProperty(audiocity::plugin::kPlayerPadVelocity,
                playerPadAssignments_[static_cast<std::size_t>(index)].velocity));

            playerPadAssignments_[static_cast<std::size_t>(index)] =
                audiocity::plugin::sanitizePlayerPadAssignment({ note, velocity });
        }
    }

    // Restore CC mappings
    {
        std::lock_guard<std::mutex> lock(ccMappingMutex_);
        ccToParam_.clear();
        const auto mappingsNode = state.getChildWithName(kCcMappings);
        for (int i = 0; i < mappingsNode.getNumChildren(); ++i)
        {
            const auto entry = mappingsNode.getChild(i);
            if (entry.hasType(kCcEntry))
            {
                const auto ccNum = static_cast<int>(entry.getProperty(kCcNumber, -1));
                const auto paramId = entry.getProperty(kCcParam).toString();
                if (ccNum >= 0 && ccNum <= 127 && paramId.isNotEmpty())
                    ccToParam_[ccNum] = paramId;
            }
        }
    }
}

juce::String AudiocityAudioProcessor::createPlaybackPresetXml()
{
    juce::MemoryBlock stateData;
    getStateInformation(stateData);
    if (stateData.getSize() == 0)
        return {};

    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(stateData.getData(),
        static_cast<int>(stateData.getSize())));
    if (xml == nullptr)
        return {};

    const auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid() || !state.hasType(kPatchRoot))
        return {};

    return audiocity::plugin::encodePresetXml(buildPlaybackPresetStateTree(state));
}

bool AudiocityAudioProcessor::loadPlaybackPresetXml(const juce::String& xmlText, juce::String& errorMessage)
{
    juce::ValueTree presetState;
    if (!audiocity::plugin::decodePresetXml(xmlText, presetState, errorMessage))
        return false;

    if (!presetState.isValid() || !presetState.hasType(kPatchRoot))
    {
        errorMessage = "Preset payload is not a valid Audiocity patch.";
        return false;
    }

    juce::MemoryBlock currentStateData;
    getStateInformation(currentStateData);
    if (currentStateData.getSize() == 0)
    {
        errorMessage = "Unable to read current processor state.";
        return false;
    }

    std::unique_ptr<juce::XmlElement> currentXml(getXmlFromBinary(currentStateData.getData(),
        static_cast<int>(currentStateData.getSize())));
    if (currentXml == nullptr)
    {
        errorMessage = "Unable to parse current processor state.";
        return false;
    }

    auto currentState = juce::ValueTree::fromXml(*currentXml);
    if (!currentState.isValid() || !currentState.hasType(kPatchRoot))
    {
        errorMessage = "Current processor state is invalid.";
        return false;
    }

    const auto filteredPresetState = buildPlaybackPresetStateTree(presetState);
    for (int propertyIndex = 0; propertyIndex < filteredPresetState.getNumProperties(); ++propertyIndex)
    {
        const auto propertyName = filteredPresetState.getPropertyName(propertyIndex);
        currentState.setProperty(propertyName, filteredPresetState.getProperty(propertyName), nullptr);
    }

    const auto presetPads = filteredPresetState.getChildWithName(audiocity::plugin::kPlayerPads);
    if (presetPads.isValid())
    {
        const auto existingPads = currentState.getChildWithName(audiocity::plugin::kPlayerPads);
        if (existingPads.isValid())
            currentState.removeChild(existingPads, nullptr);
        currentState.appendChild(presetPads.createCopy(), nullptr);
    }

    const auto presetCcMappings = filteredPresetState.getChildWithName(kCcMappings);
    if (presetCcMappings.isValid())
    {
        const auto existingCcMappings = currentState.getChildWithName(kCcMappings);
        if (existingCcMappings.isValid())
            currentState.removeChild(existingCcMappings, nullptr);
        currentState.appendChild(presetCcMappings.createCopy(), nullptr);
    }

    if (auto xml = currentState.createXml())
    {
        juce::MemoryBlock mergedStateData;
        copyXmlToBinary(*xml, mergedStateData);
        setStateInformation(mergedStateData.getData(), static_cast<int>(mergedStateData.getSize()));
        return true;
    }

    errorMessage = "Failed to serialize merged preset state.";
    return false;
}

juce::String AudiocityAudioProcessor::getLastStateRestoreSourceLabel() const
{
    switch (lastStateRestoreSource_.load(std::memory_order_relaxed))
    {
        case 1: return "file";
        case 2: return "generated";
        case 3: return "captured";
        default: return "none";
    }
}

bool AudiocityAudioProcessor::loadSampleFromFile(const juce::File& file)
{
    if (!engine_.loadSampleFromFile(file))
        return false;

    generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
    capturedAudioLoaded_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_.clear();
        capturedSampleState_.clear();
        capturedSampleRateState_ = 44100.0;
    }

    suspendParamSyncBlocks_.store(8, std::memory_order_relaxed);

    setAmpEnvelope(engine_.getAmpEnvelope());
    setAmpLfoSettings(engine_.getAmpLfoSettings());
    setPitchLfoSettings(engine_.getPitchLfoSettings());
    setFilterEnvelope(engine_.getFilterEnvelope());
    setFilterSettings(engine_.getFilterSettings());

    syncSampleDerivedParametersFromEngine();

    // Reset the stored waveform view range so the editor shows the full sample
    setWaveformViewRange(0, engine_.getLoadedSampleLength());

    return true;
}

void AudiocityAudioProcessor::loadGeneratedWaveformAsSample(const std::vector<float>& waveform, const int rootMidiNote)
{
    if (waveform.empty())
        return;

    juce::AudioBuffer<float> buffer(1, static_cast<int>(waveform.size()));
    auto* write = buffer.getWritePointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        write[i] = waveform[static_cast<std::size_t>(i)];

    const auto clampedRoot = juce::jlimit(0, 127, rootMidiNote);
    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(clampedRoot);
    const auto generatedSampleRate = juce::jmax(1.0, targetHz * static_cast<double>(buffer.getNumSamples()));

    engine_.setSampleData(buffer, generatedSampleRate, clampedRoot);
    engine_.setRootMidiNote(clampedRoot);
    engine_.clearSamplePath();
    if (getPlaybackMode() != PlaybackMode::loop)
        setPlaybackMode(PlaybackMode::loop);
    generatedWaveformLoaded_.store(true, std::memory_order_relaxed);
    capturedAudioLoaded_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_ = waveform;
        capturedSampleState_.clear();
        capturedSampleRateState_ = 44100.0;
    }
    stopGeneratedWaveformPreview();
    suspendParamSyncBlocks_.store(8, std::memory_order_relaxed);
    syncSampleDerivedParametersFromEngine();
    setWaveformViewRange(0, engine_.getLoadedSampleLength());
}

juce::String AudiocityAudioProcessor::getLoadedSamplePath() const
{
    return engine_.getSamplePath();
}

void AudiocityAudioProcessor::setPlaybackMode(const PlaybackMode mode) noexcept
{
    engine_.setPlaybackMode(mode);
    updateParameterFromPlainValue(kParamPlaybackMode, static_cast<float>(mode));
}

AudiocityAudioProcessor::PlaybackMode AudiocityAudioProcessor::getPlaybackMode() const noexcept
{
    return engine_.getPlaybackMode();
}

void AudiocityAudioProcessor::setMonoMode(const bool enabled) noexcept
{
    engine_.setMonoMode(enabled);
    updateParameterFromPlainValue(kParamMonoMode, enabled ? 1.0f : 0.0f);
}

void AudiocityAudioProcessor::setLegatoMode(const bool enabled) noexcept
{
    engine_.setLegatoMode(enabled);
    updateParameterFromPlainValue(kParamLegatoMode, enabled ? 1.0f : 0.0f);
}

void AudiocityAudioProcessor::setGlideSeconds(const float seconds) noexcept
{
    engine_.setGlideSeconds(seconds);
    updateParameterFromPlainValue(kParamGlideSeconds, engine_.getGlideSeconds());
}

void AudiocityAudioProcessor::setPolyphonyLimit(const int voices) noexcept
{
    engine_.setPolyphonyLimit(voices);
    updateParameterFromPlainValue(kParamPolyphonyLimit, static_cast<float>(engine_.getPolyphonyLimit()));
}

void AudiocityAudioProcessor::setFadeSamples(const int fadeInSamples, const int fadeOutSamples) noexcept
{
    engine_.setFadeSamples(fadeInSamples, fadeOutSamples);
    updateParameterFromPlainValue(kParamFadeIn, static_cast<float>(engine_.getFadeInSamples()));
    updateParameterFromPlainValue(kParamFadeOut, static_cast<float>(engine_.getFadeOutSamples()));
}

void AudiocityAudioProcessor::setReversePlayback(const bool enabled) noexcept
{
    engine_.setReversePlayback(enabled);
    updateParameterFromPlainValue(kParamReversePlayback, enabled ? 1.0f : 0.0f);
}

void AudiocityAudioProcessor::setAmpEnvelope(const AdsrSettings& settings) noexcept
{
    engine_.setAmpEnvelope(settings);
    updateParameterFromPlainValue(kParamAmpAttack, settings.attackSeconds);
    updateParameterFromPlainValue(kParamAmpDecay, settings.decaySeconds);
    updateParameterFromPlainValue(kParamAmpSustain, settings.sustainLevel);
    updateParameterFromPlainValue(kParamAmpRelease, settings.releaseSeconds);
}

void AudiocityAudioProcessor::setAmpLfoSettings(const AmpLfoSettings& settings) noexcept
{
    engine_.setAmpLfoSettings(settings);
    const auto applied = engine_.getAmpLfoSettings();
    updateParameterFromPlainValue(kParamAmpLfoRate, applied.rateHz);
    updateParameterFromPlainValue(kParamAmpLfoDepth, applied.depth);
    updateParameterFromPlainValue(kParamAmpLfoShape, static_cast<float>(applied.shape));
}

void AudiocityAudioProcessor::setRootMidiNote(const int rootNote) noexcept
{
    engine_.setRootMidiNote(rootNote);
    updateParameterFromPlainValue(kParamRootMidiNote, static_cast<float>(engine_.getRootMidiNote()));
}

void AudiocityAudioProcessor::setCoarseTuneSemitones(const float semitones) noexcept
{
    engine_.setCoarseTuneSemitones(semitones);
    updateParameterFromPlainValue(kParamTuneCoarse, engine_.getCoarseTuneSemitones());
}

void AudiocityAudioProcessor::setFineTuneCents(const float cents) noexcept
{
    engine_.setFineTuneCents(cents);
    updateParameterFromPlainValue(kParamTuneFine, engine_.getFineTuneCents());
}

void AudiocityAudioProcessor::setPitchBendRangeSemitones(const float semitones) noexcept
{
    engine_.setPitchBendRangeSemitones(semitones);
    updateParameterFromPlainValue(kParamPitchBendRange, engine_.getPitchBendRangeSemitones());
}

void AudiocityAudioProcessor::setPitchLfoSettings(const PitchLfoSettings& settings) noexcept
{
    engine_.setPitchLfoSettings(settings);
    const auto applied = engine_.getPitchLfoSettings();
    updateParameterFromPlainValue(kParamPitchLfoRate, applied.rateHz);
    updateParameterFromPlainValue(kParamPitchLfoDepth, applied.depthCents);
}

void AudiocityAudioProcessor::setSampleWindow(const int startSample, const int endSample) noexcept
{
    engine_.setSampleWindow(startSample, endSample);
    updateParameterFromPlainValue(kParamPlaybackStart, static_cast<float>(engine_.getSampleWindowStart()));
    updateParameterFromPlainValue(kParamPlaybackEnd, static_cast<float>(engine_.getSampleWindowEnd()));
}

void AudiocityAudioProcessor::setWaveformViewRange(const int startSample, const int sampleCount) noexcept
{
    waveformViewStartSample_.store(juce::jmax(0, startSample), std::memory_order_relaxed);
    waveformViewSampleCount_.store(juce::jmax(0, sampleCount), std::memory_order_relaxed);
}

std::pair<int, int> AudiocityAudioProcessor::getWaveformViewRange() const noexcept
{
    return {
        waveformViewStartSample_.load(std::memory_order_relaxed),
        waveformViewSampleCount_.load(std::memory_order_relaxed)
    };
}

void AudiocityAudioProcessor::setEditorTabIndex(const int tabIndex) noexcept
{
    editorTabIndex_.store(juce::jlimit(0, 4, tabIndex), std::memory_order_relaxed);
}

int AudiocityAudioProcessor::getEditorTabIndex() const noexcept
{
    return juce::jlimit(0, 4, editorTabIndex_.load(std::memory_order_relaxed));
}

void AudiocityAudioProcessor::setWaveformDisplayMode(const int modeId) noexcept
{
    waveformDisplayMode_.store(juce::jlimit(1, 2, modeId), std::memory_order_relaxed);
}

int AudiocityAudioProcessor::getWaveformDisplayMode() const noexcept
{
    return juce::jlimit(1, 2, waveformDisplayMode_.load(std::memory_order_relaxed));
}

void AudiocityAudioProcessor::setGenerateWaveType(const int waveType) noexcept
{
    generateWaveType_.store(juce::jlimit(0, 7, waveType), std::memory_order_relaxed);
}

int AudiocityAudioProcessor::getGenerateWaveType() const noexcept
{
    return juce::jlimit(0, 7, generateWaveType_.load(std::memory_order_relaxed));
}

void AudiocityAudioProcessor::setGenerateSampleCount(const int sampleCount) noexcept
{
    const auto clamped = juce::jlimit(16, 8192, sampleCount);
    int quantized = 16;
    while (quantized < clamped)
        quantized <<= 1;

    if (quantized > 8192)
        quantized = 8192;

    generateSampleCount_.store(quantized, std::memory_order_relaxed);
}

int AudiocityAudioProcessor::getGenerateSampleCount() const noexcept
{
    return juce::jlimit(16, 8192, generateSampleCount_.load(std::memory_order_relaxed));
}

void AudiocityAudioProcessor::setGenerateBitDepth(const int bitDepth) noexcept
{
    int normalized = 16;
    if (bitDepth <= 8)
        normalized = 8;
    else if (bitDepth >= 24)
        normalized = 24;

    generateBitDepth_.store(normalized, std::memory_order_relaxed);
}

int AudiocityAudioProcessor::getGenerateBitDepth() const noexcept
{
    const auto value = generateBitDepth_.load(std::memory_order_relaxed);
    if (value <= 8)
        return 8;
    if (value >= 24)
        return 24;
    return 16;
}

void AudiocityAudioProcessor::setGeneratePulseWidth(const float pulseWidthPercent) noexcept
{
    generatePulseWidth_.store(juce::jlimit(1.0f, 99.0f, pulseWidthPercent), std::memory_order_relaxed);
}

float AudiocityAudioProcessor::getGeneratePulseWidth() const noexcept
{
    return juce::jlimit(1.0f, 99.0f, generatePulseWidth_.load(std::memory_order_relaxed));
}

void AudiocityAudioProcessor::setGenerateFrequencyMidiNote(const int midiNote) noexcept
{
    generateFrequencyMidiNote_.store(juce::jlimit(0, 127, midiNote), std::memory_order_relaxed);
}

int AudiocityAudioProcessor::getGenerateFrequencyMidiNote() const noexcept
{
    return juce::jlimit(0, 127, generateFrequencyMidiNote_.load(std::memory_order_relaxed));
}

void AudiocityAudioProcessor::setGenerateSketchSmoothing(const int modeId) noexcept
{
    generateSketchSmoothing_.store(juce::jlimit(1, 2, modeId), std::memory_order_relaxed);
}

int AudiocityAudioProcessor::getGenerateSketchSmoothing() const noexcept
{
    return juce::jlimit(1, 2, generateSketchSmoothing_.load(std::memory_order_relaxed));
}

void AudiocityAudioProcessor::startInputCapture() noexcept
{
    captureInputSamples_.store(0, std::memory_order_relaxed);
    captureOverflow_.store(false, std::memory_order_relaxed);
    captureInputSampleRate_.store(juce::jmax(1.0, getSampleRate()), std::memory_order_relaxed);
    captureRecording_.store(true, std::memory_order_release);
}

void AudiocityAudioProcessor::stopInputCapture() noexcept
{
    captureRecording_.store(false, std::memory_order_release);
}

void AudiocityAudioProcessor::clearInputCapture() noexcept
{
    captureRecording_.store(false, std::memory_order_release);
    captureInputSamples_.store(0, std::memory_order_relaxed);
    captureOverflow_.store(false, std::memory_order_relaxed);
}

void AudiocityAudioProcessor::resetInputCaptureOverflow() noexcept
{
    captureOverflow_.store(false, std::memory_order_relaxed);
}

void AudiocityAudioProcessor::setCaptureTargetSampleRate(const int sampleRate) noexcept
{
    if (sampleRate <= 0)
    {
        captureTargetSampleRate_.store(0, std::memory_order_relaxed);
        return;
    }

    constexpr std::array<int, 6> kAllowedRates{ 22050, 32000, 44100, 48000, 88200, 96000 };
    int closest = kAllowedRates.front();
    int closestDistance = std::abs(sampleRate - closest);
    for (const auto candidate : kAllowedRates)
    {
        const auto distance = std::abs(sampleRate - candidate);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closest = candidate;
        }
    }

    captureTargetSampleRate_.store(closest, std::memory_order_relaxed);
}

int AudiocityAudioProcessor::getCaptureTargetSampleRate() const noexcept
{
    return captureTargetSampleRate_.load(std::memory_order_relaxed);
}

void AudiocityAudioProcessor::setCaptureChannelMode(const int modeId) noexcept
{
    captureChannelMode_.store(juce::jlimit(0, 3, modeId), std::memory_order_relaxed);
}

int AudiocityAudioProcessor::getCaptureChannelMode() const noexcept
{
    return juce::jlimit(0, 3, captureChannelMode_.load(std::memory_order_relaxed));
}

void AudiocityAudioProcessor::setCaptureBitDepth(const int bitDepth) noexcept
{
    int normalized = 16;
    if (bitDepth >= 32)
        normalized = 32;
    else if (bitDepth >= 24)
        normalized = 24;
    captureBitDepth_.store(normalized, std::memory_order_relaxed);
}

int AudiocityAudioProcessor::getCaptureBitDepth() const noexcept
{
    const auto value = captureBitDepth_.load(std::memory_order_relaxed);
    if (value >= 32)
        return 32;
    if (value >= 24)
        return 24;
    return 16;
}

void AudiocityAudioProcessor::setCaptureInputGain(const float gainLinear) noexcept
{
    captureInputGain_.store(juce::jlimit(0.0f, 2.0f, gainLinear), std::memory_order_relaxed);
}

float AudiocityAudioProcessor::getCaptureInputGain() const noexcept
{
    return juce::jlimit(0.0f, 2.0f, captureInputGain_.load(std::memory_order_relaxed));
}

AudiocityAudioProcessor::OutputPeakLevels AudiocityAudioProcessor::consumeCaptureInputPeakLevels() noexcept
{
    OutputPeakLevels levels;
    levels.left = captureInputPeakLeft_.exchange(0.0f, std::memory_order_acq_rel);
    levels.right = captureInputPeakRight_.exchange(0.0f, std::memory_order_acq_rel);
    return levels;
}

std::vector<AudiocityAudioProcessor::CaptureDisplayMinMax> AudiocityAudioProcessor::buildCapturedWaveformMinMax(
    const int maxPeaks,
    const int startSample,
    const int endSample) const
{
    const auto totalSamples = juce::jlimit(0, kCaptureMaxSamplesPerChannel,
        captureInputSamples_.load(std::memory_order_acquire));
    if (totalSamples <= 0)
        return {};

    const auto start = juce::jlimit(0, totalSamples, startSample);
    const auto end = juce::jlimit(start, totalSamples, endSample <= startSample ? totalSamples : endSample);
    if (end <= start)
        return {};

    const auto rangeSamples = end - start;
    const auto peaks = juce::jlimit(1, 8192, maxPeaks);
    std::vector<CaptureDisplayMinMax> result(static_cast<std::size_t>(peaks));

    const auto mode = getCaptureChannelMode();
    for (int i = 0; i < peaks; ++i)
    {
        const auto bucketStart64 = static_cast<long long>(start)
            + (static_cast<long long>(rangeSamples) * static_cast<long long>(i))
                / static_cast<long long>(peaks);
        const auto bucketEnd64 = static_cast<long long>(start)
            + (static_cast<long long>(rangeSamples) * static_cast<long long>(i + 1))
                / static_cast<long long>(peaks);

        const auto bucketStart = juce::jlimit(0, totalSamples - 1, static_cast<int>(bucketStart64));
        const auto bucketEnd = juce::jlimit(bucketStart + 1, totalSamples, static_cast<int>(bucketEnd64));
        const auto clampedBucketEnd = juce::jlimit(bucketStart + 1, totalSamples,
            juce::jmax(bucketStart + 1, bucketEnd));

        float minValue = 1.0f;
        float maxValue = -1.0f;

        for (int sample = bucketStart; sample < clampedBucketEnd; ++sample)
        {
            const auto left = captureInputLeft_[static_cast<std::size_t>(sample)];
            const auto right = captureInputRight_[static_cast<std::size_t>(sample)];

            float value = left;
            if (mode == 1)
                value = left;
            else if (mode == 2)
                value = right;
            else if (mode == 3)
            {
                minValue = juce::jmin(minValue, juce::jmin(left, right));
                maxValue = juce::jmax(maxValue, juce::jmax(left, right));
                continue;
            }
            else
            {
                value = 0.5f * (left + right);
            }

            minValue = juce::jmin(minValue, value);
            maxValue = juce::jmax(maxValue, value);
        }

        if (maxValue < minValue)
        {
            minValue = 0.0f;
            maxValue = 0.0f;
        }

        result[static_cast<std::size_t>(i)] = { minValue, maxValue };
    }

    return result;
}

bool AudiocityAudioProcessor::cutCapturedAudioRange(const int startSample, const int endSample) noexcept
{
    stopInputCapture();
    const auto total = juce::jlimit(0, kCaptureMaxSamplesPerChannel,
        captureInputSamples_.load(std::memory_order_acquire));
    const auto start = juce::jlimit(0, total, startSample);
    const auto end = juce::jlimit(start, total, endSample);
    if (end <= start)
        return false;

    const auto cutLength = end - start;
    const auto tailLength = total - end;
    if (tailLength > 0)
    {
        std::memmove(captureInputLeft_.data() + start,
            captureInputLeft_.data() + end,
            static_cast<std::size_t>(tailLength) * sizeof(float));
        std::memmove(captureInputRight_.data() + start,
            captureInputRight_.data() + end,
            static_cast<std::size_t>(tailLength) * sizeof(float));
    }

    captureInputSamples_.store(total - cutLength, std::memory_order_release);
    return true;
}

bool AudiocityAudioProcessor::trimCapturedAudioRange(const int startSample, const int endSample) noexcept
{
    stopInputCapture();
    const auto total = juce::jlimit(0, kCaptureMaxSamplesPerChannel,
        captureInputSamples_.load(std::memory_order_acquire));
    const auto start = juce::jlimit(0, total, startSample);
    const auto end = juce::jlimit(start, total, endSample);
    if (end <= start)
        return false;

    const auto newLength = end - start;
    if (start > 0)
    {
        std::memmove(captureInputLeft_.data(),
            captureInputLeft_.data() + start,
            static_cast<std::size_t>(newLength) * sizeof(float));
        std::memmove(captureInputRight_.data(),
            captureInputRight_.data() + start,
            static_cast<std::size_t>(newLength) * sizeof(float));
    }

    captureInputSamples_.store(newLength, std::memory_order_release);
    return true;
}

bool AudiocityAudioProcessor::normalizeCapturedAudio(const float targetPeak) noexcept
{
    stopInputCapture();
    const auto total = juce::jlimit(0, kCaptureMaxSamplesPerChannel,
        captureInputSamples_.load(std::memory_order_acquire));
    if (total <= 0)
        return false;

    float peak = 0.0f;
    for (int i = 0; i < total; ++i)
    {
        peak = juce::jmax(peak, std::abs(captureInputLeft_[static_cast<std::size_t>(i)]));
        peak = juce::jmax(peak, std::abs(captureInputRight_[static_cast<std::size_t>(i)]));
    }

    if (peak < 1.0e-6f)
        return false;

    const float scale = juce::jlimit(0.0f, 100.0f, targetPeak) / peak;
    for (int i = 0; i < total; ++i)
    {
        captureInputLeft_[static_cast<std::size_t>(i)] *= scale;
        captureInputRight_[static_cast<std::size_t>(i)] *= scale;
    }

    return true;
}

bool AudiocityAudioProcessor::loadCapturedAudioAsSample(int startSample, int endSample)
{
    stopInputCapture();
    const auto total = juce::jlimit(0, kCaptureMaxSamplesPerChannel,
        captureInputSamples_.load(std::memory_order_acquire));
    if (total <= 1)
        return false;

    startSample = juce::jlimit(0, total - 1, startSample);
    endSample = juce::jlimit(startSample + 1, total, endSample <= startSample ? total : endSample);
    const auto numSourceSamples = endSample - startSample;
    if (numSourceSamples <= 1)
        return false;

    const auto mode = getCaptureChannelMode();
    const auto bitDepth = getCaptureBitDepth();
    const auto sourceRate = juce::jmax(1.0, captureInputSampleRate_.load(std::memory_order_relaxed));
    const auto targetRateSetting = getCaptureTargetSampleRate();
    const auto targetRate = targetRateSetting > 0 ? static_cast<double>(targetRateSetting) : sourceRate;
    const auto targetChannels = mode == 3 ? 2 : 1;

    auto persistCapturedState = [this](const juce::AudioBuffer<float>& buffer, const double bufferSampleRate)
    {
        const auto channels = juce::jmax(1, buffer.getNumChannels());
        const auto samples = juce::jmax(0, buffer.getNumSamples());
        std::vector<float> mono(static_cast<std::size_t>(samples), 0.0f);

        if (samples > 0)
        {
            if (channels == 1)
            {
                const auto* read = buffer.getReadPointer(0);
                std::copy(read, read + samples, mono.begin());
            }
            else
            {
                for (int i = 0; i < samples; ++i)
                {
                    float sum = 0.0f;
                    for (int channel = 0; channel < channels; ++channel)
                        sum += buffer.getSample(channel, i);
                    mono[static_cast<std::size_t>(i)] = sum / static_cast<float>(channels);
                }
            }
        }

        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_.clear();
        capturedSampleState_ = std::move(mono);
        capturedSampleRateState_ = juce::jmax(1.0, bufferSampleRate);
    };

    juce::AudioBuffer<float> source(targetChannels, numSourceSamples);
    for (int sample = 0; sample < numSourceSamples; ++sample)
    {
        const auto sourceIndex = startSample + sample;
        const auto left = captureInputLeft_[static_cast<std::size_t>(sourceIndex)];
        const auto right = captureInputRight_[static_cast<std::size_t>(sourceIndex)];

        if (targetChannels == 2)
        {
            source.setSample(0, sample, quantizeCaptureSample(left, bitDepth));
            source.setSample(1, sample, quantizeCaptureSample(right, bitDepth));
        }
        else
        {
            float mono = 0.5f * (left + right);
            if (mode == 1)
                mono = left;
            else if (mode == 2)
                mono = right;

            source.setSample(0, sample, quantizeCaptureSample(mono, bitDepth));
        }
    }

    if (std::abs(targetRate - sourceRate) < 0.5)
    {
        engine_.setSampleData(source, sourceRate, engine_.getRootMidiNote());
        persistCapturedState(source, sourceRate);
    }
    else
    {
        const auto ratio = targetRate / sourceRate;
        const auto resampledSamples = juce::jmax(2, static_cast<int>(std::round(static_cast<double>(numSourceSamples) * ratio)));
        juce::AudioBuffer<float> resampled(targetChannels, resampledSamples);

        const auto readStep = static_cast<float>(sourceRate / targetRate);
        for (int channel = 0; channel < targetChannels; ++channel)
        {
            const auto* read = source.getReadPointer(channel);
            auto* write = resampled.getWritePointer(channel);
            float readPos = 0.0f;
            for (int sample = 0; sample < resampledSamples; ++sample)
            {
                const auto i0 = juce::jlimit(0, numSourceSamples - 1, static_cast<int>(readPos));
                const auto i1 = juce::jmin(numSourceSamples - 1, i0 + 1);
                const auto frac = readPos - static_cast<float>(i0);
                write[sample] = read[i0] + (read[i1] - read[i0]) * frac;
                readPos += readStep;
            }
        }

        engine_.setSampleData(resampled, targetRate, engine_.getRootMidiNote());
        persistCapturedState(resampled, targetRate);
    }

    generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
    capturedAudioLoaded_.store(true, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        if (capturedSampleState_.empty())
            capturedSampleRateState_ = 44100.0;
    }

    engine_.clearSamplePath();
    suspendParamSyncBlocks_.store(8, std::memory_order_relaxed);
    syncSampleDerivedParametersFromEngine();
    setWaveformViewRange(0, engine_.getLoadedSampleLength());
    return true;
}

void AudiocityAudioProcessor::setLoopPoints(const int loopStart, const int loopEnd) noexcept
{
    engine_.setLoopPoints(loopStart, loopEnd);
    updateParameterFromPlainValue(kParamLoopStart, static_cast<float>(engine_.getLoopStart()));
    updateParameterFromPlainValue(kParamLoopEnd, static_cast<float>(engine_.getLoopEnd()));
}

void AudiocityAudioProcessor::setLoopCrossfadeSamples(const int crossfadeSamples) noexcept
{
    engine_.setLoopCrossfadeSamples(crossfadeSamples);
    updateParameterFromPlainValue(kParamLoopCrossfade, static_cast<float>(engine_.getLoopCrossfadeSamples()));
}

void AudiocityAudioProcessor::setGeneratedWaveformPreview(const std::vector<float>& waveform) noexcept
{
    const auto count = juce::jlimit(0, kPreviewWaveMaxSamples, static_cast<int>(waveform.size()));
    for (int i = 0; i < count; ++i)
        previewWaveData_[static_cast<std::size_t>(i)] = juce::jlimit(-1.0f, 1.0f, waveform[static_cast<std::size_t>(i)]);

    previewWaveSamples_.store(count, std::memory_order_relaxed);
    if (count == 0)
        previewWavePlaying_.store(false, std::memory_order_relaxed);
}

void AudiocityAudioProcessor::setGeneratedWaveformPreviewMidiNote(const int midiNote) noexcept
{
    previewWaveMidiNote_.store(juce::jlimit(0, 127, midiNote), std::memory_order_relaxed);
}

void AudiocityAudioProcessor::startGeneratedWaveformPreview() noexcept
{
    if (previewWaveSamples_.load(std::memory_order_relaxed) <= 0)
        return;

    samplePreviewPlaying_.store(false, std::memory_order_relaxed);
    previewWavePhase_ = 0.0f;
    previewWavePlaying_.store(true, std::memory_order_relaxed);
}

void AudiocityAudioProcessor::stopGeneratedWaveformPreview() noexcept
{
    previewWavePlaying_.store(false, std::memory_order_relaxed);
}

bool AudiocityAudioProcessor::previewCapturedAudio()
{
    stopInputCapture();

    const auto total = juce::jlimit(0, kCaptureMaxSamplesPerChannel,
        captureInputSamples_.load(std::memory_order_acquire));
    if (total <= 1)
        return false;

    const auto samplesToPreview = juce::jlimit(1, kSamplePreviewMaxSamples, total);
    const auto mode = getCaptureChannelMode();
    for (int sample = 0; sample < samplesToPreview; ++sample)
    {
        const auto left = captureInputLeft_[static_cast<std::size_t>(sample)];
        const auto right = captureInputRight_[static_cast<std::size_t>(sample)];

        float mono = 0.5f * (left + right);
        if (mode == 1)
            mono = left;
        else if (mode == 2)
            mono = right;

        samplePreviewData_[static_cast<std::size_t>(sample)] = juce::jlimit(-1.0f, 1.0f, mono);
    }

    samplePreviewSourceRate_.store(juce::jmax(1.0, captureInputSampleRate_.load(std::memory_order_relaxed)),
        std::memory_order_relaxed);
    samplePreviewSamples_.store(samplesToPreview, std::memory_order_relaxed);
    samplePreviewReadPos_ = 0.0f;
    previewWavePlaying_.store(false, std::memory_order_relaxed);
    engine_.panic();
    samplePreviewPlaying_.store(true, std::memory_order_relaxed);
    return true;
}

bool AudiocityAudioProcessor::previewSampleFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const auto availableSamples = static_cast<int>(std::min<long long>(
        reader->lengthInSamples,
        static_cast<long long>(kSamplePreviewMaxSamples)));
    const auto samplesToRead = juce::jlimit(1, kSamplePreviewMaxSamples, availableSamples);

    juce::AudioBuffer<float> tempBuffer(juce::jmax(1, static_cast<int>(reader->numChannels)), samplesToRead);
    if (!reader->read(&tempBuffer, 0, samplesToRead, 0, true, true))
        return false;

    samplePreviewPlaying_.store(false, std::memory_order_relaxed);
    previewWavePlaying_.store(false, std::memory_order_relaxed);
    engine_.panic();

    const auto channels = tempBuffer.getNumChannels();
    for (int i = 0; i < samplesToRead; ++i)
    {
        float mono = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            mono += tempBuffer.getSample(channel, i);

        samplePreviewData_[static_cast<std::size_t>(i)] = juce::jlimit(-1.0f, 1.0f, mono / static_cast<float>(channels));
    }

    samplePreviewSourceRate_.store(juce::jmax(1.0, reader->sampleRate), std::memory_order_relaxed);
    samplePreviewSamples_.store(samplesToRead, std::memory_order_relaxed);
    samplePreviewReadPos_ = 0.0f;
    samplePreviewPlaying_.store(true, std::memory_order_relaxed);
    return true;
}

void AudiocityAudioProcessor::panicAllAudio() noexcept
{
    stopGeneratedWaveformPreview();
    samplePreviewPlaying_.store(false, std::memory_order_relaxed);
    engine_.panic();
}

void AudiocityAudioProcessor::setPlayerPadAssignment(const int padIndex, const int noteNumber, const int velocity) noexcept
{
    if (padIndex < 0 || padIndex >= kPlayerPadCount)
        return;

    playerPadAssignments_[static_cast<std::size_t>(padIndex)] =
        audiocity::plugin::sanitizePlayerPadAssignment({ noteNumber, velocity });
}

AudiocityAudioProcessor::PlayerPadAssignment AudiocityAudioProcessor::getPlayerPadAssignment(const int padIndex) const noexcept
{
    if (padIndex < 0 || padIndex >= kPlayerPadCount)
        return {};

    return playerPadAssignments_[static_cast<std::size_t>(padIndex)];
}

void AudiocityAudioProcessor::enqueueUiMidiNoteOn(const int noteNumber, const int velocity) noexcept
{
    pushUiMidiEvent(noteNumber, velocity, true);
}

void AudiocityAudioProcessor::enqueueUiMidiNoteOff(const int noteNumber) noexcept
{
    pushUiMidiEvent(noteNumber, 0, false);
}

// ─── CC FIFO ───────────────────────────────────────────────────────────────────

void AudiocityAudioProcessor::pushCcEvent(const int ccNumber, const int value)
{
    const auto writePos = ccFifoWritePos_.load(std::memory_order_relaxed);
    const auto nextWrite = (writePos + 1) % kCcFifoSize;

    // If full, drop oldest by advancing read pos
    if (nextWrite == ccFifoReadPos_.load(std::memory_order_acquire))
        ccFifoReadPos_.store((ccFifoReadPos_.load(std::memory_order_relaxed) + 1) % kCcFifoSize,
                             std::memory_order_release);

    ccFifo_[static_cast<std::size_t>(writePos)] = { ccNumber, value };
    ccFifoWritePos_.store(nextWrite, std::memory_order_release);
}

bool AudiocityAudioProcessor::popCcEvent(CcEvent& out)
{
    const auto readPos = ccFifoReadPos_.load(std::memory_order_relaxed);
    if (readPos == ccFifoWritePos_.load(std::memory_order_acquire))
        return false;

    out = ccFifo_[static_cast<std::size_t>(readPos)];
    ccFifoReadPos_.store((readPos + 1) % kCcFifoSize, std::memory_order_release);
    return true;
}

void AudiocityAudioProcessor::pushUiMidiEvent(const int noteNumber, const int velocity, const bool isNoteOn) noexcept
{
    const auto writePos = uiMidiWritePos_.load(std::memory_order_relaxed);
    const auto nextWrite = (writePos + 1) % kUiMidiFifoSize;

    if (nextWrite == uiMidiReadPos_.load(std::memory_order_acquire))
        uiMidiReadPos_.store((uiMidiReadPos_.load(std::memory_order_relaxed) + 1) % kUiMidiFifoSize,
                             std::memory_order_release);

    uiMidiFifo_[static_cast<std::size_t>(writePos)] = {
        juce::jlimit(0, 127, noteNumber),
        juce::jlimit(1, 127, velocity),
        isNoteOn
    };
    uiMidiWritePos_.store(nextWrite, std::memory_order_release);
}

bool AudiocityAudioProcessor::popUiMidiEvent(UiMidiEvent& out) noexcept
{
    const auto readPos = uiMidiReadPos_.load(std::memory_order_relaxed);
    if (readPos == uiMidiWritePos_.load(std::memory_order_acquire))
        return false;

    out = uiMidiFifo_[static_cast<std::size_t>(readPos)];
    uiMidiReadPos_.store((readPos + 1) % kUiMidiFifoSize, std::memory_order_release);
    return true;
}

void AudiocityAudioProcessor::updateCaptureInputMonitorLevels(
    const juce::AudioBuffer<float>& buffer,
    int sourceChannels) noexcept
{
    sourceChannels = juce::jlimit(0, buffer.getNumChannels(), sourceChannels);
    if (sourceChannels <= 0)
        return;

    const auto* inLeft = sourceChannels > 0 ? buffer.getReadPointer(0) : nullptr;
    const auto* inRight = sourceChannels > 1 ? buffer.getReadPointer(1) : inLeft;
    if (inLeft == nullptr && inRight == nullptr)
        return;

    const auto gain = getCaptureInputGain();
    float localPeakLeft = 0.0f;
    float localPeakRight = 0.0f;
    const auto numSamples = buffer.getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto left = inLeft != nullptr ? juce::jlimit(-1.0f, 1.0f, inLeft[sample] * gain) : 0.0f;
        const auto right = inRight != nullptr ? juce::jlimit(-1.0f, 1.0f, inRight[sample] * gain) : 0.0f;
        localPeakLeft = juce::jmax(localPeakLeft, std::abs(left));
        localPeakRight = juce::jmax(localPeakRight, std::abs(right));
    }

    auto accumulatePeak = [](std::atomic<float>& atomicPeak, const float candidate)
    {
        auto current = atomicPeak.load(std::memory_order_relaxed);
        while (candidate > current
            && !atomicPeak.compare_exchange_weak(current, candidate,
                std::memory_order_release, std::memory_order_relaxed))
        {
        }
    };

    accumulatePeak(captureInputPeakLeft_, localPeakLeft);
    accumulatePeak(captureInputPeakRight_, localPeakRight);
}

bool AudiocityAudioProcessor::captureInputAudio(const juce::AudioBuffer<float>& buffer, int sourceChannels) noexcept
{
    if (!captureRecording_.load(std::memory_order_acquire))
        return false;

    updateCaptureInputMonitorLevels(buffer, sourceChannels);

    sourceChannels = juce::jlimit(0, buffer.getNumChannels(), sourceChannels);
    if (sourceChannels <= 0)
        return false;

    auto writePos = juce::jlimit(0, kCaptureMaxSamplesPerChannel,
        captureInputSamples_.load(std::memory_order_relaxed));
    if (writePos >= kCaptureMaxSamplesPerChannel)
    {
        captureRecording_.store(false, std::memory_order_release);
        captureOverflow_.store(true, std::memory_order_relaxed);
        return false;
    }

    const auto samplesToCapture = juce::jmin(buffer.getNumSamples(), kCaptureMaxSamplesPerChannel - writePos);
    if (samplesToCapture <= 0)
    {
        captureRecording_.store(false, std::memory_order_release);
        captureOverflow_.store(true, std::memory_order_relaxed);
        return false;
    }

    const auto* inLeft = sourceChannels > 0 ? buffer.getReadPointer(0) : nullptr;
    const auto* inRight = sourceChannels > 1 ? buffer.getReadPointer(1) : inLeft;
    const auto gain = getCaptureInputGain();

    for (int sample = 0; sample < samplesToCapture; ++sample)
    {
        const auto rawIndex = writePos + sample;
        if (rawIndex < 0 || rawIndex >= kCaptureMaxSamplesPerChannel)
        {
            captureRecording_.store(false, std::memory_order_release);
            captureOverflow_.store(true, std::memory_order_relaxed);
            break;
        }

        const auto index = static_cast<std::size_t>(rawIndex);
        const auto left = inLeft != nullptr ? juce::jlimit(-1.0f, 1.0f, inLeft[sample] * gain) : 0.0f;
        const auto right = inRight != nullptr ? juce::jlimit(-1.0f, 1.0f, inRight[sample] * gain) : 0.0f;
        captureInputLeft_[index] = left;
        captureInputRight_[index] = right;
    }

    writePos += samplesToCapture;
    captureInputSamples_.store(writePos, std::memory_order_release);

    if (writePos >= kCaptureMaxSamplesPerChannel)
    {
        captureRecording_.store(false, std::memory_order_release);
        captureOverflow_.store(true, std::memory_order_relaxed);
    }

    return true;
}

float AudiocityAudioProcessor::quantizeCaptureSample(const float sample, const int bitDepth) noexcept
{
    const auto clamped = juce::jlimit(-1.0f, 1.0f, sample);
    if (bitDepth >= 32)
        return clamped;

    const auto levels = bitDepth >= 24 ? 8388607.0f : 32767.0f;
    return std::round(clamped * levels) / levels;
}

void AudiocityAudioProcessor::updateHostTempoFromPlayHead() noexcept
{
    if (auto* currentPlayHead = getPlayHead())
    {
        if (const auto pos = currentPlayHead->getPosition())
        {
            if (const auto bpm = pos->getBpm())
            {
                const auto bpmValue = static_cast<float>(*bpm);
                if (bpmValue > 1.0f)
                    hostBpm_.store(bpmValue, std::memory_order_relaxed);
            }
        }
    }
}

float AudiocityAudioProcessor::lfoRateHzFromTempoSync(const int divisionIndex) const noexcept
{
    constexpr std::array<float, 12> beatsPerCycle{
        0.25f,
        1.0f / 6.0f,
        0.375f,
        0.5f,
        1.0f / 3.0f,
        0.75f,
        1.0f,
        2.0f / 3.0f,
        1.5f,
        2.0f,
        4.0f,
        8.0f
    };
    const auto idx = juce::jlimit(0, static_cast<int>(beatsPerCycle.size()) - 1, divisionIndex);
    const auto bpm = juce::jmax(1.0f, hostBpm_.load(std::memory_order_relaxed));
    return juce::jlimit(0.0f, 40.0f, (bpm / 60.0f) / beatsPerCycle[static_cast<std::size_t>(idx)]);
}

// ─── CC Mapping ────────────────────────────────────────────────────────────────

void AudiocityAudioProcessor::setCcMapping(const int ccNumber, const juce::String& paramId)
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    // Remove any existing mapping to this param
    for (auto it = ccToParam_.begin(); it != ccToParam_.end(); )
    {
        if (it->second == paramId)
            it = ccToParam_.erase(it);
        else
            ++it;
    }
    ccToParam_[ccNumber] = paramId;
}

void AudiocityAudioProcessor::clearCcMapping(const int ccNumber)
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    ccToParam_.erase(ccNumber);
}

void AudiocityAudioProcessor::clearCcMappingByParam(const juce::String& paramId)
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    for (auto it = ccToParam_.begin(); it != ccToParam_.end(); )
    {
        if (it->second == paramId)
            it = ccToParam_.erase(it);
        else
            ++it;
    }
}

int AudiocityAudioProcessor::getCcForParam(const juce::String& paramId) const
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    for (const auto& [ccNum, pid] : ccToParam_)
    {
        if (pid == paramId)
            return ccNum;
    }
    return -1;
}

juce::String AudiocityAudioProcessor::getParamForCc(const int ccNumber) const
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    const auto it = ccToParam_.find(ccNumber);
    return it != ccToParam_.end() ? it->second : juce::String{};
}

std::map<int, juce::String> AudiocityAudioProcessor::getAllCcMappings() const
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    return ccToParam_;
}

void AudiocityAudioProcessor::syncSampleDerivedParametersFromEngine() noexcept
{
    updateParameterFromPlainValue(kParamRootMidiNote, static_cast<float>(engine_.getRootMidiNote()));
    updateParameterFromPlainValue(kParamTuneCoarse, engine_.getCoarseTuneSemitones());
    updateParameterFromPlainValue(kParamTuneFine, engine_.getFineTuneCents());
    updateParameterFromPlainValue(kParamPlaybackMode, static_cast<float>(engine_.getPlaybackMode()));
    updateParameterFromPlainValue(kParamPlaybackStart, static_cast<float>(engine_.getSampleWindowStart()));
    updateParameterFromPlainValue(kParamPlaybackEnd, static_cast<float>(engine_.getSampleWindowEnd()));
    updateParameterFromPlainValue(kParamLoopStart, static_cast<float>(engine_.getLoopStart()));
    updateParameterFromPlainValue(kParamLoopEnd, static_cast<float>(engine_.getLoopEnd()));
    updateParameterFromPlainValue(kParamLoopCrossfade, static_cast<float>(engine_.getLoopCrossfadeSamples()));
}

void AudiocityAudioProcessor::renderGeneratedWavePreview(juce::AudioBuffer<float>& buffer) noexcept
{
    const auto count = previewWaveSamples_.load(std::memory_order_relaxed);
    if (count <= 0)
    {
        buffer.clear();
        return;
    }

    const auto midiNote = previewWaveMidiNote_.load(std::memory_order_relaxed);
    const auto hz = 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
    const auto phaseIncrement = static_cast<float>((hz * static_cast<double>(count)) / getSampleRate());
    const auto channels = buffer.getNumChannels();
    const auto samples = buffer.getNumSamples();
    const auto masterVolume = juce::jlimit(0.0f, 1.0f,
        apvts_.getRawParameterValue(kParamMasterVolume)->load());

    for (int sample = 0; sample < samples; ++sample)
    {
        const auto readIndex = juce::jlimit(0, count - 1, static_cast<int>(previewWavePhase_));
        const auto value = previewWaveData_[static_cast<std::size_t>(readIndex)] * 0.25f * masterVolume;
        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample(channel, sample, value);

        previewWavePhase_ += phaseIncrement;
        while (previewWavePhase_ >= static_cast<float>(count))
            previewWavePhase_ -= static_cast<float>(count);
    }
}

void AudiocityAudioProcessor::renderSampleFilePreview(juce::AudioBuffer<float>& buffer) noexcept
{
    const auto count = samplePreviewSamples_.load(std::memory_order_relaxed);
    if (count <= 0)
    {
        buffer.clear();
        samplePreviewPlaying_.store(false, std::memory_order_relaxed);
        return;
    }

    const auto channels = buffer.getNumChannels();
    const auto samples = buffer.getNumSamples();
    const auto sourceRate = samplePreviewSourceRate_.load(std::memory_order_relaxed);
    const auto hostRate = juce::jmax(1.0, getSampleRate());
    const auto increment = static_cast<float>(sourceRate / hostRate);
    const auto masterVolume = juce::jlimit(0.0f, 1.0f,
        apvts_.getRawParameterValue(kParamMasterVolume)->load());

    for (int sample = 0; sample < samples; ++sample)
    {
        const auto i0 = static_cast<int>(samplePreviewReadPos_);
        if (i0 >= count)
        {
            for (int channel = 0; channel < channels; ++channel)
                buffer.setSample(channel, sample, 0.0f);
            continue;
        }

        const auto i1 = juce::jmin(count - 1, i0 + 1);
        const auto frac = samplePreviewReadPos_ - static_cast<float>(i0);
        const auto s0 = samplePreviewData_[static_cast<std::size_t>(i0)];
        const auto s1 = samplePreviewData_[static_cast<std::size_t>(i1)];
        const auto value = (s0 + (s1 - s0) * frac) * 0.35f * masterVolume;

        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample(channel, sample, value);

        samplePreviewReadPos_ += increment;
    }

    if (samplePreviewReadPos_ >= static_cast<float>(count))
        samplePreviewPlaying_.store(false, std::memory_order_relaxed);
}

void AudiocityAudioProcessor::applyOutputBoundarySmoothing(juce::AudioBuffer<float>& buffer) noexcept
{
    const auto channels = buffer.getNumChannels();
    const auto samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    constexpr int kRampSamples = 32;
    const auto smoothingSamples = juce::jmin(kRampSamples, samples);

    if (outputBoundaryHasLastSample_)
    {
        const auto smoothedChannels = juce::jmin(channels, static_cast<int>(outputBoundaryLastSample_.size()));
        constexpr float kDiscontinuityThreshold = 0.05f;
        for (int channel = 0; channel < smoothedChannels; ++channel)
        {
            auto* write = buffer.getWritePointer(channel);
            const auto delta = outputBoundaryLastSample_[static_cast<std::size_t>(channel)] - write[0];

            if (std::abs(delta) < kDiscontinuityThreshold)
                continue;

            for (int sample = 0; sample < smoothingSamples; ++sample)
            {
                const auto t = static_cast<float>(sample + 1) / static_cast<float>(smoothingSamples);
                write[sample] += (1.0f - t) * delta;
            }
        }
    }

    const auto trackedChannels = static_cast<int>(outputBoundaryLastSample_.size());
    for (int channel = 0; channel < trackedChannels; ++channel)
    {
        if (channel < channels)
            outputBoundaryLastSample_[static_cast<std::size_t>(channel)] = buffer.getSample(channel, samples - 1);
        else
            outputBoundaryLastSample_[static_cast<std::size_t>(channel)] = 0.0f;
    }

    outputBoundaryHasLastSample_ = true;
}

void AudiocityAudioProcessor::updateOutputPeakLevels(const juce::AudioBuffer<float>& buffer) noexcept
{
    const auto channels = buffer.getNumChannels();
    const auto samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    const auto leftPeak = buffer.getMagnitude(0, 0, samples);
    const auto rightPeak = channels > 1 ? buffer.getMagnitude(1, 0, samples) : leftPeak;

    auto accumulatePeak = [](std::atomic<float>& atomicPeak, const float candidate)
    {
        auto current = atomicPeak.load(std::memory_order_relaxed);
        while (candidate > current
            && !atomicPeak.compare_exchange_weak(current, candidate,
                std::memory_order_release, std::memory_order_relaxed))
        {
        }
    };

    accumulatePeak(outputPeakLeft_, leftPeak);
    accumulatePeak(outputPeakRight_, rightPeak);
}

void AudiocityAudioProcessor::updateVoicePlaybackPositionsFromEngine() noexcept
{
    const auto voiceStates = engine_.getVoicePlaybackStates();
    for (std::size_t voiceIndex = 0; voiceIndex < voiceStates.size(); ++voiceIndex)
    {
        const auto& state = voiceStates[voiceIndex];
        voicePlaybackPositions_[voiceIndex].store(state.active ? state.sampleIndex : -1,
            std::memory_order_relaxed);
    }
}

void AudiocityAudioProcessor::clearVoicePlaybackPositions() noexcept
{
    for (auto& position : voicePlaybackPositions_)
        position.store(-1, std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudiocityAudioProcessor();
}
