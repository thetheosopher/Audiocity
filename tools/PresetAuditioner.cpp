// Audiocity Preset Auditioner
//
// Offline tool that loads every .acp factory preset, configures an
// `audiocity::engine::EngineCore` with its parameters and embedded
// sample, renders a deterministic family-specific MIDI audition to
// a stereo WAV under <output-dir>/audio/, and writes a CSV row of
// objective audio measurements (peak, RMS, crest, DC offset, stereo
// correlation, spectral centroid, band ratios, envelope motion,
// centroid motion, loop-seam click energy, clipping).
//
// A separate <output-dir>/regressions.txt is written listing presets
// that fail concrete sanity checks (DC offset, clipping, no envelope
// motion when an LFO is declared, mono-imager when the family is
// expected to be stereo, loop click above threshold, centroid out of
// family band).
//
// Usage:
//     audiocity_preset_auditioner [preset-dir] [output-dir]
// Defaults:
//     preset-dir = <source>/assets/factory_presets
//     output-dir = <source>/artifacts/preset_audition
//
// Real-time safety: offline tool. EngineCore is driven on the main
// thread with block-sized renders, the same call pattern the plugin
// uses on the audio thread, but never from a real audio callback.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../src/plugin/PresetJson.h"
#include "../src/engine/EngineCore.h"
#include "FactoryPresetKeys.h"

namespace fk = audiocity::factory_keys;
using audiocity::engine::EngineCore;

namespace
{
constexpr double kRenderSampleRate = 48000.0;
constexpr int kRenderBlockSize = 256;
constexpr int kRenderChannels = 2;

enum class Family
{
    bass,
    lead,
    pad,
    pluck,
    keys,
    bell,
    ensemble,
    fx,
    unknown
};

const char* familyName(Family f) noexcept
{
    switch (f)
    {
        case Family::bass: return "Bass";
        case Family::lead: return "Lead";
        case Family::pad: return "Pad";
        case Family::pluck: return "Pluck";
        case Family::keys: return "Keys";
        case Family::bell: return "Bell";
        case Family::ensemble: return "Ensemble";
        case Family::fx: return "FX";
        default: return "Unknown";
    }
}

Family detectFamilyFromFile(const juce::File& file) noexcept
{
    const auto name = file.getFileName();
    if (name.containsIgnoreCase(" - Bass - "))     return Family::bass;
    if (name.containsIgnoreCase(" - Lead - "))     return Family::lead;
    if (name.containsIgnoreCase(" - Pad - "))      return Family::pad;
    if (name.containsIgnoreCase(" - Pluck - "))    return Family::pluck;
    if (name.containsIgnoreCase(" - Keys - "))     return Family::keys;
    if (name.containsIgnoreCase(" - Bell - "))     return Family::bell;
    if (name.containsIgnoreCase(" - Ensemble - ")) return Family::ensemble;
    if (name.containsIgnoreCase(" - FX - "))       return Family::fx;
    return Family::unknown;
}

struct AuditionScript
{
    double durationSeconds = 4.0;
    struct Event
    {
        double timeSeconds = 0.0;
        bool isNoteOn = false;     // false => noteOff
        int midiNote = 60;
        float velocity = 0.0f;
        int ccNumber = -1;         // -1 => not a CC
        int ccValue = 0;
    };
    std::vector<Event> events;
};

AuditionScript scriptForFamily(Family family, int rootMidi)
{
    AuditionScript s;

    auto addNote = [&s](double on, double off, int note, float vel)
    {
        s.events.push_back({ on, true, note, vel, -1, 0 });
        s.events.push_back({ off, false, note, 0.0f, -1, 0 });
    };
    auto addCc = [&s](double t, int cc, int v)
    {
        s.events.push_back({ t, false, 0, 0.0f, cc, v });
    };
    auto sweepCc = [&](double from, double to, int cc, int v0, int v1, int steps)
    {
        for (int i = 0; i <= steps; ++i)
        {
            const double a = static_cast<double>(i) / static_cast<double>(steps);
            const double t = from + (to - from) * a;
            const int v = static_cast<int>(std::round(v0 + (v1 - v0) * a));
            addCc(t, cc, juce::jlimit(0, 127, v));
        }
    };

    switch (family)
    {
        case Family::bass:
        {
            s.durationSeconds = 6.0;
            addNote(0.10, 1.10, rootMidi,        0.80f);
            addNote(1.20, 2.20, rootMidi + 7,    0.95f);
            addNote(2.30, 3.30, rootMidi + 12,   1.00f);
            addNote(3.50, 5.50, rootMidi,        0.65f); // hold for filter motion
            sweepCc(3.50, 5.30, 1, 0, 127, 24);          // mod wheel sweep
            break;
        }
        case Family::lead:
        {
            s.durationSeconds = 5.5;
            addNote(0.10, 1.10, rootMidi,        0.85f);
            addNote(1.20, 2.20, rootMidi + 4,    0.95f);
            addNote(2.30, 3.30, rootMidi + 7,    1.00f);
            addNote(3.40, 5.20, rootMidi + 12,   0.90f);
            sweepCc(3.50, 5.00, 1, 0, 110, 18);
            break;
        }
        case Family::pad:
        {
            s.durationSeconds = 8.0;
            // sustained chord with slow mod wheel sweep
            addNote(0.10, 6.20, rootMidi,        0.70f);
            addNote(0.10, 6.20, rootMidi + 4,    0.70f);
            addNote(0.10, 6.20, rootMidi + 7,    0.70f);
            addNote(0.10, 6.20, rootMidi + 12,   0.65f);
            sweepCc(0.50, 6.00, 1, 0, 127, 32);
            break;
        }
        case Family::pluck:
        {
            s.durationSeconds = 4.5;
            const std::array<int, 6> degrees { 0, 4, 7, 12, 7, 4 };
            for (size_t i = 0; i < degrees.size(); ++i)
            {
                const double t = 0.10 + 0.40 * static_cast<double>(i);
                addNote(t, t + 0.30, rootMidi + degrees[i], 0.85f);
            }
            // tail
            addNote(3.10, 4.10, rootMidi, 1.00f);
            break;
        }
        case Family::keys:
        {
            s.durationSeconds = 5.0;
            // hand-played chord stab + sustained chord
            addNote(0.05, 0.80, rootMidi,      0.95f);
            addNote(0.05, 0.80, rootMidi + 4,  0.90f);
            addNote(0.05, 0.80, rootMidi + 7,  0.90f);
            addNote(1.00, 4.50, rootMidi,      0.75f);
            addNote(1.00, 4.50, rootMidi + 4,  0.70f);
            addNote(1.00, 4.50, rootMidi + 7,  0.75f);
            addNote(1.00, 4.50, rootMidi + 11, 0.70f);
            break;
        }
        case Family::bell:
        {
            s.durationSeconds = 4.0;
            addNote(0.10, 0.30, rootMidi, 1.00f);
            addNote(1.40, 1.60, rootMidi + 7, 0.85f);
            addNote(2.40, 2.55, rootMidi + 12, 1.00f);
            break;
        }
        case Family::ensemble:
        {
            s.durationSeconds = 6.0;
            addNote(0.30, 5.20, rootMidi,      0.80f);
            addNote(0.30, 5.20, rootMidi + 4,  0.75f);
            addNote(0.30, 5.20, rootMidi + 7,  0.80f);
            sweepCc(0.50, 4.80, 1, 0, 100, 20);
            break;
        }
        case Family::fx:
        {
            s.durationSeconds = 5.0;
            addNote(0.05, 4.50, rootMidi, 1.00f);
            sweepCc(0.20, 4.50, 1, 0, 127, 30);
            break;
        }
        case Family::unknown:
        default:
        {
            s.durationSeconds = 4.0;
            addNote(0.10, 2.10, rootMidi, 0.85f);
            addNote(2.20, 3.50, rootMidi + 7, 0.95f);
            break;
        }
    }

    std::sort(s.events.begin(), s.events.end(),
        [](const AuditionScript::Event& a, const AuditionScript::Event& b)
        { return a.timeSeconds < b.timeSeconds; });
    return s;
}

// ---- Settings translation -------------------------------------------------

EngineCore::FilterSettings::Mode toFilterMode(int v) noexcept
{
    using Mode = EngineCore::FilterSettings::Mode;
    switch (v)
    {
        case 0: return Mode::lowPass12;
        case 1: return Mode::lowPass24;
        case 2: return Mode::highPass12;
        case 3: return Mode::highPass24;
        case 4: return Mode::bandPass12;
        case 5: return Mode::notch12;
        default: return Mode::lowPass12;
    }
}

EngineCore::FilterSettings::LfoShape toLfoShape(int v) noexcept
{
    using Shape = EngineCore::FilterSettings::LfoShape;
    switch (v)
    {
        case 0: return Shape::sine;
        case 1: return Shape::triangle;
        case 2: return Shape::square;
        case 3: return Shape::sawUp;
        case 4: return Shape::sawDown;
        default: return Shape::sine;
    }
}

EngineCore::SaturationSettings::Mode toSaturationMode(int v) noexcept
{
    using Mode = EngineCore::SaturationSettings::Mode;
    switch (v)
    {
        case 0: return Mode::softClip;
        case 1: return Mode::hardClip;
        case 2: return Mode::tape;
        case 3: return Mode::tube;
        default: return Mode::softClip;
    }
}

EngineCore::PlaybackMode toPlaybackMode(int v) noexcept
{
    switch (v)
    {
        case 1: return EngineCore::PlaybackMode::oneShot;
        case 2: return EngineCore::PlaybackMode::loop;
        default: return EngineCore::PlaybackMode::gate;
    }
}

EngineCore::QualityTier toQualityTier(int v) noexcept
{
    switch (v)
    {
        case 0: return EngineCore::QualityTier::cpu;
        case 2: return EngineCore::QualityTier::ultra;
        default: return EngineCore::QualityTier::fidelity;
    }
}

EngineCore::VelocityCurve toVelocityCurve(int v) noexcept
{
    switch (v)
    {
        case 1: return EngineCore::VelocityCurve::soft;
        case 2: return EngineCore::VelocityCurve::hard;
        default: return EngineCore::VelocityCurve::linear;
    }
}

float propFloat(const juce::ValueTree& s, const char* key, float def)
{
    return static_cast<float>(s.getProperty(juce::Identifier(key), def));
}
int propInt(const juce::ValueTree& s, const char* key, int def)
{
    return static_cast<int>(s.getProperty(juce::Identifier(key), def));
}
bool propBool(const juce::ValueTree& s, const char* key, bool def)
{
    return propInt(s, key, def ? 1 : 0) != 0;
}

bool extractEmbeddedSample(const juce::ValueTree& state,
                           juce::AudioBuffer<float>& outBuffer,
                           double& outSampleRate,
                           int& outRootMidi)
{
    const auto* bytes = state.getProperty(juce::Identifier(fk::kEmbeddedSampleData)).getBinaryData();
    if (bytes == nullptr || bytes->getSize() < sizeof(float))
        return false;

    const int channels = juce::jmax(1, propInt(state, fk::kEmbeddedSampleChannels, 1));
    const int totalFloats = static_cast<int>(bytes->getSize() / sizeof(float));
    const int framesPerChannel = totalFloats / channels;
    if (framesPerChannel <= 0)
        return false;

    outSampleRate = juce::jmax(1.0, static_cast<double>(state.getProperty(juce::Identifier(fk::kEmbeddedSampleRate), 44100.0)));
    outRootMidi = juce::jlimit(0, 127, propInt(state, fk::kEmbeddedSampleRootMidiNote, 60));

    outBuffer.setSize(channels, framesPerChannel, false, true, true);
    const auto* src = static_cast<const float*>(bytes->getData());
    // PresetAuthor writes interleaved when channels > 1; today it always
    // writes mono. Be defensive either way.
    if (channels == 1)
    {
        std::memcpy(outBuffer.getWritePointer(0), src, sizeof(float) * static_cast<size_t>(framesPerChannel));
    }
    else
    {
        for (int c = 0; c < channels; ++c)
        {
            auto* dst = outBuffer.getWritePointer(c);
            for (int i = 0; i < framesPerChannel; ++i)
                dst[i] = src[i * channels + c];
        }
    }
    return true;
}

void applyStateToEngine(const juce::ValueTree& s, EngineCore& engine)
{
    // Envelopes
    EngineCore::AdsrSettings amp;
    amp.attackSeconds  = propFloat(s, fk::kAmpAttack, 0.005f);
    amp.decaySeconds   = propFloat(s, fk::kAmpDecay, 0.20f);
    amp.sustainLevel   = propFloat(s, fk::kAmpSustain, 0.85f);
    amp.releaseSeconds = propFloat(s, fk::kAmpRelease, 0.30f);
    engine.setAmpEnvelope(amp);

    EngineCore::AdsrSettings flt;
    flt.attackSeconds  = propFloat(s, fk::kFilterAttack, 0.005f);
    flt.decaySeconds   = propFloat(s, fk::kFilterDecay, 0.20f);
    flt.sustainLevel   = propFloat(s, fk::kFilterSustain, 0.50f);
    flt.releaseSeconds = propFloat(s, fk::kFilterRelease, 0.30f);
    engine.setFilterEnvelope(flt);

    EngineCore::FilterSettings fs;
    fs.baseCutoffHz             = propFloat(s, fk::kFilterBaseCutoff, 6000.0f);
    fs.envAmountHz              = propFloat(s, fk::kFilterEnvAmount, 0.0f);
    fs.resonance                = propFloat(s, fk::kFilterResonance, 0.2f);
    fs.mode                     = toFilterMode(propInt(s, fk::kFilterMode, 0));
    fs.keyTracking              = propFloat(s, fk::kFilterKeyTracking, 0.0f);
    fs.velocityAmountHz         = propFloat(s, fk::kFilterVelocityAmount, 0.0f);
    fs.lfoRateHz                = propFloat(s, fk::kFilterLfoRate, 0.0f);
    fs.lfoRateKeyTracking       = propFloat(s, fk::kFilterLfoRateKeytrack, 0.0f);
    fs.lfoAmountHz              = propFloat(s, fk::kFilterLfoAmount, 0.0f);
    fs.lfoAmountKeyTracking     = propFloat(s, fk::kFilterLfoAmountKeytrack, 0.0f);
    fs.lfoStartPhaseDegrees     = propFloat(s, fk::kFilterLfoStartPhase, 0.0f);
    fs.lfoStartPhaseRandomDegrees = propFloat(s, fk::kFilterLfoStartPhaseRandom, 0.0f);
    fs.lfoFadeInMs              = propFloat(s, fk::kFilterLfoFadeIn, 0.0f);
    fs.lfoKeytrackLinear        = propBool(s, fk::kFilterLfoKeytrackLinear, false);
    fs.lfoUnipolar              = propBool(s, fk::kFilterLfoUnipolar, false);
    fs.lfoShape                 = toLfoShape(propInt(s, fk::kFilterLfoShape, 0));
    fs.lfoRetrigger             = propBool(s, fk::kFilterLfoRetrigger, true);
    fs.lfoTempoSync             = propBool(s, fk::kFilterLfoTempoSync, false);
    fs.lfoRateKeytrackInTempoSync = propBool(s, fk::kFilterLfoRateKeytrackInTempoSync, true);
    fs.lfoSyncDivision          = propInt(s, fk::kFilterLfoSyncDivision, 6);
    engine.setFilterSettings(fs);

    EngineCore::AmpLfoSettings ampLfo;
    ampLfo.rateHz = propFloat(s, fk::kAmpLfoRate, 0.0f);
    ampLfo.depth  = propFloat(s, fk::kAmpLfoDepth, 0.0f);
    ampLfo.shape  = toLfoShape(propInt(s, fk::kAmpLfoShape, 0));
    engine.setAmpLfoSettings(ampLfo);

    EngineCore::PitchLfoSettings pitchLfo;
    pitchLfo.rateHz     = propFloat(s, fk::kPitchLfoRate, 0.0f);
    pitchLfo.depthCents = propFloat(s, fk::kPitchLfoDepth, 0.0f);
    engine.setPitchLfoSettings(pitchLfo);

    EngineCore::ModulationRoutingSettings mod;
    mod.modWheel.toPitchCents   = propFloat(s, fk::kModWheelToPitch, 0.0f);
    mod.modWheel.toFilterHz     = propFloat(s, fk::kModWheelToFilter, 0.0f);
    mod.modWheel.toAmp          = propFloat(s, fk::kModWheelToAmp, 0.0f);
    mod.aftertouch.toPitchCents = propFloat(s, fk::kAftertouchToPitch, 0.0f);
    mod.aftertouch.toFilterHz   = propFloat(s, fk::kAftertouchToFilter, 0.0f);
    mod.aftertouch.toAmp        = propFloat(s, fk::kAftertouchToAmp, 0.0f);
    mod.velocity.toPitchCents   = propFloat(s, fk::kVelocityToPitch, 0.0f);
    mod.velocity.toFilterHz     = propFloat(s, fk::kVelocityToFilter, 0.0f);
    mod.velocity.toAmp          = propFloat(s, fk::kVelocityToAmp, 0.0f);
    mod.macros[0].toPitchCents  = propFloat(s, fk::kMacro1ToPitch, 0.0f);
    mod.macros[0].toFilterHz    = propFloat(s, fk::kMacro1ToFilter, 0.0f);
    mod.macros[0].toAmp         = propFloat(s, fk::kMacro1ToAmp, 0.0f);
    mod.macros[1].toPitchCents  = propFloat(s, fk::kMacro2ToPitch, 0.0f);
    mod.macros[1].toFilterHz    = propFloat(s, fk::kMacro2ToFilter, 0.0f);
    mod.macros[1].toAmp         = propFloat(s, fk::kMacro2ToAmp, 0.0f);
    engine.setModulationRoutingSettings(mod);

    EngineCore::MacroControlValues macros { propFloat(s, fk::kMacro1Value, 0.0f),
                                            propFloat(s, fk::kMacro2Value, 0.0f) };
    engine.setMacroControlValues(macros);

    EngineCore::DelaySettings delay;
    delay.timeMs    = propFloat(s, fk::kDelayTimeMs, 320.0f);
    delay.feedback  = propFloat(s, fk::kDelayFeedback, 0.35f);
    delay.mix       = propFloat(s, fk::kDelayMix, 0.0f);
    delay.tempoSync = propBool(s, fk::kDelayTempoSync, false);
    engine.setDelaySettings(delay);

    EngineCore::DcFilterSettings dc;
    dc.enabled  = propBool(s, fk::kDcFilterEnabled, true);
    dc.cutoffHz = propFloat(s, fk::kDcFilterCutoffHz, 10.0f);
    engine.setDcFilterSettings(dc);

    EngineCore::AutopanSettings autopan;
    autopan.rateHz = propFloat(s, fk::kAutopanRateHz, 0.5f);
    autopan.depth  = propFloat(s, fk::kAutopanDepth, 0.0f);
    engine.setAutopanSettings(autopan);

    EngineCore::SaturationSettings sat;
    sat.drive = propFloat(s, fk::kSaturationDrive, 0.0f);
    sat.mode  = toSaturationMode(propInt(s, fk::kSaturationMode, 0));
    engine.setSaturationSettings(sat);

    engine.setReverbMix(propFloat(s, fk::kReverbMix, 0.0f));
    engine.setMasterVolume(propFloat(s, fk::kMasterVolume, 0.85f));
    engine.setPan(propFloat(s, fk::kPan, 0.0f));
    engine.setQualityTier(toQualityTier(propInt(s, fk::kQualityTier, 1)));
    engine.setVelocityCurve(toVelocityCurve(propInt(s, fk::kVelocityCurve, 0)));
    engine.setPlaybackMode(toPlaybackMode(propInt(s, fk::kPlaybackMode, 0)));
    engine.setMonoMode(propBool(s, fk::kMonoMode, false));
    engine.setLegatoMode(propBool(s, fk::kLegatoMode, false));
    engine.setGlideSeconds(propFloat(s, fk::kGlideSeconds, 0.0f));
    engine.setPolyphonyLimit(propInt(s, fk::kPolyphonyLimit, 16));
    engine.setPitchBendRangeSemitones(propFloat(s, fk::kPitchBendRangeSemitones, 2.0f));
    engine.setCoarseTuneSemitones(propFloat(s, fk::kCoarseTuneSemitones, 0.0f));
    engine.setFineTuneCents(propFloat(s, fk::kFineTuneCents, 0.0f));
    engine.setReversePlayback(propBool(s, fk::kReversePlayback, false));
    engine.setFadeSamples(propInt(s, fk::kFadeInSamples, 0),
                          propInt(s, fk::kFadeOutSamples, 0));

    const auto loopStart = propInt(s, fk::kLoopStart, -1);
    const auto loopEnd   = propInt(s, fk::kLoopEnd, -1);
    if (loopStart >= 0 && loopEnd > loopStart)
    {
        engine.setLoopPoints(loopStart, loopEnd);
        engine.setLoopCrossfadeSamples(propInt(s, fk::kLoopCrossfadeSamples, 0));
    }
}

// ---- Render -------------------------------------------------------------

juce::AudioBuffer<float> renderAudition(const juce::ValueTree& state,
                                        Family family,
                                        int rootMidi,
                                        double& outDurationSeconds)
{
    juce::AudioBuffer<float> sample;
    double sampleRate = 44100.0;
    int embeddedRoot = rootMidi;
    if (!extractEmbeddedSample(state, sample, sampleRate, embeddedRoot))
    {
        outDurationSeconds = 0.0;
        return juce::AudioBuffer<float>(kRenderChannels, 1);
    }

    EngineCore engine;
    engine.prepare(kRenderSampleRate, kRenderBlockSize, kRenderChannels);
    engine.setSampleData(sample, sampleRate, embeddedRoot);
    engine.setRootMidiNote(propInt(state, fk::kRootMidiNote, embeddedRoot));
    applyStateToEngine(state, engine);

    const auto script = scriptForFamily(family, rootMidi);
    outDurationSeconds = script.durationSeconds;
    const int totalSamples = static_cast<int>(std::ceil(script.durationSeconds * kRenderSampleRate));
    juce::AudioBuffer<float> output(kRenderChannels, totalSamples);
    output.clear();

    size_t nextEvent = 0;
    int rendered = 0;
    juce::AudioBuffer<float> blockBuffer(kRenderChannels, kRenderBlockSize);

    while (rendered < totalSamples)
    {
        const int blockSamples = juce::jmin(kRenderBlockSize, totalSamples - rendered);
        blockBuffer.setSize(kRenderChannels, blockSamples, false, false, true);
        blockBuffer.clear();

        juce::MidiBuffer midi;
        const double blockStart = static_cast<double>(rendered) / kRenderSampleRate;
        const double blockEnd   = static_cast<double>(rendered + blockSamples) / kRenderSampleRate;
        while (nextEvent < script.events.size()
               && script.events[nextEvent].timeSeconds < blockEnd)
        {
            const auto& e = script.events[nextEvent];
            if (e.timeSeconds < blockStart)
            {
                ++nextEvent;
                continue;
            }
            const int offset = juce::jlimit(0, blockSamples - 1,
                static_cast<int>(std::round((e.timeSeconds - blockStart) * kRenderSampleRate)));
            if (e.ccNumber >= 0)
            {
                midi.addEvent(juce::MidiMessage::controllerEvent(1, e.ccNumber, e.ccValue), offset);
            }
            else if (e.isNoteOn)
            {
                midi.addEvent(juce::MidiMessage::noteOn(1, e.midiNote, e.velocity), offset);
            }
            else
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, e.midiNote), offset);
            }
            ++nextEvent;
        }

        engine.render(blockBuffer, midi);
        for (int c = 0; c < kRenderChannels; ++c)
            output.copyFrom(c, rendered, blockBuffer, c, 0, blockSamples);
        rendered += blockSamples;
    }

    return output;
}

bool writeWav(const juce::File& outFile, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    juce::WavAudioFormat fmt;
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(outFile);
    if (!stream->openedOk())
        return false;
    std::unique_ptr<juce::AudioFormatWriter> writer(fmt.createWriterFor(
        stream.get(), sampleRate, static_cast<unsigned int>(buffer.getNumChannels()),
        24, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release(); // writer owns it now
    const bool ok = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();
    return ok;
}

// ---- Metrics ------------------------------------------------------------

struct Metrics
{
    double peakDbfs = -120.0;
    double rmsDbfs = -120.0;
    double crestDb = 0.0;
    double dcOffset = 0.0;
    double stereoCorrelation = 1.0; // 1=mono, 0=independent, -1=anti
    double spectralCentroidHz = 0.0;
    double lowBandRatio = 0.0;
    double midBandRatio = 0.0;
    double highBandRatio = 0.0;
    double envelopeMotionDb = 0.0;  // std-dev of 100ms-block RMS in dB
    double centroidMotionHz = 0.0;  // std-dev of 100ms-block centroid
    double loopClickEnergy = 0.0;   // |x[loopEnd] - x[loopStart]| max across channels
    int clippedSamples = 0;
};

double toDbfs(double linear) noexcept
{
    return linear <= 1.0e-10 ? -120.0 : 20.0 * std::log10(linear);
}

double computeRms(const juce::AudioBuffer<float>& b, int startSample, int numSamples)
{
    double sum = 0.0;
    int count = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const auto* d = b.getReadPointer(c) + startSample;
        for (int i = 0; i < numSamples; ++i)
        {
            const double v = d[i];
            sum += v * v;
        }
        count += numSamples;
    }
    return count > 0 ? std::sqrt(sum / count) : 0.0;
}

double computeSpectralCentroid(const float* mono, int numSamples, double sr,
                               double& outLowRatio, double& outMidRatio, double& outHighRatio)
{
    if (numSamples < 64)
    {
        outLowRatio = outMidRatio = outHighRatio = 0.0;
        return 0.0;
    }
    // Round numSamples down to next power of two, max 8192
    int order = 0;
    int n = 1;
    while ((n << 1) <= juce::jmin(numSamples, 8192))
    {
        n <<= 1;
        ++order;
    }
    if (order < 6)
    {
        outLowRatio = outMidRatio = outHighRatio = 0.0;
        return 0.0;
    }

    std::vector<float> fftBuf(static_cast<size_t>(n) * 2, 0.0f);
    // Hann window
    for (int i = 0; i < n; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
            * static_cast<float>(i) / static_cast<float>(n - 1));
        fftBuf[static_cast<size_t>(i)] = mono[i] * w;
    }
    juce::dsp::FFT fft(order);
    fft.performFrequencyOnlyForwardTransform(fftBuf.data());

    double weighted = 0.0;
    double total = 0.0;
    double low = 0.0, mid = 0.0, high = 0.0;
    for (int k = 1; k < n / 2; ++k)
    {
        const double mag = fftBuf[static_cast<size_t>(k)];
        const double freq = sr * k / n;
        weighted += freq * mag;
        total += mag;
        if (freq < 200.0)         low  += mag;
        else if (freq < 2000.0)   mid  += mag;
        else                      high += mag;
    }
    if (total <= 1.0e-12)
    {
        outLowRatio = outMidRatio = outHighRatio = 0.0;
        return 0.0;
    }
    outLowRatio  = low  / total;
    outMidRatio  = mid  / total;
    outHighRatio = high / total;
    return weighted / total;
}

Metrics computeMetrics(const juce::AudioBuffer<float>& buffer,
                       const juce::ValueTree& state,
                       double sampleRate)
{
    Metrics m;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (n == 0 || channels == 0)
        return m;

    // peak, rms, dc, clipping
    double peak = 0.0;
    double sumSq = 0.0;
    double sum = 0.0;
    int total = 0;
    for (int c = 0; c < channels; ++c)
    {
        const auto* d = buffer.getReadPointer(c);
        for (int i = 0; i < n; ++i)
        {
            const double v = d[i];
            peak = std::max(peak, std::abs(v));
            sumSq += v * v;
            sum += v;
            if (std::abs(v) > 0.999) ++m.clippedSamples;
        }
        total += n;
    }
    m.peakDbfs = toDbfs(peak);
    const double rms = total > 0 ? std::sqrt(sumSq / total) : 0.0;
    m.rmsDbfs = toDbfs(rms);
    m.crestDb = m.peakDbfs - m.rmsDbfs;
    m.dcOffset = total > 0 ? sum / total : 0.0;

    // stereo correlation
    if (channels >= 2)
    {
        const auto* l = buffer.getReadPointer(0);
        const auto* r = buffer.getReadPointer(1);
        double sumLR = 0.0, sumL2 = 0.0, sumR2 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            sumLR += l[i] * r[i];
            sumL2 += l[i] * l[i];
            sumR2 += r[i] * r[i];
        }
        const double denom = std::sqrt(sumL2 * sumR2);
        m.stereoCorrelation = denom > 1.0e-12 ? sumLR / denom : 1.0;
    }

    // sustain-window FFT centroid: take 8192 samples from the middle
    const int analysisStart = juce::jmax(0, n / 3);
    const int analysisLen = juce::jmin(8192, n - analysisStart);
    if (analysisLen >= 1024)
    {
        std::vector<float> mono(static_cast<size_t>(analysisLen));
        for (int i = 0; i < analysisLen; ++i)
        {
            float s2 = 0.0f;
            for (int c = 0; c < channels; ++c)
                s2 += buffer.getReadPointer(c)[analysisStart + i];
            mono[static_cast<size_t>(i)] = s2 / static_cast<float>(channels);
        }
        m.spectralCentroidHz = computeSpectralCentroid(mono.data(), analysisLen, sampleRate,
            m.lowBandRatio, m.midBandRatio, m.highBandRatio);
    }

    // envelope motion: std-dev of 100ms-block RMS in dB across the render
    const int blockSamples = static_cast<int>(std::round(0.100 * sampleRate));
    if (blockSamples > 0 && n >= blockSamples * 4)
    {
        std::vector<double> blockDb;
        std::vector<double> blockCentroid;
        for (int start = 0; start + blockSamples <= n; start += blockSamples)
        {
            const double r = computeRms(buffer, start, blockSamples);
            blockDb.push_back(toDbfs(r));

            // also centroid per block
            std::vector<float> mono(static_cast<size_t>(blockSamples));
            for (int i = 0; i < blockSamples; ++i)
            {
                float s2 = 0.0f;
                for (int c = 0; c < channels; ++c)
                    s2 += buffer.getReadPointer(c)[start + i];
                mono[static_cast<size_t>(i)] = s2 / static_cast<float>(channels);
            }
            double lo, mi, hi;
            const auto cen = computeSpectralCentroid(mono.data(), blockSamples, sampleRate, lo, mi, hi);
            if (cen > 0.0) blockCentroid.push_back(cen);
        }
        // ignore blocks at floor (no signal yet or fully released)
        std::vector<double> active;
        for (double db : blockDb) if (db > -80.0) active.push_back(db);
        if (active.size() >= 4)
        {
            double mean = 0.0;
            for (double v : active) mean += v;
            mean /= active.size();
            double var = 0.0;
            for (double v : active) var += (v - mean) * (v - mean);
            m.envelopeMotionDb = std::sqrt(var / active.size());
        }
        if (blockCentroid.size() >= 4)
        {
            double mean = 0.0;
            for (double v : blockCentroid) mean += v;
            mean /= blockCentroid.size();
            double var = 0.0;
            for (double v : blockCentroid) var += (v - mean) * (v - mean);
            m.centroidMotionHz = std::sqrt(var / blockCentroid.size());
        }
    }

    // loop-seam click energy: only when the preset is authored as loop
    // and has loop points. Read the EMBEDDED sample (not the render).
    if (propInt(state, fk::kPlaybackMode, 0) == 2)
    {
        const int loopStart = propInt(state, fk::kLoopStart, -1);
        const int loopEnd   = propInt(state, fk::kLoopEnd, -1);
        const auto* bytes = state.getProperty(juce::Identifier(fk::kEmbeddedSampleData)).getBinaryData();
        if (bytes != nullptr && loopStart >= 0 && loopEnd > loopStart)
        {
            const int srcChannels = juce::jmax(1, propInt(state, fk::kEmbeddedSampleChannels, 1));
            const int totalFloats = static_cast<int>(bytes->getSize() / sizeof(float));
            const int framesPerChannel = totalFloats / srcChannels;
            if (loopEnd < framesPerChannel)
            {
                const auto* src = static_cast<const float*>(bytes->getData());
                double maxJump = 0.0;
                for (int c = 0; c < srcChannels; ++c)
                {
                    const float a = src[loopEnd * srcChannels + c];
                    const float b = src[loopStart * srcChannels + c];
                    maxJump = std::max(maxJump, static_cast<double>(std::abs(a - b)));
                }
                m.loopClickEnergy = maxJump;
            }
        }
    }

    return m;
}

// ---- Regression rules ---------------------------------------------------

std::vector<juce::String> evaluateRegressions(const juce::String& name,
                                              Family family,
                                              const juce::ValueTree& state,
                                              const Metrics& m)
{
    std::vector<juce::String> issues;

    if (m.peakDbfs < -40.0)
        issues.push_back("silent or near-silent (peak " + juce::String(m.peakDbfs, 1) + " dBFS)");
    if (m.clippedSamples > 16)
        issues.push_back("hard clipping (" + juce::String(m.clippedSamples) + " samples)");
    if (std::abs(m.dcOffset) > 0.01)
        issues.push_back("DC offset " + juce::String(m.dcOffset, 4));
    if (m.loopClickEnergy > 0.55)
        issues.push_back("loop seam click " + juce::String(m.loopClickEnergy, 3));

    // Family-specific expectations
    const auto centroid = m.spectralCentroidHz;
    switch (family)
    {
        case Family::bass:
            if (centroid > 1500.0)
                issues.push_back("bass centroid too high (" + juce::String(centroid, 0) + " Hz)");
            break;
        case Family::pad:
        case Family::ensemble:
            if (m.envelopeMotionDb < 0.5 && propFloat(state, fk::kFilterLfoRate, 0.0f) > 0.0f)
                issues.push_back("filter LFO declared but no envelope/centroid motion detected");
            if (m.stereoCorrelation > 0.985)
                issues.push_back("stereo image nearly mono (corr " + juce::String(m.stereoCorrelation, 3) + ")");
            break;
        case Family::lead:
            if (centroid < 800.0)
                issues.push_back("lead centroid too dark (" + juce::String(centroid, 0) + " Hz)");
            break;
        case Family::bell:
            if (centroid < 1100.0)
                issues.push_back("bell centroid too dark (" + juce::String(centroid, 0) + " Hz)");
            break;
        case Family::pluck:
            if (m.envelopeMotionDb < 1.0)
                issues.push_back("pluck shows no envelope motion (env std " + juce::String(m.envelopeMotionDb, 2) + " dB)");
            break;
        case Family::fx:
            if (m.centroidMotionHz < 50.0 && propFloat(state, fk::kFilterLfoRate, 0.0f) > 0.0f)
                issues.push_back("FX filter LFO declared but centroid is static");
            break;
        default:
            break;
    }

    juce::ignoreUnused(name);
    return issues;
}

juce::String csvEscape(const juce::String& s)
{
    if (s.containsAnyOf(",\""))
        return "\"" + s.replace("\"", "\"\"") + "\"";
    return s;
}

} // namespace

int main(int argc, char* argv[])
{
    juce::File presetDir;
    juce::File outputDir;

#if defined(AUDIOCITY_SOURCE_DIR)
    const juce::File sourceDir(AUDIOCITY_SOURCE_DIR);
    presetDir = sourceDir.getChildFile("assets").getChildFile("factory_presets");
    outputDir = sourceDir.getChildFile("artifacts").getChildFile("preset_audition");
#endif

    if (argc > 1)
        presetDir = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1]));
    if (argc > 2)
        outputDir = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[2]));

    if (!presetDir.isDirectory())
    {
        std::fprintf(stderr, "Preset directory not found: %s\n", presetDir.getFullPathName().toRawUTF8());
        return 1;
    }

    if (!outputDir.exists() && !outputDir.createDirectory().wasOk())
    {
        std::fprintf(stderr, "Cannot create output dir: %s\n", outputDir.getFullPathName().toRawUTF8());
        return 1;
    }
    const auto audioDir = outputDir.getChildFile("audio");
    audioDir.createDirectory();

    juce::Array<juce::File> presetFiles;
    presetDir.findChildFiles(presetFiles, juce::File::TypesOfFileToFind::findFiles, false, "*.acp");
    presetFiles.sort();

    if (presetFiles.isEmpty())
    {
        std::fprintf(stderr, "No .acp files found in %s\n", presetDir.getFullPathName().toRawUTF8());
        return 1;
    }

    // CSV header
    juce::String csv;
    csv << "filename,family,name,peakDbfs,rmsDbfs,crestDb,dcOffset,stereoCorrelation,"
           "centroidHz,lowRatio,midRatio,highRatio,envMotionDb,centroidMotionHz,"
           "loopClickEnergy,clippedSamples,issues\n";

    juce::String regressionsReport;
    int total = 0, withIssues = 0, decodeFailures = 0;

    for (const auto& file : presetFiles)
    {
        ++total;
        const auto xml = file.loadFileAsString();
        juce::ValueTree state;
        juce::String err;
        if (!audiocity::plugin::decodePresetXml(xml, state, err)
            || !state.hasType(fk::kPatchRoot))
        {
            ++decodeFailures;
            std::fprintf(stderr, "[FAIL decode] %s: %s\n",
                file.getFileName().toRawUTF8(), err.toRawUTF8());
            continue;
        }

        const auto family = detectFamilyFromFile(file);
        const int rootMidi = juce::jlimit(0, 127, propInt(state, fk::kRootMidiNote,
            propInt(state, fk::kEmbeddedSampleRootMidiNote, 60)));
        const auto presetName = state.getProperty(juce::Identifier(fk::kEmbeddedSampleName),
            file.getFileNameWithoutExtension()).toString();

        double durationSec = 0.0;
        const auto rendered = renderAudition(state, family, rootMidi, durationSec);
        if (rendered.getNumSamples() <= 1)
        {
            std::fprintf(stderr, "[FAIL render] %s\n", file.getFileName().toRawUTF8());
            continue;
        }

        const auto wavFile = audioDir.getChildFile(file.getFileNameWithoutExtension() + ".wav");
        if (!writeWav(wavFile, rendered, kRenderSampleRate))
            std::fprintf(stderr, "[WARN wav write failed] %s\n", wavFile.getFullPathName().toRawUTF8());

        const auto metrics = computeMetrics(rendered, state, kRenderSampleRate);
        const auto issues = evaluateRegressions(presetName, family, state, metrics);
        if (!issues.empty())
        {
            ++withIssues;
            regressionsReport << file.getFileName() << "  [" << familyName(family) << "]\n";
            for (const auto& issue : issues)
                regressionsReport << "    - " << issue << "\n";
            regressionsReport << "\n";
        }

        juce::String issueJoined;
        for (size_t i = 0; i < issues.size(); ++i)
        {
            if (i > 0) issueJoined << "; ";
            issueJoined << issues[i];
        }

        csv << csvEscape(file.getFileName()) << ','
            << familyName(family) << ','
            << csvEscape(presetName) << ','
            << juce::String(metrics.peakDbfs, 2) << ','
            << juce::String(metrics.rmsDbfs, 2) << ','
            << juce::String(metrics.crestDb, 2) << ','
            << juce::String(metrics.dcOffset, 6) << ','
            << juce::String(metrics.stereoCorrelation, 4) << ','
            << juce::String(metrics.spectralCentroidHz, 1) << ','
            << juce::String(metrics.lowBandRatio, 4) << ','
            << juce::String(metrics.midBandRatio, 4) << ','
            << juce::String(metrics.highBandRatio, 4) << ','
            << juce::String(metrics.envelopeMotionDb, 3) << ','
            << juce::String(metrics.centroidMotionHz, 1) << ','
            << juce::String(metrics.loopClickEnergy, 4) << ','
            << metrics.clippedSamples << ','
            << csvEscape(issueJoined) << '\n';
    }

    outputDir.getChildFile("preset_audition.csv").replaceWithText(csv);
    outputDir.getChildFile("regressions.txt").replaceWithText(
        regressionsReport.isEmpty() ? juce::String("(no regressions)\n") : regressionsReport);

    std::printf("Auditioned %d presets. %d with issues, %d decode failures.\n"
                "Audio:        %s\n"
                "Metrics CSV:  %s\n"
                "Regressions:  %s\n",
        total, withIssues, decodeFailures,
        audioDir.getFullPathName().toRawUTF8(),
        outputDir.getChildFile("preset_audition.csv").getFullPathName().toRawUTF8(),
        outputDir.getChildFile("regressions.txt").getFullPathName().toRawUTF8());

    return decodeFailures == 0 ? 0 : 2;
}
