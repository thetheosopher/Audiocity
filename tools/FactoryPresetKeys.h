// Audiocity factory-preset state keys.
//
// These constants must stay in sync with the anonymous-namespace key
// strings in src/plugin/PluginProcessor.cpp. They describe the property
// names used inside the patch ValueTree that is serialized as XML inside
// .acp files. The shared header is used by:
//   - tools/PresetAuthor.cpp     (writes presets)
//   - tools/PresetAuditioner.cpp (reads + renders presets offline)
//
// Real-time safety: header-only string constants. Not used from the
// audio thread.

#pragma once

namespace audiocity::factory_keys
{
inline constexpr auto kPatchRoot = "AudiocityPatch";

inline constexpr auto kEmbeddedSampleData = "embeddedSampleData";
inline constexpr auto kEmbeddedSampleRate = "embeddedSampleRate";
inline constexpr auto kEmbeddedSampleRootMidiNote = "embeddedSampleRootMidiNote";
inline constexpr auto kEmbeddedSampleName = "embeddedSampleName";
inline constexpr auto kEmbeddedSampleChannels = "embeddedSampleChannels";

inline constexpr auto kRootMidiNote = "rootMidiNote";
inline constexpr auto kCoarseTuneSemitones = "coarseTuneSemitones";
inline constexpr auto kFineTuneCents = "fineTuneCents";
inline constexpr auto kPitchBendRangeSemitones = "pitchBendRangeSemitones";
inline constexpr auto kPitchLfoRate = "pitchLfoRate";
inline constexpr auto kPitchLfoDepth = "pitchLfoDepth";

inline constexpr auto kModWheelToPitch = "modWheelToPitch";
inline constexpr auto kModWheelToFilter = "modWheelToFilter";
inline constexpr auto kModWheelToAmp = "modWheelToAmp";
inline constexpr auto kAftertouchToPitch = "aftertouchToPitch";
inline constexpr auto kAftertouchToFilter = "aftertouchToFilter";
inline constexpr auto kAftertouchToAmp = "aftertouchToAmp";
inline constexpr auto kVelocityToPitch = "velocityToPitch";
inline constexpr auto kVelocityToFilter = "velocityToFilter";
inline constexpr auto kVelocityToAmp = "velocityToAmp";

inline constexpr auto kAmpAttack = "ampAttack";
inline constexpr auto kAmpDecay = "ampDecay";
inline constexpr auto kAmpSustain = "ampSustain";
inline constexpr auto kAmpRelease = "ampRelease";
inline constexpr auto kAmpLfoRate = "ampLfoRate";
inline constexpr auto kAmpLfoDepth = "ampLfoDepth";
inline constexpr auto kAmpLfoShape = "ampLfoShape";

inline constexpr auto kFilterAttack = "filterAttack";
inline constexpr auto kFilterDecay = "filterDecay";
inline constexpr auto kFilterSustain = "filterSustain";
inline constexpr auto kFilterRelease = "filterRelease";
inline constexpr auto kFilterBaseCutoff = "filterBaseCutoff";
inline constexpr auto kFilterEnvAmount = "filterEnvAmount";
inline constexpr auto kFilterResonance = "filterResonance";
inline constexpr auto kFilterMode = "filterMode";
inline constexpr auto kFilterKeyTracking = "filterKeyTracking";
inline constexpr auto kFilterVelocityAmount = "filterVelocityAmount";
inline constexpr auto kFilterLfoRate = "filterLfoRate";
inline constexpr auto kFilterLfoRateKeytrack = "filterLfoRateKeytrack";
inline constexpr auto kFilterLfoAmount = "filterLfoAmount";
inline constexpr auto kFilterLfoAmountKeytrack = "filterLfoAmountKeytrack";
inline constexpr auto kFilterLfoStartPhase = "filterLfoStartPhase";
inline constexpr auto kFilterLfoStartPhaseRandom = "filterLfoStartPhaseRandom";
inline constexpr auto kFilterLfoFadeIn = "filterLfoFadeIn";
inline constexpr auto kFilterLfoShape = "filterLfoShape";
inline constexpr auto kFilterLfoRetrigger = "filterLfoRetrigger";
inline constexpr auto kFilterLfoTempoSync = "filterLfoTempoSync";
inline constexpr auto kFilterLfoRateKeytrackInTempoSync = "filterLfoRateKeytrackInTempoSync";
inline constexpr auto kFilterLfoKeytrackLinear = "filterLfoKeytrackLinear";
inline constexpr auto kFilterLfoUnipolar = "filterLfoUnipolar";
inline constexpr auto kFilterLfoSyncDivision = "filterLfoSyncDivision";

inline constexpr auto kPlaybackMode = "playbackMode";
inline constexpr auto kQualityTier = "qualityTier";
inline constexpr auto kVelocityCurve = "velocityCurve";
inline constexpr auto kReverbMix = "reverbMix";
inline constexpr auto kDelayMix = "delayMix";
inline constexpr auto kDelayTimeMs = "delayTimeMs";
inline constexpr auto kDelayFeedback = "delayFeedback";
inline constexpr auto kDelayTempoSync = "delayTempoSync";
inline constexpr auto kDcFilterEnabled = "dcFilterEnabled";
inline constexpr auto kDcFilterCutoffHz = "dcFilterCutoffHz";
inline constexpr auto kAutopanRateHz = "autopanRateHz";
inline constexpr auto kAutopanDepth = "autopanDepth";
inline constexpr auto kSaturationDrive = "saturationDrive";
inline constexpr auto kSaturationMode = "saturationMode";
inline constexpr auto kPan = "pan";
inline constexpr auto kMasterVolume = "masterVolume";
inline constexpr auto kMonoMode = "monoMode";
inline constexpr auto kLegatoMode = "legatoMode";
inline constexpr auto kGlideSeconds = "glideSeconds";
inline constexpr auto kPolyphonyLimit = "polyphonyLimit";

inline constexpr auto kLoopStart = "loopStart";
inline constexpr auto kLoopEnd = "loopEnd";
inline constexpr auto kLoopCrossfadeSamples = "loopCrossfadeSamples";
inline constexpr auto kFadeInSamples = "fadeInSamples";
inline constexpr auto kFadeOutSamples = "fadeOutSamples";
inline constexpr auto kReversePlayback = "reversePlayback";

inline constexpr auto kMacro1Value = "macro1Value";
inline constexpr auto kMacro2Value = "macro2Value";
inline constexpr auto kMacro1ToPitch = "macro1ToPitch";
inline constexpr auto kMacro1ToFilter = "macro1ToFilter";
inline constexpr auto kMacro1ToAmp = "macro1ToAmp";
inline constexpr auto kMacro2ToPitch = "macro2ToPitch";
inline constexpr auto kMacro2ToFilter = "macro2ToFilter";
inline constexpr auto kMacro2ToAmp = "macro2ToAmp";
}
