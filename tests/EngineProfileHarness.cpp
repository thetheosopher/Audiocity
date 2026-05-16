#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "../src/engine/EngineCore.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace
{
juce::AudioBuffer<float> createProfileSample(const int length)
{
    juce::AudioBuffer<float> buffer(1, length);

    for (int i = 0; i < length; ++i)
    {
        const auto phaseA = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 110.0 / 48000.0);
        const auto phaseB = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 440.0 / 48000.0);
        const auto phaseC = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 1320.0 / 48000.0);
        const auto window = 0.5f - 0.5f * std::cos((2.0f * juce::MathConstants<float>::pi * static_cast<float>(i))
            / static_cast<float>(juce::jmax(1, length - 1)));
        const auto value = (0.45f * std::sin(phaseA))
            + (0.25f * std::sin(phaseB))
            + (0.10f * std::sin(phaseC));
        buffer.setSample(0, i, window * value);
    }

    return buffer;
}

audiocity::engine::EngineCore::QualityTier parseQualityTier(const juce::String& value)
{
    if (value.equalsIgnoreCase("cpu"))
        return audiocity::engine::EngineCore::QualityTier::cpu;

    if (value.equalsIgnoreCase("ultra"))
        return audiocity::engine::EngineCore::QualityTier::ultra;

    return audiocity::engine::EngineCore::QualityTier::fidelity;
}

const char* qualityTierName(const audiocity::engine::EngineCore::QualityTier tier)
{
    switch (tier)
    {
        case audiocity::engine::EngineCore::QualityTier::cpu:
            return "cpu";
        case audiocity::engine::EngineCore::QualityTier::ultra:
            return "ultra";
        case audiocity::engine::EngineCore::QualityTier::fidelity:
        default:
            return "fidelity";
    }
}

juce::String readArgumentValue(const juce::StringArray& arguments,
                              const juce::String& option,
                              const juce::String& fallback)
{
    const auto index = arguments.indexOf(option);
    if (index >= 0 && index + 1 < arguments.size())
        return arguments[index + 1];

    return fallback;
}

bool hasFlag(const juce::StringArray& arguments, const juce::String& option)
{
    return arguments.contains(option);
}
}

int main(int argc, char* argv[])
{
    constexpr int channels = 2;
    constexpr double sampleRate = 48000.0;
    constexpr int defaultActiveVoices = 24;
    constexpr double defaultWallClockSeconds = 8.0;

    juce::StringArray arguments;
    for (int index = 1; index < argc; ++index)
        arguments.add(juce::String::fromUTF8(argv[index]));

    const auto blockSize = juce::jmax(16, readArgumentValue(arguments, "--block-size", "128").getIntValue());
    const auto activeVoices = juce::jlimit(1, 64,
        readArgumentValue(arguments, "--voices", juce::String(defaultActiveVoices)).getIntValue());
    const auto requestedSeconds = readArgumentValue(arguments, "--seconds", juce::String(defaultWallClockSeconds)).getDoubleValue();
    const auto wallClockSeconds = requestedSeconds > 0.0 ? requestedSeconds : defaultWallClockSeconds;
    const auto qualityTier = parseQualityTier(readArgumentValue(arguments, "--quality", "fidelity"));
    const auto enableCrossfade = !hasFlag(arguments, "--no-loop-crossfade");

    auto sample = createProfileSample(8192);

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setQualityTier(qualityTier);
    engine.setSampleData(sample, sampleRate, 60);
    engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
    engine.setLoopPoints(384, 7167);
    engine.setLoopCrossfadeSamples(enableCrossfade ? 48 : 0);
    engine.setPolyphonyLimit(activeVoices);

    audiocity::engine::EngineCore::AdsrSettings ampEnvelope;
    ampEnvelope.attackSeconds = 0.0005f;
    ampEnvelope.decaySeconds = 0.010f;
    ampEnvelope.sustainLevel = 1.0f;
    ampEnvelope.releaseSeconds = 0.100f;
    engine.setAmpEnvelope(ampEnvelope);

    audiocity::engine::EngineCore::AdsrSettings filterEnvelope;
    filterEnvelope.attackSeconds = 0.0005f;
    filterEnvelope.decaySeconds = 0.050f;
    filterEnvelope.sustainLevel = 0.70f;
    filterEnvelope.releaseSeconds = 0.120f;
    engine.setFilterEnvelope(filterEnvelope);

    audiocity::engine::EngineCore::FilterSettings filterSettings;
    filterSettings.baseCutoffHz = 1600.0f;
    filterSettings.envAmountHz = 2400.0f;
    filterSettings.resonance = 0.18f;
    filterSettings.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass24;
    filterSettings.velocityAmountHz = 700.0f;
    filterSettings.lfoRateHz = 2.25f;
    filterSettings.lfoAmountHz = 900.0f;
    filterSettings.lfoRetrigger = false;
    filterSettings.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
    engine.setFilterSettings(filterSettings);

    audiocity::engine::EngineCore::AmpLfoSettings ampLfo;
    ampLfo.rateHz = 4.0f;
    ampLfo.depth = 0.15f;
    ampLfo.shape = audiocity::engine::EngineCore::FilterSettings::LfoShape::triangle;
    engine.setAmpLfoSettings(ampLfo);

    audiocity::engine::EngineCore::PitchLfoSettings pitchLfo;
    pitchLfo.rateHz = 5.0f;
    pitchLfo.depthCents = 9.0f;
    engine.setPitchLfoSettings(pitchLfo);

    audiocity::engine::EngineCore::ModulationRoutingSettings modulationRoutes;
    modulationRoutes.velocity.toAmp = 0.15f;
    modulationRoutes.velocity.toFilterHz = 1200.0f;
    modulationRoutes.modWheel.toPitchCents = 18.0f;
    modulationRoutes.aftertouch.toFilterHz = 850.0f;
    modulationRoutes.macros[0].toAmp = 0.08f;
    modulationRoutes.macros[1].toFilterHz = 500.0f;
    engine.setModulationRoutingSettings(modulationRoutes);
    engine.setMacroControlValues({ 0.55f, 0.30f });

    juce::AudioBuffer<float> block(channels, blockSize);
    block.clear();

    juce::MidiBuffer startupMidi;
    for (int voiceIndex = 0; voiceIndex < activeVoices; ++voiceIndex)
    {
        const auto note = 36 + (voiceIndex % 36);
        const auto velocity = 0.45f + 0.45f * (static_cast<float>(voiceIndex % 7) / 6.0f);
        const auto offset = voiceIndex % juce::jmax(1, juce::jmin(16, blockSize));
        startupMidi.addEvent(juce::MidiMessage::noteOn(1, note, velocity), offset);
    }

    engine.render(block, startupMidi);

    double checksum = 0.0;
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const auto* data = block.getReadPointer(channel);
        for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); sampleIndex += 17)
            checksum += data[sampleIndex];
    }

    const auto start = std::chrono::steady_clock::now();
    auto blocksRendered = 1;
    auto modulationPhase = 0.0f;

    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < wallClockSeconds)
    {
        juce::MidiBuffer midi;
        if ((blocksRendered % 8) == 0)
        {
            modulationPhase += 0.13f;
            const auto modWheelValue = juce::jlimit(0, 127,
                static_cast<int>(std::round(63.0 + 48.0 * std::sin(modulationPhase))));
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, modWheelValue), 0);
        }

        if ((blocksRendered % 11) == 0)
        {
            const auto pressureValue = juce::jlimit(0, 127,
                static_cast<int>(std::round(72.0 + 40.0 * std::cos(modulationPhase * 0.7f))));
            midi.addEvent(juce::MidiMessage::channelPressureChange(1, pressureValue), blockSize / 2);
        }

        engine.render(block, midi);
        ++blocksRendered;

        for (int channel = 0; channel < block.getNumChannels(); ++channel)
        {
            const auto* data = block.getReadPointer(channel);
            for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); sampleIndex += 17)
                checksum += data[sampleIndex];
        }
    }

    const auto elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto renderedSamples = static_cast<double>(blocksRendered) * static_cast<double>(blockSize);
    const auto realtimeFactor = elapsedSeconds > 0.0
        ? (renderedSamples / sampleRate) / elapsedSeconds
        : 0.0;

    std::printf("engine-profile quality=%s blockSize=%d activeVoices=%d blocks=%d elapsed=%.3f realtimeFactor=%.2fx checksum=%.9f\n",
        qualityTierName(qualityTier),
        blockSize,
        activeVoices,
        blocksRendered,
        elapsedSeconds,
        realtimeFactor,
        checksum);

    return std::isfinite(checksum) ? 0 : 1;
}