#pragma once

#include "ProgramModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <vector>

namespace audiocity::engine::transient_slice
{
constexpr int kDefaultSliceBaseMidiNote = 36;

struct TransientSliceSettings
{
    int baseMidiNote = kDefaultSliceBaseMidiNote;
    int maxSlices = 32;
    int minSliceSamples = 2048;
    int maxBacktrackSamples = 256;
    float transientThreshold = 0.12f;
    float levelThreshold = 0.02f;
    float fastEnvelopeCoefficient = 0.22f;
    float slowEnvelopeCoefficient = 0.015f;
};

struct TransientSliceProgram
{
    Program program;
    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    std::vector<int> sliceStartSamples;
    int baseMidiNote = kDefaultSliceBaseMidiNote;
    bool truncated = false;
};

inline bool detectTransientSliceStarts(const juce::AudioBuffer<float>& sampleData,
                                       std::vector<int>& sliceStarts,
                                       const TransientSliceSettings& settings = {}) noexcept
{
    sliceStarts.clear();

    const auto totalSamples = sampleData.getNumSamples();
    const auto channels = sampleData.getNumChannels();
    if (totalSamples <= 0 || channels <= 0)
        return false;

    sliceStarts.push_back(0);
    if (totalSamples == 1)
        return true;

    std::vector<float> rectified;
    rectified.resize(static_cast<std::size_t>(totalSamples), 0.0f);

    auto maxAbs = 0.0f;
    for (int sample = 0; sample < totalSamples; ++sample)
    {
        auto mono = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            mono += std::abs(sampleData.getSample(channel, sample));

        mono /= static_cast<float>(channels);
        rectified[static_cast<std::size_t>(sample)] = mono;
        maxAbs = juce::jmax(maxAbs, mono);
    }

    if (maxAbs <= 1.0e-5f)
        return true;

    const auto maxSliceCount = juce::jmax(1, settings.maxSlices);
    const auto minSliceSamples = juce::jlimit(128, juce::jmax(128, totalSamples), settings.minSliceSamples);
    const auto maxBacktrackSamples = juce::jlimit(0, minSliceSamples, settings.maxBacktrackSamples);
    const auto fastCoeff = juce::jlimit(0.01f, 1.0f, settings.fastEnvelopeCoefficient);
    const auto slowCoeff = juce::jlimit(0.001f, 1.0f, settings.slowEnvelopeCoefficient);
    const auto transientThreshold = juce::jmax(settings.transientThreshold, maxAbs * 0.18f);
    const auto levelThreshold = juce::jmax(settings.levelThreshold, maxAbs * 0.08f);
    const auto backtrackThreshold = levelThreshold * 0.35f;

    auto fastEnvelope = rectified.front();
    auto slowEnvelope = rectified.front();
    auto lastSliceStart = 0;

    for (int sample = 1; sample < totalSamples; ++sample)
    {
        const auto level = rectified[static_cast<std::size_t>(sample)];
        fastEnvelope += (level - fastEnvelope) * fastCoeff;
        slowEnvelope += (level - slowEnvelope) * slowCoeff;

        if (sample - lastSliceStart < minSliceSamples)
            continue;

        const auto transientLevel = juce::jmax(0.0f, fastEnvelope - slowEnvelope);
        if (transientLevel < transientThreshold || level < levelThreshold)
            continue;

        auto candidateStart = sample;
        const auto earliestStart = juce::jmax(lastSliceStart + (minSliceSamples / 2), sample - maxBacktrackSamples);
        while (candidateStart > earliestStart
               && rectified[static_cast<std::size_t>(candidateStart)] > backtrackThreshold)
        {
            --candidateStart;
        }

        if (candidateStart - lastSliceStart < minSliceSamples)
            continue;

        sliceStarts.push_back(candidateStart);
        lastSliceStart = candidateStart;
        if (static_cast<int>(sliceStarts.size()) >= maxSliceCount)
            break;
    }

    return true;
}

inline bool buildTransientSliceProgram(const juce::File& sourceFile,
                                       const juce::AudioBuffer<float>& sampleData,
                                       const double sampleRateHz,
                                       TransientSliceProgram& out,
                                       const TransientSliceSettings& settings = {}) noexcept
{
    out = {};

    if (sampleData.getNumChannels() <= 0 || sampleData.getNumSamples() <= 0 || sampleRateHz <= 0.0)
        return false;

    if (!detectTransientSliceStarts(sampleData, out.sliceStartSamples, settings))
        return false;

    const auto totalSlices = static_cast<int>(out.sliceStartSamples.size());
    if (totalSlices <= 1)
        return false;

    const auto clampedBaseMidiNote = juce::jlimit(kMidiNoteMin, kMidiNoteMax, settings.baseMidiNote);
    const auto availableMidiNotes = kMidiNoteMax - clampedBaseMidiNote + 1;
    const auto mappedSliceCount = juce::jmin(totalSlices, availableMidiNotes);
    if (mappedSliceCount <= 1)
        return false;

    out.baseMidiNote = clampedBaseMidiNote;
    out.truncated = mappedSliceCount < totalSlices;
    out.program.name = sourceFile.getFileNameWithoutExtension().isNotEmpty()
        ? (sourceFile.getFileNameWithoutExtension() + " Slices").toStdString()
        : std::string("Transient Slices");

    SampleAsset asset;
    asset.sourcePath = sourceFile.getFullPathName().toStdString();
    asset.displayName = sourceFile.getFileName().toStdString();
    asset.lengthSamples = sampleData.getNumSamples();
    asset.numChannels = sampleData.getNumChannels();
    asset.sampleRateHz = sampleRateHz;
    asset.rootMidiNote = clampedBaseMidiNote;
    asset.embeddedInProgram = true;
    out.program.sampleAssets.push_back(asset);
    out.sampleDataByAsset.push_back(sampleData);

    out.program.zones.reserve(static_cast<std::size_t>(mappedSliceCount));
    for (int sliceIndex = 0; sliceIndex < mappedSliceCount; ++sliceIndex)
    {
        const auto sliceStart = juce::jlimit(0, sampleData.getNumSamples() - 1, out.sliceStartSamples[static_cast<std::size_t>(sliceIndex)]);
        const auto nextSliceStart = sliceIndex + 1 < mappedSliceCount
            ? out.sliceStartSamples[static_cast<std::size_t>(sliceIndex + 1)]
            : sampleData.getNumSamples();
        const auto sliceEndExclusive = juce::jlimit(sliceStart + 1, sampleData.getNumSamples(), nextSliceStart);
        const auto mappedNote = clampedBaseMidiNote + sliceIndex;

        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.keyRange = MidiRange::single(mappedNote);
        zone.velocityRange = VelocityRange::full();
        zone.rootMidiNote = mappedNote;
        zone.sampleStart = sliceStart;
        zone.sampleEndExclusive = sliceEndExclusive;
        zone.triggerMode = ZoneTriggerMode::oneShot;
        zone.loopMode = ZoneLoopMode::noLoop;
        out.program.zones.push_back(zone);
    }

    return out.program.hasPlayableZones();
}
} // namespace audiocity::engine::transient_slice