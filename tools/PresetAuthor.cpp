// Audiocity Stock Preset Authoring Tool
//
// Produces deterministic .acp factory preset files that contain pre-synthesized
// embedded sample audio plus engine parameter overrides. The bank covers
// 8 families x 16 presets = 128 factory presets.
//
// The output path is provided as the first command-line argument:
//     audiocity_preset_author <output_dir>
// If omitted, presets are written to "<source-dir>/assets/factory_presets".
//
// Real-time safety: this is an offline tool; the synthesis runs on the main
// thread before exiting. None of this code runs from the audio thread.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <string>
#include <limits>
#include <vector>

#include "../src/plugin/PresetJson.h"
#include "../src/engine/Sf2Importer.h"

namespace
{
constexpr auto kPatchRoot = "AudiocityPatch";

// Embedded-sample state keys (kept in sync with PluginProcessor.cpp anonymous namespace).
constexpr auto kEmbeddedSampleData = "embeddedSampleData";
constexpr auto kEmbeddedSampleRate = "embeddedSampleRate";
constexpr auto kEmbeddedSampleRootMidiNote = "embeddedSampleRootMidiNote";
constexpr auto kEmbeddedSampleName = "embeddedSampleName";
constexpr auto kEmbeddedSampleChannels = "embeddedSampleChannels";
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

constexpr auto kPlaybackMode = "playbackMode"; // 0=gate, 1=oneShot, 2=loop
constexpr auto kQualityTier = "qualityTier";
constexpr auto kVelocityCurve = "velocityCurve";
constexpr auto kReverbMix = "reverbMix";
constexpr auto kDelayMix = "delayMix";
constexpr auto kDelayTimeMs = "delayTimeMs";
constexpr auto kDelayFeedback = "delayFeedback";
constexpr auto kDelayTempoSync = "delayTempoSync";
constexpr auto kDcFilterEnabled = "dcFilterEnabled";
constexpr auto kDcFilterCutoffHz = "dcFilterCutoffHz";
constexpr auto kAutopanRateHz = "autopanRateHz";
constexpr auto kAutopanDepth = "autopanDepth";
constexpr auto kSaturationDrive = "saturationDrive";
constexpr auto kSaturationMode = "saturationMode";
constexpr auto kPan = "pan";
constexpr auto kMasterVolume = "masterVolume";
constexpr auto kMonoMode = "monoMode";
constexpr auto kLegatoMode = "legatoMode";
constexpr auto kGlideSeconds = "glideSeconds";
constexpr auto kPolyphonyLimit = "polyphonyLimit";

constexpr auto kLoopStart = "loopStart";
constexpr auto kLoopEnd = "loopEnd";
constexpr auto kLoopCrossfadeSamples = "loopCrossfadeSamples";
constexpr auto kFadeInSamples = "fadeInSamples";
constexpr auto kFadeOutSamples = "fadeOutSamples";
constexpr auto kReversePlayback = "reversePlayback";

// Macros / mod routes
constexpr auto kMacro1Value = "macro1Value";
constexpr auto kMacro2Value = "macro2Value";
constexpr auto kMacro1ToPitch = "macro1ToPitch";
constexpr auto kMacro1ToFilter = "macro1ToFilter";
constexpr auto kMacro1ToAmp = "macro1ToAmp";
constexpr auto kMacro2ToPitch = "macro2ToPitch";
constexpr auto kMacro2ToFilter = "macro2ToFilter";
constexpr auto kMacro2ToAmp = "macro2ToAmp";

constexpr int kSampleRate = 44100;

enum class Family
{
    bass,
    lead,
    pad,
    pluck,
    keys,
    bell,
    ensemble,
    fx
};

struct Recipe
{
    juce::String name;
    Family family;
    int rootMidiNote = 60;
    float lengthSeconds = 1.0f;
    float baseCutoffHz = 6000.0f;
    float resonance = 0.2f;
    int filterMode = 0; // lp
    float ampAttack = 0.005f;
    float ampDecay = 0.20f;
    float ampSustain = 0.85f;
    float ampRelease = 0.30f;
    float ampLfoRate = 0.0f;
    float ampLfoDepth = 0.0f;
    int ampLfoShape = 0;
    float filterAttack = 0.005f;
    float filterDecay = 0.20f;
    float filterSustain = 0.50f;
    float filterRelease = 0.30f;
    float filterEnvAmount = 0.0f;
    float filterKeyTracking = 0.0f;
    float filterVelocityAmount = 0.0f;
    float filterLfoRate = 0.0f;
    float filterLfoRateKeytracking = 0.0f;
    float filterLfoAmount = 0.0f;
    float filterLfoAmountKeytracking = 0.0f;
    float filterLfoStartPhase = 0.0f;
    float filterLfoStartPhaseRandom = 0.0f;
    float filterLfoFadeInMs = 0.0f;
    int filterLfoShape = 0;
    bool filterLfoRetrigger = true;
    bool filterLfoTempoSync = false;
    bool filterLfoRateKeytrackInTempoSync = true;
    bool filterLfoKeytrackLinear = false;
    bool filterLfoUnipolar = false;
    int filterLfoSyncDivision = 6;
    float reverbMix = 0.10f;
    float delayMix = 0.0f;
    float delayTimeMs = 250.0f;
    float delayFeedback = 0.30f;
    bool delayTempoSync = false;
    bool dcFilterEnabled = true;
    float dcFilterCutoffHz = 10.0f;
    float autopanRateHz = 0.5f;
    float autopanDepth = 0.0f;
    float saturationDrive = 0.0f;
    int saturationMode = 0;
    int qualityTier = 1; // 0=CPU, 1=Fidelity, 2=Ultra
    int velocityCurve = 0; // 0=linear, 1=soft, 2=hard
    int playbackMode = 0; // gate
    bool monoMode = false;
    bool legatoMode = false;
    float glideSeconds = 0.0f;
    int polyphonyLimit = 16;
    int fadeInSamples = 0;
    int fadeOutSamples = 0;
    bool reversePlayback = false;
    float pan = 0.0f;
    float masterVolume = 0.85f;
    float pitchBendRangeSemitones = 2.0f;
    float pitchLfoRate = 0.0f;
    float pitchLfoDepth = 0.0f;
    float macro1Value = 0.35f;
    float macro2Value = 0.25f;
    float macro1ToPitch = 0.0f;
    float macro1ToFilter = 4000.0f;
    float macro1ToAmp = 0.0f;
    float macro2ToPitch = 0.0f;
    float macro2ToFilter = 1000.0f;
    float macro2ToAmp = 0.0f;
    float modWheelToPitch = 0.0f;
    float modWheelToFilter = 2500.0f;
    float modWheelToAmp = 0.0f;
    float aftertouchToPitch = 0.0f;
    float aftertouchToFilter = 0.0f;
    float aftertouchToAmp = 0.0f;
    float velocityToPitch = 0.0f;
    float velocityToFilter = 1500.0f;
    float velocityToAmp = 0.25f;
    int recipeSeed = 0;
    int waveformKind = 0; // see synth dispatch
    float param1 = 0.0f;
    float param2 = 0.0f;

    // ---- Optional SF2-sourced sample capture ----
    // When sf2File is a valid existing file, renderRecipeAudio() captures the
    // sample from the SF2 preset instead of synthesizing a waveform.
    juce::File sf2File;             // path to .sf2 (empty -> synthesized recipe)
    int sf2GmProgram = -1;          // 0..127 GM program; -1 to use namePattern only
    int sf2Bank = -1;               // preferred bank (e.g. 0); -1 = any bank
    juce::String sf2NamePattern;    // fallback substring match (case-insensitive)
    int sf2CaptureMidiNote = 60;    // pitch to capture (selects matching zone)
    float sf2CaptureSeconds = 2.0f; // length of capture in destination samples
    bool sf2UseEmbeddedLoop = true; // honor zone loop points when present
};

struct RecipeAudio
{
    std::vector<float> samples;
    int loopStart = -1;   // -1 = use computeLoopWindow fallback
    int loopEnd = -1;
};

struct LoopWindow
{
    int start = 0;
    int end = 0;
    int crossfadeSamples = 0;
};

inline float wrapPhase(float p)
{
    return p - std::floor(p);
}

inline float poly_blep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

void normalize(std::vector<float>& buf, float target = 0.95f)
{
    float peak = 0.0f;
    for (float v : buf)
        peak = std::max(peak, std::fabs(v));
    if (peak <= 1.0e-6f)
        return;
    const float gain = target / peak;
    for (float& v : buf)
        v *= gain;
}

void applyShortFades(std::vector<float>& buf, int fadeSamples)
{
    const auto n = static_cast<int>(buf.size());
    fadeSamples = std::min(fadeSamples, n / 2);
    for (int i = 0; i < fadeSamples; ++i)
    {
        const float g = static_cast<float>(i) / static_cast<float>(fadeSamples);
        buf[static_cast<std::size_t>(i)] *= g;
        buf[static_cast<std::size_t>(n - 1 - i)] *= g;
    }
}

bool usesWholeSampleLoopWindow(const Recipe& recipe) noexcept
{
    switch (recipe.waveformKind)
    {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 10:
            return true;
        default:
            return false;
    }
}

LoopWindow computeLoopWindow(const Recipe& recipe, const int totalSamples) noexcept
{
    if (totalSamples <= 1)
        return {};

    // SF2-sourced recipes are real recorded samples; never use the
    // whole-sample loop window for them (it produces loopStart==0 which the
    // factory validator rejects, and the seam will not be musical anyway).
    const bool isSf2 = (recipe.sf2File != juce::File());
    if (! isSf2 && (usesWholeSampleLoopWindow(recipe) || totalSamples <= 4096))
    {
        const auto loopLength = juce::jmax(1, totalSamples);
        return { 0, totalSamples - 1, juce::jlimit(0, loopLength / 2, juce::jmin(64, loopLength / 8)) };
    }

    const auto attackLeadSeconds = juce::jlimit(0.04f, 0.35f, recipe.ampAttack * 1.5f + 0.03f);
    const auto releaseTrimSeconds = juce::jlimit(0.04f, 0.35f, recipe.ampRelease * 0.5f + 0.03f);

    auto loopStart = static_cast<int>(std::round(static_cast<float>(kSampleRate) * attackLeadSeconds));
    auto loopEnd = totalSamples - 1
        - static_cast<int>(std::round(static_cast<float>(kSampleRate) * releaseTrimSeconds));

    const auto minimumLoopLength = juce::jlimit(2048, juce::jmax(2048, totalSamples - 2), totalSamples / 4);
    loopStart = juce::jlimit(0, juce::jmax(0, totalSamples - minimumLoopLength - 1), loopStart);
    loopEnd = juce::jlimit(loopStart + minimumLoopLength - 1, totalSamples - 1, loopEnd);

    if (loopEnd - loopStart + 1 < minimumLoopLength)
        loopStart = juce::jmax(0, loopEnd - minimumLoopLength + 1);

    const auto loopLength = juce::jmax(1, loopEnd - loopStart + 1);
    const auto crossfade = juce::jlimit(128, juce::jmax(128, loopLength / 2), loopLength / 12);
    return { loopStart, loopEnd, crossfade };
}

// --- Synthesis primitives -------------------------------------------------

std::vector<float> renderSawCycle(int samples, float pulseWidth = 0.5f)
{
    juce::ignoreUnused(pulseWidth);
    std::vector<float> out(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        out[static_cast<std::size_t>(i)] = 2.0f * t - 1.0f;
    }
    return out;
}

std::vector<float> renderSquareCycle(int samples, float pulseWidth = 0.5f)
{
    std::vector<float> out(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        out[static_cast<std::size_t>(i)] = (t < pulseWidth) ? 1.0f : -1.0f;
    }
    return out;
}

std::vector<float> renderSineCycle(int samples)
{
    std::vector<float> out(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        out[static_cast<std::size_t>(i)] = std::sin(juce::MathConstants<float>::twoPi * t);
    }
    return out;
}

std::vector<float> renderTriangleCycle(int samples)
{
    std::vector<float> out(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        out[static_cast<std::size_t>(i)] = 4.0f * std::fabs(t - 0.5f) - 1.0f;
    }
    return out;
}

// Additive harmonics over a single cycle.
std::vector<float> renderAdditiveCycle(int samples,
    std::initializer_list<float> harmonicGains)
{
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    int harmonic = 1;
    for (float gain : harmonicGains)
    {
        for (int i = 0; i < samples; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(samples);
            out[static_cast<std::size_t>(i)] += gain
                * std::sin(juce::MathConstants<float>::twoPi * static_cast<float>(harmonic) * t);
        }
        ++harmonic;
    }
    return out;
}

// Multi-cycle FM render to get evolving timbres for pads/pluck/bell sources.
std::vector<float> renderFm(int samples,
    float carrierFreqHz,
    float modIndex,
    float modRatio,
    float decayTau,
    int seed = 0)
{
    juce::Random rng(static_cast<juce::int64>(seed) + 1);
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    float modPhase = 0.0f;
    float carPhase = rng.nextFloat() * 0.05f;
    const float dt = 1.0f / static_cast<float>(kSampleRate);
    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) * dt;
        const float env = std::exp(-t / std::max(0.01f, decayTau));
        const float modSig = std::sin(juce::MathConstants<float>::twoPi * modPhase);
        const float carInst = carrierFreqHz + modIndex * carrierFreqHz * modSig * env;
        carPhase += carInst * dt;
        modPhase += carrierFreqHz * modRatio * dt;
        out[static_cast<std::size_t>(i)] = std::sin(juce::MathConstants<float>::twoPi * carPhase);
    }
    return out;
}

std::vector<float> renderNoise(int samples, int seed = 0)
{
    juce::Random rng(static_cast<juce::int64>(seed) + 31);
    std::vector<float> out(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i)
        out[static_cast<std::size_t>(i)] = rng.nextFloat() * 2.0f - 1.0f;
    return out;
}

// One-pole low-pass for offline shaping.
void onePoleLpfInPlace(std::vector<float>& buf, float cutoffHz, float sampleRate)
{
    const float rc = 1.0f / (juce::MathConstants<float>::twoPi * std::max(1.0f, cutoffHz));
    const float dt = 1.0f / sampleRate;
    const float alpha = dt / (rc + dt);
    float prev = 0.0f;
    for (float& v : buf)
    {
        prev += alpha * (v - prev);
        v = prev;
    }
}

void onePoleHpfInPlace(std::vector<float>& buf, float cutoffHz, float sampleRate)
{
    const float rc = 1.0f / (juce::MathConstants<float>::twoPi * std::max(1.0f, cutoffHz));
    const float dt = 1.0f / sampleRate;
    const float alpha = dt / (rc + dt);
    float low = 0.0f;
    for (float& v : buf)
    {
        low += alpha * (v - low);
        v -= low;
    }
}

void applyDriveInPlace(std::vector<float>& buf, float drive)
{
    const float gain = 1.0f + 8.0f * juce::jlimit(0.0f, 1.0f, drive);
    const float normalizer = std::tanh(gain);
    for (float& v : buf)
        v = std::tanh(v * gain) / juce::jmax(0.001f, normalizer);
}

void applyTremoloInPlace(std::vector<float>& buf, float rateHz, float depth, float phaseOffset = 0.0f)
{
    depth = juce::jlimit(0.0f, 1.0f, depth);
    for (int i = 0; i < static_cast<int>(buf.size()); ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float lfo = 0.5f + 0.5f * std::sin(juce::MathConstants<float>::twoPi * rateHz * t + phaseOffset);
        buf[static_cast<std::size_t>(i)] *= (1.0f - depth) + depth * lfo;
    }
}

float sawFromPhase(float phase) noexcept
{
    return 2.0f * wrapPhase(phase) - 1.0f;
}

float squareFromPhase(float phase, float pulseWidth) noexcept
{
    return wrapPhase(phase) < pulseWidth ? 1.0f : -1.0f;
}

std::vector<float> renderDetunedStack(float frequencyHz,
                                      int samples,
                                      float detuneCents,
                                      float squareMix,
                                      float subMix,
                                      int seed)
{
    juce::Random rng(static_cast<juce::int64>(seed) + 211);
    constexpr std::array<float, 5> detuneSpread { -1.0f, -0.42f, 0.0f, 0.37f, 0.92f };
    std::array<float, detuneSpread.size()> phases{};
    for (auto& phase : phases)
        phase = rng.nextFloat();

    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    const float dt = 1.0f / static_cast<float>(kSampleRate);
    const float pulseWidth = 0.45f + 0.10f * std::sin(static_cast<float>(seed));
    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) * dt;
        const float drift = 1.0f + 0.0018f * std::sin(juce::MathConstants<float>::twoPi * (0.07f + 0.01f * static_cast<float>(seed % 7)) * t);
        float sample = 0.0f;
        for (std::size_t voice = 0; voice < detuneSpread.size(); ++voice)
        {
            const float cents = detuneCents * detuneSpread[voice];
            const float ratio = std::pow(2.0f, cents / 1200.0f) * drift;
            phases[voice] = wrapPhase(phases[voice] + frequencyHz * ratio * dt);
            const float saw = sawFromPhase(phases[voice]);
            const float sq = squareFromPhase(phases[voice], pulseWidth);
            sample += (1.0f - squareMix) * saw + squareMix * sq;
        }
        sample /= static_cast<float>(detuneSpread.size());
        sample += subMix * std::sin(juce::MathConstants<float>::twoPi * frequencyHz * 0.5f * t);
        out[static_cast<std::size_t>(i)] = sample;
    }
    return out;
}

std::vector<float> renderReeseGrowl(float frequencyHz, int samples, float growl, int seed)
{
    auto out = renderDetunedStack(frequencyHz, samples, 9.0f + 22.0f * growl, 0.30f, 0.32f, seed);
    juce::Random rng(static_cast<juce::int64>(seed) + 227);
    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float formant = std::sin(juce::MathConstants<float>::twoPi * (1.1f + 0.4f * growl) * t);
        out[static_cast<std::size_t>(i)] += (0.07f + 0.05f * growl) * formant
            * squareFromPhase(frequencyHz * 1.5f * t + rng.nextFloat() * 0.002f, 0.35f);
    }
    onePoleLpfInPlace(out, 900.0f + 2200.0f * growl, static_cast<float>(kSampleRate));
    applyDriveInPlace(out, 0.25f + 0.45f * growl);
    return out;
}

std::vector<float> renderFormantTone(float frequencyHz, int samples, float vowelIndex, float air, int seed)
{
    juce::Random rng(static_cast<juce::int64>(seed) + 241);
    const float vowel = juce::jlimit(0.0f, 1.0f, vowelIndex);
    const std::array<float, 3> formantA { 700.0f, 1220.0f, 2600.0f };
    const std::array<float, 3> formantO { 430.0f, 820.0f, 2700.0f };
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    const float dt = 1.0f / static_cast<float>(kSampleRate);
    for (int harmonic = 1; harmonic <= 36; ++harmonic)
    {
        const float partialHz = frequencyHz * static_cast<float>(harmonic);
        float gain = 0.0f;
        for (std::size_t f = 0; f < formantA.size(); ++f)
        {
            const float center = formantA[f] * (1.0f - vowel) + formantO[f] * vowel;
            const float width = 150.0f + 170.0f * static_cast<float>(f);
            const float distance = (partialHz - center) / width;
            gain += std::exp(-0.5f * distance * distance);
        }
        gain *= 1.0f / std::pow(static_cast<float>(harmonic), 0.85f);
        const float phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        for (int i = 0; i < samples; ++i)
            out[static_cast<std::size_t>(i)] += gain * std::sin(juce::MathConstants<float>::twoPi * partialHz * static_cast<float>(i) * dt + phase);
    }
    if (air > 0.0f)
    {
        auto noise = renderNoise(samples, seed + 19);
        onePoleHpfInPlace(noise, 2400.0f, static_cast<float>(kSampleRate));
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] += air * noise[i];
    }
    onePoleLpfInPlace(out, 6200.0f, static_cast<float>(kSampleRate));
    return out;
}

std::vector<float> renderSweepTexture(int samples, float startHz, float endHz, int seed)
{
    juce::Random rng(static_cast<juce::int64>(seed) + 257);
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    float low = 0.0f;
    float phase = rng.nextFloat();
    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(juce::jmax(1, samples - 1));
        const float curved = t * t * (3.0f - 2.0f * t);
        const float cutoff = startHz * std::pow(endHz / juce::jmax(1.0f, startHz), curved);
        const float rc = 1.0f / (juce::MathConstants<float>::twoPi * juce::jmax(1.0f, cutoff));
        const float alpha = (1.0f / static_cast<float>(kSampleRate)) / (rc + (1.0f / static_cast<float>(kSampleRate)));
        const float noise = rng.nextFloat() * 2.0f - 1.0f;
        low += alpha * (noise - low);
        phase = wrapPhase(phase + (80.0f + 160.0f * curved) / static_cast<float>(kSampleRate));
        out[static_cast<std::size_t>(i)] = low + 0.18f * std::sin(juce::MathConstants<float>::twoPi * phase);
    }
    return out;
}

std::vector<float> renderTapeWobble(float frequencyHz, int samples, float wobbleDepth, int seed)
{
    juce::Random rng(static_cast<juce::int64>(seed) + 271);
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    const float dt = 1.0f / static_cast<float>(kSampleRate);
    float phase = rng.nextFloat();
    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) * dt;
        const float wow = 1.0f + wobbleDepth * 0.010f * std::sin(juce::MathConstants<float>::twoPi * 0.48f * t)
            + wobbleDepth * 0.004f * std::sin(juce::MathConstants<float>::twoPi * 5.7f * t);
        phase = wrapPhase(phase + frequencyHz * wow * dt);
        out[static_cast<std::size_t>(i)] = 0.62f * std::sin(juce::MathConstants<float>::twoPi * phase)
            + 0.26f * std::sin(juce::MathConstants<float>::twoPi * wrapPhase(phase * 2.01f))
            + 0.12f * (rng.nextFloat() * 2.0f - 1.0f);
    }
    onePoleLpfInPlace(out, 2600.0f, static_cast<float>(kSampleRate));
    return out;
}

std::vector<float> renderGatedTexture(float frequencyHz, int samples, float rateHz, int seed)
{
    auto out = renderDetunedStack(frequencyHz, samples, 14.0f, 0.45f, 0.05f, seed);
    auto noise = renderNoise(samples, seed + 23);
    onePoleLpfInPlace(noise, 1800.0f, static_cast<float>(kSampleRate));
    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float gatePhase = wrapPhase(rateHz * t);
        const float gate = gatePhase < 0.48f ? 1.0f : 0.18f;
        out[static_cast<std::size_t>(i)] = gate * (0.75f * out[static_cast<std::size_t>(i)] + 0.25f * noise[static_cast<std::size_t>(i)]);
    }
    return out;
}

std::vector<float> renderBell(float frequencyHz, int samples, float decayTau, int seed);

std::vector<float> renderShimmerTone(float frequencyHz, int samples, float brightness, int seed)
{
    auto base = renderBell(frequencyHz, samples, 1.7f, seed);
    auto high = renderBell(frequencyHz * 2.0f, samples, 2.4f, seed + 37);
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = 0.55f * base[i] + (0.25f + 0.35f * brightness) * high[i];
    onePoleHpfInPlace(out, 90.0f, static_cast<float>(kSampleRate));
    return out;
}

std::vector<float> renderPunchBass(float frequencyHz, int samples, float click, int seed)
{
    juce::Random rng(static_cast<juce::int64>(seed) + 283);
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    const float dt = 1.0f / static_cast<float>(kSampleRate);
    float phase = 0.0f;
    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) * dt;
        const float transient = std::exp(-t / 0.012f);
        const float pitchDrop = 1.0f + 0.55f * transient;
        phase = wrapPhase(phase + frequencyHz * pitchDrop * dt);
        out[static_cast<std::size_t>(i)] = 0.88f * std::sin(juce::MathConstants<float>::twoPi * phase)
            + 0.18f * sawFromPhase(phase * 2.0f)
            + click * transient * (rng.nextFloat() * 2.0f - 1.0f);
    }
    onePoleLpfInPlace(out, 1600.0f, static_cast<float>(kSampleRate));
    return out;
}

// Synthesizes a multi-cycle sustained tone built from harmonic stack with optional noise.
std::vector<float> renderHarmonicTone(float frequencyHz,
                                      int samples,
                                      std::initializer_list<float> harmonicGains,
                                      float noiseLevel = 0.0f,
                                      int seed = 0)
{
    juce::Random rng(static_cast<juce::int64>(seed) + 71);
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    const float dt = 1.0f / static_cast<float>(kSampleRate);
    int harmonic = 1;
    for (float gain : harmonicGains)
    {
        const float omega = juce::MathConstants<float>::twoPi * frequencyHz * static_cast<float>(harmonic);
        for (int i = 0; i < samples; ++i)
            out[static_cast<std::size_t>(i)] += gain * std::sin(omega * static_cast<float>(i) * dt);
        ++harmonic;
    }
    if (noiseLevel > 0.0f)
    {
        for (int i = 0; i < samples; ++i)
            out[static_cast<std::size_t>(i)] += noiseLevel * (rng.nextFloat() * 2.0f - 1.0f);
    }
    return out;
}

// Bell-style inharmonic partials.
std::vector<float> renderBell(float frequencyHz, int samples, float decayTau, int seed = 0)
{
    juce::Random rng(static_cast<juce::int64>(seed) + 17);
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    const float dt = 1.0f / static_cast<float>(kSampleRate);
    const std::array<float, 6> ratios { 1.00f, 2.01f, 2.97f, 4.10f, 5.43f, 6.78f };
    const std::array<float, 6> gains  { 1.00f, 0.55f, 0.42f, 0.30f, 0.20f, 0.15f };
    for (std::size_t k = 0; k < ratios.size(); ++k)
    {
        const float omega = juce::MathConstants<float>::twoPi * frequencyHz * ratios[k];
        const float partialDecay = decayTau * (1.0f - 0.10f * static_cast<float>(k));
        const float phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        for (int i = 0; i < samples; ++i)
        {
            const float t = static_cast<float>(i) * dt;
            const float env = std::exp(-t / std::max(0.05f, partialDecay));
            out[static_cast<std::size_t>(i)] += gains[k] * env * std::sin(omega * t + phase);
        }
    }
    return out;
}

// --- SF2 sample capture ---------------------------------------------------
// Cached SF2 probe results keyed by full file path. Probing every recipe
// would re-parse multi-megabyte SoundFont files, so we cache.
struct Sf2ProbeCache
{
    audiocity::engine::sf2::ProbeResult& get(const juce::File& f)
    {
        const auto key = f.getFullPathName().toStdString();
        auto it = probes.find(key);
        if (it == probes.end())
        {
            auto r = audiocity::engine::sf2::probeFile(f);
            it = probes.emplace(key, std::move(r)).first;
        }
        return it->second;
    }

    std::unordered_map<std::string, audiocity::engine::sf2::ProbeResult> probes;
};

inline Sf2ProbeCache& sf2Cache()
{
    static Sf2ProbeCache cache;
    return cache;
}

int findSf2PresetIndex(const audiocity::engine::sf2::ProbeResult& probe,
                       const int gmProgram,
                       const int preferredBank,
                       const juce::String& namePattern)
{
    int bestIdx = -1;
    int bestBankDistance = std::numeric_limits<int>::max();
    if (gmProgram >= 0)
    {
        for (std::size_t i = 0; i < probe.availablePresets.size(); ++i)
        {
            const auto& p = probe.availablePresets[i];
            if (p.program != gmProgram)
                continue;
            const int dist = preferredBank >= 0 ? std::abs(p.bank - preferredBank) : p.bank;
            if (dist < bestBankDistance)
            {
                bestBankDistance = dist;
                bestIdx = static_cast<int>(i);
            }
        }
        if (bestIdx >= 0)
            return bestIdx;
    }
    if (namePattern.isNotEmpty())
    {
        for (std::size_t i = 0; i < probe.availablePresets.size(); ++i)
            if (probe.availablePresets[i].name.containsIgnoreCase(namePattern))
                return static_cast<int>(i);
    }
    return -1;
}

RecipeAudio renderRecipeFromSf2(const Recipe& r)
{
    RecipeAudio out;
    if (r.sf2File == juce::File() || !r.sf2File.existsAsFile())
    {
        std::fprintf(stderr, "  [sf2] missing file: %s\n", r.sf2File.getFullPathName().toRawUTF8());
        return out;
    }

    const auto& probe = sf2Cache().get(r.sf2File);
    if (probe.availablePresets.empty())
    {
        std::fprintf(stderr, "  [sf2] no presets in %s\n", r.sf2File.getFileName().toRawUTF8());
        return out;
    }

    const int presetIdx = findSf2PresetIndex(probe, r.sf2GmProgram, r.sf2Bank, r.sf2NamePattern);
    if (presetIdx < 0)
    {
        std::fprintf(stderr, "  [sf2] no preset matches program=%d name='%s' in %s\n",
            r.sf2GmProgram, r.sf2NamePattern.toRawUTF8(), r.sf2File.getFileName().toRawUTF8());
        return out;
    }

    const auto imported = audiocity::engine::sf2::importFilePreset(r.sf2File, presetIdx);
    if (!imported.hasPlayableProgram())
    {
        std::fprintf(stderr, "  [sf2] preset %d has no playable program in %s\n",
            presetIdx, r.sf2File.getFileName().toRawUTF8());
        return out;
    }

    // Find a zone matching capture note. Try common velocities then any.
    int zoneIdx = imported.program.findFirstMatchingZoneIndex(r.sf2CaptureMidiNote, 100);
    if (zoneIdx < 0) zoneIdx = imported.program.findFirstMatchingZoneIndex(r.sf2CaptureMidiNote, 127);
    if (zoneIdx < 0) zoneIdx = imported.program.findFirstMatchingZoneIndex(r.sf2CaptureMidiNote, 64);
    if (zoneIdx < 0)
    {
        for (std::size_t i = 0; i < imported.program.zones.size(); ++i)
        {
            const auto& z = imported.program.zones[i];
            if (imported.program.isZoneSampleIndexValid(z)
                && imported.program.sampleAssets[static_cast<std::size_t>(z.sampleAssetIndex)].hasAudio())
            {
                zoneIdx = static_cast<int>(i);
                break;
            }
        }
    }
    if (zoneIdx < 0)
    {
        std::fprintf(stderr, "  [sf2] no zone for note %d in preset %d of %s\n",
            r.sf2CaptureMidiNote, presetIdx, r.sf2File.getFileName().toRawUTF8());
        return out;
    }

    const auto& zone = imported.program.zones[static_cast<std::size_t>(zoneIdx)];
    const auto& asset = imported.program.sampleAssets[static_cast<std::size_t>(zone.sampleAssetIndex)];
    const auto& srcBuf = imported.sampleDataByAsset[static_cast<std::size_t>(zone.sampleAssetIndex)];

    const int srcChannels = srcBuf.getNumChannels();
    const int srcFrames = srcBuf.getNumSamples();
    if (srcChannels <= 0 || srcFrames <= 1 || asset.sampleRateHz <= 0.0)
        return out;

    const int srcStart = juce::jlimit(0, srcFrames - 1, zone.sampleStart);
    const int srcEndExclusive = (zone.sampleEndExclusive > 0)
        ? juce::jlimit(srcStart + 1, srcFrames, zone.sampleEndExclusive)
        : srcFrames;

    const float semitonesUp = static_cast<float>(r.sf2CaptureMidiNote - zone.rootMidiNote)
                              + zone.tuneCents / 100.0f;
    const double pitchRatio = std::pow(2.0, semitonesUp / 12.0);
    const double srcSr = asset.sampleRateHz;
    const double dstSr = static_cast<double>(kSampleRate);
    const double srcPerDst = (srcSr / dstSr) * pitchRatio;
    if (srcPerDst <= 0.0)
        return out;

    const int dstLen = std::max(64,
        static_cast<int>(std::round(dstSr * static_cast<double>(r.sf2CaptureSeconds))));

    auto sampleAt = [&](double p) -> float
    {
        if (p < 0.0) return 0.0f;
        const int i0 = static_cast<int>(std::floor(p));
        if (i0 >= srcFrames - 1) return 0.0f;
        const int i1 = i0 + 1;
        const double frac = p - static_cast<double>(i0);
        float s0 = 0.0f, s1 = 0.0f;
        for (int c = 0; c < srcChannels; ++c)
        {
            s0 += srcBuf.getSample(c, i0);
            s1 += srcBuf.getSample(c, i1);
        }
        const float invC = 1.0f / static_cast<float>(srcChannels);
        s0 *= invC; s1 *= invC;
        return static_cast<float>((1.0 - frac) * s0 + frac * s1);
    };

    out.samples.resize(static_cast<std::size_t>(dstLen), 0.0f);
    int producedFrames = 0;
    for (int i = 0; i < dstLen; ++i)
    {
        const double srcPos = static_cast<double>(srcStart) + static_cast<double>(i) * srcPerDst;
        if (srcPos >= static_cast<double>(srcEndExclusive - 1))
            break;
        out.samples[static_cast<std::size_t>(i)] = sampleAt(srcPos);
        producedFrames = i + 1;
    }
    if (producedFrames < static_cast<int>(out.samples.size()))
        out.samples.resize(static_cast<std::size_t>(producedFrames));

    if (out.samples.size() < 64)
    {
        std::fprintf(stderr, "  [sf2] capture too short (%zu samples) in %s\n",
            out.samples.size(), r.sf2File.getFileName().toRawUTF8());
        return {};
    }

    // Translate zone loop points (in source frames) to destination frames.
    if (r.sf2UseEmbeddedLoop
        && zone.loopStart >= 0
        && zone.loopEndExclusive > zone.loopStart
        && zone.loopStart >= srcStart
        && zone.loopEndExclusive <= srcEndExclusive)
    {
        const double dstStart = static_cast<double>(zone.loopStart - srcStart) / srcPerDst;
        const double dstEnd   = static_cast<double>(zone.loopEndExclusive - 1 - srcStart) / srcPerDst;
        const int loopStartDst = static_cast<int>(std::round(dstStart));
        const int loopEndDst   = static_cast<int>(std::round(dstEnd));
        if (loopStartDst >= 0
            && loopEndDst > loopStartDst + 64
            && loopEndDst < static_cast<int>(out.samples.size()))
        {
            out.loopStart = loopStartDst;
            out.loopEnd = loopEndDst;
        }
    }

    // Small edge fade then normalize. Target leaves polyphonic headroom
    // (audition stacks 3-4 voices) and per-voice envelope/master gain.
    applyShortFades(out.samples, 32);
    normalize(out.samples, 0.55f);

    // If the resolved loop seam has a large discontinuity even after our
    // crossfade hint, drop the embedded loop and let buildPresetState fall
    // back to a synthesized loop window picked from the rendered audio.
    if (out.loopStart >= 0
        && out.loopEnd > out.loopStart
        && out.loopEnd < static_cast<int>(out.samples.size()))
    {
        const float a = out.samples[static_cast<std::size_t>(out.loopEnd)];
        const float b = out.samples[static_cast<std::size_t>(out.loopStart)];
        if (std::fabs(a - b) > 0.25f)
        {
            out.loopStart = -1;
            out.loopEnd = -1;
        }
    }

    return out;
}

// --- Recipe -> waveform dispatch ------------------------------------------

std::vector<float> renderRecipeAudio(const Recipe& r)
{
    const float freqHz = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(r.rootMidiNote));
    const int sampleCount = std::max(64, static_cast<int>(static_cast<float>(kSampleRate) * r.lengthSeconds));

    std::vector<float> buf;

    switch (r.waveformKind)
    {
        case 0: // single-cycle saw - small (1 cycle at root note)
        {
            const int cycleSamples = std::max(8,
                static_cast<int>(std::round(static_cast<float>(kSampleRate) / freqHz)));
            buf = renderSawCycle(cycleSamples);
            break;
        }
        case 1: // single-cycle square
        {
            const int cycleSamples = std::max(8,
                static_cast<int>(std::round(static_cast<float>(kSampleRate) / freqHz)));
            buf = renderSquareCycle(cycleSamples, juce::jlimit(0.05f, 0.95f, r.param1 > 0.0f ? r.param1 : 0.5f));
            break;
        }
        case 2: // single-cycle sine
        {
            const int cycleSamples = std::max(8,
                static_cast<int>(std::round(static_cast<float>(kSampleRate) / freqHz)));
            buf = renderSineCycle(cycleSamples);
            break;
        }
        case 3: // single-cycle triangle
        {
            const int cycleSamples = std::max(8,
                static_cast<int>(std::round(static_cast<float>(kSampleRate) / freqHz)));
            buf = renderTriangleCycle(cycleSamples);
            break;
        }
        case 4: // additive cycle (param1=odd-bias)
        {
            const int cycleSamples = std::max(8,
                static_cast<int>(std::round(static_cast<float>(kSampleRate) / freqHz)));
            buf = renderAdditiveCycle(cycleSamples,
                { 1.0f, r.param1, 0.5f, r.param2, 0.3f, 0.15f, 0.10f });
            break;
        }
        case 5: // FM tone (sustained)
        {
            buf = renderFm(sampleCount, freqHz, r.param1, r.param2, r.lengthSeconds * 0.6f, r.recipeSeed);
            break;
        }
        case 6: // bell
        {
            buf = renderBell(freqHz, sampleCount, r.param1 > 0.0f ? r.param1 : 1.5f, r.recipeSeed);
            break;
        }
        case 7: // harmonic tone (organ-like)
        {
            buf = renderHarmonicTone(freqHz, sampleCount,
                { 1.0f, 0.7f, 0.5f, 0.4f, 0.3f, 0.2f, 0.15f, 0.10f }, 0.0f, r.recipeSeed);
            break;
        }
        case 8: // filtered noise
        {
            buf = renderNoise(sampleCount, r.recipeSeed);
            onePoleLpfInPlace(buf, std::max(80.0f, r.param1), static_cast<float>(kSampleRate));
            break;
        }
        case 9: // FM bell hybrid
        {
            auto fm = renderFm(sampleCount, freqHz, r.param1, r.param2, r.lengthSeconds * 0.4f, r.recipeSeed);
            auto bell = renderBell(freqHz, sampleCount, 1.2f, r.recipeSeed + 5);
            buf.assign(static_cast<std::size_t>(sampleCount), 0.0f);
            for (std::size_t i = 0; i < buf.size(); ++i)
                buf[i] = 0.6f * fm[i] + 0.4f * bell[i];
            break;
        }
        case 10: // single-cycle PWM-ish (averaged saw + square)
        {
            const int cycleSamples = std::max(8,
                static_cast<int>(std::round(static_cast<float>(kSampleRate) / freqHz)));
            auto saw = renderSawCycle(cycleSamples);
            auto sq  = renderSquareCycle(cycleSamples, juce::jlimit(0.20f, 0.80f, 0.5f + r.param1 * 0.3f));
            buf.assign(static_cast<std::size_t>(cycleSamples), 0.0f);
            for (std::size_t i = 0; i < buf.size(); ++i)
                buf[i] = 0.55f * saw[i] + 0.45f * sq[i];
            break;
        }
        case 11: // pad-style harmonic stack with noise
        {
            buf = renderHarmonicTone(freqHz, sampleCount,
                { 1.0f, 0.5f, 0.35f, 0.25f, 0.18f, 0.12f }, std::max(0.0f, r.param2), r.recipeSeed);
            onePoleLpfInPlace(buf, std::max(400.0f, r.param1 * freqHz + 800.0f),
                static_cast<float>(kSampleRate));
            break;
        }
        case 12: // detuned analog/supersaw stack
        {
            buf = renderDetunedStack(freqHz, sampleCount,
                r.param1 > 0.0f ? r.param1 : 8.0f,
                juce::jlimit(0.0f, 1.0f, r.param2),
                (r.family == Family::bass) ? 0.25f : 0.06f,
                r.recipeSeed);
            onePoleLpfInPlace(buf, r.family == Family::bass ? 3200.0f : 7800.0f,
                static_cast<float>(kSampleRate));
            break;
        }
        case 13: // reese/growl bass or aggressive lead body
        {
            buf = renderReeseGrowl(freqHz, sampleCount, juce::jlimit(0.0f, 1.0f, r.param1), r.recipeSeed);
            break;
        }
        case 14: // vocal/formant source
        {
            buf = renderFormantTone(freqHz, sampleCount,
                juce::jlimit(0.0f, 1.0f, r.param1),
                juce::jlimit(0.0f, 0.3f, r.param2),
                r.recipeSeed);
            break;
        }
        case 15: // noise sweep / cinematic wash
        {
            const auto startHz = r.param1 > 0.0f ? r.param1 : 180.0f;
            const auto endHz = r.param2 > 0.0f ? r.param2 : 8000.0f;
            buf = renderSweepTexture(sampleCount, startHz, endHz, r.recipeSeed);
            break;
        }
        case 16: // tape/lo-fi wobble
        {
            buf = renderTapeWobble(freqHz, sampleCount, r.param1 > 0.0f ? r.param1 : 0.7f, r.recipeSeed);
            break;
        }
        case 17: // gated pulse texture
        {
            buf = renderGatedTexture(freqHz, sampleCount, r.param1 > 0.0f ? r.param1 : 4.0f, r.recipeSeed);
            break;
        }
        case 18: // shimmer bell/pad tone
        {
            buf = renderShimmerTone(freqHz, sampleCount, juce::jlimit(0.0f, 1.0f, r.param1), r.recipeSeed);
            break;
        }
        case 19: // punchy bass body with transient
        {
            buf = renderPunchBass(freqHz, sampleCount, juce::jlimit(0.0f, 1.0f, r.param1), r.recipeSeed);
            break;
        }
        default:
        {
            const int cycleSamples = std::max(8,
                static_cast<int>(std::round(static_cast<float>(kSampleRate) / freqHz)));
            buf = renderSineCycle(cycleSamples);
            break;
        }
    }

    if (buf.empty())
        return buf;

    applyShortFades(buf, std::min(64, static_cast<int>(buf.size()) / 8));
    normalize(buf, 0.92f);
    return buf;
}

// Top-level recipe -> rendered audio dispatch. SF2-sourced recipes capture a
// sample from a SoundFont and may carry zone loop points back to the caller.
RecipeAudio renderRecipe(const Recipe& r)
{
    if (r.sf2File != juce::File() && r.sf2File.existsAsFile())
        return renderRecipeFromSf2(r);

    RecipeAudio out;
    out.samples = renderRecipeAudio(r);
    return out;
}

bool isIndexOneOf(const int index, std::initializer_list<int> values) noexcept
{
    return std::find(values.begin(), values.end(), index) != values.end();
}

void applyFilterLfo(Recipe& r,
                    const float rateHz,
                    const float amountHz,
                    const int shape,
                    const bool tempoSync,
                    const int syncDivision,
                    const bool unipolar = false)
{
    r.filterLfoRate = rateHz;
    r.filterLfoAmount = amountHz;
    r.filterLfoShape = shape;
    r.filterLfoTempoSync = tempoSync;
    r.filterLfoSyncDivision = syncDivision;
    r.filterLfoUnipolar = unipolar;
    r.filterLfoStartPhaseRandom = tempoSync ? 0.0f : 90.0f;
    r.filterLfoRetrigger = !tempoSync;
}

void applyFactorySoundDesign(Recipe& r, const int familyIndex)
{
    const auto i = familyIndex;

    r.qualityTier = 1;
    r.velocityCurve = (i % 5 == 0) ? 1 : 0;
    r.delayTimeMs = 180.0f + 35.0f * static_cast<float>(i % 9);
    r.delayFeedback = 0.22f + 0.035f * static_cast<float>(i % 6);
    r.filterEnvAmount = (r.family == Family::bass || r.family == Family::pluck) ? 3600.0f : 700.0f;
    r.filterVelocityAmount = 900.0f + 120.0f * static_cast<float>(i % 9);
    r.filterKeyTracking = (r.family == Family::bass || r.family == Family::lead || r.family == Family::keys) ? 0.25f : 0.10f;
    r.macro1Value = 0.30f + 0.03f * static_cast<float>(i % 7);
    r.macro2Value = 0.20f + 0.04f * static_cast<float>((i + 2) % 6);
    r.macro1ToFilter = 2600.0f + 180.0f * static_cast<float>(i);
    r.macro2ToFilter = 800.0f + 120.0f * static_cast<float>(i % 8);
    r.modWheelToFilter = 1600.0f + 140.0f * static_cast<float>(i % 12);
    r.velocityToFilter = 1000.0f + 180.0f * static_cast<float>(i % 10);
    r.velocityToAmp = 0.18f + 0.025f * static_cast<float>(i % 5);
    r.pitchBendRangeSemitones = (r.family == Family::bass || r.family == Family::lead) ? 12.0f : 2.0f;
    r.fadeInSamples = isIndexOneOf(i, { 5, 9, 13 }) ? 16 : 0;
    r.fadeOutSamples = 64;

    switch (r.family)
    {
        case Family::bass:
        {
            static constexpr std::array<int, 16> kinds { 19, 12, 13, 19, 13, 13, 13, 19, 5, 16, 19, 12, 13, 5, 12, 19 };
            static constexpr std::array<float, 16> lengths { 1.25f, 1.55f, 1.45f, 1.35f, 1.60f, 1.80f, 1.45f, 1.20f,
                1.55f, 1.70f, 1.20f, 1.45f, 1.35f, 1.55f, 1.45f, 1.25f };
            r.waveformKind = kinds[static_cast<std::size_t>(i)];
            r.lengthSeconds = lengths[static_cast<std::size_t>(i)];
            r.filterMode = isIndexOneOf(i, { 2, 4, 5, 12, 14 }) ? 1 : (i == 8 ? 4 : 0);
            r.filterAttack = 0.001f;
            r.filterDecay = isIndexOneOf(i, { 2, 12, 15 }) ? 0.075f : 0.14f;
            r.filterSustain = isIndexOneOf(i, { 2, 12 }) ? 0.12f : 0.28f;
            r.filterRelease = 0.12f;
            r.filterEnvAmount = 2200.0f + 220.0f * static_cast<float>(i % 8);
            r.filterKeyTracking = 0.18f;
            r.filterVelocityAmount = 900.0f + 120.0f * static_cast<float>(i % 6);
            r.reverbMix = isIndexOneOf(i, { 5, 8 }) ? 0.10f : 0.035f;
            r.delayMix = isIndexOneOf(i, { 2, 11, 14 }) ? 0.12f : 0.0f;
            r.delayTempoSync = r.delayMix > 0.0f;
            r.saturationDrive = isIndexOneOf(i, { 2, 4, 5, 9, 14 }) ? 0.45f : 0.18f;
            r.saturationMode = isIndexOneOf(i, { 4, 14 }) ? 3 : (i == 9 ? 2 : 0);
            r.param1 = isIndexOneOf(i, { 4, 5, 12 }) ? 0.82f : (i == 15 ? 0.65f : 0.34f + 0.02f * static_cast<float>(i));
            r.param2 = isIndexOneOf(i, { 1, 11, 14 }) ? 0.12f : 0.0f;
            r.monoMode = true;
            r.legatoMode = true;
            r.glideSeconds = i == 11 ? 0.13f : 0.035f;
            r.polyphonyLimit = 1;
            r.ampDecay = isIndexOneOf(i, { 7, 10, 15 }) ? 0.10f : 0.18f;
            r.ampSustain = isIndexOneOf(i, { 7, 10, 15 }) ? 0.55f : 0.80f;
            r.pitchLfoRate = isIndexOneOf(i, { 5, 9 }) ? 4.8f : 0.0f;
            r.pitchLfoDepth = isIndexOneOf(i, { 5, 9 }) ? 5.0f : 0.0f;
            if (isIndexOneOf(i, { 2, 4, 5, 6, 12, 14 }))
                applyFilterLfo(r, 2.0f + 0.35f * static_cast<float>(i % 4), 650.0f + 260.0f * static_cast<float>(i % 5), 3, true, 3 + (i % 5), true);
            r.macro1ToFilter = 5200.0f;
            r.macro2ToFilter = isIndexOneOf(i, { 4, 5, 6 }) ? 3200.0f : 1200.0f;
            r.modWheelToFilter = 4200.0f;
            r.aftertouchToFilter = 1200.0f;
            r.velocityToAmp = 0.28f;
            break;
        }

        case Family::lead:
        {
            static constexpr std::array<int, 16> kinds { 12, 12, 1, 12, 12, 14, 5, 14, 13, 18, 14, 14, 10, 12, 17, 1 };
            r.waveformKind = kinds[static_cast<std::size_t>(i)];
            r.lengthSeconds = isIndexOneOf(i, { 5, 7, 9, 10, 11, 14 }) ? 2.0f : 1.55f;
            r.filterMode = isIndexOneOf(i, { 5, 10, 11 }) ? 4 : (i == 14 ? 1 : 0);
            r.filterAttack = isIndexOneOf(i, { 7, 10 }) ? 0.035f : 0.002f;
            r.filterDecay = 0.18f;
            r.filterSustain = 0.45f;
            r.filterRelease = 0.24f;
            r.filterEnvAmount = 1200.0f + 260.0f * static_cast<float>(i % 8);
            r.filterKeyTracking = 0.38f;
            r.reverbMix = isIndexOneOf(i, { 5, 7, 9, 10 }) ? 0.22f : 0.12f;
            r.delayMix = isIndexOneOf(i, { 1, 4, 8, 14, 15 }) ? 0.24f : 0.08f;
            r.delayTempoSync = isIndexOneOf(i, { 1, 4, 8, 14, 15 });
            r.saturationDrive = isIndexOneOf(i, { 3, 8, 14, 15 }) ? 0.36f : 0.12f;
            r.saturationMode = i == 8 ? 1 : (isIndexOneOf(i, { 3, 14 }) ? 3 : 0);
            r.autopanDepth = i == 1 ? 0.28f : (i == 13 ? 0.18f : 0.0f);
            r.autopanRateHz = 0.18f + 0.03f * static_cast<float>(i % 4);
            r.param1 = isIndexOneOf(i, { 5, 10, 11 }) ? 0.25f + 0.07f * static_cast<float>(i % 5) : 9.0f + static_cast<float>(i % 6);
            r.param2 = isIndexOneOf(i, { 5, 10, 11 }) ? 0.05f : 0.18f;
            r.monoMode = (i != 1 && i != 13);
            r.legatoMode = r.monoMode;
            r.glideSeconds = i == 4 ? 0.16f : (r.monoMode ? 0.045f : 0.0f);
            r.polyphonyLimit = r.monoMode ? 1 : 6;
            r.pitchLfoRate = isIndexOneOf(i, { 5, 7, 10 }) ? 5.4f : 0.0f;
            r.pitchLfoDepth = isIndexOneOf(i, { 5, 7, 10 }) ? 9.0f : 0.0f;
            if (isIndexOneOf(i, { 3, 5, 11, 12, 14 }))
                applyFilterLfo(r, 3.2f, 1200.0f + 160.0f * static_cast<float>(i), i == 14 ? 2 : 0, i == 14, 6, false);
            r.macro1ToFilter = 6200.0f;
            r.macro2ToPitch = isIndexOneOf(i, { 4, 5, 7 }) ? 120.0f : 0.0f;
            r.aftertouchToPitch = isIndexOneOf(i, { 5, 7, 10 }) ? 25.0f : 8.0f;
            r.aftertouchToFilter = 2600.0f;
            r.modWheelToPitch = 35.0f;
            break;
        }

        case Family::pad:
        {
            static constexpr std::array<int, 16> kinds { 12, 18, 14, 16, 18, 15, 12, 12, 17, 16, 11, 7, 15, 5, 17, 18 };
            r.waveformKind = kinds[static_cast<std::size_t>(i)];
            r.lengthSeconds = 3.0f + 0.08f * static_cast<float>(i % 5);
            r.filterMode = isIndexOneOf(i, { 5, 12 }) ? 2 : (isIndexOneOf(i, { 2, 10 }) ? 4 : (i == 15 ? 5 : 0));
            r.filterAttack = 0.65f + 0.08f * static_cast<float>(i % 4);
            r.filterDecay = 1.10f;
            r.filterSustain = 0.70f;
            r.filterRelease = 1.40f;
            r.filterEnvAmount = 900.0f + 150.0f * static_cast<float>(i % 7);
            r.filterKeyTracking = 0.08f;
            r.reverbMix = 0.48f + 0.025f * static_cast<float>(i % 5);
            r.delayMix = isIndexOneOf(i, { 1, 4, 8, 15 }) ? 0.26f : 0.14f;
            r.delayTempoSync = true;
            r.delayFeedback = 0.38f + 0.025f * static_cast<float>(i % 5);
            r.autopanDepth = 0.20f + 0.025f * static_cast<float>(i % 6);
            r.autopanRateHz = 0.05f + 0.015f * static_cast<float>(i % 6);
            r.saturationDrive = isIndexOneOf(i, { 3, 9 }) ? 0.18f : 0.04f;
            r.saturationMode = isIndexOneOf(i, { 3, 9 }) ? 2 : 0;
            r.param1 = isIndexOneOf(i, { 1, 4, 15 }) ? 0.72f : (isIndexOneOf(i, { 5, 12 }) ? 120.0f : 10.0f + static_cast<float>(i));
            r.param2 = isIndexOneOf(i, { 2, 5, 12 }) ? 0.12f : 0.04f;
            r.ampAttack = 0.65f + 0.04f * static_cast<float>(i % 4);
            r.ampRelease = 1.60f;
            r.qualityTier = 2;
            applyFilterLfo(r, 0.08f + 0.02f * static_cast<float>(i % 5), 650.0f + 120.0f * static_cast<float>(i % 8), i % 5, false, 10, true);
            if (isIndexOneOf(i, { 8, 14 }))
            {
                r.ampLfoRate = 1.0f + 0.5f * static_cast<float>(i % 3);
                r.ampLfoDepth = 0.18f;
                r.ampLfoShape = 1;
            }
            r.pitchLfoRate = isIndexOneOf(i, { 3, 9 }) ? 0.42f : 0.0f;
            r.pitchLfoDepth = isIndexOneOf(i, { 3, 9 }) ? 5.0f : 0.0f;
            r.macro1ToFilter = 4200.0f;
            r.macro2ToFilter = 2600.0f;
            r.macro2ToAmp = 0.12f;
            r.modWheelToFilter = 3600.0f;
            r.aftertouchToFilter = 2200.0f;
            r.aftertouchToAmp = 0.10f;
            break;
        }

        case Family::pluck:
        {
            static constexpr std::array<int, 16> kinds { 18, 12, 1, 12, 6, 18, 13, 17, 4, 15, 14, 16, 9, 19, 2, 10 };
            r.waveformKind = kinds[static_cast<std::size_t>(i)];
            r.lengthSeconds = isIndexOneOf(i, { 0, 4, 5, 12 }) ? 1.55f : 1.20f;
            r.filterMode = isIndexOneOf(i, { 3, 6, 10, 12 }) ? 4 : 0;
            r.filterAttack = 0.001f;
            r.filterDecay = 0.08f + 0.015f * static_cast<float>(i % 5);
            r.filterSustain = 0.0f;
            r.filterRelease = 0.12f;
            r.filterEnvAmount = 3500.0f + 260.0f * static_cast<float>(i % 6);
            r.filterVelocityAmount = 1800.0f;
            r.reverbMix = 0.16f + 0.015f * static_cast<float>(i % 5);
            r.delayMix = isIndexOneOf(i, { 1, 3, 6, 7, 12, 15 }) ? 0.20f : 0.07f;
            r.delayTempoSync = isIndexOneOf(i, { 1, 6, 7, 15 });
            r.saturationDrive = isIndexOneOf(i, { 6, 9, 11, 13 }) ? 0.22f : 0.06f;
            r.saturationMode = isIndexOneOf(i, { 6, 13 }) ? 3 : 0;
            r.param1 = isIndexOneOf(i, { 0, 5 }) ? 0.55f : (i == 9 ? 6200.0f : 0.38f + 0.02f * static_cast<float>(i));
            r.param2 = 0.18f;
            r.velocityCurve = 2;
            if (isIndexOneOf(i, { 6, 7, 15 }))
                applyFilterLfo(r, 5.0f, 1400.0f, 2, true, 0 + (i % 4), true);
            r.macro1ToFilter = 4800.0f;
            r.macro2ToAmp = isIndexOneOf(i, { 7, 15 }) ? 0.22f : 0.0f;
            break;
        }

        case Family::keys:
        {
            static constexpr std::array<int, 16> kinds { 5, 9, 5, 14, 1, 7, 19, 4, 5, 7, 16, 12, 18, 5, 9, 5 };
            r.waveformKind = kinds[static_cast<std::size_t>(i)];
            r.lengthSeconds = 1.85f + 0.05f * static_cast<float>(i % 4);
            r.filterMode = isIndexOneOf(i, { 2, 3 }) ? 4 : (i == 10 ? 5 : 0);
            r.filterAttack = 0.003f;
            r.filterDecay = 0.28f;
            r.filterSustain = 0.25f;
            r.filterRelease = 0.42f;
            r.filterEnvAmount = 900.0f + 120.0f * static_cast<float>(i % 5);
            r.filterVelocityAmount = 2200.0f;
            r.reverbMix = 0.20f + 0.02f * static_cast<float>(i % 4);
            r.delayMix = isIndexOneOf(i, { 11, 13 }) ? 0.16f : 0.05f;
            r.delayTempoSync = isIndexOneOf(i, { 11, 13 });
            r.autopanDepth = isIndexOneOf(i, { 11, 13 }) ? 0.12f : 0.0f;
            r.autopanRateHz = 0.35f;
            r.saturationDrive = isIndexOneOf(i, { 0, 2, 10, 15 }) ? 0.16f : 0.07f;
            r.saturationMode = isIndexOneOf(i, { 10 }) ? 2 : 0;
            r.param1 = isIndexOneOf(i, { 3 }) ? 0.35f : 1.45f;
            r.param2 = isIndexOneOf(i, { 3 }) ? 0.06f : 1.0f;
            r.velocityCurve = 1;
            r.qualityTier = 2;
            if (isIndexOneOf(i, { 11, 13 }))
            {
                r.ampLfoRate = 0.65f;
                r.ampLfoDepth = 0.08f;
            }
            r.macro1ToFilter = 3600.0f;
            r.aftertouchToFilter = 1400.0f;
            break;
        }

        case Family::bell:
        {
            r.waveformKind = isIndexOneOf(i, { 4, 10, 11, 12, 14 }) ? 18 : (i == 13 ? 15 : 6);
            r.lengthSeconds = isIndexOneOf(i, { 0, 3, 7, 12, 14, 15 }) ? 2.35f : 2.0f;
            r.filterMode = isIndexOneOf(i, { 13, 15 }) ? 4 : 0;
            r.filterEnvAmount = 400.0f;
            r.filterVelocityAmount = 1200.0f;
            r.reverbMix = isIndexOneOf(i, { 3, 12, 14 }) ? 0.42f : 0.30f;
            r.delayMix = isIndexOneOf(i, { 7, 14 }) ? 0.16f : 0.05f;
            r.delayTempoSync = i == 14;
            r.param1 = isIndexOneOf(i, { 4, 10, 11, 12, 14 }) ? 0.55f : (1.0f + 0.18f * static_cast<float>(i % 5));
            r.param2 = 0.0f;
            r.qualityTier = 2;
            r.velocityCurve = 2;
            r.pitchLfoRate = i == 7 ? 5.5f : 0.0f;
            r.pitchLfoDepth = i == 7 ? 7.0f : 0.0f;
            r.macro1ToFilter = 2200.0f;
            r.modWheelToAmp = 0.10f;
            break;
        }

        case Family::ensemble:
        {
            static constexpr std::array<int, 16> kinds { 12, 12, 12, 19, 12, 14, 14, 7, 14, 14, 12, 17, 12, 16, 12, 18 };
            r.waveformKind = kinds[static_cast<std::size_t>(i)];
            r.lengthSeconds = 2.85f + 0.06f * static_cast<float>(i % 4);
            r.filterMode = isIndexOneOf(i, { 6, 8, 9 }) ? 4 : (i == 13 ? 5 : 0);
            r.filterAttack = 0.22f + 0.04f * static_cast<float>(i % 4);
            r.filterDecay = 0.70f;
            r.filterSustain = 0.62f;
            r.filterRelease = 0.95f;
            r.filterEnvAmount = 950.0f;
            r.filterKeyTracking = 0.12f;
            r.reverbMix = 0.34f + 0.02f * static_cast<float>(i % 6);
            r.delayMix = isIndexOneOf(i, { 8, 11, 15 }) ? 0.18f : 0.06f;
            r.delayTempoSync = isIndexOneOf(i, { 8, 11, 15 });
            r.autopanDepth = 0.12f + 0.02f * static_cast<float>(i % 5);
            r.autopanRateHz = 0.07f + 0.02f * static_cast<float>(i % 4);
            r.param1 = isIndexOneOf(i, { 5, 6, 8, 9 }) ? 0.35f : 12.0f;
            r.param2 = isIndexOneOf(i, { 9, 13 }) ? 0.10f : 0.04f;
            r.qualityTier = 2;
            applyFilterLfo(r, 0.18f + 0.03f * static_cast<float>(i % 5), 500.0f + 70.0f * static_cast<float>(i), 0, false, 9, true);
            if (i == 11)
            {
                r.ampLfoRate = 5.5f;
                r.ampLfoDepth = 0.14f;
                r.ampLfoShape = 1;
            }
            r.aftertouchToFilter = 2000.0f;
            r.aftertouchToAmp = 0.08f;
            r.macro2ToFilter = 2400.0f;
            break;
        }

        case Family::fx:
        {
            static constexpr std::array<int, 16> kinds { 15, 15, 19, 16, 15, 13, 17, 18, 16, 16, 17, 17, 18, 17, 13, 18 };
            r.waveformKind = kinds[static_cast<std::size_t>(i)];
            r.lengthSeconds = isIndexOneOf(i, { 0, 1, 3, 4, 5, 8, 9, 11, 13, 14 }) ? 3.65f : 2.60f;
            r.filterMode = isIndexOneOf(i, { 4, 8, 9, 13, 14 }) ? 5 : (isIndexOneOf(i, { 5, 10, 11 }) ? 4 : 0);
            r.filterAttack = 0.20f;
            r.filterDecay = 1.20f;
            r.filterSustain = 0.55f;
            r.filterRelease = 1.10f;
            r.filterEnvAmount = isIndexOneOf(i, { 0, 1, 4 }) ? 2600.0f : 1100.0f;
            r.reverbMix = 0.42f + 0.025f * static_cast<float>(i % 6);
            r.delayMix = 0.18f + 0.025f * static_cast<float>(i % 4);
            r.delayTempoSync = true;
            r.delayFeedback = 0.40f + 0.025f * static_cast<float>(i % 5);
            r.autopanDepth = 0.18f + 0.03f * static_cast<float>(i % 7);
            r.autopanRateHz = 0.06f + 0.04f * static_cast<float>(i % 6);
            r.saturationDrive = isIndexOneOf(i, { 2, 6, 9, 10, 14 }) ? 0.30f : 0.10f;
            r.saturationMode = isIndexOneOf(i, { 8, 9 }) ? 2 : (isIndexOneOf(i, { 6, 10, 14 }) ? 1 : 0);
            r.param1 = (i == 0) ? 120.0f : (i == 1 ? 9000.0f : (isIndexOneOf(i, { 6, 11, 13 }) ? 5.5f : 0.72f));
            r.param2 = (i == 0) ? 9000.0f : (i == 1 ? 140.0f : (i == 4 ? 10000.0f : 0.42f));
            r.reversePlayback = i == 3;
            r.qualityTier = 2;
            applyFilterLfo(r, 0.12f + 0.06f * static_cast<float>(i % 5), 900.0f + 120.0f * static_cast<float>(i % 8), i % 5, isIndexOneOf(i, { 6, 11, 13 }), 3 + (i % 6), true);
            if (isIndexOneOf(i, { 6, 11, 13, 14 }))
            {
                r.ampLfoRate = 2.0f + 0.75f * static_cast<float>(i % 4);
                r.ampLfoDepth = 0.18f + 0.04f * static_cast<float>(i % 3);
                r.ampLfoShape = 2;
            }
            r.pitchLfoRate = isIndexOneOf(i, { 8, 9, 14 }) ? 0.55f : 0.0f;
            r.pitchLfoDepth = isIndexOneOf(i, { 8, 9, 14 }) ? 8.0f : 0.0f;
            r.macro1ToFilter = 6400.0f;
            r.macro2ToFilter = 3600.0f;
            r.macro2ToAmp = 0.16f;
            r.aftertouchToFilter = 1800.0f;
            break;
        }
    }
}

// --- Recipe table ---------------------------------------------------------
//
// 64 factory presets (8 families x 8). The majority are captured from real
// SoundFont (SF2) instruments via Sf2Importer, so the embedded samples sound
// like real basses / leads / pads / etc. instead of single-cycle waveforms.
// A small number of FX recipes remain procedural so we still demonstrate the
// engine's filter/LFO/sweep capabilities on synthetic material.
//
// SF2 search root is `${AUDIOCITY_SF2_DIR}` (env var) and falls back to
// `J:/Samples/SoundFont`. Recipes whose SF2 file or preset cannot be located
// are silently skipped — the bank simply ends up smaller.

namespace
{
juce::File resolveSf2Root()
{
    if (const char* env = std::getenv("AUDIOCITY_SF2_DIR"); env != nullptr && env[0] != '\0')
        return juce::File(juce::String::fromUTF8(env));
    return juce::File("J:/Samples/SoundFont");
}

juce::File sf2(const juce::String& relative)
{
    return resolveSf2Root().getChildFile(relative);
}
} // namespace

std::vector<Recipe> buildRecipes()
{
    std::vector<Recipe> recipes;
    recipes.reserve(64);

    auto add = [&recipes](Recipe r)
    {
        int familyIndex = 0;
        for (const auto& existing : recipes)
            if (existing.family == r.family)
                ++familyIndex;

        // Skip the synth-oriented sound-design sweetening pass on SF2 recipes;
        // the natural instrument character is what we want to preserve. We do
        // still set a few neutral defaults.
        if (r.sf2File == juce::File())
        {
            applyFactorySoundDesign(r, familyIndex);
        }
        else
        {
            r.qualityTier = 1;
            r.velocityCurve = 0;
            r.pitchBendRangeSemitones = 2.0f;
            r.fadeOutSamples = 64;
        }
        recipes.push_back(r);
    };

    const juce::File fluid = sf2("Fluid/Fluid.sf2");
    const juce::File orbit = sf2("Orbit Presets.sf2");
    const juce::File vintage = sf2("Vintage Keys Presets.sf2");

    // Common SF2 recipe factory.
    auto sfx = [&](Family fam,
                   const juce::String& displayName,
                   const juce::File& bank,
                   int gmProgram,
                   int captureNote,
                   float captureSeconds,
                   int playbackMode,
                   bool mono)
    {
        Recipe r;
        r.name = displayName;
        r.family = fam;
        r.rootMidiNote = captureNote;
        r.lengthSeconds = captureSeconds;
        r.baseCutoffHz = 8000.0f;
        r.resonance = 0.10f;
        r.filterMode = 0;
        r.filterEnvAmount = 0.0f;
        r.ampAttack = 0.005f;
        r.ampDecay = 0.20f;
        r.ampSustain = 0.85f;
        r.ampRelease = 0.30f;
        r.reverbMix = 0.10f;
        r.delayMix = 0.0f;
        r.masterVolume = 0.45f;
        r.playbackMode = playbackMode;
        r.monoMode = mono;
        r.velocityToAmp = 0.30f;
        r.velocityToFilter = 800.0f;
        r.modWheelToFilter = 2500.0f;
        r.macro1ToFilter = 3000.0f;

        r.sf2File = bank;
        r.sf2GmProgram = gmProgram;
        r.sf2CaptureMidiNote = captureNote;
        r.sf2CaptureSeconds = captureSeconds;
        r.sf2UseEmbeddedLoop = (playbackMode == 2);
        return r;
    };

    // ----- BASS x8 (GM 32-39, captured at C2 = 36) -----
    {
        struct B { const char* name; int prog; bool mono; float secs; float cutoff; float drive; };
        const std::array<B, 8> entries {{
            { "Acoustic Bass",      32, true,  2.0f,  800.0f, 0.05f },
            { "Fingered E-Bass",    33, true,  2.0f, 1100.0f, 0.10f },
            { "Picked E-Bass",      34, true,  2.0f, 1200.0f, 0.18f },
            { "Fretless Bass",      35, true,  2.5f,  900.0f, 0.05f },
            { "Slap Bass",          36, true,  2.0f, 1500.0f, 0.22f },
            { "Slap Bass 2",        37, true,  2.0f, 1500.0f, 0.25f },
            { "Synth Bass 1",       38, true,  2.5f,  500.0f, 0.04f },
            { "Synth Bass 2",       39, true,  2.5f, 1100.0f, 0.12f },
        }};
        for (const auto& e : entries)
        {
            auto r = sfx(Family::bass, juce::String("Bass / ") + e.name, fluid,
                         e.prog, /*captureNote*/36, e.secs, /*playbackMode*/2, e.mono);
            r.baseCutoffHz = e.cutoff;
            r.saturationDrive = e.drive;
            r.ampRelease = 0.18f;
            r.filterKeyTracking = 0.25f;
            r.macro1ToFilter = 2500.0f;
            // Keep mod-wheel sweeps inside the bass register so the centroid
            // stays below the auditioner's 1200 Hz threshold.
            r.modWheelToFilter = 800.0f;
            add(r);
        }
    }

    // ----- LEAD x8 (GM 80-87 + Vintage Keys, captured at C4 = 60) -----
    {
        struct L { const char* name; int prog; juce::File bank; float secs; float cutoff; bool mono; };
        const std::array<L, 8> entries {{
            { "Square Lead",        80, fluid,   2.5f, 6000.0f, true  },
            { "Sawtooth Lead",      81, fluid,   2.5f, 6500.0f, true  },
            { "Calliope Lead",      82, fluid,   2.5f, 7000.0f, true  },
            { "Chiff Lead",         83, fluid,   2.5f, 7500.0f, true  },
            { "Charang Lead",       84, fluid,   2.5f, 6500.0f, true  },
            { "Voice Lead",         85, fluid,   2.5f, 5500.0f, true  },
            { "Fifths Lead",        86, fluid,   2.5f, 6000.0f, true  },
            { "Bass+Lead",          87, fluid,   2.5f, 5500.0f, true  },
        }};
        for (const auto& e : entries)
        {
            auto r = sfx(Family::lead, juce::String("Lead / ") + e.name, e.bank,
                         e.prog, /*captureNote*/60, e.secs, /*playbackMode*/2, e.mono);
            r.baseCutoffHz = e.cutoff;
            r.reverbMix = 0.12f;
            r.delayMix = 0.10f;
            r.delayTimeMs = 260.0f;
            r.delayFeedback = 0.28f;
            r.modWheelToFilter = 3500.0f;
            r.velocityToFilter = 1500.0f;
            r.masterVolume = 0.40f;
            // Add filter LFO motion to a couple of leads so the bank shows
            // off the modulation matrix on more than just pads/ensembles.
            if (e.prog == 82 || e.prog == 84 || e.prog == 86)
            {
                r.filterLfoRate = 4.5f;
                r.filterLfoAmount = 350.0f;
            }
            // Distorted leads.
            if (e.prog == 84 || e.prog == 87)
                r.saturationDrive = 0.24f;
            add(r);
        }
    }

    // ----- PAD x8 (GM 88-95, captured at C4, long sustain, lush stereo) -----
    {
        struct P { const char* name; int prog; float secs; float cutoff; float attack; };
        const std::array<P, 8> entries {{
            { "New Age Pad",        88, 4.0f, 5500.0f, 0.10f },
            { "Warm Pad",           89, 4.5f, 4500.0f, 0.20f },
            { "Polysynth Pad",      90, 4.0f, 5800.0f, 0.05f },
            { "Choir Pad",          91, 4.5f, 5000.0f, 0.30f },
            { "Bowed Pad",          92, 4.5f, 4800.0f, 0.40f },
            { "Metallic Pad",       93, 4.0f, 6500.0f, 0.10f },
            { "Halo Pad",           94, 4.5f, 5200.0f, 0.25f },
            { "Sweep Pad",          95, 4.5f, 5500.0f, 0.15f },
        }};
        for (const auto& e : entries)
        {
            auto r = sfx(Family::pad, juce::String("Pad / ") + e.name, fluid,
                         e.prog, /*captureNote*/60, e.secs, /*playbackMode*/2, /*mono*/false);
            r.baseCutoffHz = e.cutoff;
            r.ampAttack = e.attack;
            r.ampDecay = 0.50f;
            r.ampSustain = 0.95f;
            r.ampRelease = 1.20f;
            r.reverbMix = 0.35f;
            r.delayMix = 0.12f;
            r.delayTimeMs = 360.0f;
            r.filterLfoRate = 0.18f;
            r.filterLfoAmount = 300.0f;
            r.filterLfoShape = 0;
            r.autopanRateHz = 0.25f;
            r.autopanDepth = 0.20f;
            r.modWheelToFilter = 4000.0f;
            // Diversify filter mode across the pad bank for engine coverage.
            // 0=lp (default), 1=hp on shimmery pads, 2=bp on metallic/halo.
            if (e.prog == 93 || e.prog == 94)
                r.filterMode = 2;
            else if (e.prog == 90 || e.prog == 92)
                r.filterMode = 1;
            add(r);
        }
    }

    // ----- PLUCK x8 (guitars 24-31 + harp 46 + kalimba 108 + koto 107) -----
    {
        struct K { const char* name; int prog; float secs; float cutoff; };
        const std::array<K, 8> entries {{
            { "Nylon Guitar",       24, 3.0f, 6000.0f },
            { "Steel Guitar",       25, 3.0f, 6500.0f },
            { "Jazz Guitar",        26, 3.0f, 5500.0f },
            { "Clean Guitar",       27, 3.0f, 6500.0f },
            { "Muted Guitar",       28, 2.5f, 5000.0f },
            { "Harp",               46, 3.5f, 7000.0f },
            { "Koto",              107, 3.0f, 6500.0f },
            { "Kalimba",          108, 3.0f, 7500.0f },
        }};
        for (const auto& e : entries)
        {
            auto r = sfx(Family::pluck, juce::String("Pluck / ") + e.name, fluid,
                         e.prog, /*captureNote*/60, e.secs, /*playbackMode*/1, /*mono*/false);
            r.baseCutoffHz = e.cutoff;
            r.ampAttack = 0.001f;
            r.ampDecay = 0.40f;
            r.ampSustain = 0.40f;
            r.ampRelease = 0.40f;
            r.reverbMix = 0.18f;
            r.velocityToAmp = 0.35f;
            r.velocityToFilter = 1800.0f;
            add(r);
        }
    }

    // ----- KEYS x8 (GM 0-7 + Vintage Keys EP, captured at C4) -----
    {
        struct K { const char* name; int prog; juce::File bank; juce::String nameHint; float secs; float cutoff; };
        const std::array<K, 8> entries {{
            { "Acoustic Grand",      0, fluid,   "",            3.5f, 7500.0f },
            { "Bright Piano",        1, fluid,   "",            3.5f, 8000.0f },
            { "Electric Grand",      2, fluid,   "",            3.5f, 7000.0f },
            { "Honky-Tonk",          3, fluid,   "",            3.0f, 7000.0f },
            { "Rhodes EP",           4, fluid,   "Rhodes",      3.5f, 6500.0f },
            { "Chorus EP",           5, fluid,   "Chorused",    3.5f, 6500.0f },
            { "Harpsichord",         6, fluid,   "Harpsichord", 3.0f, 7000.0f },
            { "Clavinet",            7, fluid,   "Clavinet",    2.5f, 6500.0f },
        }};
        for (const auto& e : entries)
        {
            auto r = sfx(Family::keys, juce::String("Keys / ") + e.name, e.bank,
                         e.prog, /*captureNote*/60, e.secs, /*playbackMode*/2, /*mono*/false);
            r.sf2NamePattern = e.nameHint;
            r.baseCutoffHz = e.cutoff;
            r.ampAttack = 0.002f;
            r.ampDecay = 0.30f;
            r.ampSustain = 0.55f;
            r.ampRelease = 0.45f;
            r.reverbMix = 0.16f;
            r.masterVolume = 0.40f;
            r.velocityToAmp = 0.40f;
            r.velocityToFilter = 1500.0f;
            r.filterKeyTracking = 0.20f;
            // Clavinet's SF2 loop region clicks; render as a natural decay.
            if (e.prog == 7)
            {
                r.playbackMode = 1;
                r.sf2UseEmbeddedLoop = false;
            }
            add(r);
        }
    }

    // ----- BELL x8 (GM 8-15 chromatic percussion, captured at C5 = 72) -----
    {
        struct B { const char* name; int prog; int capture; float secs; };
        const std::array<B, 8> entries {{
            { "Celesta",             8, 72, 3.0f },
            { "Glockenspiel",        9, 84, 3.0f },
            { "Music Box",          10, 72, 3.5f },
            { "Vibraphone",         11, 72, 3.5f },
            { "Marimba",            12, 72, 3.0f },
            { "Xylophone",          13, 72, 2.5f },
            { "Tubular Bells",      14, 60, 4.0f },
            { "Dulcimer",           15, 60, 3.0f },
        }};
        for (const auto& e : entries)
        {
            auto r = sfx(Family::bell, juce::String("Bell / ") + e.name, fluid,
                         e.prog, e.capture, e.secs, /*playbackMode*/1, /*mono*/false);
            r.baseCutoffHz = 12000.0f;
            r.ampAttack = 0.001f;
            r.ampDecay = 0.80f;
            r.ampSustain = 0.05f;
            r.ampRelease = 0.80f;
            r.reverbMix = 0.18f;
            r.saturationDrive = 0.08f;
            r.velocityToAmp = 0.45f;
            r.velocityToFilter = 1000.0f;
            r.filterKeyTracking = 0.10f;
            add(r);
        }
    }

    // ----- ENSEMBLE x8 (GM 48-55 strings/choirs/synth strings) -----
    {
        struct E { const char* name; int prog; float secs; float attack; float cutoff; };
        const std::array<E, 8> entries {{
            { "String Ensemble",    48, 4.0f, 0.20f, 5000.0f },
            { "Strings 2",          49, 4.0f, 0.30f, 4800.0f },
            { "Synth Strings 1",    50, 4.0f, 0.20f, 5200.0f },
            { "Synth Strings 2",    51, 4.0f, 0.25f, 5400.0f },
            { "Choir Aahs",         52, 4.5f, 0.20f, 4500.0f },
            { "Voice Oohs",         53, 4.5f, 0.25f, 4200.0f },
            { "Synth Voice",        54, 4.0f, 0.20f, 4800.0f },
            { "Orchestra Hit",      55, 2.0f, 0.001f, 6500.0f },
        }};
        for (const auto& e : entries)
        {
            auto r = sfx(Family::ensemble, juce::String("Ensemble / ") + e.name, fluid,
                         e.prog, /*captureNote*/60, e.secs, /*playbackMode*/2, /*mono*/false);
            r.baseCutoffHz = e.cutoff;
            r.ampAttack = e.attack;
            r.ampDecay = 0.40f;
            r.ampSustain = 0.90f;
            r.ampRelease = 0.80f;
            r.reverbMix = 0.30f;
            r.masterVolume = 0.40f;
            r.filterLfoRate = 0.20f;
            r.filterLfoAmount = 220.0f;
            r.autopanRateHz = 0.30f;
            r.autopanDepth = 0.12f;
            r.modWheelToFilter = 3500.0f;
            // Synth-string entries get a touch of analogue grit.
            if (e.prog == 50 || e.prog == 51 || e.prog == 54)
                r.saturationDrive = 0.12f;
            add(r);
        }
    }

    // ----- FX x8 (GM 96-103 sound-effects programs + procedural risers) -----
    // First 6 from SF2 (atmospheric SFX programs), last 2 procedural to keep
    // a couple of demonstrably synth-driven sweeps in the bank.
    {
        struct F { const char* name; int prog; float secs; };
        const std::array<F, 6> sf2Entries {{
            { "Rain",                96, 4.0f },
            { "Soundtrack",          97, 4.0f },
            { "Crystal",             98, 3.5f },
            { "Atmosphere",          99, 4.5f },
            { "Brightness",         100, 4.0f },
            { "Goblins",            101, 4.0f },
        }};
        for (const auto& e : sf2Entries)
        {
            auto r = sfx(Family::fx, juce::String("FX / ") + e.name, fluid,
                         e.prog, /*captureNote*/60, e.secs, /*playbackMode*/2, /*mono*/false);
            r.baseCutoffHz = 5500.0f;
            r.ampAttack = 0.05f;
            r.ampDecay = 0.50f;
            r.ampSustain = 0.85f;
            r.ampRelease = 1.00f;
            r.reverbMix = 0.40f;
            r.delayMix = 0.20f;
            r.delayTimeMs = 420.0f;
            r.delayFeedback = 0.35f;
            r.filterLfoRate = 0.35f;
            r.filterLfoAmount = 400.0f;
            r.autopanRateHz = 0.20f;
            r.autopanDepth = 0.25f;
            r.modWheelToFilter = 4000.0f;
            r.saturationDrive = 0.06f;
            r.masterVolume = 0.55f;
            // Crystal is unusually peaky in our SF2 source; bypass saturator
            // and trim the wet FX tails so its bell stack doesn't pile up.
            if (e.prog == 98)
            {
                r.saturationDrive = 0.0f;
                r.masterVolume    = 0.35f;
                r.delayFeedback   = 0.15f;
                r.delayMix        = 0.12f;
                r.reverbMix       = 0.28f;
            }
            add(r);
        }

        // Procedural riser
        {
            Recipe r;
            r.name = "FX / Synth Riser";
            r.family = Family::fx;
            r.rootMidiNote = 60;
            r.lengthSeconds = 4.0f;
            r.baseCutoffHz = 2000.0f;
            r.resonance = 0.40f;
            r.filterMode = 0;
            r.filterEnvAmount = 6000.0f;
            r.filterAttack = 3.0f;
            r.filterDecay = 0.5f;
            r.filterSustain = 1.0f;
            r.filterRelease = 0.5f;
            r.ampAttack = 0.5f;
            r.ampDecay = 0.5f;
            r.ampSustain = 1.0f;
            r.ampRelease = 0.5f;
            r.reverbMix = 0.45f;
            r.delayMix = 0.25f;
            r.delayTimeMs = 380.0f;
            r.delayFeedback = 0.40f;
            r.playbackMode = 2;
            r.monoMode = false;
            r.masterVolume = 0.45f;
            r.recipeSeed = 901;
            r.waveformKind = 12; // detuned stack
            r.param1 = 8.0f;
            r.param2 = 0.4f;
            add(r);
        }

        // Procedural downer
        {
            Recipe r;
            r.name = "FX / Synth Downer";
            r.family = Family::fx;
            r.rootMidiNote = 60;
            r.lengthSeconds = 4.0f;
            r.baseCutoffHz = 4000.0f;
            r.resonance = 0.35f;
            r.filterMode = 0;
            r.filterEnvAmount = -2500.0f;
            r.filterAttack = 0.05f;
            r.filterDecay = 3.0f;
            r.filterSustain = 0.2f;
            r.filterRelease = 0.5f;
            r.ampAttack = 0.10f;
            r.ampDecay = 0.50f;
            r.ampSustain = 0.85f;
            r.ampRelease = 0.80f;
            r.reverbMix = 0.45f;
            r.delayMix = 0.25f;
            r.delayTimeMs = 420.0f;
            r.delayFeedback = 0.40f;
            r.playbackMode = 2;
            r.monoMode = false;
            r.masterVolume = 0.55f;
            r.recipeSeed = 902;
            r.waveformKind = 11; // pad-style harmonic stack
            r.param1 = 1.5f;
            r.param2 = 0.20f;
            add(r);
        }
    }

    juce::ignoreUnused(orbit, vintage);
    return recipes;
}

// --- ValueTree assembly ---------------------------------------------------

juce::ValueTree buildPresetState(const Recipe& r, const RecipeAudio& ra)
{
    const auto& audio = ra.samples;
    juce::ValueTree state(kPatchRoot);

    juce::MemoryBlock bytes(audio.size() * sizeof(float));
    if (!audio.empty())
        std::memcpy(bytes.getData(), audio.data(), bytes.getSize());

    // Embedded sample is always stored at the recipe's root pitch — for SF2
    // recipes the sample has been pitch-shifted to sf2CaptureMidiNote so that
    // it plays at unity at the same root the engine uses.
    const int storedRoot = (r.sf2File != juce::File()) ? r.sf2CaptureMidiNote : r.rootMidiNote;

    state.setProperty(kEmbeddedSampleData, juce::var(bytes), nullptr);
    state.setProperty(kEmbeddedSampleRate, static_cast<double>(kSampleRate), nullptr);
    state.setProperty(kEmbeddedSampleRootMidiNote, storedRoot, nullptr);
    state.setProperty(kEmbeddedSampleChannels, 1, nullptr);
    state.setProperty(kEmbeddedSampleName, r.name, nullptr);

    state.setProperty(kRootMidiNote, storedRoot, nullptr);
    state.setProperty(kCoarseTuneSemitones, 0.0f, nullptr);
    state.setProperty(kFineTuneCents, 0.0f, nullptr);
    state.setProperty(kPitchBendRangeSemitones, r.pitchBendRangeSemitones, nullptr);
    state.setProperty(kPitchLfoRate, r.pitchLfoRate, nullptr);
    state.setProperty(kPitchLfoDepth, r.pitchLfoDepth, nullptr);

    state.setProperty(kAmpAttack, r.ampAttack, nullptr);
    state.setProperty(kAmpDecay, r.ampDecay, nullptr);
    state.setProperty(kAmpSustain, r.ampSustain, nullptr);
    state.setProperty(kAmpRelease, r.ampRelease, nullptr);
    state.setProperty(kAmpLfoRate, r.ampLfoRate, nullptr);
    state.setProperty(kAmpLfoDepth, r.ampLfoDepth, nullptr);
    state.setProperty(kAmpLfoShape, r.ampLfoShape, nullptr);

    state.setProperty(kFilterAttack, r.filterAttack, nullptr);
    state.setProperty(kFilterDecay, r.filterDecay, nullptr);
    state.setProperty(kFilterSustain, r.filterSustain, nullptr);
    state.setProperty(kFilterRelease, r.filterRelease, nullptr);
    state.setProperty(kFilterBaseCutoff, r.baseCutoffHz, nullptr);
    state.setProperty(kFilterEnvAmount, r.filterEnvAmount, nullptr);
    state.setProperty(kFilterResonance, r.resonance, nullptr);
    state.setProperty(kFilterMode, r.filterMode, nullptr);
    state.setProperty(kFilterKeyTracking, r.filterKeyTracking, nullptr);
    state.setProperty(kFilterVelocityAmount, r.filterVelocityAmount, nullptr);
    state.setProperty(kFilterLfoRate, r.filterLfoRate, nullptr);
    state.setProperty(kFilterLfoRateKeytrack, r.filterLfoRateKeytracking, nullptr);
    state.setProperty(kFilterLfoAmount, r.filterLfoAmount, nullptr);
    state.setProperty(kFilterLfoAmountKeytrack, r.filterLfoAmountKeytracking, nullptr);
    state.setProperty(kFilterLfoStartPhase, r.filterLfoStartPhase, nullptr);
    state.setProperty(kFilterLfoStartPhaseRandom, r.filterLfoStartPhaseRandom, nullptr);
    state.setProperty(kFilterLfoFadeIn, r.filterLfoFadeInMs, nullptr);
    state.setProperty(kFilterLfoShape, r.filterLfoShape, nullptr);
    state.setProperty(kFilterLfoRetrigger, r.filterLfoRetrigger ? 1 : 0, nullptr);
    state.setProperty(kFilterLfoTempoSync, r.filterLfoTempoSync ? 1 : 0, nullptr);
    state.setProperty(kFilterLfoRateKeytrackInTempoSync, r.filterLfoRateKeytrackInTempoSync ? 1 : 0, nullptr);
    state.setProperty(kFilterLfoKeytrackLinear, r.filterLfoKeytrackLinear ? 1 : 0, nullptr);
    state.setProperty(kFilterLfoUnipolar, r.filterLfoUnipolar ? 1 : 0, nullptr);
    state.setProperty(kFilterLfoSyncDivision, r.filterLfoSyncDivision, nullptr);

    state.setProperty(kPlaybackMode, r.playbackMode, nullptr);
    state.setProperty(kQualityTier, r.qualityTier, nullptr);
    state.setProperty(kVelocityCurve, r.velocityCurve, nullptr);
    state.setProperty(kReverbMix, r.reverbMix, nullptr);
    state.setProperty(kDelayMix, r.delayMix, nullptr);
    state.setProperty(kDelayTimeMs, r.delayTimeMs, nullptr);
    state.setProperty(kDelayFeedback, r.delayFeedback, nullptr);
    state.setProperty(kDelayTempoSync, r.delayTempoSync ? 1 : 0, nullptr);
    state.setProperty(kDcFilterEnabled, r.dcFilterEnabled ? 1 : 0, nullptr);
    state.setProperty(kDcFilterCutoffHz, r.dcFilterCutoffHz, nullptr);
    state.setProperty(kAutopanRateHz, r.autopanRateHz, nullptr);
    state.setProperty(kAutopanDepth, r.autopanDepth, nullptr);
    state.setProperty(kSaturationDrive, r.saturationDrive, nullptr);
    state.setProperty(kSaturationMode, r.saturationMode, nullptr);
    state.setProperty(kPan, r.pan, nullptr);
    state.setProperty(kMasterVolume, r.masterVolume, nullptr);
    state.setProperty(kMonoMode, r.monoMode ? 1 : 0, nullptr);
    state.setProperty(kLegatoMode, r.legatoMode ? 1 : 0, nullptr);
    state.setProperty(kGlideSeconds, r.glideSeconds, nullptr);
    state.setProperty(kPolyphonyLimit, r.polyphonyLimit, nullptr);
    state.setProperty(kFadeInSamples, r.fadeInSamples, nullptr);
    state.setProperty(kFadeOutSamples, r.fadeOutSamples, nullptr);
    state.setProperty(kReversePlayback, r.reversePlayback ? 1 : 0, nullptr);

    if (r.playbackMode == 2)
    {
        const int totalLen = static_cast<int>(audio.size());
        if (ra.loopStart > 0
            && ra.loopEnd > ra.loopStart
            && ra.loopEnd < totalLen - 1
            && (ra.loopEnd - ra.loopStart + 1) >= 1024)
        {
            const int loopLen = ra.loopEnd - ra.loopStart + 1;
            // Larger crossfade hides loop-seam discontinuities that the
            // source SF2 zone can introduce when sample-stitched. Keep the
            // crossfade strictly less than half the loop so the factory
            // validator accepts it.
            const int maxXfade = juce::jmax(1, (loopLen / 2) - 1);
            const int crossfade = juce::jlimit(juce::jmin(256, maxXfade), maxXfade, loopLen / 3);
            state.setProperty(kLoopStart, ra.loopStart, nullptr);
            state.setProperty(kLoopEnd, ra.loopEnd, nullptr);
            state.setProperty(kLoopCrossfadeSamples, crossfade, nullptr);
        }
        else
        {
            const auto loopWindow = computeLoopWindow(r, static_cast<int>(audio.size()));
            state.setProperty(kLoopStart, loopWindow.start, nullptr);
            state.setProperty(kLoopEnd, loopWindow.end, nullptr);
            state.setProperty(kLoopCrossfadeSamples, loopWindow.crossfadeSamples, nullptr);
        }
    }

    state.setProperty(kMacro1Value, r.macro1Value, nullptr);
    state.setProperty(kMacro2Value, r.macro2Value, nullptr);
    state.setProperty(kMacro1ToPitch, r.macro1ToPitch, nullptr);
    state.setProperty(kMacro1ToFilter, r.macro1ToFilter, nullptr);
    state.setProperty(kMacro1ToAmp, r.macro1ToAmp, nullptr);
    state.setProperty(kMacro2ToPitch, r.macro2ToPitch, nullptr);
    state.setProperty(kMacro2ToFilter, r.macro2ToFilter, nullptr);
    state.setProperty(kMacro2ToAmp, r.macro2ToAmp, nullptr);
    state.setProperty(kModWheelToPitch, r.modWheelToPitch, nullptr);
    state.setProperty(kModWheelToFilter, r.modWheelToFilter, nullptr);
    state.setProperty(kModWheelToAmp, r.modWheelToAmp, nullptr);
    state.setProperty(kAftertouchToPitch, r.aftertouchToPitch, nullptr);
    state.setProperty(kAftertouchToFilter, r.aftertouchToFilter, nullptr);
    state.setProperty(kAftertouchToAmp, r.aftertouchToAmp, nullptr);
    state.setProperty(kVelocityToPitch, r.velocityToPitch, nullptr);
    state.setProperty(kVelocityToFilter, r.velocityToFilter, nullptr);
    state.setProperty(kVelocityToAmp, r.velocityToAmp, nullptr);

    return state;
}

juce::String sanitizeFilename(const juce::String& s)
{
    juce::String out;
    for (int i = 0; i < s.length(); ++i)
    {
        const auto c = s[i];
        if (juce::CharacterFunctions::isLetterOrDigit(c) || c == '-' || c == '_' || c == ' ')
            out += c;
        else if (c == '/')
            out += "- ";
        else
            out += '_';
    }
    return out.trim();
}

} // namespace

int main(int argc, char* argv[])
{
    juce::ConsoleApplication app;

    juce::File outputDir;
    if (argc > 1)
    {
        outputDir = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1]));
    }
    else
    {
#if defined(AUDIOCITY_SOURCE_DIR)
        outputDir = juce::File(AUDIOCITY_SOURCE_DIR).getChildFile("assets").getChildFile("factory_presets");
#else
        outputDir = juce::File::getCurrentWorkingDirectory().getChildFile("factory_presets");
#endif
    }

    if (!outputDir.exists())
    {
        const auto result = outputDir.createDirectory();
        if (!result.wasOk())
        {
            std::fprintf(stderr, "Failed to create output directory: %s\n",
                outputDir.getFullPathName().toRawUTF8());
            return 1;
        }
    }

    const auto recipes = buildRecipes();
    if (recipes.size() < 64)
    {
        std::fprintf(stderr, "Recipe table has only %d entries; expected >= 64.\n",
            static_cast<int>(recipes.size()));
        return 1;
    }

    int written = 0;
    int failed = 0;
    for (std::size_t i = 0; i < recipes.size(); ++i)
    {
        const auto& r = recipes[i];
        const auto rendered = renderRecipe(r);
        if (rendered.samples.empty())
        {
            ++failed;
            std::fprintf(stderr, "[%03zu] EMPTY  %s\n", i, r.name.toRawUTF8());
            continue;
        }

        const auto state = buildPresetState(r, rendered);
        const auto xml = audiocity::plugin::encodePresetXml(state);
        if (xml.isEmpty())
        {
            ++failed;
            std::fprintf(stderr, "[%03zu] NO_XML %s\n", i, r.name.toRawUTF8());
            continue;
        }

        const auto safeName = sanitizeFilename(r.name);
        const auto fileName = juce::String::formatted("%03zu - ", i + 1) + safeName + ".acp";
        const auto outFile = outputDir.getChildFile(fileName);

        if (!outFile.replaceWithText(xml))
        {
            ++failed;
            std::fprintf(stderr, "[%03zu] WRITE_FAIL %s\n", i, outFile.getFullPathName().toRawUTF8());
            continue;
        }

        ++written;
    }

    std::printf("Wrote %d presets to %s (failed: %d).\n",
        written, outputDir.getFullPathName().toRawUTF8(), failed);

    return failed == 0 ? 0 : 2;
}
