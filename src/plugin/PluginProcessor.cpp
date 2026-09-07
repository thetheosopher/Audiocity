#include "PluginProcessor.h"

#include "AudioStateCodec.h"
#include "ImportedProgramState.h"
#include "PluginEditor.h"
#include "PresetJson.h"
#include "../engine/AudioFileSupport.h"
#include "../engine/ImportCancellation.h"
#include "../engine/LegacyNkiProbe.h"
#include "../engine/RexSliceProgram.h"
#include "../engine/SfzImporter.h"
#include "../engine/SfzExporter.h"
#include "../engine/Sf2Importer.h"
#include "../engine/DecentSamplerImporter.h"
#include "../engine/DecentSamplerExporter.h"
#include "../engine/BitwigMultisampleImporter.h"
#include "../engine/XmlMultisampleImporters.h"
#include "../engine/BinaryMultisampleImporters.h"
#include "../engine/TransientSliceProgram.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <thread>

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
constexpr auto kPatchRoot = "AudiocityPatch";
constexpr auto kSamplePath = "samplePath";
constexpr auto kSfzMappingEdits = "sfzMappingEdits";
constexpr auto kSfzMappingZone = "zone";
constexpr auto kZoneIndex = "zoneIndex";
constexpr auto kKeyLow = "keyLow";
constexpr auto kKeyHigh = "keyHigh";
constexpr auto kVelocityLow = "velocityLow";
constexpr auto kVelocityHigh = "velocityHigh";
constexpr auto kZoneRootMidiNote = "rootMidiNote";
constexpr auto kZoneSampleStart = "sampleStart";
constexpr auto kZoneSampleEnd = "sampleEnd";
constexpr auto kZoneLoopStart = "zoneLoopStart";
constexpr auto kZoneLoopEnd = "zoneLoopEnd";
constexpr auto kZoneGainDb = "gainDb";
constexpr auto kZonePan = "zonePan";
constexpr auto kZoneRoundRobinGroup = "roundRobinGroup";
constexpr auto kZoneRoundRobinPosition = "roundRobinPosition";
constexpr auto kZoneChokeGroup = "chokeGroup";
constexpr auto kZoneTriggerMode = "triggerMode";
constexpr auto kZoneLoopMode = "loopMode";
constexpr auto kGeneratedWaveformData = "generatedWaveformData";
constexpr auto kGeneratedWaveformAsset = "generatedWaveformAssetV1";
constexpr auto kCapturedSampleData = "capturedSampleData";
constexpr auto kCapturedSampleAsset = "capturedSampleAssetV1";
constexpr auto kCapturedSampleRate = "capturedSampleRate";
constexpr auto kEmbeddedSampleData = "embeddedSampleData";
constexpr auto kEmbeddedSampleAsset = "embeddedSampleAssetV1";
constexpr auto kEmbeddedSampleRate = "embeddedSampleRate";
constexpr auto kEmbeddedSampleRootMidiNote = "embeddedSampleRootMidiNote";
constexpr auto kEmbeddedSampleName = "embeddedSampleName";
constexpr auto kEmbeddedSampleChannels = "embeddedSampleChannels";
constexpr auto kAudioStateDecodedBytes = "audioStateDecodedBytes";
constexpr auto kAudioStateSizeWarning = "audioStateSizeWarning";
constexpr auto kSampleBrowserRootFolder = "sampleBrowserRootFolder";
constexpr auto kRootMidiNote = "rootMidiNote";
constexpr auto kCoarseTuneSemitones = "coarseTuneSemitones";
constexpr auto kFineTuneCents = "fineTuneCents";
constexpr auto kPitchBendRangeSemitones = "pitchBendRangeSemitones";
constexpr auto kPitchLfoRate = "pitchLfoRate";
constexpr auto kPitchLfoDepth = "pitchLfoDepth";
constexpr auto kModWheelToPitch = "modWheelToPitch";
constexpr auto kModWheelToFilter = "modWheelToFilter";
constexpr auto kModWheelToAmp = "modWheelToAmp";
constexpr auto kAftertouchToPitch = "aftertouchToPitch";
constexpr auto kAftertouchToFilter = "aftertouchToFilter";
constexpr auto kAftertouchToAmp = "aftertouchToAmp";
constexpr auto kVelocityToPitch = "velocityToPitch";
constexpr auto kVelocityToFilter = "velocityToFilter";
constexpr auto kVelocityToAmp = "velocityToAmp";
constexpr auto kMacro1Value = "macro1Value";
constexpr auto kMacro2Value = "macro2Value";
constexpr auto kMacro1ToPitch = "macro1ToPitch";
constexpr auto kMacro1ToFilter = "macro1ToFilter";
constexpr auto kMacro1ToAmp = "macro1ToAmp";
constexpr auto kMacro2ToPitch = "macro2ToPitch";
constexpr auto kMacro2ToFilter = "macro2ToFilter";
constexpr auto kMacro2ToAmp = "macro2ToAmp";

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
constexpr auto kSampleInspectorFilterModExpanded = "sampleInspectorFilterModExpanded";
constexpr auto kSampleInspectorEffectsExpanded = "sampleInspectorEffectsExpanded";
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
constexpr auto kParamModWheelToPitch = "p_modWheelPitch";
constexpr auto kParamModWheelToFilter = "p_modWheelFilter";
constexpr auto kParamModWheelToAmp = "p_modWheelAmp";
constexpr auto kParamAftertouchToPitch = "p_aftertouchPitch";
constexpr auto kParamAftertouchToFilter = "p_aftertouchFilter";
constexpr auto kParamAftertouchToAmp = "p_aftertouchAmp";
constexpr auto kParamVelocityToPitch = "p_velocityPitch";
constexpr auto kParamVelocityToFilter = "p_velocityFilter";
constexpr auto kParamVelocityToAmp = "p_velocityAmp";
constexpr auto kParamMacro1Value = "p_macro1Value";
constexpr auto kParamMacro2Value = "p_macro2Value";
constexpr auto kParamMacro1ToPitch = "p_macro1Pitch";
constexpr auto kParamMacro1ToFilter = "p_macro1Filter";
constexpr auto kParamMacro1ToAmp = "p_macro1Amp";
constexpr auto kParamMacro2ToPitch = "p_macro2Pitch";
constexpr auto kParamMacro2ToFilter = "p_macro2Filter";
constexpr auto kParamMacro2ToAmp = "p_macro2Amp";
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

juce::String formatImportedProgramSampleAssetName(const audiocity::engine::SampleAsset& asset, const int sampleAssetIndex)
{
    if (!asset.displayName.empty())
        return juce::String::fromUTF8(asset.displayName.c_str());

    if (!asset.sourcePath.empty())
        return juce::File(juce::String::fromUTF8(asset.sourcePath.c_str())).getFileName();

    return "sample " + juce::String(sampleAssetIndex + 1);
}

bool isPlaybackPresetExcludedProperty(const juce::Identifier& property)
{
    const auto propertyName = property.toString();
    return propertyName == kSampleBrowserRootFolder
        || propertyName == kWaveformViewStartSample
        || propertyName == kWaveformViewSampleCount
        || propertyName == kEditorTabIndex
        || propertyName == kSampleInspectorFilterModExpanded
        || propertyName == kSampleInspectorEffectsExpanded
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

bool containsOnlyFiniteFloatSamples(const void* const data, const std::size_t byteCount) noexcept
{
    if (data == nullptr || byteCount == 0 || (byteCount % sizeof(float)) != 0)
        return false;

    const auto* source = static_cast<const std::uint8_t*>(data);
    for (std::size_t offset = 0; offset < byteCount; offset += sizeof(float))
    {
        float sample = 0.0f;
        std::memcpy(&sample, source + offset, sizeof(sample));
        if (!std::isfinite(sample))
            return false;
    }

    return true;
}

bool deserializeAudioBufferSamples(const juce::MemoryBlock& bytes,
                                   const int channels,
                                   juce::AudioBuffer<float>& buffer,
                                   juce::String* const errorMessage = nullptr)
{
    if (errorMessage != nullptr)
        errorMessage->clear();

    const auto safeChannels = juce::jmax(1, channels);
    const auto totalBytes = bytes.getSize();
    if (safeChannels > audiocity::plugin::AudioStateCodecLimits::maximumChannels
        || totalBytes > audiocity::plugin::AudioStateCodecLimits::maximumDecodedBytes)
    {
        if (errorMessage != nullptr)
            *errorMessage = "Legacy audio state exceeds the 64 MiB/8-channel limit";
        return false;
    }
    const auto channelStrideBytes = static_cast<std::size_t>(safeChannels) * sizeof(float);
    if (totalBytes < channelStrideBytes || (totalBytes % channelStrideBytes) != 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = "Legacy audio state has an invalid planar-float byte count";
        return false;
    }

    if (!containsOnlyFiniteFloatSamples(bytes.getData(), totalBytes))
    {
        if (errorMessage != nullptr)
            *errorMessage = "Legacy audio state contains a non-finite sample";
        return false;
    }

    const auto samples = static_cast<int>(totalBytes / sizeof(float) / static_cast<std::size_t>(safeChannels));
    if (samples <= 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = "Legacy audio state contains no complete samples";
        return false;
    }

    juce::AudioBuffer<float> restored(safeChannels, samples);
    const auto* src = static_cast<const float*>(bytes.getData());
    for (int channel = 0; channel < safeChannels; ++channel)
    {
        std::memcpy(restored.getWritePointer(channel),
                    src + static_cast<std::size_t>(channel) * static_cast<std::size_t>(samples),
                    static_cast<std::size_t>(samples) * sizeof(float));
    }

    buffer = std::move(restored);
    return true;
}

bool writeEmbeddedSampleState(juce::ValueTree& state,
                              const juce::AudioBuffer<float>& buffer,
                              const double sampleRate,
                              const int rootMidiNote,
                              const juce::String& sampleName,
                              juce::String* const errorMessage = nullptr)
{
    if (errorMessage != nullptr)
        errorMessage->clear();

    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
    {
        const auto error = juce::String("Embedded audio state is empty");
        state.setProperty(kAudioStateSizeWarning, error, nullptr);
        if (errorMessage != nullptr)
            *errorMessage = error;
        return false;
    }

    juce::String codecError;
    const auto encoded = audiocity::plugin::encodeAudioStateAsset(buffer, &codecError);
    if (encoded.isEmpty())
    {
        if (codecError.isEmpty())
            codecError = "Embedded audio state serialization failed";
        state.setProperty(kAudioStateSizeWarning, codecError, nullptr);
        if (errorMessage != nullptr)
            *errorMessage = codecError;
        return false;
    }

    state.setProperty(kEmbeddedSampleAsset, juce::var(encoded), nullptr);
    state.setProperty(kEmbeddedSampleRate, juce::jmax(1.0, sampleRate), nullptr);
    state.setProperty(kEmbeddedSampleRootMidiNote, juce::jlimit(0, 127, rootMidiNote), nullptr);
    state.setProperty(kEmbeddedSampleChannels, buffer.getNumChannels(), nullptr);

    if (sampleName.isNotEmpty())
        state.setProperty(kEmbeddedSampleName, sampleName, nullptr);
    else
        state.removeProperty(kEmbeddedSampleName, nullptr);

    return true;
}

juce::ValueTree buildPlaybackPresetStateTree(const juce::ValueTree& fullState)
{
    auto presetState = fullState.createCopy();

    for (int childIndex = presetState.getNumChildren() - 1; childIndex >= 0; --childIndex)
    {
        if (presetState.getChild(childIndex).hasType(audiocity::plugin::LibraryMetadata::valueTreeType()))
            presetState.removeChild(childIndex, nullptr);
    }

    for (int propertyIndex = presetState.getNumProperties() - 1; propertyIndex >= 0; --propertyIndex)
    {
        const auto propertyName = presetState.getPropertyName(propertyIndex);
        if (isPlaybackPresetExcludedProperty(propertyName))
            presetState.removeProperty(propertyName, nullptr);
    }

    return presetState;
}

juce::String makeDiagnosticText(const audiocity::engine::SfzDiagnostic& diagnostic)
{
    auto text = juce::String::fromUTF8(diagnostic.message.c_str());
    if (diagnostic.line > 0)
        text += " (line " + juce::String(diagnostic.line) + ")";
    return text;
}

juce::String makeSfzImportSummary(const audiocity::engine::SfzImportResult& result, const bool imported)
{
    int warnings = 0;
    int errors = 0;
    for (const auto& diagnostic : result.diagnostics)
    {
        if (diagnostic.severity == audiocity::engine::SfzDiagnostic::Severity::error)
            ++errors;
        else
            ++warnings;
    }

    juce::String summary = imported
        ? ("SFZ imported: " + juce::String(static_cast<int>(result.program.zones.size()))
            + " zones, " + juce::String(static_cast<int>(result.program.sampleAssets.size())) + " samples")
        : juce::String("SFZ import failed");

    if (errors > 0 || warnings > 0)
    {
        summary += " (";
        if (errors > 0)
            summary += juce::String(errors) + " errors";
        if (errors > 0 && warnings > 0)
            summary += ", ";
        if (warnings > 0)
            summary += juce::String(warnings) + " warnings";
        summary += ")";
    }

    if (!result.diagnostics.empty())
    {
        const auto& diagnostic = result.diagnostics.front();
        summary += " | ";
        summary += diagnostic.severity == audiocity::engine::SfzDiagnostic::Severity::error ? "Error: " : "Warning: ";
        summary += makeDiagnosticText(diagnostic);
    }

    return summary;
}

juce::String makeLegacyNkiImportSummary(const audiocity::engine::nki::ImportResult& result, const bool imported)
{
    int warnings = 0;
    int errors = 0;
    for (const auto& diagnostic : result.probe.diagnostics)
    {
        if (diagnostic.severity == audiocity::engine::nki::DiagnosticSeverity::error)
            ++errors;
        else if (diagnostic.severity == audiocity::engine::nki::DiagnosticSeverity::warning)
            ++warnings;
    }

    juce::String summary = imported
        ? ("NKI imported: " + juce::String(static_cast<int>(result.program.zones.size()))
            + " zones, " + juce::String(static_cast<int>(result.program.sampleAssets.size())) + " samples")
        : juce::String("NKI import failed");

    if (errors > 0 || warnings > 0)
    {
        summary += " (";
        if (errors > 0)
            summary += juce::String(errors) + " errors";
        if (errors > 0 && warnings > 0)
            summary += ", ";
        if (warnings > 0)
            summary += juce::String(warnings) + " warnings";
        summary += ")";
    }

    if (!result.probe.diagnostics.empty())
        summary += " | " + result.probe.diagnostics.front().message;

    return summary;
}

juce::String makeRexSliceImportSummary(const audiocity::engine::rex::ChromaticSliceProgram& sliceProgram)
{
    auto summary = "REX slices imported: "
        + juce::String(static_cast<int>(sliceProgram.program.zones.size()))
        + " zones, mapped chromatically from MIDI "
        + juce::String(sliceProgram.baseMidiNote);

    if (sliceProgram.truncated)
        summary += " (truncated at MIDI 127)";

    return summary;
}

juce::String makeTransientSliceImportSummary(
    const audiocity::engine::transient_slice::TransientSliceProgram& sliceProgram)
{
    auto summary = "Transient slices imported: "
        + juce::String(static_cast<int>(sliceProgram.program.zones.size()))
        + " zones, mapped chromatically from MIDI "
        + juce::String(sliceProgram.baseMidiNote);

    if (sliceProgram.truncated)
        summary += " (truncated at MIDI 127)";

    return summary;
}

bool readAudioFileToBuffer(const juce::File& file,
                           juce::AudioBuffer<float>& buffer,
                           double& sampleRateHz)
{
    if (!file.existsAsFile())
        return false;

    juce::AudioFormatManager formatManager;
    audiocity::engine::audio_file::registerAudioFormats(formatManager);

    auto openResult = audiocity::engine::audio_file::openReaderForFile(formatManager, file);
    auto reader = std::move(openResult.reader);
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const auto totalSamples = static_cast<int>(std::min<long long>(
        reader->lengthInSamples,
        static_cast<long long>(std::numeric_limits<int>::max())));
    if (totalSamples <= 0)
        return false;

    buffer.setSize(juce::jmax(1, static_cast<int>(reader->numChannels)), totalSamples, false, true, true);
    if (!audiocity::engine::readAudioInCancellableChunks(*reader, buffer, totalSamples))
        return false;

    sampleRateHz = juce::jmax(1.0, reader->sampleRate);
    return true;
}

template <typename ResultT>
AudiocityAudioProcessor::PreparedBackgroundImport prepareBackgroundImportedProgramResult(
    ResultT&& result,
    juce::String summary,
    const audiocity::plugin::ImportedProgramFormat format,
    const juce::String& failurePrefix,
    const int selectionIndex = -1)
{
    AudiocityAudioProcessor::PreparedBackgroundImport prepared;
    prepared.format = format;
    prepared.selectionIndex = selectionIndex;
    prepared.importedProgram = true;

    if (audiocity::engine::isImportCancellationRequested())
    {
        prepared.diagnosticSummary = "Import cancelled";
        return prepared;
    }

    const auto hasPlayable = result.hasPlayableProgram();
    const auto imported = !result.hasErrors() && hasPlayable;
    if (!hasPlayable && !result.hasErrors())
        summary = failurePrefix + " import failed: no playable zones";

    if (!imported)
    {
        prepared.diagnosticSummary = std::move(summary);
        return prepared;
    }

    for (std::size_t i = 0; i < result.sampleDataByAsset.size(); ++i)
    {
        if (result.sampleDataByAsset[i].getNumChannels() > 0
            && result.sampleDataByAsset[i].getNumSamples() > 0)
        {
            prepared.displayAssetIndex = static_cast<int>(i);
            break;
        }
    }

    if (prepared.displayAssetIndex < 0)
    {
        prepared.diagnosticSummary = failurePrefix + " import failed: decoded samples were empty";
        return prepared;
    }

    prepared.program = std::move(result.program);
    prepared.sampleData = std::move(result.sampleDataByAsset);
    prepared.diagnosticSummary = std::move(summary);
    prepared.ok = true;
    return prepared;
}

using ModulationRoute = audiocity::engine::EngineCore::ModulationRoute;
using ModulationRoutingSettings = audiocity::engine::EngineCore::ModulationRoutingSettings;
using MacroControlValues = audiocity::engine::EngineCore::MacroControlValues;

enum class ModulationRouteSlot
{
    modWheel,
    aftertouch,
    velocity,
    macro1,
    macro2
};

struct ModulationRouteDescriptor
{
    ModulationRouteSlot slot;
    const char* displayName;
    const char* pitchParameterId;
    const char* filterParameterId;
    const char* ampParameterId;
    const char* pitchProperty;
    const char* filterProperty;
    const char* ampProperty;
};

struct MacroControlDescriptor
{
    std::size_t index;
    const char* displayName;
    const char* parameterId;
    const char* property;
};

constexpr std::array<MacroControlDescriptor, audiocity::engine::EngineCore::kMacroControlCount> kMacroControlDescriptors{{
    { 0, "Macro 1", kParamMacro1Value, kMacro1Value },
    { 1, "Macro 2", kParamMacro2Value, kMacro2Value }
}};

constexpr std::array<ModulationRouteDescriptor, 5> kModulationRouteDescriptors{{
    { ModulationRouteSlot::modWheel, "Mod Wheel", kParamModWheelToPitch, kParamModWheelToFilter, kParamModWheelToAmp,
        kModWheelToPitch, kModWheelToFilter, kModWheelToAmp },
    { ModulationRouteSlot::aftertouch, "Aftertouch", kParamAftertouchToPitch, kParamAftertouchToFilter, kParamAftertouchToAmp,
        kAftertouchToPitch, kAftertouchToFilter, kAftertouchToAmp },
    { ModulationRouteSlot::velocity, "Velocity", kParamVelocityToPitch, kParamVelocityToFilter, kParamVelocityToAmp,
        kVelocityToPitch, kVelocityToFilter, kVelocityToAmp },
    { ModulationRouteSlot::macro1, "Macro 1", kParamMacro1ToPitch, kParamMacro1ToFilter, kParamMacro1ToAmp,
        kMacro1ToPitch, kMacro1ToFilter, kMacro1ToAmp },
    { ModulationRouteSlot::macro2, "Macro 2", kParamMacro2ToPitch, kParamMacro2ToFilter, kParamMacro2ToAmp,
        kMacro2ToPitch, kMacro2ToFilter, kMacro2ToAmp }
}};

ModulationRoute& modulationRouteForSlot(ModulationRoutingSettings& settings, const ModulationRouteSlot slot) noexcept
{
    switch (slot)
    {
        case ModulationRouteSlot::modWheel: return settings.modWheel;
        case ModulationRouteSlot::aftertouch: return settings.aftertouch;
        case ModulationRouteSlot::velocity: return settings.velocity;
        case ModulationRouteSlot::macro1: return settings.macros[0];
        case ModulationRouteSlot::macro2: return settings.macros[1];
    }

    return settings.modWheel;
}

const ModulationRoute& modulationRouteForSlot(const ModulationRoutingSettings& settings, const ModulationRouteSlot slot) noexcept
{
    switch (slot)
    {
        case ModulationRouteSlot::modWheel: return settings.modWheel;
        case ModulationRouteSlot::aftertouch: return settings.aftertouch;
        case ModulationRouteSlot::velocity: return settings.velocity;
        case ModulationRouteSlot::macro1: return settings.macros[0];
        case ModulationRouteSlot::macro2: return settings.macros[1];
    }

    return settings.modWheel;
}

const MacroControlDescriptor* macroControlDescriptorForSlot(const ModulationRouteSlot slot) noexcept
{
    switch (slot)
    {
        case ModulationRouteSlot::macro1: return &kMacroControlDescriptors[0];
        case ModulationRouteSlot::macro2: return &kMacroControlDescriptors[1];
        default: return nullptr;
    }
}

void addMacroControlParameter(std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                              const MacroControlDescriptor& descriptor)
{
    params.push_back(std::make_unique<juce::AudioParameterFloat>(descriptor.parameterId, descriptor.displayName,
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
}

void addModulationParameters(std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params)
{
    const juce::NormalisableRange<float> pitchRange(-1200.0f, 1200.0f, 1.0f);
    const juce::NormalisableRange<float> filterRange(-20000.0f, 20000.0f, 0.01f, 0.35f);
    const juce::NormalisableRange<float> ampRange(-1.0f, 1.0f);

    for (const auto& descriptor : kModulationRouteDescriptors)
    {
        if (const auto* macroDescriptor = macroControlDescriptorForSlot(descriptor.slot))
            addMacroControlParameter(params, *macroDescriptor);

        const juce::String sourceName(descriptor.displayName);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(descriptor.pitchParameterId, sourceName + " To Pitch",
            pitchRange, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(descriptor.filterParameterId, sourceName + " To Filter",
            filterRange, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(descriptor.ampParameterId, sourceName + " To Amp",
            ampRange, 0.0f));
    }
}

void loadModulationRoutesFromParameters(const juce::AudioProcessorValueTreeState& apvts,
                                        ModulationRoutingSettings& settings) noexcept
{
    for (const auto& descriptor : kModulationRouteDescriptors)
    {
        auto& route = modulationRouteForSlot(settings, descriptor.slot);
        route.toPitchCents = apvts.getRawParameterValue(descriptor.pitchParameterId)->load();
        route.toFilterHz = apvts.getRawParameterValue(descriptor.filterParameterId)->load();
        route.toAmp = apvts.getRawParameterValue(descriptor.ampParameterId)->load();
    }
}

void loadMacroControlsFromParameters(const juce::AudioProcessorValueTreeState& apvts,
                                     MacroControlValues& values) noexcept
{
    for (const auto& descriptor : kMacroControlDescriptors)
        values[descriptor.index] = apvts.getRawParameterValue(descriptor.parameterId)->load();
}

void storeModulationRoutesToState(juce::ValueTree& state, const ModulationRoutingSettings& settings)
{
    for (const auto& descriptor : kModulationRouteDescriptors)
    {
        const auto& route = modulationRouteForSlot(settings, descriptor.slot);
        state.setProperty(descriptor.pitchProperty, route.toPitchCents, nullptr);
        state.setProperty(descriptor.filterProperty, route.toFilterHz, nullptr);
        state.setProperty(descriptor.ampProperty, route.toAmp, nullptr);
    }
}

void storeMacroControlsToState(juce::ValueTree& state, const MacroControlValues& values)
{
    for (const auto& descriptor : kMacroControlDescriptors)
        state.setProperty(descriptor.property, values[descriptor.index], nullptr);
}

void restoreModulationRoutesFromState(const juce::ValueTree& state, ModulationRoutingSettings& settings)
{
    for (const auto& descriptor : kModulationRouteDescriptors)
    {
        auto& route = modulationRouteForSlot(settings, descriptor.slot);
        route.toPitchCents = static_cast<float>(state.getProperty(descriptor.pitchProperty, route.toPitchCents));
        route.toFilterHz = static_cast<float>(state.getProperty(descriptor.filterProperty, route.toFilterHz));
        route.toAmp = static_cast<float>(state.getProperty(descriptor.ampProperty, route.toAmp));
    }
}

void restoreMacroControlsFromState(const juce::ValueTree& state, MacroControlValues& values)
{
    for (const auto& descriptor : kMacroControlDescriptors)
        values[descriptor.index] = static_cast<float>(state.getProperty(descriptor.property, values[descriptor.index]));
}

template <typename UpdatePlainValue>
void updateModulationRouteParameters(const ModulationRoutingSettings& settings, UpdatePlainValue&& updatePlainValue)
{
    for (const auto& descriptor : kModulationRouteDescriptors)
    {
        const auto& route = modulationRouteForSlot(settings, descriptor.slot);
        updatePlainValue(descriptor.pitchParameterId, route.toPitchCents);
        updatePlainValue(descriptor.filterParameterId, route.toFilterHz);
        updatePlainValue(descriptor.ampParameterId, route.toAmp);
    }
}

template <typename UpdatePlainValue>
void updateMacroControlParameters(const MacroControlValues& values, UpdatePlainValue&& updatePlainValue)
{
    for (const auto& descriptor : kMacroControlDescriptors)
        updatePlainValue(descriptor.parameterId, values[descriptor.index]);
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

AudiocityAudioProcessor::~AudiocityAudioProcessor()
{
    captureRecording_.store(false, std::memory_order_release);
    waitForCaptureAudioReaders();
    releaseCaptureWorkingStorage();
    samplePreviewSnapshot_.publish(nullptr);
    stopStreamPrimeWorker();
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
    addModulationParameters(params);
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
    }
}

float AudiocityAudioProcessor::getParameterPlainValue(const char* const parameterId) const noexcept
{
    if (const auto* value = apvts_.getRawParameterValue(parameterId))
        return value->load(std::memory_order_relaxed);
    return 0.0f;
}

AudiocityAudioProcessor::EngineControlSnapshot AudiocityAudioProcessor::loadEngineControlSnapshot() const noexcept
{
    EngineControlSnapshot controls;
    controls.hostTempoBpm = hostBpm_.load(std::memory_order_relaxed);

    controls.ampEnvelope.attackSeconds = getParameterPlainValue(kParamAmpAttack);
    controls.ampEnvelope.decaySeconds = getParameterPlainValue(kParamAmpDecay);
    controls.ampEnvelope.sustainLevel = getParameterPlainValue(kParamAmpSustain);
    controls.ampEnvelope.releaseSeconds = getParameterPlainValue(kParamAmpRelease);

    controls.ampLfo.rateHz = getParameterPlainValue(kParamAmpLfoRate);
    controls.ampLfo.depth = getParameterPlainValue(kParamAmpLfoDepth);
    controls.ampLfo.shape = static_cast<FilterSettings::LfoShape>(juce::jlimit(0, 4,
        static_cast<int>(std::round(getParameterPlainValue(kParamAmpLfoShape)))));

    controls.filter.baseCutoffHz = getParameterPlainValue(kParamFilterCutoff);
    controls.filter.resonance = getParameterPlainValue(kParamFilterRes);
    controls.filter.envAmountHz = getParameterPlainValue(kParamFilterEnvAmt);
    controls.filter.mode = static_cast<FilterSettings::Mode>(juce::jlimit(0, 5,
        static_cast<int>(std::round(getParameterPlainValue(kParamFilterMode)))));
    controls.filter.keyTracking = getParameterPlainValue(kParamFilterKeytrack);
    controls.filter.velocityAmountHz = getParameterPlainValue(kParamFilterVel);
    controls.filter.lfoRateHz = getParameterPlainValue(kParamFilterLfoRate);
    controls.filter.lfoRateKeyTracking = getParameterPlainValue(kParamFilterLfoRateKeytrack);
    controls.filter.lfoAmountHz = getParameterPlainValue(kParamFilterLfoAmount);
    controls.filter.lfoAmountKeyTracking = getParameterPlainValue(kParamFilterLfoAmountKeytrack);
    controls.filter.lfoStartPhaseDegrees = getParameterPlainValue(kParamFilterLfoStartPhase);
    controls.filter.lfoStartPhaseRandomDegrees = getParameterPlainValue(kParamFilterLfoStartPhaseRandom);
    controls.filter.lfoFadeInMs = getParameterPlainValue(kParamFilterLfoFadeIn);
    controls.filter.lfoShape = static_cast<FilterSettings::LfoShape>(juce::jlimit(0, 4,
        static_cast<int>(std::round(getParameterPlainValue(kParamFilterLfoShape)))));
    controls.filter.lfoRetrigger = getParameterPlainValue(kParamFilterLfoRetrigger) >= 0.5f;
    controls.filter.lfoTempoSync = getParameterPlainValue(kParamFilterLfoTempoSync) >= 0.5f;
    controls.filter.lfoRateKeytrackInTempoSync = getParameterPlainValue(kParamFilterLfoRateKeytrackInTempoSync) >= 0.5f;
    controls.filter.lfoKeytrackLinear = getParameterPlainValue(kParamFilterLfoKeytrackLinear) >= 0.5f;
    controls.filter.lfoUnipolar = getParameterPlainValue(kParamFilterLfoUnipolar) >= 0.5f;
    controls.filter.lfoSyncDivision = juce::jlimit(0, 11,
        static_cast<int>(std::round(getParameterPlainValue(kParamFilterLfoSyncDivision))));
    if (controls.filter.lfoTempoSync)
        controls.filter.lfoRateHz = lfoRateHzFromTempoSync(controls.filter.lfoSyncDivision);

    controls.filterEnvelope.attackSeconds = getParameterPlainValue(kParamFilterAttack);
    controls.filterEnvelope.decaySeconds = getParameterPlainValue(kParamFilterDecay);
    controls.filterEnvelope.sustainLevel = getParameterPlainValue(kParamFilterSustain);
    controls.filterEnvelope.releaseSeconds = getParameterPlainValue(kParamFilterRelease);

    controls.playbackMode = static_cast<PlaybackMode>(juce::jlimit(0, 2,
        static_cast<int>(std::round(getParameterPlainValue(kParamPlaybackMode)))));
    controls.monoMode = getParameterPlainValue(kParamMonoMode) >= 0.5f;
    controls.legatoMode = getParameterPlainValue(kParamLegatoMode) >= 0.5f;
    controls.glideSeconds = getParameterPlainValue(kParamGlideSeconds);
    controls.polyphonyLimit = static_cast<int>(std::round(getParameterPlainValue(kParamPolyphonyLimit)));
    controls.fadeInSamples = static_cast<int>(std::round(getParameterPlainValue(kParamFadeIn)));
    controls.fadeOutSamples = static_cast<int>(std::round(getParameterPlainValue(kParamFadeOut)));
    controls.reversePlayback = getParameterPlainValue(kParamReversePlayback) >= 0.5f;
    controls.rootMidiNote = static_cast<int>(std::round(getParameterPlainValue(kParamRootMidiNote)));
    controls.coarseTuneSemitones = getParameterPlainValue(kParamTuneCoarse);
    controls.fineTuneCents = getParameterPlainValue(kParamTuneFine);
    controls.pitchBendRangeSemitones = getParameterPlainValue(kParamPitchBendRange);
    controls.pitchLfo.rateHz = getParameterPlainValue(kParamPitchLfoRate);
    controls.pitchLfo.depthCents = getParameterPlainValue(kParamPitchLfoDepth);

    loadModulationRoutesFromParameters(apvts_, controls.modulationRouting);
    loadMacroControlsFromParameters(apvts_, controls.macroControls);

    controls.sampleWindowStart = static_cast<int>(std::round(getParameterPlainValue(kParamPlaybackStart)));
    controls.sampleWindowEnd = static_cast<int>(std::round(getParameterPlainValue(kParamPlaybackEnd)));
    controls.loopStart = static_cast<int>(std::round(getParameterPlainValue(kParamLoopStart)));
    controls.loopEnd = static_cast<int>(std::round(getParameterPlainValue(kParamLoopEnd)));
    controls.loopCrossfadeSamples = static_cast<int>(std::round(getParameterPlainValue(kParamLoopCrossfade)));

    controls.velocityCurve = static_cast<VelocityCurve>(juce::jlimit(0, 2,
        static_cast<int>(std::round(getParameterPlainValue(kParamVelocityCurve)))));
    controls.qualityTier = static_cast<QualityTier>(juce::jlimit(0, 2,
        static_cast<int>(std::round(getParameterPlainValue(kParamQualityTier)))));
    controls.reverbMix = getParameterPlainValue(kParamReverbMix);

    controls.delay.timeMs = getParameterPlainValue(kParamDelayTime);
    controls.delay.feedback = getParameterPlainValue(kParamDelayFeedback);
    controls.delay.mix = getParameterPlainValue(kParamDelayMix);
    controls.delay.tempoSync = getParameterPlainValue(kParamDelayTempoSync) >= 0.5f;

    controls.dcFilter.enabled = getParameterPlainValue(kParamDcFilterEnabled) >= 0.5f;
    controls.dcFilter.cutoffHz = getParameterPlainValue(kParamDcFilterCutoff);

    controls.autopan.rateHz = getParameterPlainValue(kParamAutopanRate);
    controls.autopan.depth = getParameterPlainValue(kParamAutopanDepth);

    controls.saturation.drive = getParameterPlainValue(kParamSaturationDrive);
    controls.saturation.mode = static_cast<SaturationSettings::Mode>(juce::jlimit(0, 3,
        static_cast<int>(std::round(getParameterPlainValue(kParamSaturationMode)))));
    controls.pan = getParameterPlainValue(kParamPan);
    controls.masterVolume = getParameterPlainValue(kParamMasterVolume);
    return controls;
}

void AudiocityAudioProcessor::syncEngineFromAutomatableParameters() noexcept
{
    const auto controls = loadEngineControlSnapshot();
    const auto firstApply = !hasAppliedControls_;
    std::uint64_t appliedGroups = 0;

    const auto applyIfChanged = [&appliedGroups, firstApply](const auto& current,
                                                             const auto& previous,
                                                             auto&& apply)
    {
        if (firstApply || current != previous)
        {
            apply();
            ++appliedGroups;
        }
    };

    applyIfChanged(controls.hostTempoBpm, lastAppliedControls_.hostTempoBpm,
        [&] { engine_.setHostTempoBpm(controls.hostTempoBpm); });
    applyIfChanged(controls.ampEnvelope, lastAppliedControls_.ampEnvelope,
        [&] { engine_.setAmpEnvelope(controls.ampEnvelope); });
    applyIfChanged(controls.ampLfo, lastAppliedControls_.ampLfo,
        [&] { engine_.setAmpLfoSettings(controls.ampLfo); });
    applyIfChanged(controls.filter, lastAppliedControls_.filter,
        [&] { engine_.setFilterSettings(controls.filter); });
    applyIfChanged(controls.filterEnvelope, lastAppliedControls_.filterEnvelope,
        [&] { engine_.setFilterEnvelope(controls.filterEnvelope); });
    applyIfChanged(controls.playbackMode, lastAppliedControls_.playbackMode,
        [&] { engine_.setPlaybackMode(controls.playbackMode); });
    applyIfChanged(controls.monoMode, lastAppliedControls_.monoMode,
        [&] { engine_.setMonoMode(controls.monoMode); });
    applyIfChanged(controls.legatoMode, lastAppliedControls_.legatoMode,
        [&] { engine_.setLegatoMode(controls.legatoMode); });
    applyIfChanged(controls.glideSeconds, lastAppliedControls_.glideSeconds,
        [&] { engine_.setGlideSeconds(controls.glideSeconds); });
    applyIfChanged(controls.polyphonyLimit, lastAppliedControls_.polyphonyLimit,
        [&] { engine_.setPolyphonyLimit(controls.polyphonyLimit); });
    const auto fadesChanged = firstApply
        || controls.fadeInSamples != lastAppliedControls_.fadeInSamples
        || controls.fadeOutSamples != lastAppliedControls_.fadeOutSamples;
    if (fadesChanged)
    {
        engine_.setFadeSamples(controls.fadeInSamples, controls.fadeOutSamples);
        ++appliedGroups;
    }
    applyIfChanged(controls.reversePlayback, lastAppliedControls_.reversePlayback,
        [&] { engine_.setReversePlayback(controls.reversePlayback); });
    applyIfChanged(controls.rootMidiNote, lastAppliedControls_.rootMidiNote,
        [&] { engine_.setRootMidiNote(controls.rootMidiNote); });
    applyIfChanged(controls.coarseTuneSemitones, lastAppliedControls_.coarseTuneSemitones,
        [&] { engine_.setCoarseTuneSemitones(controls.coarseTuneSemitones); });
    applyIfChanged(controls.fineTuneCents, lastAppliedControls_.fineTuneCents,
        [&] { engine_.setFineTuneCents(controls.fineTuneCents); });
    applyIfChanged(controls.pitchBendRangeSemitones, lastAppliedControls_.pitchBendRangeSemitones,
        [&] { engine_.setPitchBendRangeSemitones(controls.pitchBendRangeSemitones); });
    applyIfChanged(controls.pitchLfo, lastAppliedControls_.pitchLfo,
        [&] { engine_.setPitchLfoSettings(controls.pitchLfo); });
    applyIfChanged(controls.modulationRouting, lastAppliedControls_.modulationRouting,
        [&] { engine_.setModulationRoutingSettings(controls.modulationRouting); });
    applyIfChanged(controls.macroControls, lastAppliedControls_.macroControls,
        [&] { engine_.setMacroControlValues(controls.macroControls); });

    const auto sampleWindowChanged = firstApply
        || controls.sampleWindowStart != lastAppliedControls_.sampleWindowStart
        || controls.sampleWindowEnd != lastAppliedControls_.sampleWindowEnd;
    if (sampleWindowChanged)
    {
        engine_.setSampleWindow(controls.sampleWindowStart, controls.sampleWindowEnd);
        ++appliedGroups;
    }
    const auto loopChanged = firstApply
        || controls.loopStart != lastAppliedControls_.loopStart
        || controls.loopEnd != lastAppliedControls_.loopEnd;
    if (loopChanged)
    {
        engine_.setLoopPoints(controls.loopStart, controls.loopEnd);
        ++appliedGroups;
    }
    applyIfChanged(controls.loopCrossfadeSamples, lastAppliedControls_.loopCrossfadeSamples,
        [&] { engine_.setLoopCrossfadeSamples(controls.loopCrossfadeSamples); });
    applyIfChanged(controls.velocityCurve, lastAppliedControls_.velocityCurve,
        [&] { engine_.setVelocityCurve(controls.velocityCurve); });
    applyIfChanged(controls.qualityTier, lastAppliedControls_.qualityTier,
        [&] { engine_.setQualityTier(controls.qualityTier); });
    applyIfChanged(controls.reverbMix, lastAppliedControls_.reverbMix,
        [&] { engine_.setReverbMix(controls.reverbMix); });
    applyIfChanged(controls.delay, lastAppliedControls_.delay,
        [&] { engine_.setDelaySettings(controls.delay); });
    applyIfChanged(controls.dcFilter, lastAppliedControls_.dcFilter,
        [&] { engine_.setDcFilterSettings(controls.dcFilter); });
    applyIfChanged(controls.autopan, lastAppliedControls_.autopan,
        [&] { engine_.setAutopanSettings(controls.autopan); });
    applyIfChanged(controls.saturation, lastAppliedControls_.saturation,
        [&] { engine_.setSaturationSettings(controls.saturation); });
    applyIfChanged(controls.pan, lastAppliedControls_.pan,
        [&] { engine_.setPan(controls.pan); });
    applyIfChanged(controls.masterVolume, lastAppliedControls_.masterVolume,
        [&] { engine_.setMasterVolume(controls.masterVolume); });

    lastAppliedControls_ = controls;
    hasAppliedControls_ = true;
    if (appliedGroups > 0)
        appliedControlGroupCount_.fetch_add(appliedGroups, std::memory_order_relaxed);
}

void AudiocityAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    engine_.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    startStreamPrimeWorker();
    captureInputSampleRate_.store(juce::jmax(1.0, sampleRate), std::memory_order_relaxed);
    outputBoundaryLastSample_.fill(0.0f);
    outputBoundaryHasLastSample_ = false;
}

void AudiocityAudioProcessor::releaseResources()
{
    stopStreamPrimeWorker();
    engine_.release();
    outputBoundaryLastSample_.fill(0.0f);
    outputBoundaryHasLastSample_ = false;
}

void AudiocityAudioProcessor::startStreamPrimeWorker()
{
    stopStreamPrimeWorker();
    stopStreamPrimeWorkerRequested_.store(false, std::memory_order_release);
    streamPrimeWorker_ = std::thread([this]
    {
        using namespace std::chrono_literals;

        while (!stopStreamPrimeWorkerRequested_.load(std::memory_order_acquire))
        {
            engine_.serviceStreamPriming();
            std::this_thread::sleep_for(2ms);
        }
    });
}

void AudiocityAudioProcessor::stopStreamPrimeWorker()
{
    stopStreamPrimeWorkerRequested_.store(true, std::memory_order_release);
    if (streamPrimeWorker_.joinable())
        streamPrimeWorker_.join();
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
        || getEditorTabIndex() == 5;

    for (auto channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    if (monitorCaptureInput && numInputChannels > 0)
        updateCaptureInputMonitorLevels(buffer, numInputChannels);

    bool capturedThisBlock = false;
    if (captureRecording && numInputChannels > 0)
        capturedThisBlock = captureInputAudio(buffer, numInputChannels);

    updateHostTempoFromPlayHead();
    if (controlResyncRequested_.exchange(false, std::memory_order_acq_rel))
        hasAppliedControls_ = false;
    syncEngineFromAutomatableParameters();

    // Extract CC messages and push to FIFO for the editor to consume
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isController())
            pushCcEvent(msg.getControllerNumber(), msg.getControllerValue());

        if (msg.isNoteOnOrOff())
        {
            pushExternalMidiDisplayEvent(msg.getNoteNumber(),
                                         juce::jlimit(0, 127, static_cast<int>(std::round(msg.getFloatVelocity() * 127.0f))),
                                         msg.isNoteOn());
        }
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

    if (samplePreviewPlaying_.load(std::memory_order_relaxed))
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
        switch (uiEvent.type)
        {
            case UiMidiEvent::Type::noteOn:
                engine_.noteOn(uiEvent.noteNumber, static_cast<float>(uiEvent.velocity) / 127.0f, 0);
                break;
            case UiMidiEvent::Type::noteOff:
                engine_.noteOff(uiEvent.noteNumber, 0);
                break;
        }
    }

    // Apply panic after draining queued UI notes so the latch also clears any requests that
    // accumulated before it. Host MIDI in this block remains authoritative and is handled by
    // EngineCore::render below.
    if (panicRequested_.exchange(false, std::memory_order_acq_rel))
        engine_.panic();

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
    updateParameterFromPlainValue(kParamQualityTier, static_cast<float>(tier));
}

AudiocityAudioProcessor::QualityTier AudiocityAudioProcessor::getQualityTier() const noexcept
{
    return static_cast<QualityTier>(juce::jlimit(0, 2,
        static_cast<int>(std::round(getParameterPlainValue(kParamQualityTier)))));
}

void AudiocityAudioProcessor::setVelocityCurve(const VelocityCurve curve) noexcept
{
    updateParameterFromPlainValue(kParamVelocityCurve, static_cast<float>(curve));
}

AudiocityAudioProcessor::VelocityCurve AudiocityAudioProcessor::getVelocityCurve() const noexcept
{
    return static_cast<VelocityCurve>(juce::jlimit(0, 2,
        static_cast<int>(std::round(getParameterPlainValue(kParamVelocityCurve)))));
}

void AudiocityAudioProcessor::setReverbMix(const float mix) noexcept
{
    updateParameterFromPlainValue(kParamReverbMix, mix);
}

float AudiocityAudioProcessor::getReverbMix() const noexcept
{
    return getParameterPlainValue(kParamReverbMix);
}

void AudiocityAudioProcessor::setDelaySettings(const DelaySettings& settings) noexcept
{
    updateParameterFromPlainValue(kParamDelayTime, settings.timeMs);
    updateParameterFromPlainValue(kParamDelayFeedback, settings.feedback);
    updateParameterFromPlainValue(kParamDelayMix, settings.mix);
    updateParameterFromPlainValue(kParamDelayTempoSync, settings.tempoSync ? 1.0f : 0.0f);
}

AudiocityAudioProcessor::DelaySettings AudiocityAudioProcessor::getDelaySettings() const noexcept
{
    return {
        getParameterPlainValue(kParamDelayTime),
        getParameterPlainValue(kParamDelayFeedback),
        getParameterPlainValue(kParamDelayMix),
        getParameterPlainValue(kParamDelayTempoSync) >= 0.5f
    };
}

void AudiocityAudioProcessor::setDcFilterSettings(const DcFilterSettings& settings) noexcept
{
    updateParameterFromPlainValue(kParamDcFilterEnabled, settings.enabled ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamDcFilterCutoff, settings.cutoffHz);
}

AudiocityAudioProcessor::DcFilterSettings AudiocityAudioProcessor::getDcFilterSettings() const noexcept
{
    return {
        getParameterPlainValue(kParamDcFilterEnabled) >= 0.5f,
        getParameterPlainValue(kParamDcFilterCutoff)
    };
}

void AudiocityAudioProcessor::setAutopanSettings(const AutopanSettings& settings) noexcept
{
    updateParameterFromPlainValue(kParamAutopanRate, settings.rateHz);
    updateParameterFromPlainValue(kParamAutopanDepth, settings.depth);
}

AudiocityAudioProcessor::AutopanSettings AudiocityAudioProcessor::getAutopanSettings() const noexcept
{
    return {
        getParameterPlainValue(kParamAutopanRate),
        getParameterPlainValue(kParamAutopanDepth)
    };
}

void AudiocityAudioProcessor::setSaturationSettings(const SaturationSettings& settings) noexcept
{
    updateParameterFromPlainValue(kParamSaturationDrive, settings.drive);
    updateParameterFromPlainValue(kParamSaturationMode, static_cast<float>(settings.mode));
}

AudiocityAudioProcessor::SaturationSettings AudiocityAudioProcessor::getSaturationSettings() const noexcept
{
    return {
        getParameterPlainValue(kParamSaturationDrive),
        static_cast<SaturationSettings::Mode>(juce::jlimit(0, 3,
            static_cast<int>(std::round(getParameterPlainValue(kParamSaturationMode)))))
    };
}

void AudiocityAudioProcessor::setPan(const float pan) noexcept
{
    updateParameterFromPlainValue(kParamPan, pan);
}

float AudiocityAudioProcessor::getPan() const noexcept
{
    return getParameterPlainValue(kParamPan);
}

void AudiocityAudioProcessor::setMasterVolume(const float volume) noexcept
{
    updateParameterFromPlainValue(kParamMasterVolume, volume);
}

float AudiocityAudioProcessor::getMasterVolume() const noexcept
{
    return getParameterPlainValue(kParamMasterVolume);
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

int AudiocityAudioProcessor::getActiveVoiceCount() const noexcept
{
    int active = 0;
    for (const auto& position : voicePlaybackPositions_)
        if (position.load(std::memory_order_relaxed) >= 0)
            ++active;
    return active;
}

void AudiocityAudioProcessor::setFilterEnvelope(const AdsrSettings& settings) noexcept
{
    updateParameterFromPlainValue(kParamFilterAttack, settings.attackSeconds);
    updateParameterFromPlainValue(kParamFilterDecay, settings.decaySeconds);
    updateParameterFromPlainValue(kParamFilterSustain, settings.sustainLevel);
    updateParameterFromPlainValue(kParamFilterRelease, settings.releaseSeconds);
}

AudiocityAudioProcessor::AdsrSettings AudiocityAudioProcessor::getFilterEnvelope() const noexcept
{
    return {
        getParameterPlainValue(kParamFilterAttack),
        getParameterPlainValue(kParamFilterDecay),
        getParameterPlainValue(kParamFilterSustain),
        getParameterPlainValue(kParamFilterRelease)
    };
}

void AudiocityAudioProcessor::setFilterSettings(const FilterSettings& settings) noexcept
{
    updateParameterFromPlainValue(kParamFilterCutoff, settings.baseCutoffHz);
    updateParameterFromPlainValue(kParamFilterRes, settings.resonance);
    updateParameterFromPlainValue(kParamFilterEnvAmt, settings.envAmountHz);
    updateParameterFromPlainValue(kParamFilterMode, static_cast<float>(settings.mode));
    updateParameterFromPlainValue(kParamFilterKeytrack, settings.keyTracking);
    updateParameterFromPlainValue(kParamFilterVel, settings.velocityAmountHz);
    updateParameterFromPlainValue(kParamFilterLfoRate, settings.lfoRateHz);
    updateParameterFromPlainValue(kParamFilterLfoRateKeytrack, settings.lfoRateKeyTracking);
    updateParameterFromPlainValue(kParamFilterLfoAmount, settings.lfoAmountHz);
    updateParameterFromPlainValue(kParamFilterLfoAmountKeytrack, settings.lfoAmountKeyTracking);
    updateParameterFromPlainValue(kParamFilterLfoStartPhase, settings.lfoStartPhaseDegrees);
    updateParameterFromPlainValue(kParamFilterLfoStartPhaseRandom, settings.lfoStartPhaseRandomDegrees);
    updateParameterFromPlainValue(kParamFilterLfoFadeIn, settings.lfoFadeInMs);
    updateParameterFromPlainValue(kParamFilterLfoShape, static_cast<float>(settings.lfoShape));
    updateParameterFromPlainValue(kParamFilterLfoRetrigger, settings.lfoRetrigger ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamFilterLfoTempoSync, settings.lfoTempoSync ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamFilterLfoRateKeytrackInTempoSync, settings.lfoRateKeytrackInTempoSync ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamFilterLfoKeytrackLinear, settings.lfoKeytrackLinear ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamFilterLfoUnipolar, settings.lfoUnipolar ? 1.0f : 0.0f);
    updateParameterFromPlainValue(kParamFilterLfoSyncDivision, static_cast<float>(settings.lfoSyncDivision));
}

AudiocityAudioProcessor::FilterSettings AudiocityAudioProcessor::getFilterSettings() const noexcept
{
    return loadEngineControlSnapshot().filter;
}

juce::AudioProcessorEditor* AudiocityAudioProcessor::createEditor()
{
    return new AudiocityAudioProcessorEditor(*this);
}

void AudiocityAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = juce::ValueTree(kPatchRoot);
    auto decodedAssetBytes = std::uint64_t{ 0 };
    juce::String assetWarning;

    state.setProperty(kSamplePath, engine_.getSamplePath(), nullptr);
    const auto importedProgramState = importedProgramStore_.capturePersistentState();
    if (importedProgramState.programPath.isNotEmpty())
    {
        audiocity::plugin::appendImportedProgramState(state,
                                  importedProgramState.programPath,
                                  importedProgramState.mappingState,
                                  importedProgramState.format,
                                  importedProgramState.selectionIndex,
                                  importedProgramState.assetManifest);
    }
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        if (generatedWaveformLoaded_.load(std::memory_order_relaxed) && !generatedWaveformState_.empty())
        {
            juce::String codecError;
            const auto generatedAsset = audiocity::plugin::encodeMonoAudioStateAsset(generatedWaveformState_, &codecError);
            if (!generatedAsset.isEmpty())
            {
                state.setProperty(kGeneratedWaveformAsset, juce::var(generatedAsset), nullptr);
                decodedAssetBytes += generatedWaveformState_.size() * sizeof(float);
            }
            else
            {
                assetWarning = codecError;
            }
        }

        if (capturedAudioLoaded_.load(std::memory_order_relaxed) && !capturedSampleState_.empty())
        {
            juce::String codecError;
            const auto capturedAsset = audiocity::plugin::encodeMonoAudioStateAsset(capturedSampleState_, &codecError);
            if (!capturedAsset.isEmpty())
            {
                state.setProperty(kCapturedSampleAsset, juce::var(capturedAsset), nullptr);
                decodedAssetBytes += capturedSampleState_.size() * sizeof(float);
            }
            else if (assetWarning.isEmpty())
            {
                assetWarning = codecError;
            }
            state.setProperty(kCapturedSampleRate, capturedSampleRateState_, nullptr);
        }

        if (embeddedSampleLoaded_.load(std::memory_order_relaxed) && !embeddedSampleState_.empty())
        {
            const auto embeddedBuffer = engine_.copyLoadedSampleDisplayData();
            juce::String codecError;
            if (writeEmbeddedSampleState(state,
                                         embeddedBuffer,
                                         embeddedSampleRateState_,
                                         embeddedSampleRootMidiNoteState_,
                                         embeddedSampleNameState_,
                                         &codecError))
            {
                decodedAssetBytes += static_cast<std::uint64_t>(juce::jmax(0, embeddedBuffer.getNumChannels()))
                    * static_cast<std::uint64_t>(juce::jmax(0, embeddedBuffer.getNumSamples()))
                    * sizeof(float);
            }
            else if (assetWarning.isEmpty())
            {
                assetWarning = codecError.isNotEmpty()
                    ? codecError
                    : juce::String("Embedded audio state serialization failed");
            }
        }
    }
    constexpr auto kStateAssetWarningThresholdBytes = std::uint64_t{ 32 } * 1024u * 1024u;
    if (decodedAssetBytes > kStateAssetWarningThresholdBytes && assetWarning.isEmpty())
        assetWarning = "Embedded audio state exceeds 32 MiB decoded; consider referencing a file instead";
    state.setProperty(kAudioStateDecodedBytes, static_cast<juce::int64>(decodedAssetBytes), nullptr);
    if (assetWarning.isNotEmpty())
        state.setProperty(kAudioStateSizeWarning, assetWarning, nullptr);
    lastSerializedAssetStateBytes_.store(decodedAssetBytes, std::memory_order_relaxed);
    stateAssetSizeWarning_.store(assetWarning.isNotEmpty(), std::memory_order_relaxed);
    state.setProperty(kSampleBrowserRootFolder, sampleBrowserRootFolderPath_, nullptr);
    {
        std::lock_guard<std::mutex> lock(libraryMetadataMutex_);
        state.appendChild(libraryMetadata_.toValueTree(), nullptr);
    }
    state.setProperty(kRootMidiNote, getRootMidiNote(), nullptr);
    state.setProperty(kCoarseTuneSemitones, getCoarseTuneSemitones(), nullptr);
    state.setProperty(kFineTuneCents, getFineTuneCents(), nullptr);
    state.setProperty(kPitchBendRangeSemitones, getPitchBendRangeSemitones(), nullptr);
    const auto pitchLfo = getPitchLfoSettings();
    state.setProperty(kPitchLfoRate, pitchLfo.rateHz, nullptr);
    state.setProperty(kPitchLfoDepth, pitchLfo.depthCents, nullptr);
    const auto modulationRouting = getModulationRoutingSettings();
    storeModulationRoutesToState(state, modulationRouting);

    const auto macroValues = getMacroControlValues();
    storeMacroControlsToState(state, macroValues);

    const auto amp = getAmpEnvelope();
    state.setProperty(kAmpAttack, amp.attackSeconds, nullptr);
    state.setProperty(kAmpDecay, amp.decaySeconds, nullptr);
    state.setProperty(kAmpSustain, amp.sustainLevel, nullptr);
    state.setProperty(kAmpRelease, amp.releaseSeconds, nullptr);
    const auto ampLfo = getAmpLfoSettings();
    state.setProperty(kAmpLfoRate, ampLfo.rateHz, nullptr);
    state.setProperty(kAmpLfoDepth, ampLfo.depth, nullptr);
    state.setProperty(kAmpLfoShape, static_cast<int>(ampLfo.shape), nullptr);

    const auto filterAdsr = getFilterEnvelope();
    state.setProperty(kFilterAttack, filterAdsr.attackSeconds, nullptr);
    state.setProperty(kFilterDecay, filterAdsr.decaySeconds, nullptr);
    state.setProperty(kFilterSustain, filterAdsr.sustainLevel, nullptr);
    state.setProperty(kFilterRelease, filterAdsr.releaseSeconds, nullptr);

    const auto filter = getFilterSettings();
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
    state.setProperty(kSampleInspectorFilterModExpanded,
        sampleInspectorFilterModExpanded_.load(std::memory_order_relaxed) ? 1 : 0,
        nullptr);
    state.setProperty(kSampleInspectorEffectsExpanded,
        sampleInspectorEffectsExpanded_.load(std::memory_order_relaxed) ? 1 : 0,
        nullptr);
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

    panicAllAudio();
    clearPendingImportedAssetRelink();

    const auto importedProgramPath = audiocity::plugin::readImportedProgramStatePath(state);
    const auto importedProgramSelectionIndex =
        audiocity::plugin::readImportedProgramStateSelectionIndex(state);
    const auto importedProgramFormat = audiocity::plugin::readImportedProgramStateFormat(state);
    const auto importedProgramMappingState =
        audiocity::plugin::readImportedProgramMappingState(state);
    const auto samplePath = state.getProperty(kSamplePath).toString();
    const auto storedRootMidiNote = static_cast<int>(state.getProperty(kRootMidiNote, getRootMidiNote()));

    bool restoredSample = false;
    int restoredSampleSource = 0;

    juce::AudioBuffer<float> restoredEmbeddedBuffer;
    auto decodedEmbeddedAsset = false;
    if (const auto* embeddedAsset = state.getProperty(kEmbeddedSampleAsset).getBinaryData(); embeddedAsset != nullptr)
    {
        juce::String codecError;
        decodedEmbeddedAsset = audiocity::plugin::decodeAudioStateAsset(
            *embeddedAsset, restoredEmbeddedBuffer, &codecError);
        if (!decodedEmbeddedAsset)
            setLastImportDiagnosticSummary("Embedded state asset rejected: " + codecError);
    }
    else if (const auto* embeddedData = state.getProperty(kEmbeddedSampleData).getBinaryData(); embeddedData != nullptr)
    {
        const auto embeddedChannels = juce::jmax(1,
            static_cast<int>(state.getProperty(kEmbeddedSampleChannels, 1)));
        juce::String codecError;
        decodedEmbeddedAsset = deserializeAudioBufferSamples(
            *embeddedData, embeddedChannels, restoredEmbeddedBuffer, &codecError);
        if (!decodedEmbeddedAsset)
            setLastImportDiagnosticSummary("Legacy embedded state rejected: " + codecError);
    }

    if (decodedEmbeddedAsset)
    {
        const auto storedSampleRate = juce::jmax(1.0,
            static_cast<double>(state.getProperty(kEmbeddedSampleRate, 44100.0)));
        const auto embeddedRoot = juce::jlimit(0, 127,
            static_cast<int>(state.getProperty(kEmbeddedSampleRootMidiNote, storedRootMidiNote)));
        const auto embeddedName = state.getProperty(kEmbeddedSampleName, juce::var{}).toString();

        loadEmbeddedSampleAsSample(restoredEmbeddedBuffer, storedSampleRate, embeddedRoot, embeddedName);
        restoredSample = true;
        restoredSampleSource = 5;
    }

    if (!restoredSample && importedProgramPath.isNotEmpty())
    {
        auto importedProgramFile = juce::File(importedProgramPath);
        auto assetsReady = true;
        auto assetManifest = audiocity::plugin::readImportedAssetManifestState(state);
        if (assetManifest.isValid())
        {
            std::vector<juce::File> knownRoots;
            const auto storedBrowserRoot = state.getProperty(kSampleBrowserRootFolder).toString();
            if (storedBrowserRoot.isNotEmpty())
                knownRoots.emplace_back(storedBrowserRoot);
            if (samplePath.isNotEmpty())
                knownRoots.push_back(juce::File(samplePath).getParentDirectory());
            knownRoots.push_back(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Audiocity").getChildFile("Libraries"));

            const auto resolution = audiocity::plugin::resolveImportedAssetManifest(
                assetManifest, knownRoots);
            if (resolution.complete)
            {
                importedProgramFile = resolution.resolvedProgramFile();
            }
            else
            {
                assetsReady = false;
                setPendingImportedAssetRelink(
                    assetManifest,
                    importedProgramFormat,
                    importedProgramSelectionIndex,
                    importedProgramMappingState,
                    "Imported program recovery required: " + resolution.diagnostic
                        + "\nOriginal program: " + importedProgramPath
                        + "\nChoose the moved library root to relink every asset at once.");
            }
        }
        else if (!importedProgramFile.existsAsFile())
        {
            assetsReady = false;
            assetManifest = audiocity::plugin::createLegacyImportedProgramManifest(importedProgramFile);
            setPendingImportedAssetRelink(
                assetManifest,
                importedProgramFormat,
                importedProgramSelectionIndex,
                importedProgramMappingState,
                "Imported program file is missing: " + importedProgramPath
                    + "\nThis is a legacy state without an asset manifest; choose the moved library folder.");
        }

        if (assetsReady)
        {
            restoredSample = restoreImportedProgramFromState(importedProgramFile,
                                                              importedProgramFormat,
                                                              importedProgramSelectionIndex,
                                                              importedProgramMappingState);
            if (restoredSample)
                restoredSampleSource = 4;
        }
    }

    if (!restoredSample && !hasPendingImportedAssetRelink() && samplePath.isNotEmpty())
    {
        restoredSample = loadSampleFromFile(juce::File(samplePath));
        if (restoredSample)
            restoredSampleSource = 1;
    }

    if (!restoredSample && !hasPendingImportedAssetRelink())
    {
        std::vector<float> waveform;
        auto decodedGeneratedAsset = false;
        if (const auto* generatedAsset = state.getProperty(kGeneratedWaveformAsset).getBinaryData(); generatedAsset != nullptr)
        {
            juce::String codecError;
            decodedGeneratedAsset = audiocity::plugin::decodeMonoAudioStateAsset(
                *generatedAsset, waveform, &codecError);
            if (!decodedGeneratedAsset)
                setLastImportDiagnosticSummary("Generated waveform state rejected: " + codecError);
        }
        else if (const auto* generatedData = state.getProperty(kGeneratedWaveformData).getBinaryData(); generatedData != nullptr)
        {
            const auto totalBytes = generatedData->getSize();
            if (totalBytes >= sizeof(float)
                && totalBytes <= audiocity::plugin::AudioStateCodecLimits::maximumDecodedBytes
                && (totalBytes % sizeof(float)) == 0
                && containsOnlyFiniteFloatSamples(generatedData->getData(), totalBytes))
            {
                const auto sampleCount = static_cast<std::size_t>(totalBytes / sizeof(float));
                waveform.resize(sampleCount, 0.0f);
                std::memcpy(waveform.data(), generatedData->getData(), totalBytes);
                decodedGeneratedAsset = true;
            }
            else
            {
                setLastImportDiagnosticSummary(
                    "Legacy generated waveform state rejected: invalid size or non-finite sample");
            }
        }

        if (decodedGeneratedAsset)
        {
            loadGeneratedWaveformAsSample(waveform, storedRootMidiNote);
            restoredSample = true;
            restoredSampleSource = 2;
        }
    }

    if (!restoredSample && !hasPendingImportedAssetRelink())
    {
        std::vector<float> capturedSamples;
        auto decodedCapturedAsset = false;
        if (const auto* capturedAsset = state.getProperty(kCapturedSampleAsset).getBinaryData(); capturedAsset != nullptr)
        {
            juce::String codecError;
            decodedCapturedAsset = audiocity::plugin::decodeMonoAudioStateAsset(
                *capturedAsset, capturedSamples, &codecError);
            if (!decodedCapturedAsset)
                setLastImportDiagnosticSummary("Captured audio state rejected: " + codecError);
        }
        else if (const auto* capturedData = state.getProperty(kCapturedSampleData).getBinaryData(); capturedData != nullptr)
        {
            const auto totalBytes = capturedData->getSize();
            if (totalBytes >= sizeof(float)
                && totalBytes <= audiocity::plugin::AudioStateCodecLimits::maximumDecodedBytes
                && (totalBytes % sizeof(float)) == 0
                && containsOnlyFiniteFloatSamples(capturedData->getData(), totalBytes))
            {
                capturedSamples.resize(totalBytes / sizeof(float));
                std::memcpy(capturedSamples.data(), capturedData->getData(), totalBytes);
                decodedCapturedAsset = true;
            }
            else
            {
                setLastImportDiagnosticSummary(
                    "Legacy captured audio state rejected: invalid size or non-finite sample");
            }
        }

        if (decodedCapturedAsset && !capturedSamples.empty())
        {
            const auto sampleCount = static_cast<int>(capturedSamples.size());
            const auto restoredSampleRate = juce::jmax(1.0,
                static_cast<double>(state.getProperty(kCapturedSampleRate, 44100.0)));
            juce::AudioBuffer<float> restoredBuffer(1, sampleCount);
            std::memcpy(restoredBuffer.getWritePointer(0), capturedSamples.data(), capturedSamples.size() * sizeof(float));

            engine_.publishSampleData(restoredBuffer, restoredSampleRate);
            engine_.clearSamplePath();
            generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
            capturedAudioLoaded_.store(true, std::memory_order_relaxed);
            embeddedSampleLoaded_.store(false, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
                generatedWaveformState_.clear();
                capturedSampleState_ = capturedSamples;
                capturedSampleRateState_ = restoredSampleRate;
            }

            resetControlsForPublishedSample(storedRootMidiNote, sampleCount);
            restoredSample = true;
            restoredSampleSource = 3;
        }
    }

    if (!restoredSample && !hasPendingImportedAssetRelink())
    {
        generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
        capturedAudioLoaded_.store(false, std::memory_order_relaxed);
        embeddedSampleLoaded_.store(false, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_.clear();
        capturedSampleState_.clear();
        capturedSampleRateState_ = 44100.0;
        embeddedSampleState_.clear();
        embeddedSampleNameState_.clear();
        embeddedSampleRateState_ = 44100.0;
        embeddedSampleRootMidiNoteState_ = 60;
    }

    lastStateRestoreSource_.store(restoredSampleSource, std::memory_order_relaxed);

    sampleBrowserRootFolderPath_ = state.getProperty(kSampleBrowserRootFolder, {}).toString();
    {
        std::lock_guard<std::mutex> lock(libraryMetadataMutex_);
        libraryMetadata_ = audiocity::plugin::LibraryMetadata::fromValueTree(
            state.getChildWithName(audiocity::plugin::LibraryMetadata::valueTreeType()));
    }

    setRootMidiNote(static_cast<int>(state.getProperty(kRootMidiNote, getRootMidiNote())));
    setCoarseTuneSemitones(static_cast<float>(state.getProperty(kCoarseTuneSemitones, getCoarseTuneSemitones())));
    setFineTuneCents(static_cast<float>(state.getProperty(kFineTuneCents, getFineTuneCents())));
    setPitchBendRangeSemitones(static_cast<float>(state.getProperty(kPitchBendRangeSemitones, getPitchBendRangeSemitones())));
    auto pitchLfo = getPitchLfoSettings();
    pitchLfo.rateHz = static_cast<float>(state.getProperty(kPitchLfoRate, pitchLfo.rateHz));
    pitchLfo.depthCents = static_cast<float>(state.getProperty(kPitchLfoDepth, pitchLfo.depthCents));
    setPitchLfoSettings(pitchLfo);

    auto modulationRouting = getModulationRoutingSettings();
    restoreModulationRoutesFromState(state, modulationRouting);
    setModulationRoutingSettings(modulationRouting);

    auto macroValues = getMacroControlValues();
    restoreMacroControlsFromState(state, macroValues);
    setMacroControlValues(macroValues);

    auto amp = getAmpEnvelope();
    amp.attackSeconds = static_cast<float>(state.getProperty(kAmpAttack, amp.attackSeconds));
    amp.decaySeconds = static_cast<float>(state.getProperty(kAmpDecay, amp.decaySeconds));
    amp.sustainLevel = static_cast<float>(state.getProperty(kAmpSustain, amp.sustainLevel));
    amp.releaseSeconds = static_cast<float>(state.getProperty(kAmpRelease, amp.releaseSeconds));
    setAmpEnvelope(amp);

    auto ampLfo = getAmpLfoSettings();
    ampLfo.rateHz = static_cast<float>(state.getProperty(kAmpLfoRate, ampLfo.rateHz));
    ampLfo.depth = static_cast<float>(state.getProperty(kAmpLfoDepth, ampLfo.depth));
    ampLfo.shape = static_cast<FilterSettings::LfoShape>(juce::jlimit(0, 4,
        static_cast<int>(state.getProperty(kAmpLfoShape, static_cast<int>(ampLfo.shape)))));
    setAmpLfoSettings(ampLfo);

    auto filterAdsr = getFilterEnvelope();
    filterAdsr.attackSeconds = static_cast<float>(state.getProperty(kFilterAttack, filterAdsr.attackSeconds));
    filterAdsr.decaySeconds = static_cast<float>(state.getProperty(kFilterDecay, filterAdsr.decaySeconds));
    filterAdsr.sustainLevel = static_cast<float>(state.getProperty(kFilterSustain, filterAdsr.sustainLevel));
    filterAdsr.releaseSeconds = static_cast<float>(state.getProperty(kFilterRelease, filterAdsr.releaseSeconds));
    setFilterEnvelope(filterAdsr);

    auto filter = getFilterSettings();
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
    setSampleInspectorFilterModExpanded(static_cast<int>(state.getProperty(
        kSampleInspectorFilterModExpanded,
        sampleInspectorFilterModExpanded_.load(std::memory_order_relaxed) ? 1 : 0)) != 0);
    setSampleInspectorEffectsExpanded(static_cast<int>(state.getProperty(
        kSampleInspectorEffectsExpanded,
        sampleInspectorEffectsExpanded_.load(std::memory_order_relaxed) ? 1 : 0)) != 0);
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

    auto presetState = buildPlaybackPresetStateTree(state);
    const auto samplePath = presetState.getProperty(kSamplePath).toString();
    const auto hasEmbeddedSample = presetState.getProperty(kEmbeddedSampleData).getBinaryData() != nullptr;
    const auto hasGeneratedSample = presetState.getProperty(kGeneratedWaveformData).getBinaryData() != nullptr;
    const auto hasCapturedSample = presetState.getProperty(kCapturedSampleData).getBinaryData() != nullptr;

    if (samplePath.isNotEmpty()
        && !hasEmbeddedSample
        && !hasGeneratedSample
        && !hasCapturedSample
        && audiocity::plugin::readImportedProgramStatePath(presetState).isEmpty())
    {
        const auto displaySample = engine_.copyLoadedSampleDisplayData();
        if (displaySample.getNumChannels() > 0 && displaySample.getNumSamples() > 0)
        {
            if (writeEmbeddedSampleState(presetState,
                                         displaySample,
                                         engine_.getLoadedSampleRateHz(),
                                         static_cast<int>(presetState.getProperty(kRootMidiNote, getRootMidiNote())),
                                         juce::File(samplePath).getFileName()))
            {
                presetState.removeProperty(kSamplePath, nullptr);
            }
        }
    }

    return audiocity::plugin::encodePresetXml(presetState);
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
    const auto presetChangesSampleSource =
        filteredPresetState.getProperty(kEmbeddedSampleData).getBinaryData() != nullptr
        || filteredPresetState.getProperty(kGeneratedWaveformData).getBinaryData() != nullptr
        || filteredPresetState.getProperty(kCapturedSampleData).getBinaryData() != nullptr
        || filteredPresetState.getProperty(kSamplePath).toString().isNotEmpty()
        || audiocity::plugin::readImportedProgramStatePath(filteredPresetState).isNotEmpty();

    if (presetChangesSampleSource)
    {
        if (!filteredPresetState.hasProperty(kSampleWindowStart))
            currentState.removeProperty(kSampleWindowStart, nullptr);

        if (!filteredPresetState.hasProperty(kSampleWindowEnd))
            currentState.removeProperty(kSampleWindowEnd, nullptr);
    }

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
        case 4: return "program";
        case 5: return "embedded";
        default: return "none";
    }
}

void AudiocityAudioProcessor::setLibraryFavorite(const juce::String& filePath, const bool shouldBeFavorite)
{
    std::lock_guard<std::mutex> lock(libraryMetadataMutex_);
    libraryMetadata_.setFavorite(filePath, shouldBeFavorite);
}

bool AudiocityAudioProcessor::isLibraryFavorite(const juce::String& filePath) const
{
    std::lock_guard<std::mutex> lock(libraryMetadataMutex_);
    return libraryMetadata_.isFavorite(filePath);
}

void AudiocityAudioProcessor::markLibraryRecent(const juce::String& filePath)
{
    std::lock_guard<std::mutex> lock(libraryMetadataMutex_);
    libraryMetadata_.markRecent(filePath);
}

void AudiocityAudioProcessor::setLibraryTags(const juce::String& filePath, const juce::StringArray& tags)
{
    std::lock_guard<std::mutex> lock(libraryMetadataMutex_);
    libraryMetadata_.setTags(filePath, tags);
}

void AudiocityAudioProcessor::addLibraryBookmark(const juce::String& folderPath)
{
    std::lock_guard<std::mutex> lock(libraryMetadataMutex_);
    libraryMetadata_.addBookmark(folderPath);
}

void AudiocityAudioProcessor::removeLibraryBookmark(const juce::String& folderPath)
{
    std::lock_guard<std::mutex> lock(libraryMetadataMutex_);
    libraryMetadata_.removeBookmark(folderPath);
}

audiocity::plugin::LibraryMetadata AudiocityAudioProcessor::getLibraryMetadataSnapshot() const
{
    std::lock_guard<std::mutex> lock(libraryMetadataMutex_);
    return libraryMetadata_;
}

void AudiocityAudioProcessor::clearImportedProgramMetadata()
{
    importedProgramStore_.clear();
}

void AudiocityAudioProcessor::setImportedProgramMetadata(const juce::File& file,
                                                         const audiocity::plugin::ImportedProgramFormat format,
                                                         const audiocity::engine::Program& program,
                                                         const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset,
                                                         const juce::String& diagnosticSummary,
                                                         const int zoneCount,
                                                         const int selectionIndex)
{
    importedProgramStore_.loadProgram(file,
                                      format,
                                      program,
                                      sampleDataByAsset,
                                      diagnosticSummary,
                                      zoneCount,
                                      selectionIndex);
}

void AudiocityAudioProcessor::setLastImportDiagnosticSummary(const juce::String& diagnosticSummary)
{
    importedProgramStore_.setLastDiagnosticSummary(diagnosticSummary);
}

juce::String AudiocityAudioProcessor::getImportedProgramPath() const
{
    return importedProgramStore_.getProgramPath();
}

audiocity::plugin::ImportedProgramFormat AudiocityAudioProcessor::getImportedProgramFormat() const
{
    return importedProgramStore_.getFormat();
}

juce::String AudiocityAudioProcessor::getImportedProgramName() const
{
    return importedProgramStore_.getProgramName();
}

juce::String AudiocityAudioProcessor::getImportedProgramMapSummary() const
{
    return importedProgramStore_.getMapSummary();
}

juce::StringArray AudiocityAudioProcessor::getImportedProgramSampleAssetNames() const
{
    juce::StringArray sampleAssetNames;
    importedProgramStore_.read([&sampleAssetNames](const audiocity::engine::Program& program,
                                                   const std::vector<juce::AudioBuffer<float>>&)
    {
        sampleAssetNames.ensureStorageAllocated(static_cast<int>(program.sampleAssets.size()));
        for (std::size_t index = 0; index < program.sampleAssets.size(); ++index)
        {
            const auto& asset = program.sampleAssets[index];
            auto label = formatImportedProgramSampleAssetName(asset, static_cast<int>(index));
            if (!asset.hasAudio())
                label += " (missing)";

            sampleAssetNames.add(label);
        }
    });

    return sampleAssetNames;
}

std::vector<audiocity::plugin::ProgramZoneListRow> AudiocityAudioProcessor::getImportedProgramZoneRows() const
{
    return importedProgramStore_.getZoneRows();
}

std::vector<int> AudiocityAudioProcessor::getImportedProgramSliceMarkerSamples() const
{
    std::vector<int> markers;
    importedProgramStore_.read([&markers](const audiocity::engine::Program& program,
                                          const std::vector<juce::AudioBuffer<float>>&)
    {
        if (program.sampleAssets.size() != 1 || program.zones.empty())
            return;

        markers.reserve(program.zones.size() * 2);
        for (const auto& zone : program.zones)
        {
            if (zone.sampleAssetIndex != 0)
            {
                markers.clear();
                return;
            }

            markers.push_back(juce::jmax(0, zone.sampleStart));
            if (zone.sampleEndExclusive > zone.sampleStart)
                markers.push_back(zone.sampleEndExclusive);
        }
    });

    std::sort(markers.begin(), markers.end());
    markers.erase(std::unique(markers.begin(), markers.end()), markers.end());
    return markers;
}

juce::ValueTree AudiocityAudioProcessor::createImportedProgramMappingState() const
{
    juce::ValueTree mappingState;
    importedProgramStore_.read([&mappingState](const audiocity::engine::Program& program,
                                               const std::vector<juce::AudioBuffer<float>>&)
    {
        mappingState = audiocity::plugin::createProgramZoneMappingState(program);
    });

    return mappingState;
}

bool AudiocityAudioProcessor::applyImportedProgramMappingState(const juce::ValueTree& mappingState)
{
    return importedProgramStore_.edit([&mappingState](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.sampleDataByAsset.empty())
            return outcome;

        if (!audiocity::plugin::restoreImportedProgramMappingState(edit.program, mappingState))
            return outcome;

        outcome.ok = true;
        outcome.label = "Mapping restored";
        return outcome;
    }).ok;
}

bool AudiocityAudioProcessor::updateImportedProgramZoneMapping(const audiocity::plugin::ProgramZoneEdit& edit)
{
    return updateImportedProgramZoneMappings({ edit });
}

bool AudiocityAudioProcessor::updateImportedProgramZoneMappings(
    const std::vector<audiocity::plugin::ProgramZoneEdit>& edits)
{
    return importedProgramStore_.edit([&edits](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.program.zones.empty() || edit.sampleDataByAsset.empty())
            return outcome;

        if (!audiocity::plugin::applyProgramZoneEditsAtomic(edit.program, edits))
            return outcome;

        outcome.ok = true;
        outcome.label = edits.size() == 1
                            ? "Mapping updated: zone " + juce::String(edits.front().zoneIndex + 1)
                            : "Mapping updated: batch";
        return outcome;
    }).ok;
}

bool AudiocityAudioProcessor::ensureImportedProgramSampleAsset(const juce::File& sampleFile,
                                                              int& sampleAssetIndexOut,
                                                              juce::String& errorOut)
{
    sampleAssetIndexOut = -1;
    errorOut.clear();

    if (!hasImportedProgram())
    {
        errorOut = "No library is currently loaded; create or open one before adding samples.";
        return false;
    }

    if (!sampleFile.existsAsFile())
    {
        errorOut = "Sample file does not exist: " + sampleFile.getFullPathName();
        return false;
    }

    const auto samplePath = sampleFile.getFullPathName();
    auto findExistingSampleAssetIndex = [&](const audiocity::engine::Program& program) -> int
    {
        for (int assetIndex = 0; assetIndex < static_cast<int>(program.sampleAssets.size()); ++assetIndex)
        {
            const auto& asset = program.sampleAssets[static_cast<std::size_t>(assetIndex)];
            const auto assetPath = juce::String(asset.sourcePath);
            if (assetPath.isNotEmpty() && assetPath.equalsIgnoreCase(samplePath))
                return assetIndex;
        }

        return -1;
    };

    // Decoding is expensive, so look for an asset we already hold before paying for it.
    auto alreadyPresentIndex = -1;
    if (!importedProgramStore_.read([&](const audiocity::engine::Program& program,
                                        const std::vector<juce::AudioBuffer<float>>&)
    {
        alreadyPresentIndex = findExistingSampleAssetIndex(program);
    }))
    {
        errorOut = "Library state was cleared while adding sample.";
        return false;
    }

    if (alreadyPresentIndex >= 0)
    {
        sampleAssetIndexOut = alreadyPresentIndex;
        return true;
    }

    juce::AudioBuffer<float> buffer;
    double sampleRateHz = 44100.0;
    if (!readAudioFileToBuffer(sampleFile, buffer, sampleRateHz))
    {
        errorOut = "Failed to decode audio file: " + sampleFile.getFullPathName();
        return false;
    }

    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
    {
        errorOut = "Decoded audio buffer was empty: " + sampleFile.getFullPathName();
        return false;
    }

    // The program may have changed while the file was being decoded, so re-check before adding.
    auto raced = false;
    const auto outcome = importedProgramStore_.edit(
        [&](audiocity::plugin::ImportedProgramEdit& edit)
        {
            audiocity::plugin::ImportedProgramEditOutcome result;

            const auto existingIndex = findExistingSampleAssetIndex(edit.program);
            if (existingIndex >= 0)
            {
                raced = true;
                sampleAssetIndexOut = existingIndex;
                return result;
            }

            audiocity::engine::SampleAsset asset;
            asset.sourcePath = samplePath.toStdString();
            asset.displayName = sampleFile.getFileNameWithoutExtension().toStdString();
            asset.lengthSamples = buffer.getNumSamples();
            asset.numChannels = buffer.getNumChannels();
            asset.sampleRateHz = sampleRateHz;
            asset.rootMidiNote = 60;
            asset.bitDepth = 0;
            asset.embeddedInProgram = false;

            edit.program.sampleAssets.push_back(std::move(asset));
            result.appendedSampleData.push_back(buffer);
            result.resultIndex = static_cast<int>(edit.program.sampleAssets.size()) - 1;
            result.label = "Sample added: " + sampleFile.getFileName();
            result.ok = true;
            return result;
        });

    if (raced)
        return sampleAssetIndexOut >= 0;

    if (!outcome.ok)
    {
        errorOut = "Library state was cleared while adding sample.";
        return false;
    }

    sampleAssetIndexOut = outcome.resultIndex;
    return sampleAssetIndexOut >= 0;
}

int AudiocityAudioProcessor::createImportedProgramZoneForSampleAsset(const int sampleAssetIndex,
                                                                     const int seedZoneIndex)
{
    return importedProgramStore_.edit([sampleAssetIndex, seedZoneIndex](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.sampleDataByAsset.empty())
            return outcome;

        const auto newZoneIndex = audiocity::plugin::createProgramZoneForSampleAsset(edit.program,
                                                                                     sampleAssetIndex,
                                                                                     seedZoneIndex);
        if (newZoneIndex < 0)
            return outcome;

        outcome.ok = true;
        outcome.resultIndex = newZoneIndex;
        outcome.label = "Mapping created: zone " + juce::String(newZoneIndex + 1);
        return outcome;
    }).resultIndex;
}

int AudiocityAudioProcessor::createImportedProgramZone(const int seedZoneIndex)
{
    return createImportedProgramZoneForSampleAsset(-1, seedZoneIndex);
}

bool AudiocityAudioProcessor::createEmptyImportedSfzProgram(const juce::String& libraryName)
{
    samplePreviewPlaying_.store(false, std::memory_order_relaxed);
    stopGeneratedWaveformPreview();

    auto trimmedName = libraryName.trim();
    if (trimmedName.isEmpty())
        trimmedName = "New Library";

    audiocity::engine::Program program;
    program.name = trimmedName.toStdString();

    const std::vector<juce::AudioBuffer<float>> sampleData; // empty

    const auto destinationFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                       .getChildFile("Audiocity")
                                       .getChildFile("Libraries");
    const auto syntheticPath = destinationFolder.getChildFile(trimmedName + ".sfz");

    panicAllAudio();
    engine_.clearSamplePath();
    const auto publishResult = engine_.setProgram(program, sampleData);
    if (!publishResult)
    {
        setLastImportDiagnosticSummary(publishResult.diagnostic);
        return false;
    }
    generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
    capturedAudioLoaded_.store(false, std::memory_order_relaxed);
    embeddedSampleLoaded_.store(false, std::memory_order_relaxed);

    setImportedProgramMetadata(syntheticPath,
                               audiocity::plugin::ImportedProgramFormat::sfz,
                               program,
                               sampleData,
                               juce::String("New empty SFZ library created"),
                               0,
                               -1);
    return true;
}

bool AudiocityAudioProcessor::addSampleAssetToImportedProgram(const juce::File& sampleFile,
                                                              juce::String& errorOut)
{
    int sampleAssetIndex = -1;
    if (!ensureImportedProgramSampleAsset(sampleFile, sampleAssetIndex, errorOut))
        return false;

    if (createImportedProgramZoneForSampleAsset(sampleAssetIndex, -1) < 0)
    {
        errorOut = "Failed to create zone for sample asset.";
        return false;
    }

    return true;
}

bool AudiocityAudioProcessor::saveImportedProgramAsSfz(const juce::File& destSfzFile,
                                                       const bool copySamples,
                                                       juce::String& errorOut,
                                                       juce::StringArray* warningsOut)
{
    errorOut.clear();
    if (warningsOut != nullptr)
        warningsOut->clear();

    if (!hasImportedProgram())
    {
        errorOut = "No library is currently loaded.";
        return false;
    }
    if (destSfzFile == juce::File{})
    {
        errorOut = "Destination path is empty.";
        return false;
    }

    audiocity::engine::Program snapshotProgram;
    std::vector<juce::AudioBuffer<float>> snapshotSamples;
    importedProgramStore_.captureSnapshot(snapshotProgram, snapshotSamples);
    const auto libraryDisplayName = importedProgramStore_.getProgramName();

    audiocity::engine::sfz_export::ExportOptions options;
    options.copySamples = copySamples;
    options.libraryDisplayName = libraryDisplayName.toStdString();

    auto result = audiocity::engine::sfz_export::exportProgramToSfz(destSfzFile,
                                                                     snapshotProgram,
                                                                     snapshotSamples,
                                                                     options);

    if (warningsOut != nullptr)
    {
        for (const auto& d : result.diagnostics)
        {
            if (d.severity == audiocity::engine::sfz_export::ExportDiagnostic::Severity::warning)
                warningsOut->add(juce::String::fromUTF8(d.message.c_str()));
        }
    }

    if (result.hasErrors())
    {
        for (const auto& d : result.diagnostics)
        {
            if (d.severity == audiocity::engine::sfz_export::ExportDiagnostic::Severity::error)
            {
                errorOut = juce::String::fromUTF8(d.message.c_str());
                break;
            }
        }
        if (errorOut.isEmpty())
            errorOut = "SFZ export failed.";
        return false;
    }

    // Track the on-disk SFZ path so subsequent saves and the diagnostics panel
    // know where the library lives.
    importedProgramStore_.setSavedLocation(destSfzFile,
                                           audiocity::plugin::ImportedProgramFormat::sfz,
                                           "Library saved to "
                                               + destSfzFile.getFileName()
                                               + " (" + juce::String(result.writtenRegionCount) + " regions, "
                                               + juce::String(result.copiedSampleCount) + " samples copied)");

    return true;
}

bool AudiocityAudioProcessor::saveImportedProgramAsDecentSampler(const juce::File& destPresetFile,
                                                                 const bool copySamples,
                                                                 juce::String& errorOut,
                                                                 juce::StringArray* warningsOut)
{
    errorOut.clear();
    if (warningsOut != nullptr)
        warningsOut->clear();

    if (!hasImportedProgram())
    {
        errorOut = "No library is currently loaded.";
        return false;
    }
    if (destPresetFile == juce::File{})
    {
        errorOut = "Destination path is empty.";
        return false;
    }

    audiocity::engine::Program snapshotProgram;
    std::vector<juce::AudioBuffer<float>> snapshotSamples;
    importedProgramStore_.captureSnapshot(snapshotProgram, snapshotSamples);
    const auto libraryDisplayName = importedProgramStore_.getProgramName();

    audiocity::engine::dspreset_export::ExportOptions options;
    options.copySamples = copySamples;
    options.libraryDisplayName = libraryDisplayName.toStdString();

    auto result = audiocity::engine::dspreset_export::exportProgramToDecentSampler(destPresetFile,
                                                                                    snapshotProgram,
                                                                                    snapshotSamples,
                                                                                    options);

    if (warningsOut != nullptr)
    {
        for (const auto& d : result.diagnostics)
        {
            if (d.severity == audiocity::engine::dspreset_export::ExportDiagnostic::Severity::warning)
                warningsOut->add(juce::String::fromUTF8(d.message.c_str()));
        }
    }

    if (result.hasErrors())
    {
        for (const auto& d : result.diagnostics)
        {
            if (d.severity == audiocity::engine::dspreset_export::ExportDiagnostic::Severity::error)
            {
                errorOut = juce::String::fromUTF8(d.message.c_str());
                break;
            }
        }
        if (errorOut.isEmpty())
            errorOut = "DecentSampler export failed.";
        return false;
    }

    importedProgramStore_.setSavedLocation(destPresetFile,
                                           audiocity::plugin::ImportedProgramFormat::decentSampler,
                                           "Library saved to "
                                               + destPresetFile.getFileName()
                                               + " (" + juce::String(result.writtenSampleCount) + " zones, "
                                               + juce::String(result.copiedSampleCount) + " samples copied)");

    return true;
}

int AudiocityAudioProcessor::duplicateImportedProgramZone(const int zoneIndex)
{
    return importedProgramStore_.edit([zoneIndex](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.program.zones.empty() || edit.sampleDataByAsset.empty())
            return outcome;

        const auto newZoneIndex = audiocity::plugin::duplicateProgramZone(edit.program, zoneIndex);
        if (newZoneIndex < 0)
            return outcome;

        outcome.ok = true;
        outcome.resultIndex = newZoneIndex;
        outcome.label = "Mapping duplicated: zone " + juce::String(newZoneIndex + 1);
        return outcome;
    }).resultIndex;
}

bool AudiocityAudioProcessor::deleteImportedProgramZone(const int zoneIndex)
{
    return deleteImportedProgramZones({ zoneIndex });
}

bool AudiocityAudioProcessor::deleteImportedProgramZones(const std::vector<int>& zoneIndices)
{
    return importedProgramStore_.edit([&zoneIndices](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.program.zones.empty() || edit.sampleDataByAsset.empty())
            return outcome;

        if (!audiocity::plugin::deleteProgramZonesAtomic(edit.program, zoneIndices))
            return outcome;

        outcome.ok = true;
        outcome.label = zoneIndices.size() == 1
                            ? "Mapping deleted: zone " + juce::String(zoneIndices.front() + 1)
                            : "Mapping deleted: batch";
        return outcome;
    }).ok;
}

int AudiocityAudioProcessor::splitImportedProgramZone(const int zoneIndex)
{
    return importedProgramStore_.edit([zoneIndex](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.program.zones.empty() || edit.sampleDataByAsset.empty())
            return outcome;

        const auto newZoneIndex = audiocity::plugin::splitProgramZoneByKey(edit.program, zoneIndex);
        if (newZoneIndex < 0)
            return outcome;

        outcome.ok = true;
        outcome.resultIndex = newZoneIndex;
        outcome.label = "Mapping split: zone " + juce::String(zoneIndex + 1);
        return outcome;
    }).resultIndex;
}

int AudiocityAudioProcessor::splitImportedProgramSliceAtSample(const int sampleIndex)
{
    if (getImportedProgramFormat() != audiocity::plugin::ImportedProgramFormat::sampleSlices)
        return -1;

    return importedProgramStore_.edit([sampleIndex](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.program.zones.empty() || edit.sampleDataByAsset.empty())
            return outcome;

        const auto newZoneIndex = audiocity::plugin::splitProgramSliceAtSample(edit.program, sampleIndex);
        if (newZoneIndex < 0)
            return outcome;

        outcome.ok = true;
        outcome.resultIndex = newZoneIndex;
        outcome.label = "Slice split: zone " + juce::String(newZoneIndex + 1);
        return outcome;
    }).resultIndex;
}

int AudiocityAudioProcessor::mergeImportedProgramSlicesAtSampleBoundary(const int boundarySample)
{
    if (getImportedProgramFormat() != audiocity::plugin::ImportedProgramFormat::sampleSlices)
        return -1;

    return importedProgramStore_.edit([boundarySample](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.program.zones.empty() || edit.sampleDataByAsset.empty())
            return outcome;

        const auto mergedZoneIndex = audiocity::plugin::mergeProgramSlicesAtSampleBoundary(edit.program,
                                                                                           boundarySample);
        if (mergedZoneIndex < 0)
            return outcome;

        outcome.ok = true;
        outcome.resultIndex = mergedZoneIndex;
        outcome.label = "Slice merged: zone " + juce::String(mergedZoneIndex + 1);
        return outcome;
    }).resultIndex;
}

bool AudiocityAudioProcessor::remapImportedProgramZonesChromatically(const std::vector<int>& zoneIndices,
                                                                    const int baseMidiNote)
{
    return importedProgramStore_.edit([&zoneIndices, baseMidiNote](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.program.zones.empty() || edit.sampleDataByAsset.empty())
            return outcome;

        if (!audiocity::plugin::remapProgramZonesChromatically(edit.program, zoneIndices, baseMidiNote))
            return outcome;

        outcome.ok = true;
        outcome.label = zoneIndices.size() == 1
                            ? "Mapping remapped: zone " + juce::String(zoneIndices.front() + 1)
                            : "Mapping remapped: chromatic";
        return outcome;
    }).ok;
}

bool AudiocityAudioProcessor::mapImportedProgramZonesToRootNotes(const std::vector<int>& zoneIndices)
{
    return importedProgramStore_.edit([&zoneIndices](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.program.zones.empty() || edit.sampleDataByAsset.empty())
            return outcome;

        if (!audiocity::plugin::mapProgramZonesToRootNotes(edit.program, zoneIndices))
            return outcome;

        outcome.ok = true;
        outcome.label = "Mapping remapped: root notes";
        return outcome;
    }).ok;
}

bool AudiocityAudioProcessor::spreadImportedProgramZonesAcrossKeyRange(const std::vector<int>& zoneIndices)
{
    return importedProgramStore_.edit([&zoneIndices](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.program.zones.empty() || edit.sampleDataByAsset.empty())
            return outcome;

        if (!audiocity::plugin::spreadProgramZonesAcrossKeyRange(edit.program, zoneIndices))
            return outcome;

        outcome.ok = true;
        outcome.label = "Mapping spread: key range";
        return outcome;
    }).ok;
}

bool AudiocityAudioProcessor::deriveImportedProgramZoneRootsFromKeyRanges(const std::vector<int>& zoneIndices)
{
    return importedProgramStore_.edit([&zoneIndices](audiocity::plugin::ImportedProgramEdit& edit)
    {
        audiocity::plugin::ImportedProgramEditOutcome outcome;
        if (edit.program.zones.empty() || edit.sampleDataByAsset.empty())
            return outcome;

        if (!audiocity::plugin::deriveProgramZoneRootNotesFromKeyRanges(edit.program, zoneIndices))
            return outcome;

        outcome.ok = true;
        outcome.label = "Mapping roots derived: key range";
        return outcome;
    }).ok;
}

juce::String AudiocityAudioProcessor::getLastImportDiagnosticSummary() const
{
    return importedProgramStore_.getLastDiagnosticSummary();
}

juce::String AudiocityAudioProcessor::getPendingImportedAssetRelinkDiagnostic() const
{
    std::lock_guard<std::mutex> lock(pendingImportedAssetMutex_);
    return pendingImportedAssetDiagnostic_;
}

void AudiocityAudioProcessor::setPendingImportedAssetRelink(
    const audiocity::plugin::ImportedAssetManifest& manifest,
    const audiocity::plugin::ImportedProgramFormat format,
    const int selectionIndex,
    const juce::ValueTree& mappingState,
    const juce::String& diagnostic)
{
    {
        std::lock_guard<std::mutex> lock(pendingImportedAssetMutex_);
        pendingImportedAssetManifest_ = manifest;
        pendingImportedAssetFormat_ = format;
        pendingImportedAssetSelectionIndex_ = selectionIndex;
        pendingImportedAssetMappingState_ = mappingState.createCopy();
        pendingImportedAssetDiagnostic_ = diagnostic;
    }
    pendingImportedAssetRelink_.store(true, std::memory_order_release);
    setLastImportDiagnosticSummary(diagnostic);
}

void AudiocityAudioProcessor::clearPendingImportedAssetRelink()
{
    pendingImportedAssetRelink_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(pendingImportedAssetMutex_);
    pendingImportedAssetManifest_ = {};
    pendingImportedAssetFormat_ = audiocity::plugin::ImportedProgramFormat::unknown;
    pendingImportedAssetSelectionIndex_ = -1;
    pendingImportedAssetMappingState_ = {};
    pendingImportedAssetDiagnostic_.clear();
}

bool AudiocityAudioProcessor::restoreImportedProgramFromState(
    const juce::File& file,
    const audiocity::plugin::ImportedProgramFormat format,
    const int selectionIndex,
    const juce::ValueTree& mappingState)
{
    auto prepared = prepareBackgroundImport(file, format, selectionIndex);
    if (!prepared.ok || !prepared.importedProgram)
    {
        setLastImportDiagnosticSummary(prepared.diagnosticSummary.isNotEmpty()
            ? prepared.diagnosticSummary
            : juce::String("Imported program restore could not prepare a complete instrument"));
        return false;
    }

    if (mappingState.isValid())
    {
        const auto restored = audiocity::plugin::buildImportedProgramRestoreResult(
            prepared.program, mappingState);
        if (!restored || !restored->hasPublishableZones)
        {
            setLastImportDiagnosticSummary(
                "Imported program restore rejected its saved mapping; the current instrument was preserved");
            return false;
        }
        prepared.program = std::move(restored->program);
        prepared.diagnosticSummary += "\nSaved mapping restored";
    }

    return publishPreparedBackgroundImport(file, std::move(prepared));
}

bool AudiocityAudioProcessor::relinkPendingImportedProgramFromFolder(const juce::File& selectedRoot)
{
    audiocity::plugin::ImportedAssetManifest manifest;
    auto format = audiocity::plugin::ImportedProgramFormat::unknown;
    int selectionIndex = -1;
    juce::ValueTree mappingState;
    {
        std::lock_guard<std::mutex> lock(pendingImportedAssetMutex_);
        if (!pendingImportedAssetRelink_.load(std::memory_order_acquire))
            return false;
        manifest = pendingImportedAssetManifest_;
        format = pendingImportedAssetFormat_;
        selectionIndex = pendingImportedAssetSelectionIndex_;
        mappingState = pendingImportedAssetMappingState_.createCopy();
    }

    // A manual choice is an explicit search boundary. Do not allow the stale original root
    // to introduce a second solution outside the folder the user selected.
    manifest.originalRoot.clear();
    const auto resolution = audiocity::plugin::resolveImportedAssetManifest(manifest, { selectedRoot });
    if (!resolution.complete)
    {
        setPendingImportedAssetRelink(manifest,
                                      format,
                                      selectionIndex,
                                      mappingState,
                                      "Relink failed: " + resolution.diagnostic);
        return false;
    }

    const auto resolvedProgram = resolution.resolvedProgramFile();
    if (resolvedProgram == juce::File{}
        || !restoreImportedProgramFromState(resolvedProgram, format, selectionIndex, mappingState))
    {
        setPendingImportedAssetRelink(manifest,
                                      format,
                                      selectionIndex,
                                      mappingState,
                                      "Relink found the files but import failed: " + getLastImportDiagnosticSummary());
        return false;
    }

    clearPendingImportedAssetRelink();
    setLastImportDiagnosticSummary("Relinked all imported assets under "
        + resolution.resolvedRoot.getFullPathName());
    lastStateRestoreSource_.store(4, std::memory_order_relaxed);
    return true;
}

bool AudiocityAudioProcessor::loadSampleFromFile(const juce::File& file)
{
    auto prepared = prepareBackgroundImport(file, audiocity::plugin::ImportedProgramFormat::unknown);
    return publishPreparedBackgroundImport(file, std::move(prepared));
}

bool AudiocityAudioProcessor::importSfzProgram(const juce::File& file)
{
    auto prepared = prepareBackgroundImport(file, audiocity::plugin::ImportedProgramFormat::sfz);
    return publishPreparedBackgroundImport(file, std::move(prepared));
}

bool AudiocityAudioProcessor::importSf2Program(const juce::File& file, const int presetIndex)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".sf2"))
    {
        setLastImportDiagnosticSummary("SF2 import failed: file not found or unsupported extension");
        return false;
    }

    auto result = audiocity::engine::sf2::importFilePreset(file, presetIndex);
    const auto hasPlayableProgram = result.hasPlayableProgram();
    const auto imported = !result.hasErrors() && hasPlayableProgram;
    auto summary = audiocity::engine::sf2::buildImportSummary(result, imported);
    if (!hasPlayableProgram && !result.hasErrors())
        summary = "SF2 import failed: no playable zones in chosen preset";

    if (!imported)
    {
        setLastImportDiagnosticSummary(summary);
        return false;
    }

    int displayAssetIndex = -1;
    for (std::size_t i = 0; i < result.sampleDataByAsset.size(); ++i)
    {
        if (result.sampleDataByAsset[i].getNumChannels() > 0
            && result.sampleDataByAsset[i].getNumSamples() > 0)
        {
            displayAssetIndex = static_cast<int>(i);
            break;
        }
    }
    if (displayAssetIndex < 0)
    {
        setLastImportDiagnosticSummary("SF2 import failed: decoded samples were empty");
        return false;
    }

    samplePreviewPlaying_.store(false, std::memory_order_relaxed);
    stopGeneratedWaveformPreview();
    panicAllAudio();

    const auto& displaySample = result.sampleDataByAsset[static_cast<std::size_t>(displayAssetIndex)];
    const auto& displayAsset = result.program.sampleAssets[static_cast<std::size_t>(displayAssetIndex)];
    const auto displaySampleRate = displayAsset.sampleRateHz > 0.0 ? displayAsset.sampleRateHz : 44100.0;
    engine_.publishSampleData(displaySample, displaySampleRate);
    engine_.clearSamplePath();
    const auto publishResult = engine_.setProgram(result.program, result.sampleDataByAsset);
    if (!publishResult)
    {
        setLastImportDiagnosticSummary(publishResult.diagnostic);
        return false;
    }

    generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
    capturedAudioLoaded_.store(false, std::memory_order_relaxed);
    embeddedSampleLoaded_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_.clear();
        capturedSampleState_.clear();
        capturedSampleRateState_ = 44100.0;
        embeddedSampleState_.clear();
        embeddedSampleNameState_.clear();
        embeddedSampleRateState_ = 44100.0;
        embeddedSampleRootMidiNoteState_ = 60;
    }

    setImportedProgramMetadata(file,
                               audiocity::plugin::ImportedProgramFormat::sf2,
                               result.program,
                               result.sampleDataByAsset,
                               summary,
                               static_cast<int>(result.program.zones.size()),
                               result.chosenPresetIndex);
    resetControlsForPublishedSample(displayAsset.rootMidiNote, displaySample.getNumSamples());
    setWaveformViewRange(0, engine_.getLoadedSampleLength());
    return true;
}

bool AudiocityAudioProcessor::importDecentSamplerProgram(const juce::File& file)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".dspreset"))
    {
        setLastImportDiagnosticSummary("DecentSampler import failed: file not found or unsupported extension");
        return false;
    }

    auto result = audiocity::engine::dspreset::importFile(file);
    const auto hasPlayableProgram = result.hasPlayableProgram();
    const auto imported = !result.hasErrors() && hasPlayableProgram;
    auto summary = audiocity::engine::dspreset::buildImportSummary(result, imported);
    if (!hasPlayableProgram && !result.hasErrors())
        summary = "DecentSampler import failed: no playable zones";

    if (!imported)
    {
        setLastImportDiagnosticSummary(summary);
        return false;
    }

    int displayAssetIndex = -1;
    for (std::size_t i = 0; i < result.sampleDataByAsset.size(); ++i)
    {
        if (result.sampleDataByAsset[i].getNumChannels() > 0
            && result.sampleDataByAsset[i].getNumSamples() > 0)
        {
            displayAssetIndex = static_cast<int>(i);
            break;
        }
    }
    if (displayAssetIndex < 0)
    {
        setLastImportDiagnosticSummary("DecentSampler import failed: decoded samples were empty");
        return false;
    }

    samplePreviewPlaying_.store(false, std::memory_order_relaxed);
    stopGeneratedWaveformPreview();
    panicAllAudio();

    const auto& displaySample = result.sampleDataByAsset[static_cast<std::size_t>(displayAssetIndex)];
    const auto& displayAsset = result.program.sampleAssets[static_cast<std::size_t>(displayAssetIndex)];
    const auto displaySampleRate = displayAsset.sampleRateHz > 0.0 ? displayAsset.sampleRateHz : 44100.0;
    engine_.publishSampleData(displaySample, displaySampleRate);
    engine_.clearSamplePath();
    const auto publishResult = engine_.setProgram(result.program, result.sampleDataByAsset);
    if (!publishResult)
    {
        setLastImportDiagnosticSummary(publishResult.diagnostic);
        return false;
    }

    generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
    capturedAudioLoaded_.store(false, std::memory_order_relaxed);
    embeddedSampleLoaded_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_.clear();
        capturedSampleState_.clear();
        capturedSampleRateState_ = 44100.0;
        embeddedSampleState_.clear();
        embeddedSampleNameState_.clear();
        embeddedSampleRateState_ = 44100.0;
        embeddedSampleRootMidiNoteState_ = 60;
    }

    setImportedProgramMetadata(file,
                               audiocity::plugin::ImportedProgramFormat::decentSampler,
                               result.program,
                               result.sampleDataByAsset,
                               summary,
                               static_cast<int>(result.program.zones.size()));
    resetControlsForPublishedSample(displayAsset.rootMidiNote, displaySample.getNumSamples());
    setWaveformViewRange(0, engine_.getLoadedSampleLength());
    return true;
}

bool AudiocityAudioProcessor::importBitwigMultisampleProgram(const juce::File& file)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".multisample"))
    {
        setLastImportDiagnosticSummary("Bitwig multisample import failed: file not found or unsupported extension");
        return false;
    }

    auto result = audiocity::engine::bitwig::importFile(file);
    const auto hasPlayableProgram = result.hasPlayableProgram();
    const auto imported = !result.hasErrors() && hasPlayableProgram;
    auto summary = audiocity::engine::bitwig::buildImportSummary(result, imported);
    if (!hasPlayableProgram && !result.hasErrors())
        summary = "Bitwig multisample import failed: no playable zones";

    if (!imported)
    {
        setLastImportDiagnosticSummary(summary);
        return false;
    }

    int displayAssetIndex = -1;
    for (std::size_t i = 0; i < result.sampleDataByAsset.size(); ++i)
    {
        if (result.sampleDataByAsset[i].getNumChannels() > 0
            && result.sampleDataByAsset[i].getNumSamples() > 0)
        {
            displayAssetIndex = static_cast<int>(i);
            break;
        }
    }
    if (displayAssetIndex < 0)
    {
        setLastImportDiagnosticSummary("Bitwig multisample import failed: decoded samples were empty");
        return false;
    }

    samplePreviewPlaying_.store(false, std::memory_order_relaxed);
    stopGeneratedWaveformPreview();
    panicAllAudio();

    const auto& displaySample = result.sampleDataByAsset[static_cast<std::size_t>(displayAssetIndex)];
    const auto& displayAsset = result.program.sampleAssets[static_cast<std::size_t>(displayAssetIndex)];
    const auto displaySampleRate = displayAsset.sampleRateHz > 0.0 ? displayAsset.sampleRateHz : 44100.0;
    engine_.publishSampleData(displaySample, displaySampleRate);
    engine_.clearSamplePath();
    const auto publishResult = engine_.setProgram(result.program, result.sampleDataByAsset);
    if (!publishResult)
    {
        setLastImportDiagnosticSummary(publishResult.diagnostic);
        return false;
    }

    generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
    capturedAudioLoaded_.store(false, std::memory_order_relaxed);
    embeddedSampleLoaded_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_.clear();
        capturedSampleState_.clear();
        capturedSampleRateState_ = 44100.0;
        embeddedSampleState_.clear();
        embeddedSampleNameState_.clear();
        embeddedSampleRateState_ = 44100.0;
        embeddedSampleRootMidiNoteState_ = 60;
    }

    setImportedProgramMetadata(file,
                               audiocity::plugin::ImportedProgramFormat::bitwigMultisample,
                               result.program,
                               result.sampleDataByAsset,
                               summary,
                               static_cast<int>(result.program.zones.size()));
    resetControlsForPublishedSample(displayAsset.rootMidiNote, displaySample.getNumSamples());
    setWaveformViewRange(0, engine_.getLoadedSampleLength());
    return true;
}

namespace
{
struct PreparedImport
{
    audiocity::engine::Program program;
    std::vector<juce::AudioBuffer<float>> sampleData;
    juce::String summary;
    int displayAssetIndex = -1;
    bool ok = false;
};

template <typename ResultT>
PreparedImport prepareXmlMultisampleImport(ResultT&& result,
                                            juce::String summary,
                                            const juce::String& failurePrefix)
{
    PreparedImport prepared;
    const auto hasPlayable = result.hasPlayableProgram();
    const auto imported = !result.hasErrors() && hasPlayable;
    if (!hasPlayable && !result.hasErrors())
        summary = failurePrefix + " import failed: no playable zones";

    if (!imported)
    {
        prepared.summary = std::move(summary);
        return prepared;
    }
    int idx = -1;
    for (std::size_t i = 0; i < result.sampleDataByAsset.size(); ++i)
    {
        if (result.sampleDataByAsset[i].getNumChannels() > 0
            && result.sampleDataByAsset[i].getNumSamples() > 0)
        { idx = static_cast<int>(i); break; }
    }
    if (idx < 0)
    {
        prepared.summary = failurePrefix + " import failed: decoded samples were empty";
        return prepared;
    }
    prepared.program = std::move(result.program);
    prepared.sampleData = std::move(result.sampleDataByAsset);
    prepared.summary = std::move(summary);
    prepared.displayAssetIndex = idx;
    prepared.ok = true;
    return prepared;
}
} // namespace

bool AudiocityAudioProcessor::publishXmlMultisampleImport(
    const juce::File& file,
    const audiocity::plugin::ImportedProgramFormat formatTag,
    audiocity::engine::Program program,
    std::vector<juce::AudioBuffer<float>> sampleData,
    const juce::String& summary,
    const int displayAssetIndex)
{
    samplePreviewPlaying_.store(false, std::memory_order_relaxed);
    stopGeneratedWaveformPreview();
    panicAllAudio();

    const auto& displaySample = sampleData[static_cast<std::size_t>(displayAssetIndex)];
    const auto& displayAsset = program.sampleAssets[static_cast<std::size_t>(displayAssetIndex)];
    const auto displaySampleRate = displayAsset.sampleRateHz > 0.0 ? displayAsset.sampleRateHz : 44100.0;
    engine_.publishSampleData(displaySample, displaySampleRate);
    engine_.clearSamplePath();
    const auto publishResult = engine_.setProgram(program, sampleData);
    if (!publishResult)
    {
        setLastImportDiagnosticSummary(publishResult.diagnostic);
        return false;
    }

    generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
    capturedAudioLoaded_.store(false, std::memory_order_relaxed);
    embeddedSampleLoaded_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_.clear();
        capturedSampleState_.clear();
        capturedSampleRateState_ = 44100.0;
        embeddedSampleState_.clear();
        embeddedSampleNameState_.clear();
        embeddedSampleRateState_ = 44100.0;
        embeddedSampleRootMidiNoteState_ = 60;
    }

    setImportedProgramMetadata(file, formatTag, program, sampleData, summary,
                               static_cast<int>(program.zones.size()));
    resetControlsForPublishedSample(displayAsset.rootMidiNote, displaySample.getNumSamples());
    setWaveformViewRange(0, engine_.getLoadedSampleLength());
    return true;
}

AudiocityAudioProcessor::PreparedBackgroundImport AudiocityAudioProcessor::prepareBackgroundImport(
    const juce::File& file,
    const audiocity::plugin::ImportedProgramFormat format,
    const int selectionIndex,
    const juce::File& searchFolder,
    const std::atomic<bool>* const cancellationFlag) const
{
    return prepareBackgroundImportJob(file,
                                      format,
                                      selectionIndex,
                                      searchFolder,
                                      getRootMidiNote(),
                                      getPlaybackMode(),
                                      cancellationFlag);
}

AudiocityAudioProcessor::PreparedBackgroundImport AudiocityAudioProcessor::prepareBackgroundImportJob(
    const juce::File& file,
    const audiocity::plugin::ImportedProgramFormat format,
    const int selectionIndex,
    const juce::File& searchFolder,
    const int fallbackRootMidiNote,
    const PlaybackMode fallbackPlaybackMode,
    const std::atomic<bool>* const cancellationFlag)
{
    using audiocity::plugin::ImportedProgramFormat;
    const audiocity::engine::ImportCancellationScope cancellationScope(cancellationFlag);

    auto cancelled = [format]()
    {
        PreparedBackgroundImport prepared;
        prepared.format = format;
        prepared.diagnosticSummary = "Import cancelled";
        return prepared;
    };

    if (audiocity::engine::isImportCancellationRequested())
        return cancelled();

    if (format == ImportedProgramFormat::unknown)
    {
        PreparedBackgroundImport prepared;
        prepared.displaySample = audiocity::engine::EngineCore::prepareSampleFile(
            file,
            fallbackRootMidiNote,
            fallbackPlaybackMode);
        if (audiocity::engine::isImportCancellationRequested())
            return cancelled();
        prepared.diagnosticSummary = prepared.displaySample.errorMessage;
        prepared.ok = prepared.displaySample.ok;
        return prepared;
    }

    switch (format)
    {
        case ImportedProgramFormat::sfz:
        {
            audiocity::engine::SfzImporter importer;
            auto result = importer.importFile(file);
            if (audiocity::engine::isImportCancellationRequested())
                return cancelled();
            const auto hasPlayableProgram = result.program.hasPlayableZones() && !result.sampleDataByAsset.empty();
            const auto imported = !result.hasErrors() && hasPlayableProgram;
            auto summary = makeSfzImportSummary(result, imported);
            if (!hasPlayableProgram && !result.hasErrors())
                summary = "SFZ import failed: no playable zones";

            PreparedBackgroundImport prepared;
            prepared.format = format;
            prepared.importedProgram = true;
            if (!imported)
            {
                prepared.diagnosticSummary = std::move(summary);
                return prepared;
            }

            for (std::size_t assetIndex = 0; assetIndex < result.sampleDataByAsset.size(); ++assetIndex)
            {
                const auto& sampleData = result.sampleDataByAsset[assetIndex];
                if (sampleData.getNumChannels() > 0 && sampleData.getNumSamples() > 0)
                {
                    prepared.displayAssetIndex = static_cast<int>(assetIndex);
                    break;
                }
            }

            if (prepared.displayAssetIndex < 0)
            {
                prepared.diagnosticSummary = "SFZ import failed: decoded samples were empty";
                return prepared;
            }

            prepared.program = std::move(result.program);
            prepared.sampleData = std::move(result.sampleDataByAsset);
            prepared.diagnosticSummary = std::move(summary);
            prepared.ok = true;
            return prepared;
        }
        case ImportedProgramFormat::rex:
        {
            PreparedBackgroundImport prepared;
            prepared.format = format;
            prepared.importedProgram = true;

            audiocity::engine::rex::DecodedLoop decoded;
            if (!audiocity::engine::rex::decodeFile(file, decoded))
            {
                prepared.diagnosticSummary = "REX import failed: runtime unavailable or slices could not be decoded";
                return prepared;
            }
            if (audiocity::engine::isImportCancellationRequested())
                return cancelled();

            audiocity::engine::rex::ChromaticSliceProgram sliceProgram;
            if (!audiocity::engine::rex::buildChromaticSliceProgram(file, decoded, sliceProgram))
            {
                prepared.diagnosticSummary = "REX import failed: no playable slices";
                return prepared;
            }
            if (audiocity::engine::isImportCancellationRequested())
                return cancelled();

            prepared.displaySample = audiocity::engine::EngineCore::prepareSampleFile(
                file,
                fallbackRootMidiNote,
                fallbackPlaybackMode);
            if (audiocity::engine::isImportCancellationRequested())
                return cancelled();
            if (!prepared.displaySample.ok)
            {
                prepared.diagnosticSummary = "REX import failed: display waveform could not be loaded";
                return prepared;
            }

            prepared.program = std::move(sliceProgram.program);
            prepared.sampleData = std::move(sliceProgram.sampleDataByAsset);
            prepared.diagnosticSummary = makeRexSliceImportSummary(sliceProgram);
            prepared.selectionIndex = selectionIndex;
            prepared.ok = !prepared.program.zones.empty() && !prepared.sampleData.empty();
            if (!prepared.ok)
                prepared.diagnosticSummary = "REX import failed: no playable slices";
            return prepared;
        }
        case ImportedProgramFormat::nki:
        {
            auto result = searchFolder.isDirectory()
                ? audiocity::engine::nki::importFile(file, searchFolder)
                : audiocity::engine::nki::importFile(file);
            const auto hasPlayableProgram = result.hasPlayableProgram();
            const auto imported = !result.hasErrors() && hasPlayableProgram;
            auto summary = makeLegacyNkiImportSummary(result, imported);
            if (result.probe.status == audiocity::engine::nki::ProbeStatus::legacyDiscreteSampleCandidate
                && !hasPlayableProgram && !result.hasErrors())
            {
                summary = "NKI import failed: no playable legacy zones";
            }
            return prepareBackgroundImportedProgramResult(std::move(result),
                                                         std::move(summary),
                                                         format,
                                                         "NKI");
        }
        case ImportedProgramFormat::sf2:
        {
            auto result = audiocity::engine::sf2::importFilePreset(file, selectionIndex >= 0 ? selectionIndex : 0);
            const auto chosenSelection = result.chosenPresetIndex;
            const auto imported = !result.hasErrors() && result.hasPlayableProgram();
            auto summary = audiocity::engine::sf2::buildImportSummary(result, imported);
            if (!result.hasPlayableProgram() && !result.hasErrors())
                summary = "SF2 import failed: no playable zones in chosen preset";
            return prepareBackgroundImportedProgramResult(std::move(result),
                                                         std::move(summary),
                                                         format,
                                                         "SF2",
                                                         chosenSelection);
        }
        case ImportedProgramFormat::decentSampler:
        {
            auto result = audiocity::engine::dspreset::importFile(file);
            auto summary = audiocity::engine::dspreset::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "DecentSampler");
        }
        case ImportedProgramFormat::bitwigMultisample:
        {
            auto result = audiocity::engine::bitwig::importFile(file);
            auto summary = audiocity::engine::bitwig::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "Bitwig multisample");
        }
        case ImportedProgramFormat::mpcKeygroup:
        {
            auto result = audiocity::engine::mpc::importFile(file);
            auto summary = audiocity::engine::mpc::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "MPC keygroup");
        }
        case ImportedProgramFormat::bento1010:
        {
            auto result = audiocity::engine::bento::importFile(file);
            auto summary = audiocity::engine::bento::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "1010music preset");
        }
        case ImportedProgramFormat::talSampler:
        {
            auto result = audiocity::engine::talsmpl::importFile(file);
            auto summary = audiocity::engine::talsmpl::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "TAL Sampler");
        }
        case ImportedProgramFormat::tx16wx:
        {
            auto result = audiocity::engine::tx16wx::importFile(file);
            auto summary = audiocity::engine::tx16wx::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "TX16Wx");
        }
        case ImportedProgramFormat::korgMultisample:
        {
            auto result = audiocity::engine::korgmulti::importFile(file);
            auto summary = audiocity::engine::korgmulti::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "Korg multisample");
        }
        case ImportedProgramFormat::abletonSampler:
        {
            auto result = audiocity::engine::ableton::importFile(file);
            auto summary = audiocity::engine::ableton::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "Ableton sampler");
        }
        case ImportedProgramFormat::distingExPreset:
        {
            auto result = audiocity::engine::distingex::importFile(file);
            auto summary = audiocity::engine::distingex::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "disting EX preset");
        }
        case ImportedProgramFormat::korgKmp:
        {
            auto result = audiocity::engine::korgkmp::importFile(file);
            auto summary = audiocity::engine::korgkmp::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "Korg KMP");
        }
        case ImportedProgramFormat::logicExs24:
        {
            auto result = audiocity::engine::exs24::importFile(file);
            auto summary = audiocity::engine::exs24::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "Logic EXS24");
        }
        case ImportedProgramFormat::nnxt:
        {
            auto result = audiocity::engine::nnxt::importFile(file);
            auto summary = audiocity::engine::nnxt::buildImportSummary(result,
                !result.hasErrors() && result.hasPlayableProgram());
            return prepareBackgroundImportedProgramResult(std::move(result), std::move(summary), format, "Reason NN-XT");
        }
        case ImportedProgramFormat::sampleSlices:
        {
            PreparedBackgroundImport prepared;
            prepared.format = format;
            prepared.importedProgram = true;

            juce::AudioBuffer<float> sampleBuffer;
            auto sampleRateHz = 0.0;
            if (!readAudioFileToBuffer(file, sampleBuffer, sampleRateHz))
            {
                prepared.diagnosticSummary = "Transient slice import failed: sample file could not be read";
                return prepared;
            }
            if (audiocity::engine::isImportCancellationRequested())
                return cancelled();

            audiocity::engine::transient_slice::TransientSliceProgram sliceProgram;
            if (!audiocity::engine::transient_slice::buildTransientSliceProgram(
                    file, sampleBuffer, sampleRateHz, sliceProgram))
            {
                prepared.diagnosticSummary = "Transient slice import failed: not enough slice boundaries were detected";
                return prepared;
            }

            prepared.displaySample = audiocity::engine::EngineCore::prepareSampleFile(
                file, fallbackRootMidiNote, fallbackPlaybackMode);
            if (!prepared.displaySample.ok)
            {
                prepared.diagnosticSummary = "Transient slice import failed: display waveform could not be loaded";
                return prepared;
            }

            prepared.diagnosticSummary = makeTransientSliceImportSummary(sliceProgram);
            prepared.program = std::move(sliceProgram.program);
            prepared.sampleData = std::move(sliceProgram.sampleDataByAsset);
            prepared.displayAssetIndex = 0;
            prepared.selectionIndex = selectionIndex;
            prepared.ok = !prepared.program.zones.empty() && !prepared.sampleData.empty();
            return prepared;
        }
        case ImportedProgramFormat::unknown:
        default:
            break;
    }

    PreparedBackgroundImport failed;
    failed.format = format;
    failed.diagnosticSummary = "Background import is not available for this format";
    return failed;
}

bool AudiocityAudioProcessor::publishPreparedBackgroundImport(const juce::File& file,
                                                             PreparedBackgroundImport prepared)
{
    if (!prepared.ok)
    {
        setLastImportDiagnosticSummary(prepared.diagnosticSummary);
        return false;
    }

    if (!prepared.importedProgram)
    {
        samplePreviewPlaying_.store(false, std::memory_order_relaxed);
        stopGeneratedWaveformPreview();
        panicAllAudio();
        engine_.publishPreparedSample(prepared.displaySample);
        engine_.clearProgram();

        clearImportedProgramMetadata();
        generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
        capturedAudioLoaded_.store(false, std::memory_order_relaxed);
        embeddedSampleLoaded_.store(false, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
            generatedWaveformState_.clear();
            capturedSampleState_.clear();
            capturedSampleRateState_ = 44100.0;
            embeddedSampleState_.clear();
            embeddedSampleNameState_.clear();
            embeddedSampleRateState_ = 44100.0;
            embeddedSampleRootMidiNoteState_ = 60;
        }

        applyPreparedSampleControls(prepared.displaySample);
        setWaveformViewRange(0, engine_.getLoadedSampleLength());
        setLastImportDiagnosticSummary(prepared.diagnosticSummary);
        return true;
    }

    if (prepared.displaySample.ok)
    {
        samplePreviewPlaying_.store(false, std::memory_order_relaxed);
        stopGeneratedWaveformPreview();
        panicAllAudio();
        engine_.publishPreparedSample(prepared.displaySample);
        const auto publishResult = engine_.setProgram(prepared.program, prepared.sampleData);
        if (!publishResult)
        {
            setLastImportDiagnosticSummary(publishResult.diagnostic);
            return false;
        }

        generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
        capturedAudioLoaded_.store(false, std::memory_order_relaxed);
        embeddedSampleLoaded_.store(false, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
            generatedWaveformState_.clear();
            capturedSampleState_.clear();
            capturedSampleRateState_ = 44100.0;
            embeddedSampleState_.clear();
            embeddedSampleNameState_.clear();
            embeddedSampleRateState_ = 44100.0;
            embeddedSampleRootMidiNoteState_ = 60;
        }

        setImportedProgramMetadata(file,
                                   prepared.format,
                                   prepared.program,
                                   prepared.sampleData,
                                   prepared.diagnosticSummary,
                                   static_cast<int>(prepared.program.zones.size()),
                                   prepared.selectionIndex);
        applyPreparedSampleControls(prepared.displaySample);
        setWaveformViewRange(0, engine_.getLoadedSampleLength());
        return true;
    }

    return publishPreparedImportedProgram(file,
                                          prepared.format,
                                          std::move(prepared.program),
                                          std::move(prepared.sampleData),
                                          prepared.diagnosticSummary,
                                          prepared.displayAssetIndex,
                                          prepared.selectionIndex);
}

bool AudiocityAudioProcessor::publishPreparedImportedProgram(
    const juce::File& file,
    const audiocity::plugin::ImportedProgramFormat format,
    audiocity::engine::Program program,
    std::vector<juce::AudioBuffer<float>> sampleData,
    const juce::String& diagnosticSummary,
    const int displayAssetIndex,
    const int selectionIndex)
{
    if (displayAssetIndex < 0
        || static_cast<std::size_t>(displayAssetIndex) >= sampleData.size()
        || static_cast<std::size_t>(displayAssetIndex) >= program.sampleAssets.size())
    {
        setLastImportDiagnosticSummary("Prepared import failed: display sample index out of range");
        return false;
    }

    samplePreviewPlaying_.store(false, std::memory_order_relaxed);
    stopGeneratedWaveformPreview();
    panicAllAudio();

    const auto& displaySample = sampleData[static_cast<std::size_t>(displayAssetIndex)];
    const auto& displayAsset = program.sampleAssets[static_cast<std::size_t>(displayAssetIndex)];
    const auto displaySampleRate = displayAsset.sampleRateHz > 0.0 ? displayAsset.sampleRateHz : 44100.0;
    engine_.publishSampleData(displaySample, displaySampleRate);
    engine_.clearSamplePath();
    const auto publishResult = engine_.setProgram(program, sampleData);
    if (!publishResult)
    {
        setLastImportDiagnosticSummary(publishResult.diagnostic);
        return false;
    }

    generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
    capturedAudioLoaded_.store(false, std::memory_order_relaxed);
    embeddedSampleLoaded_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_.clear();
        capturedSampleState_.clear();
        capturedSampleRateState_ = 44100.0;
        embeddedSampleState_.clear();
        embeddedSampleNameState_.clear();
        embeddedSampleRateState_ = 44100.0;
        embeddedSampleRootMidiNoteState_ = 60;
    }

    setImportedProgramMetadata(file,
                               format,
                               program,
                               sampleData,
                               diagnosticSummary,
                               static_cast<int>(program.zones.size()),
                               selectionIndex);
    resetControlsForPublishedSample(displayAsset.rootMidiNote, displaySample.getNumSamples());
    setWaveformViewRange(0, engine_.getLoadedSampleLength());
    return true;
}

void AudiocityAudioProcessor::setImportDiagnosticSummary(const juce::String& diagnosticSummary)
{
    setLastImportDiagnosticSummary(diagnosticSummary);
}

bool AudiocityAudioProcessor::importMpcKeygroupProgram(const juce::File& file)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".xpm"))
    { setLastImportDiagnosticSummary("MPC keygroup import failed: file not found or unsupported extension"); return false; }
    auto r = audiocity::engine::mpc::importFile(file);
    auto s = audiocity::engine::mpc::buildImportSummary(r, !r.hasErrors() && r.hasPlayableProgram());
    auto prep = prepareXmlMultisampleImport(std::move(r), std::move(s), "MPC keygroup");
    if (!prep.ok) { setLastImportDiagnosticSummary(prep.summary); return false; }
    return publishXmlMultisampleImport(file, audiocity::plugin::ImportedProgramFormat::mpcKeygroup,
                                       std::move(prep.program), std::move(prep.sampleData),
                                       prep.summary, prep.displayAssetIndex);
}

bool AudiocityAudioProcessor::import1010MusicPresetProgram(const juce::File& file)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".xml"))
    { setLastImportDiagnosticSummary("1010music preset import failed: file not found or unsupported extension"); return false; }
    auto r = audiocity::engine::bento::importFile(file);
    auto s = audiocity::engine::bento::buildImportSummary(r, !r.hasErrors() && r.hasPlayableProgram());
    auto prep = prepareXmlMultisampleImport(std::move(r), std::move(s), "1010music preset");
    if (!prep.ok) { setLastImportDiagnosticSummary(prep.summary); return false; }
    return publishXmlMultisampleImport(file, audiocity::plugin::ImportedProgramFormat::bento1010,
                                       std::move(prep.program), std::move(prep.sampleData),
                                       prep.summary, prep.displayAssetIndex);
}

bool AudiocityAudioProcessor::importTalSamplerProgram(const juce::File& file)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".talsmpl"))
    { setLastImportDiagnosticSummary("TAL Sampler import failed: file not found or unsupported extension"); return false; }
    auto r = audiocity::engine::talsmpl::importFile(file);
    auto s = audiocity::engine::talsmpl::buildImportSummary(r, !r.hasErrors() && r.hasPlayableProgram());
    auto prep = prepareXmlMultisampleImport(std::move(r), std::move(s), "TAL Sampler");
    if (!prep.ok) { setLastImportDiagnosticSummary(prep.summary); return false; }
    return publishXmlMultisampleImport(file, audiocity::plugin::ImportedProgramFormat::talSampler,
                                       std::move(prep.program), std::move(prep.sampleData),
                                       prep.summary, prep.displayAssetIndex);
}

bool AudiocityAudioProcessor::importTx16WxProgram(const juce::File& file)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".txprog"))
    { setLastImportDiagnosticSummary("TX16Wx import failed: file not found or unsupported extension"); return false; }
    auto r = audiocity::engine::tx16wx::importFile(file);
    auto s = audiocity::engine::tx16wx::buildImportSummary(r, !r.hasErrors() && r.hasPlayableProgram());
    auto prep = prepareXmlMultisampleImport(std::move(r), std::move(s), "TX16Wx");
    if (!prep.ok) { setLastImportDiagnosticSummary(prep.summary); return false; }
    return publishXmlMultisampleImport(file, audiocity::plugin::ImportedProgramFormat::tx16wx,
                                       std::move(prep.program), std::move(prep.sampleData),
                                       prep.summary, prep.displayAssetIndex);
}

bool AudiocityAudioProcessor::importKorgMultisampleProgram(const juce::File& file)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".korgmultisample"))
    { setLastImportDiagnosticSummary("Korg multisample import failed: file not found or unsupported extension"); return false; }
    auto r = audiocity::engine::korgmulti::importFile(file);
    auto s = audiocity::engine::korgmulti::buildImportSummary(r, !r.hasErrors() && r.hasPlayableProgram());
    auto prep = prepareXmlMultisampleImport(std::move(r), std::move(s), "Korg multisample");
    if (!prep.ok) { setLastImportDiagnosticSummary(prep.summary); return false; }
    return publishXmlMultisampleImport(file, audiocity::plugin::ImportedProgramFormat::korgMultisample,
                                       std::move(prep.program), std::move(prep.sampleData),
                                       prep.summary, prep.displayAssetIndex);
}

bool AudiocityAudioProcessor::importAbletonSamplerProgram(const juce::File& file)
{
    const auto ext = file.getFileExtension();
    if (!file.existsAsFile() || (!ext.equalsIgnoreCase(".adv") && !ext.equalsIgnoreCase(".adg")))
    { setLastImportDiagnosticSummary("Ableton sampler import failed: file not found or unsupported extension"); return false; }
    auto r = audiocity::engine::ableton::importFile(file);
    auto s = audiocity::engine::ableton::buildImportSummary(r, !r.hasErrors() && r.hasPlayableProgram());
    auto prep = prepareXmlMultisampleImport(std::move(r), std::move(s), "Ableton sampler");
    if (!prep.ok) { setLastImportDiagnosticSummary(prep.summary); return false; }
    return publishXmlMultisampleImport(file, audiocity::plugin::ImportedProgramFormat::abletonSampler,
                                       std::move(prep.program), std::move(prep.sampleData),
                                       prep.summary, prep.displayAssetIndex);
}

bool AudiocityAudioProcessor::importDistingExPresetProgram(const juce::File& file)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".dexpreset"))
    { setLastImportDiagnosticSummary("disting EX import failed: file not found or unsupported extension"); return false; }
    auto r = audiocity::engine::distingex::importFile(file);
    auto s = audiocity::engine::distingex::buildImportSummary(r, !r.hasErrors() && r.hasPlayableProgram());
    auto prep = prepareXmlMultisampleImport(std::move(r), std::move(s), "disting EX preset");
    if (!prep.ok) { setLastImportDiagnosticSummary(prep.summary); return false; }
    return publishXmlMultisampleImport(file, audiocity::plugin::ImportedProgramFormat::distingExPreset,
                                       std::move(prep.program), std::move(prep.sampleData),
                                       prep.summary, prep.displayAssetIndex);
}

bool AudiocityAudioProcessor::importKorgKmpProgram(const juce::File& file)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".kmp"))
    { setLastImportDiagnosticSummary("Korg KMP import failed: file not found or unsupported extension"); return false; }
    auto r = audiocity::engine::korgkmp::importFile(file);
    auto s = audiocity::engine::korgkmp::buildImportSummary(r, !r.hasErrors() && r.hasPlayableProgram());
    auto prep = prepareXmlMultisampleImport(std::move(r), std::move(s), "Korg KMP");
    if (!prep.ok) { setLastImportDiagnosticSummary(prep.summary); return false; }
    return publishXmlMultisampleImport(file, audiocity::plugin::ImportedProgramFormat::korgKmp,
                                       std::move(prep.program), std::move(prep.sampleData),
                                       prep.summary, prep.displayAssetIndex);
}

bool AudiocityAudioProcessor::importLogicExs24Program(const juce::File& file)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".exs"))
    { setLastImportDiagnosticSummary("EXS24 import failed: file not found or unsupported extension"); return false; }
    auto r = audiocity::engine::exs24::importFile(file);
    auto s = audiocity::engine::exs24::buildImportSummary(r, !r.hasErrors() && r.hasPlayableProgram());
    auto prep = prepareXmlMultisampleImport(std::move(r), std::move(s), "Logic EXS24");
    if (!prep.ok) { setLastImportDiagnosticSummary(prep.summary); return false; }
    return publishXmlMultisampleImport(file, audiocity::plugin::ImportedProgramFormat::logicExs24,
                                       std::move(prep.program), std::move(prep.sampleData),
                                       prep.summary, prep.displayAssetIndex);
}

bool AudiocityAudioProcessor::importNnxtProgram(const juce::File& file)
{
    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".sxt"))
    { setLastImportDiagnosticSummary("NN-XT import failed: file not found or unsupported extension"); return false; }
    auto r = audiocity::engine::nnxt::importFile(file);
    auto s = audiocity::engine::nnxt::buildImportSummary(r, !r.hasErrors() && r.hasPlayableProgram());
    auto prep = prepareXmlMultisampleImport(std::move(r), std::move(s), "Reason NN-XT");
    if (!prep.ok) { setLastImportDiagnosticSummary(prep.summary); return false; }
    return publishXmlMultisampleImport(file, audiocity::plugin::ImportedProgramFormat::nnxt,
                                       std::move(prep.program), std::move(prep.sampleData),
                                       prep.summary, prep.displayAssetIndex);
}

bool AudiocityAudioProcessor::importLegacyNkiProgram(const juce::File& file)
{
    auto prepared = prepareBackgroundImport(file, audiocity::plugin::ImportedProgramFormat::nki);
    return publishPreparedBackgroundImport(file, std::move(prepared));
}

bool AudiocityAudioProcessor::probeLegacyNkiProgram(const juce::File& file)
{
    const auto result = audiocity::engine::nki::probeFile(file);
    setLastImportDiagnosticSummary(audiocity::engine::nki::buildProbeSummary(result));
    return false;
}

bool AudiocityAudioProcessor::importLegacyNkiProgramWithSearchFolder(const juce::File& file,
                                                                      const juce::File& searchFolder)
{
    auto prepared = prepareBackgroundImport(file,
                                            audiocity::plugin::ImportedProgramFormat::nki,
                                            -1,
                                            searchFolder);
    return publishPreparedBackgroundImport(file, std::move(prepared));
}

bool AudiocityAudioProcessor::importRexSliceProgram(const juce::File& file)
{
    auto prepared = prepareBackgroundImport(file, audiocity::plugin::ImportedProgramFormat::rex);
    return publishPreparedBackgroundImport(file, std::move(prepared));
}

bool AudiocityAudioProcessor::importTransientSliceProgram(const juce::File& file)
{
    auto prepared = prepareBackgroundImport(file, audiocity::plugin::ImportedProgramFormat::sampleSlices);
    return publishPreparedBackgroundImport(file, std::move(prepared));
}

void AudiocityAudioProcessor::loadGeneratedWaveformAsSample(const std::vector<float>& waveform, const int rootMidiNote)
{
    if (waveform.empty())
        return;

    panicAllAudio();

    juce::AudioBuffer<float> buffer(1, static_cast<int>(waveform.size()));
    auto* write = buffer.getWritePointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        write[i] = waveform[static_cast<std::size_t>(i)];

    const auto clampedRoot = juce::jlimit(0, 127, rootMidiNote);
    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(clampedRoot);
    const auto generatedSampleRate = juce::jmax(1.0, targetHz * static_cast<double>(buffer.getNumSamples()));
    engine_.publishSampleData(buffer, generatedSampleRate);
    engine_.clearSamplePath();
    engine_.clearProgram();
    clearImportedProgramMetadata();
    if (getPlaybackMode() != PlaybackMode::loop)
        setPlaybackMode(PlaybackMode::loop);
    generatedWaveformLoaded_.store(true, std::memory_order_relaxed);
    capturedAudioLoaded_.store(false, std::memory_order_relaxed);
    embeddedSampleLoaded_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_ = waveform;
        capturedSampleState_.clear();
        capturedSampleRateState_ = 44100.0;
        embeddedSampleState_.clear();
        embeddedSampleNameState_.clear();
        embeddedSampleRateState_ = 44100.0;
        embeddedSampleRootMidiNoteState_ = 60;
    }
    stopGeneratedWaveformPreview();
    resetControlsForPublishedSample(clampedRoot, buffer.getNumSamples());
    setWaveformViewRange(0, engine_.getLoadedSampleLength());
}

void AudiocityAudioProcessor::loadEmbeddedSampleAsSample(const juce::AudioBuffer<float>& buffer,
                                                         const double sampleRate,
                                                         const int rootMidiNote,
                                                         const juce::String& displayName)
{
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return;

    const auto clampedRoot = juce::jlimit(0, 127, rootMidiNote);
    const auto safeRate = juce::jmax(1.0, sampleRate);

    samplePreviewPlaying_.store(false, std::memory_order_relaxed);
    stopGeneratedWaveformPreview();
    panicAllAudio();

    engine_.publishSampleData(buffer, safeRate);
    engine_.clearSamplePath();
    engine_.clearProgram();
    clearImportedProgramMetadata();

    generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
    capturedAudioLoaded_.store(false, std::memory_order_relaxed);
    embeddedSampleLoaded_.store(true, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        generatedWaveformState_.clear();
        capturedSampleState_.clear();
        capturedSampleRateState_ = 44100.0;
        embeddedSampleState_.clear();
        embeddedSampleNameState_.clear();
        embeddedSampleRateState_ = 44100.0;
        embeddedSampleRootMidiNoteState_ = 60;
        const auto channels = buffer.getNumChannels();
        const auto samples = buffer.getNumSamples();
        embeddedSampleState_.resize(static_cast<std::size_t>(channels) * static_cast<std::size_t>(samples));
        auto* dest = embeddedSampleState_.data();
        for (int channel = 0; channel < channels; ++channel)
        {
            std::memcpy(dest + static_cast<std::size_t>(channel) * static_cast<std::size_t>(samples),
                        buffer.getReadPointer(channel),
                        static_cast<std::size_t>(samples) * sizeof(float));
        }
        embeddedSampleRateState_ = safeRate;
        embeddedSampleRootMidiNoteState_ = clampedRoot;
        embeddedSampleNameState_ = displayName;
    }

    resetControlsForPublishedSample(clampedRoot, buffer.getNumSamples());
    setWaveformViewRange(0, engine_.getLoadedSampleLength());
}

juce::String AudiocityAudioProcessor::getEmbeddedSampleName() const
{
    std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
    return embeddedSampleNameState_;
}

juce::String AudiocityAudioProcessor::getLoadedSamplePath() const
{
    return engine_.getSamplePath();
}

void AudiocityAudioProcessor::setPlaybackMode(const PlaybackMode mode) noexcept
{
    updateParameterFromPlainValue(kParamPlaybackMode, static_cast<float>(mode));
}

AudiocityAudioProcessor::PlaybackMode AudiocityAudioProcessor::getPlaybackMode() const noexcept
{
    return static_cast<PlaybackMode>(juce::jlimit(0, 2,
        static_cast<int>(std::round(getParameterPlainValue(kParamPlaybackMode)))));
}

void AudiocityAudioProcessor::setMonoMode(const bool enabled) noexcept
{
    updateParameterFromPlainValue(kParamMonoMode, enabled ? 1.0f : 0.0f);
}

bool AudiocityAudioProcessor::getMonoMode() const noexcept
{
    return getParameterPlainValue(kParamMonoMode) >= 0.5f;
}

void AudiocityAudioProcessor::setLegatoMode(const bool enabled) noexcept
{
    updateParameterFromPlainValue(kParamLegatoMode, enabled ? 1.0f : 0.0f);
}

bool AudiocityAudioProcessor::getLegatoMode() const noexcept
{
    return getParameterPlainValue(kParamLegatoMode) >= 0.5f;
}

void AudiocityAudioProcessor::setGlideSeconds(const float seconds) noexcept
{
    updateParameterFromPlainValue(kParamGlideSeconds, seconds);
}

float AudiocityAudioProcessor::getGlideSeconds() const noexcept
{
    return getParameterPlainValue(kParamGlideSeconds);
}

void AudiocityAudioProcessor::setPolyphonyLimit(const int voices) noexcept
{
    updateParameterFromPlainValue(kParamPolyphonyLimit, static_cast<float>(voices));
}

int AudiocityAudioProcessor::getPolyphonyLimit() const noexcept
{
    return static_cast<int>(std::round(getParameterPlainValue(kParamPolyphonyLimit)));
}

void AudiocityAudioProcessor::setFadeSamples(const int fadeInSamples, const int fadeOutSamples) noexcept
{
    const auto maxFade = juce::jmax(0, getSampleWindowEnd() - getSampleWindowStart());
    updateParameterFromPlainValue(kParamFadeIn, static_cast<float>(juce::jlimit(0, maxFade, fadeInSamples)));
    updateParameterFromPlainValue(kParamFadeOut, static_cast<float>(juce::jlimit(0, maxFade, fadeOutSamples)));
}

int AudiocityAudioProcessor::getFadeInSamples() const noexcept
{
    return static_cast<int>(std::round(getParameterPlainValue(kParamFadeIn)));
}

int AudiocityAudioProcessor::getFadeOutSamples() const noexcept
{
    return static_cast<int>(std::round(getParameterPlainValue(kParamFadeOut)));
}

void AudiocityAudioProcessor::setReversePlayback(const bool enabled) noexcept
{
    updateParameterFromPlainValue(kParamReversePlayback, enabled ? 1.0f : 0.0f);
}

bool AudiocityAudioProcessor::getReversePlayback() const noexcept
{
    return getParameterPlainValue(kParamReversePlayback) >= 0.5f;
}

void AudiocityAudioProcessor::setAmpEnvelope(const AdsrSettings& settings) noexcept
{
    updateParameterFromPlainValue(kParamAmpAttack, settings.attackSeconds);
    updateParameterFromPlainValue(kParamAmpDecay, settings.decaySeconds);
    updateParameterFromPlainValue(kParamAmpSustain, settings.sustainLevel);
    updateParameterFromPlainValue(kParamAmpRelease, settings.releaseSeconds);
}

AudiocityAudioProcessor::AdsrSettings AudiocityAudioProcessor::getAmpEnvelope() const noexcept
{
    return {
        getParameterPlainValue(kParamAmpAttack),
        getParameterPlainValue(kParamAmpDecay),
        getParameterPlainValue(kParamAmpSustain),
        getParameterPlainValue(kParamAmpRelease)
    };
}

void AudiocityAudioProcessor::setAmpLfoSettings(const AmpLfoSettings& settings) noexcept
{
    updateParameterFromPlainValue(kParamAmpLfoRate, settings.rateHz);
    updateParameterFromPlainValue(kParamAmpLfoDepth, settings.depth);
    updateParameterFromPlainValue(kParamAmpLfoShape, static_cast<float>(settings.shape));
}

AudiocityAudioProcessor::AmpLfoSettings AudiocityAudioProcessor::getAmpLfoSettings() const noexcept
{
    return {
        getParameterPlainValue(kParamAmpLfoRate),
        getParameterPlainValue(kParamAmpLfoDepth),
        static_cast<FilterSettings::LfoShape>(juce::jlimit(0, 4,
            static_cast<int>(std::round(getParameterPlainValue(kParamAmpLfoShape)))))
    };
}

void AudiocityAudioProcessor::setRootMidiNote(const int rootNote) noexcept
{
    updateParameterFromPlainValue(kParamRootMidiNote, static_cast<float>(rootNote));
}

int AudiocityAudioProcessor::getRootMidiNote() const noexcept
{
    return static_cast<int>(std::round(getParameterPlainValue(kParamRootMidiNote)));
}

void AudiocityAudioProcessor::setCoarseTuneSemitones(const float semitones) noexcept
{
    updateParameterFromPlainValue(kParamTuneCoarse, semitones);
}

float AudiocityAudioProcessor::getCoarseTuneSemitones() const noexcept
{
    return getParameterPlainValue(kParamTuneCoarse);
}

void AudiocityAudioProcessor::setFineTuneCents(const float cents) noexcept
{
    updateParameterFromPlainValue(kParamTuneFine, cents);
}

float AudiocityAudioProcessor::getFineTuneCents() const noexcept
{
    return getParameterPlainValue(kParamTuneFine);
}

void AudiocityAudioProcessor::setPitchBendRangeSemitones(const float semitones) noexcept
{
    updateParameterFromPlainValue(kParamPitchBendRange, semitones);
}

float AudiocityAudioProcessor::getPitchBendRangeSemitones() const noexcept
{
    return getParameterPlainValue(kParamPitchBendRange);
}

void AudiocityAudioProcessor::setPitchLfoSettings(const PitchLfoSettings& settings) noexcept
{
    updateParameterFromPlainValue(kParamPitchLfoRate, settings.rateHz);
    updateParameterFromPlainValue(kParamPitchLfoDepth, settings.depthCents);
}

AudiocityAudioProcessor::PitchLfoSettings AudiocityAudioProcessor::getPitchLfoSettings() const noexcept
{
    return {
        getParameterPlainValue(kParamPitchLfoRate),
        getParameterPlainValue(kParamPitchLfoDepth)
    };
}

void AudiocityAudioProcessor::setModulationRoutingSettings(const ModulationRoutingSettings& settings) noexcept
{
    updateModulationRouteParameters(settings, [this](const char* parameterId, const float value)
    {
        updateParameterFromPlainValue(parameterId, value);
    });
}

AudiocityAudioProcessor::ModulationRoutingSettings AudiocityAudioProcessor::getModulationRoutingSettings() const noexcept
{
    ModulationRoutingSettings settings;
    loadModulationRoutesFromParameters(apvts_, settings);
    return settings;
}

void AudiocityAudioProcessor::setMacroControlValues(const MacroControlValues& values) noexcept
{
    updateMacroControlParameters(values, [this](const char* parameterId, const float value)
    {
        updateParameterFromPlainValue(parameterId, value);
    });
}

AudiocityAudioProcessor::MacroControlValues AudiocityAudioProcessor::getMacroControlValues() const noexcept
{
    MacroControlValues values{};
    loadMacroControlsFromParameters(apvts_, values);
    return values;
}

void AudiocityAudioProcessor::setSampleWindow(const int startSample, const int endSample) noexcept
{
    const auto maxValid = juce::jmax(0, getLoadedSampleLength() - 1);
    const auto clampedStart = juce::jlimit(0, maxValid, startSample);
    auto clampedEnd = juce::jlimit(0, maxValid, endSample);
    if (clampedEnd <= clampedStart)
        clampedEnd = maxValid;

    updateParameterFromPlainValue(kParamPlaybackStart, static_cast<float>(clampedStart));
    updateParameterFromPlainValue(kParamPlaybackEnd, static_cast<float>(clampedEnd));
}

int AudiocityAudioProcessor::getSampleWindowStart() const noexcept
{
    return static_cast<int>(std::round(getParameterPlainValue(kParamPlaybackStart)));
}

int AudiocityAudioProcessor::getSampleWindowEnd() const noexcept
{
    return static_cast<int>(std::round(getParameterPlainValue(kParamPlaybackEnd)));
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
    editorTabIndex_.store(juce::jlimit(0, 6, tabIndex), std::memory_order_relaxed);
}

int AudiocityAudioProcessor::getEditorTabIndex() const noexcept
{
    return juce::jlimit(0, 6, editorTabIndex_.load(std::memory_order_relaxed));
}

void AudiocityAudioProcessor::setSampleInspectorFilterModExpanded(const bool expanded) noexcept
{
    sampleInspectorFilterModExpanded_.store(expanded, std::memory_order_relaxed);
}

bool AudiocityAudioProcessor::getSampleInspectorFilterModExpanded() const noexcept
{
    return sampleInspectorFilterModExpanded_.load(std::memory_order_relaxed);
}

void AudiocityAudioProcessor::setSampleInspectorEffectsExpanded(const bool expanded) noexcept
{
    sampleInspectorEffectsExpanded_.store(expanded, std::memory_order_relaxed);
}

bool AudiocityAudioProcessor::getSampleInspectorEffectsExpanded() const noexcept
{
    return sampleInspectorEffectsExpanded_.load(std::memory_order_relaxed);
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
    captureRecording_.store(false, std::memory_order_release);
    waitForCaptureAudioReaders();
    if (captureInputLeft_ == nullptr || captureInputRight_ == nullptr)
    {
        captureInputLeft_.reset(new (std::nothrow) float[static_cast<std::size_t>(kCaptureMaxSamplesPerChannel)]);
        captureInputRight_.reset(new (std::nothrow) float[static_cast<std::size_t>(kCaptureMaxSamplesPerChannel)]);
        if (captureInputLeft_ == nullptr || captureInputRight_ == nullptr)
        {
            releaseCaptureWorkingStorage();
            captureOverflow_.store(true, std::memory_order_relaxed);
            return;
        }
        captureWorkingStorageBytes_.store(
            static_cast<std::size_t>(kCaptureMaxSamplesPerChannel) * 2u * sizeof(float),
            std::memory_order_release);
    }

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
    waitForCaptureAudioReaders();
    captureInputSamples_.store(0, std::memory_order_relaxed);
    captureOverflow_.store(false, std::memory_order_relaxed);
    releaseCaptureWorkingStorage();
}

void AudiocityAudioProcessor::waitForCaptureAudioReaders() noexcept
{
    while (captureAudioReaders_.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
}

void AudiocityAudioProcessor::releaseCaptureWorkingStorage() noexcept
{
    captureInputLeft_.reset();
    captureInputRight_.reset();
    captureWorkingStorageBytes_.store(0, std::memory_order_release);
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
    if (captureInputLeft_ == nullptr || captureInputRight_ == nullptr)
        return {};

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
    waitForCaptureAudioReaders();
    if (captureInputLeft_ == nullptr || captureInputRight_ == nullptr)
        return false;
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
        std::memmove(captureInputLeft_.get() + start,
            captureInputLeft_.get() + end,
            static_cast<std::size_t>(tailLength) * sizeof(float));
        std::memmove(captureInputRight_.get() + start,
            captureInputRight_.get() + end,
            static_cast<std::size_t>(tailLength) * sizeof(float));
    }

    captureInputSamples_.store(total - cutLength, std::memory_order_release);
    return true;
}

bool AudiocityAudioProcessor::trimCapturedAudioRange(const int startSample, const int endSample) noexcept
{
    stopInputCapture();
    waitForCaptureAudioReaders();
    if (captureInputLeft_ == nullptr || captureInputRight_ == nullptr)
        return false;
    const auto total = juce::jlimit(0, kCaptureMaxSamplesPerChannel,
        captureInputSamples_.load(std::memory_order_acquire));
    const auto start = juce::jlimit(0, total, startSample);
    const auto end = juce::jlimit(start, total, endSample);
    if (end <= start)
        return false;

    const auto newLength = end - start;
    if (start > 0)
    {
        std::memmove(captureInputLeft_.get(),
            captureInputLeft_.get() + start,
            static_cast<std::size_t>(newLength) * sizeof(float));
        std::memmove(captureInputRight_.get(),
            captureInputRight_.get() + start,
            static_cast<std::size_t>(newLength) * sizeof(float));
    }

    captureInputSamples_.store(newLength, std::memory_order_release);
    return true;
}

bool AudiocityAudioProcessor::normalizeCapturedAudio(const float targetPeak) noexcept
{
    stopInputCapture();
    waitForCaptureAudioReaders();
    if (captureInputLeft_ == nullptr || captureInputRight_ == nullptr)
        return false;
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
    waitForCaptureAudioReaders();
    if (captureInputLeft_ == nullptr || captureInputRight_ == nullptr)
        return false;
    const auto total = juce::jlimit(0, kCaptureMaxSamplesPerChannel,
        captureInputSamples_.load(std::memory_order_acquire));
    if (total <= 1)
        return false;

    startSample = juce::jlimit(0, total - 1, startSample);
    endSample = juce::jlimit(startSample + 1, total, endSample <= startSample ? total : endSample);
    const auto numSourceSamples = endSample - startSample;
    if (numSourceSamples <= 1)
        return false;

    panicAllAudio();

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

    const auto publishedRootMidiNote = getRootMidiNote();
    auto publishedSampleLength = 0;
    if (std::abs(targetRate - sourceRate) < 0.5)
    {
        engine_.publishSampleData(source, sourceRate);
        publishedSampleLength = source.getNumSamples();
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

        engine_.publishSampleData(resampled, targetRate);
        publishedSampleLength = resampled.getNumSamples();
        persistCapturedState(resampled, targetRate);
    }

    generatedWaveformLoaded_.store(false, std::memory_order_relaxed);
    capturedAudioLoaded_.store(true, std::memory_order_relaxed);
    embeddedSampleLoaded_.store(false, std::memory_order_relaxed);
    engine_.clearProgram();
    clearImportedProgramMetadata();
    {
        std::lock_guard<std::mutex> lock(generatedWaveformStateMutex_);
        if (capturedSampleState_.empty())
            capturedSampleRateState_ = 44100.0;
            embeddedSampleState_.clear();
            embeddedSampleNameState_.clear();
            embeddedSampleRateState_ = 44100.0;
            embeddedSampleRootMidiNoteState_ = 60;
    }

    engine_.clearSamplePath();
    resetControlsForPublishedSample(publishedRootMidiNote, publishedSampleLength);
    setWaveformViewRange(0, engine_.getLoadedSampleLength());
    captureInputSamples_.store(0, std::memory_order_relaxed);
    releaseCaptureWorkingStorage();
    return true;
}

void AudiocityAudioProcessor::setLoopPoints(const int loopStart, const int loopEnd) noexcept
{
    const auto maxValid = juce::jmax(0, getLoadedSampleLength() - 1);
    const auto clampedStart = juce::jlimit(0, maxValid, loopStart);
    auto clampedEnd = juce::jlimit(0, maxValid, loopEnd);
    if (clampedEnd <= clampedStart)
        clampedEnd = maxValid;

    updateParameterFromPlainValue(kParamLoopStart, static_cast<float>(clampedStart));
    updateParameterFromPlainValue(kParamLoopEnd, static_cast<float>(clampedEnd));
}

int AudiocityAudioProcessor::getLoopStart() const noexcept
{
    return static_cast<int>(std::round(getParameterPlainValue(kParamLoopStart)));
}

int AudiocityAudioProcessor::getLoopEnd() const noexcept
{
    return static_cast<int>(std::round(getParameterPlainValue(kParamLoopEnd)));
}

void AudiocityAudioProcessor::setLoopCrossfadeSamples(const int crossfadeSamples) noexcept
{
    updateParameterFromPlainValue(kParamLoopCrossfade, static_cast<float>(crossfadeSamples));
}

int AudiocityAudioProcessor::getLoopCrossfadeSamples() const noexcept
{
    return static_cast<int>(std::round(getParameterPlainValue(kParamLoopCrossfade)));
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
    waitForCaptureAudioReaders();
    if (captureInputLeft_ == nullptr || captureInputRight_ == nullptr)
        return false;

    const auto total = juce::jlimit(0, kCaptureMaxSamplesPerChannel,
        captureInputSamples_.load(std::memory_order_acquire));
    if (total <= 1)
        return false;

    const auto samplesToPreview = juce::jlimit(1, kSamplePreviewMaxSamples, total);
    const auto mode = getCaptureChannelMode();
    auto preview = std::make_shared<SamplePreviewSnapshot>();
    preview->samples.resize(static_cast<std::size_t>(samplesToPreview));
    preview->sourceRate = juce::jmax(1.0, captureInputSampleRate_.load(std::memory_order_relaxed));
    for (int sample = 0; sample < samplesToPreview; ++sample)
    {
        const auto left = captureInputLeft_[static_cast<std::size_t>(sample)];
        const auto right = captureInputRight_[static_cast<std::size_t>(sample)];

        float mono = 0.5f * (left + right);
        if (mode == 1)
            mono = left;
        else if (mode == 2)
            mono = right;

        preview->samples[static_cast<std::size_t>(sample)] = juce::jlimit(-1.0f, 1.0f, mono);
    }

    samplePreviewSnapshot_.publish(std::move(preview));
    samplePreviewReadPos_ = 0.0f;
    previewWavePlaying_.store(false, std::memory_order_relaxed);
    panicAllAudio();
    samplePreviewPlaying_.store(true, std::memory_order_relaxed);
    return true;
}

bool AudiocityAudioProcessor::previewSampleFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    juce::AudioFormatManager formatManager;
    audiocity::engine::audio_file::registerAudioFormats(formatManager);

    auto openResult = audiocity::engine::audio_file::openReaderForFile(formatManager, file);
    auto reader = std::move(openResult.reader);
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
    panicAllAudio();

    const auto channels = tempBuffer.getNumChannels();
    auto preview = std::make_shared<SamplePreviewSnapshot>();
    preview->samples.resize(static_cast<std::size_t>(samplesToRead));
    preview->sourceRate = juce::jmax(1.0, reader->sampleRate);
    for (int i = 0; i < samplesToRead; ++i)
    {
        float mono = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            mono += tempBuffer.getSample(channel, i);

        preview->samples[static_cast<std::size_t>(i)] =
            juce::jlimit(-1.0f, 1.0f, mono / static_cast<float>(channels));
    }

    samplePreviewSnapshot_.publish(std::move(preview));
    samplePreviewReadPos_ = 0.0f;
    samplePreviewPlaying_.store(true, std::memory_order_relaxed);
    return true;
}

void AudiocityAudioProcessor::panicAllAudio() noexcept
{
    stopGeneratedWaveformPreview();
    samplePreviewPlaying_.store(false, std::memory_order_relaxed);
    panicRequested_.store(true, std::memory_order_release);
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

bool AudiocityAudioProcessor::consumeExternalMidiDisplayEvent(int& noteNumber,
                                                              int& velocity,
                                                              bool& isNoteOn) noexcept
{
    ExternalMidiDisplayEvent event{};
    if (!popExternalMidiDisplayEvent(event))
        return false;

    noteNumber = event.noteNumber;
    velocity = event.velocity;
    isNoteOn = event.isNoteOn;
    return true;
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
        isNoteOn ? UiMidiEvent::Type::noteOn : UiMidiEvent::Type::noteOff
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

void AudiocityAudioProcessor::pushExternalMidiDisplayEvent(const int noteNumber,
                                                           const int velocity,
                                                           const bool isNoteOn) noexcept
{
    const auto writePos = externalMidiDisplayWritePos_.load(std::memory_order_relaxed);
    const auto nextWrite = (writePos + 1) % kExternalMidiDisplayFifoSize;

    if (nextWrite == externalMidiDisplayReadPos_.load(std::memory_order_acquire))
    {
        externalMidiDisplayReadPos_.store((externalMidiDisplayReadPos_.load(std::memory_order_relaxed) + 1)
                                              % kExternalMidiDisplayFifoSize,
                                          std::memory_order_release);
    }

    externalMidiDisplayFifo_[static_cast<std::size_t>(writePos)] = {
        juce::jlimit(0, 127, noteNumber),
        juce::jlimit(0, 127, velocity),
        isNoteOn
    };
    externalMidiDisplayWritePos_.store(nextWrite, std::memory_order_release);
}

bool AudiocityAudioProcessor::popExternalMidiDisplayEvent(ExternalMidiDisplayEvent& out) noexcept
{
    const auto readPos = externalMidiDisplayReadPos_.load(std::memory_order_relaxed);
    if (readPos == externalMidiDisplayWritePos_.load(std::memory_order_acquire))
        return false;

    out = externalMidiDisplayFifo_[static_cast<std::size_t>(readPos)];
    externalMidiDisplayReadPos_.store((readPos + 1) % kExternalMidiDisplayFifoSize, std::memory_order_release);
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
    struct CaptureReaderScope
    {
        explicit CaptureReaderScope(std::atomic<int>& readersIn) noexcept : readers(readersIn)
        {
            readers.fetch_add(1, std::memory_order_acq_rel);
        }
        ~CaptureReaderScope()
        {
            readers.fetch_sub(1, std::memory_order_acq_rel);
        }
        std::atomic<int>& readers;
    } readerScope(captureAudioReaders_);

    if (!captureRecording_.load(std::memory_order_acquire))
        return false;

    if (captureInputLeft_ == nullptr || captureInputRight_ == nullptr)
    {
        captureRecording_.store(false, std::memory_order_release);
        captureOverflow_.store(true, std::memory_order_relaxed);
        return false;
    }

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

void AudiocityAudioProcessor::resetControlsForPublishedSample(const int rootMidiNote,
                                                              const int sampleLength) noexcept
{
    const auto lastSample = juce::jmax(0, sampleLength - 1);
    setRootMidiNote(rootMidiNote);
    setSampleWindow(0, lastSample);
    setFadeSamples(0, 0);
    setReversePlayback(false);
    setLoopPoints(0, lastSample);
    controlResyncRequested_.store(true, std::memory_order_release);
}

void AudiocityAudioProcessor::applyPreparedSampleControls(
    const audiocity::engine::EngineCore::PreparedSampleFile& prepared) noexcept
{
    resetControlsForPublishedSample(prepared.rootMidiNote, prepared.sampleData.getNumSamples());
    setAmpEnvelope({});
    setAmpLfoSettings({});
    setPitchLfoSettings({});
    setFilterEnvelope({ 0.001f, 0.120f, 0.0f, 0.100f });

    FilterSettings filter;
    filter.baseCutoffHz = 18000.0f;
    filter.envAmountHz = 0.0f;
    filter.resonance = 0.0f;
    filter.mode = FilterSettings::Mode::lowPass12;
    filter.keyTracking = 0.0f;
    filter.velocityAmountHz = 0.0f;
    filter.lfoRateHz = 0.0f;
    filter.lfoRateKeyTracking = 0.0f;
    filter.lfoAmountHz = 0.0f;
    filter.lfoAmountKeyTracking = 0.0f;
    filter.lfoStartPhaseDegrees = 0.0f;
    filter.lfoStartPhaseRandomDegrees = 0.0f;
    filter.lfoFadeInMs = 0.0f;
    filter.lfoKeytrackLinear = false;
    filter.lfoUnipolar = false;
    filter.lfoShape = FilterSettings::LfoShape::sine;
    filter.lfoRetrigger = true;
    filter.lfoTempoSync = false;
    filter.lfoRateKeytrackInTempoSync = true;
    filter.lfoSyncDivision = 6;
    setFilterSettings(filter);

    setSampleWindow(prepared.sampleWindowStart, prepared.sampleWindowEnd);
    setLoopPoints(prepared.loopStart, prepared.loopEnd);
    setPlaybackMode(prepared.playbackMode);
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
    const auto preview = samplePreviewSnapshot_.read(audiocity::engine::RtReaderRole::audio);
    if (!preview || preview->samples.empty())
    {
        buffer.clear();
        samplePreviewPlaying_.store(false, std::memory_order_relaxed);
        return;
    }

    const auto count = static_cast<int>(preview->samples.size());
    const auto channels = buffer.getNumChannels();
    const auto samples = buffer.getNumSamples();
    const auto sourceRate = preview->sourceRate;
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
        const auto s0 = preview->samples[static_cast<std::size_t>(i0)];
        const auto s1 = preview->samples[static_cast<std::size_t>(i1)];
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

#if ! defined(AUDIOCITY_UI_SNAPSHOT_HARNESS)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudiocityAudioProcessor();
}
#endif
