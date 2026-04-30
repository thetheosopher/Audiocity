#pragma once

#include "ProgramModel.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace audiocity::engine
{
struct ProgramSnapshot
{
    static constexpr std::size_t maxSampleAssets = 256;
    static constexpr std::size_t maxGroups = 128;
    static constexpr std::size_t maxZones = 512;

    struct SampleAssetRef
    {
        int lengthSamples = 0;
        int numChannels = 0;
        double sampleRateHz = 0.0;
        int rootMidiNote = 60;
        int bitDepth = 0;
        bool embeddedInProgram = false;

        [[nodiscard]] bool hasAudio() const noexcept
        {
            return lengthSamples > 0 && numChannels > 0 && sampleRateHz > 0.0;
        }
    };

    struct GroupRef
    {
        MidiRange keyRange;
        VelocityRange velocityRange;
        float gainDb = 0.0f;
        float pan = 0.0f;
        int roundRobinGroup = 0;
        int chokeGroup = 0;

        [[nodiscard]] bool matches(const int note, const int velocity) const noexcept
        {
            return keyRange.contains(note) && velocityRange.contains(velocity);
        }
    };

    struct ZoneRef
    {
        int sampleAssetIndex = -1;
        int groupIndex = -1;
        MidiRange keyRange;
        VelocityRange velocityRange;
        int rootMidiNote = 60;
        int sampleStart = 0;
        int sampleEndExclusive = -1;
        int loopStart = -1;
        int loopEndExclusive = -1;
        float gainDb = 0.0f;
        float pan = 0.0f;
        float tuneCents = 0.0f;
        int roundRobinGroup = 0;
        int roundRobinPosition = 0;
        int chokeGroup = 0;
        ZoneLoopMode loopMode = ZoneLoopMode::noLoop;

        [[nodiscard]] bool hasSample() const noexcept
        {
            return sampleAssetIndex >= 0;
        }

        [[nodiscard]] bool matches(const int note, const int velocity) const noexcept
        {
            return hasSample() && keyRange.contains(note) && velocityRange.contains(velocity);
        }
    };

    std::uint32_t version = kProgramModelVersion;
    std::array<SampleAssetRef, maxSampleAssets> sampleAssets{};
    std::array<GroupRef, maxGroups> groups{};
    std::array<ZoneRef, maxZones> zones{};
    std::size_t sampleAssetCount = 0;
    std::size_t groupCount = 0;
    std::size_t zoneCount = 0;
    bool truncated = false;

    [[nodiscard]] static ProgramSnapshot fromProgram(const Program& program) noexcept
    {
        ProgramSnapshot snapshot;
        snapshot.version = program.version;
        snapshot.truncated = program.sampleAssets.size() > maxSampleAssets
            || program.groups.size() > maxGroups
            || program.zones.size() > maxZones;

        snapshot.sampleAssetCount = program.sampleAssets.size() < maxSampleAssets
            ? program.sampleAssets.size()
            : maxSampleAssets;
        for (std::size_t index = 0; index < snapshot.sampleAssetCount; ++index)
        {
            const auto& source = program.sampleAssets[index];
            auto& target = snapshot.sampleAssets[index];
            target.lengthSamples = source.lengthSamples;
            target.numChannels = source.numChannels;
            target.sampleRateHz = source.sampleRateHz;
            target.rootMidiNote = clampMidiNote(source.rootMidiNote);
            target.bitDepth = source.bitDepth;
            target.embeddedInProgram = source.embeddedInProgram;
        }

        snapshot.groupCount = program.groups.size() < maxGroups ? program.groups.size() : maxGroups;
        for (std::size_t index = 0; index < snapshot.groupCount; ++index)
        {
            const auto& source = program.groups[index];
            auto& target = snapshot.groups[index];
            target.keyRange = source.keyRange;
            target.velocityRange = source.velocityRange;
            target.gainDb = source.gainDb;
            target.pan = source.pan;
            target.roundRobinGroup = source.roundRobinGroup;
            target.chokeGroup = source.chokeGroup;
        }

        snapshot.zoneCount = program.zones.size() < maxZones ? program.zones.size() : maxZones;
        for (std::size_t index = 0; index < snapshot.zoneCount; ++index)
        {
            const auto& source = program.zones[index];
            auto& target = snapshot.zones[index];
            target.sampleAssetIndex = source.sampleAssetIndex;
            target.groupIndex = source.groupIndex;
            target.keyRange = source.keyRange;
            target.velocityRange = source.velocityRange;
            target.rootMidiNote = clampMidiNote(source.rootMidiNote);
            target.sampleStart = source.sampleStart;
            target.sampleEndExclusive = source.sampleEndExclusive;
            target.loopStart = source.loopStart;
            target.loopEndExclusive = source.loopEndExclusive;
            target.gainDb = source.gainDb;
            target.pan = source.pan;
            target.tuneCents = source.tuneCents;
            target.roundRobinGroup = source.roundRobinGroup;
            target.roundRobinPosition = source.roundRobinPosition;
            target.chokeGroup = source.chokeGroup;
            target.loopMode = source.loopMode;
        }

        return snapshot;
    }

    [[nodiscard]] bool isZoneSampleIndexValid(const ZoneRef& zone) const noexcept
    {
        return zone.sampleAssetIndex >= 0
            && static_cast<std::size_t>(zone.sampleAssetIndex) < sampleAssetCount;
    }

    [[nodiscard]] bool isZoneGroupIndexValid(const ZoneRef& zone) const noexcept
    {
        return zone.groupIndex < 0 || static_cast<std::size_t>(zone.groupIndex) < groupCount;
    }

    [[nodiscard]] bool hasPlayableZones() const noexcept
    {
        for (std::size_t index = 0; index < zoneCount; ++index)
        {
            const auto& zone = zones[index];
            if (isZoneSampleIndexValid(zone)
                && isZoneGroupIndexValid(zone)
                && sampleAssets[static_cast<std::size_t>(zone.sampleAssetIndex)].hasAudio())
            {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] int findFirstMatchingZoneIndex(const int note, const int velocity) const noexcept
    {
        for (std::size_t index = 0; index < zoneCount; ++index)
        {
            const auto& zone = zones[index];
            if (!isZoneSampleIndexValid(zone)
                || !isZoneGroupIndexValid(zone)
                || !sampleAssets[static_cast<std::size_t>(zone.sampleAssetIndex)].hasAudio()
                || !zone.matches(note, velocity))
            {
                continue;
            }

            if (zone.groupIndex >= 0
                && !groups[static_cast<std::size_t>(zone.groupIndex)].matches(note, velocity))
            {
                continue;
            }

            return static_cast<int>(index);
        }

        return -1;
    }
};
} // namespace audiocity::engine
