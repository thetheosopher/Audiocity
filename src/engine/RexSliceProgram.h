#pragma once

#include "ProgramModel.h"
#include "RexLoader.h"

namespace audiocity::engine::rex
{
constexpr int kDefaultSliceBaseMidiNote = 36;

struct ChromaticSliceProgram
{
    Program program;
    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    int baseMidiNote = kDefaultSliceBaseMidiNote;
    bool truncated = false;
};

inline bool buildChromaticSliceProgram(const juce::File& sourceFile,
                                       const DecodedLoop& decoded,
                                       ChromaticSliceProgram& out,
                                       const int baseMidiNote = kDefaultSliceBaseMidiNote) noexcept
{
    out = {};

    if (decoded.slices.empty() || decoded.sampleRateHz <= 0.0)
        return false;

    const auto clampedBaseMidiNote = juce::jlimit(kMidiNoteMin, kMidiNoteMax, baseMidiNote);
    const auto maxSliceSlots = kMidiNoteMax - clampedBaseMidiNote + 1;
    const auto mappedSliceCount = juce::jmin<int>(static_cast<int>(decoded.slices.size()), maxSliceSlots);
    if (mappedSliceCount <= 0)
        return false;

    const auto baseName = sourceFile.getFileNameWithoutExtension();
    out.baseMidiNote = clampedBaseMidiNote;
    out.truncated = mappedSliceCount < static_cast<int>(decoded.slices.size());
    out.program.name = baseName.isNotEmpty() ? baseName.toStdString() : std::string("REX Slices");
    out.program.sampleAssets.reserve(static_cast<std::size_t>(mappedSliceCount));
    out.program.zones.reserve(static_cast<std::size_t>(mappedSliceCount));
    out.sampleDataByAsset.reserve(static_cast<std::size_t>(mappedSliceCount));

    for (int sliceIndex = 0; sliceIndex < mappedSliceCount; ++sliceIndex)
    {
        const auto& slice = decoded.slices[static_cast<std::size_t>(sliceIndex)];
        if (slice.audio.getNumChannels() <= 0 || slice.audio.getNumSamples() <= 0)
            return false;

        const auto mappedNote = clampedBaseMidiNote + sliceIndex;

        SampleAsset asset;
        asset.sourcePath = sourceFile.getFullPathName().toStdString();
        asset.displayName = (baseName + " Slice " + juce::String(sliceIndex + 1)).toStdString();
        asset.lengthSamples = slice.audio.getNumSamples();
        asset.numChannels = slice.audio.getNumChannels();
        asset.sampleRateHz = decoded.sampleRateHz;
        asset.rootMidiNote = mappedNote;
        asset.embeddedInProgram = true;
        out.program.sampleAssets.push_back(asset);
        out.sampleDataByAsset.push_back(slice.audio);

        Zone zone;
        zone.sampleAssetIndex = sliceIndex;
        zone.keyRange = MidiRange::single(mappedNote);
        zone.velocityRange = VelocityRange::full();
        zone.rootMidiNote = mappedNote;
        zone.sampleStart = 0;
        zone.sampleEndExclusive = asset.lengthSamples;
        zone.triggerMode = ZoneTriggerMode::oneShot;
        zone.loopMode = ZoneLoopMode::noLoop;
        out.program.zones.push_back(zone);
    }

    return out.program.hasPlayableZones()
        && out.program.sampleAssets.size() == out.sampleDataByAsset.size();
}

inline bool decodeFileAsChromaticSliceProgram(const juce::File& file,
                                              ChromaticSliceProgram& out,
                                              const int baseMidiNote = kDefaultSliceBaseMidiNote) noexcept
{
    DecodedLoop decoded;
    if (!decodeFile(file, decoded))
        return false;

    return buildChromaticSliceProgram(file, decoded, out, baseMidiNote);
}
} // namespace audiocity::engine::rex