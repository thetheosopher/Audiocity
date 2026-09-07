#pragma once

#include "ProgramModel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace audiocity::engine
{
struct ProgramSnapshot
{
    // Product security ceilings. These are validation limits, not storage sizes: accepted
    // programs retain every asset/group/zone in dynamically-sized immutable vectors.
    static constexpr std::size_t maxSampleAssets = 4096;
    static constexpr std::size_t maxGroups = 2048;
    static constexpr std::size_t maxZones = 16384;

    struct CapacityReport
    {
        std::size_t sampleAssetCount = 0;
        std::size_t groupCount = 0;
        std::size_t zoneCount = 0;
        std::size_t sampleAssetLimit = maxSampleAssets;
        std::size_t groupLimit = maxGroups;
        std::size_t zoneLimit = maxZones;

        [[nodiscard]] bool sampleAssetsExceeded() const noexcept { return sampleAssetCount > sampleAssetLimit; }
        [[nodiscard]] bool groupsExceeded() const noexcept { return groupCount > groupLimit; }
        [[nodiscard]] bool zonesExceeded() const noexcept { return zoneCount > zoneLimit; }
        [[nodiscard]] bool accepted() const noexcept
        {
            return !sampleAssetsExceeded() && !groupsExceeded() && !zonesExceeded();
        }
    };

    [[nodiscard]] static CapacityReport validateCapacity(const Program& program) noexcept
    {
        CapacityReport report;
        report.sampleAssetCount = program.sampleAssets.size();
        report.groupCount = program.groups.size();
        report.zoneCount = program.zones.size();
        return report;
    }

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
        VelocityFadeRange velocityFadeIn;
        VelocityFadeRange velocityFadeOut;
        float gainDb = 0.0f;
        float pan = 0.0f;
        int roundRobinGroup = 0;
        RoundRobinMode roundRobinMode = RoundRobinMode::ordered;
        ZoneTriggerMode triggerMode = ZoneTriggerMode::gate;
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
        VelocityFadeRange velocityFadeIn;
        VelocityFadeRange velocityFadeOut;
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
        int roundRobinLength = 0;
        RoundRobinMode roundRobinMode = RoundRobinMode::ordered;
        ZoneTriggerMode triggerMode = ZoneTriggerMode::gate;
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
    std::vector<SampleAssetRef> sampleAssets;
    std::vector<GroupRef> groups;
    std::vector<ZoneRef> zones;
    // Global order is prepared off-thread. Filtering this sequence preserves the legacy
    // per-note/per-velocity round-robin order without render-time sorting or allocation.
    std::vector<std::size_t> roundRobinOrderedZoneIndices;
    std::size_t sampleAssetCount = 0;
    std::size_t groupCount = 0;
    std::size_t zoneCount = 0;
    CapacityReport capacity{};
    // Retained for source compatibility. An over-budget snapshot is empty and is never
    // published; accepted snapshots are complete, so this can no longer mean partial load.
    bool truncated = false;

    [[nodiscard]] static ProgramSnapshot fromProgram(const Program& program)
    {
        ProgramSnapshot snapshot;
        snapshot.version = program.version;
        snapshot.capacity = validateCapacity(program);
        snapshot.truncated = !snapshot.capacity.accepted();
        if (snapshot.truncated)
            return snapshot;

        snapshot.sampleAssetCount = program.sampleAssets.size();
        snapshot.groupCount = program.groups.size();
        snapshot.zoneCount = program.zones.size();
        snapshot.sampleAssets.resize(snapshot.sampleAssetCount);
        snapshot.groups.resize(snapshot.groupCount);
        snapshot.zones.resize(snapshot.zoneCount);

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

        for (std::size_t index = 0; index < snapshot.groupCount; ++index)
        {
            const auto& source = program.groups[index];
            auto& target = snapshot.groups[index];
            target.keyRange = source.keyRange;
            target.velocityRange = source.velocityRange;
            target.velocityFadeIn = source.velocityFadeIn;
            target.velocityFadeOut = source.velocityFadeOut;
            target.gainDb = source.gainDb;
            target.pan = source.pan;
            target.roundRobinGroup = source.roundRobinGroup;
            target.roundRobinMode = source.roundRobinMode;
            target.triggerMode = source.triggerMode;
            target.chokeGroup = source.chokeGroup;
        }

        for (std::size_t index = 0; index < snapshot.zoneCount; ++index)
        {
            const auto& source = program.zones[index];
            auto& target = snapshot.zones[index];
            target.sampleAssetIndex = source.sampleAssetIndex;
            target.groupIndex = source.groupIndex;
            target.keyRange = source.keyRange;
            target.velocityRange = source.velocityRange;
            target.velocityFadeIn = source.velocityFadeIn;
            target.velocityFadeOut = source.velocityFadeOut;
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
            target.roundRobinLength = source.roundRobinLength;
            target.roundRobinMode = source.roundRobinMode;
            target.triggerMode = source.triggerMode;
            target.chokeGroup = source.chokeGroup;
            target.loopMode = source.loopMode;
        }

        snapshot.roundRobinOrderedZoneIndices.resize(snapshot.zoneCount);
        std::iota(snapshot.roundRobinOrderedZoneIndices.begin(), snapshot.roundRobinOrderedZoneIndices.end(), 0);
        std::sort(snapshot.roundRobinOrderedZoneIndices.begin(), snapshot.roundRobinOrderedZoneIndices.end(),
            [&snapshot](const std::size_t left, const std::size_t right) noexcept
            {
                return snapshot.roundRobinSortsBefore(left, right);
            });

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
            if (isZonePlayable(index))
                return true;
        }

        return false;
    }

    [[nodiscard]] bool isZonePlayable(const std::size_t zoneIndex) const noexcept
    {
        if (zoneIndex >= zoneCount)
            return false;

        const auto& zone = zones[zoneIndex];
        return isZoneSampleIndexValid(zone)
            && isZoneGroupIndexValid(zone)
            && sampleAssets[static_cast<std::size_t>(zone.sampleAssetIndex)].hasAudio();
    }

    [[nodiscard]] bool isZonePlayableMatch(const std::size_t zoneIndex, const int note, const int velocity) const noexcept
    {
        if (!isZonePlayable(zoneIndex))
            return false;

        const auto& zone = zones[zoneIndex];
        if (!zone.matches(note, velocity))
            return false;

        if (zone.groupIndex >= 0
            && !groups[static_cast<std::size_t>(zone.groupIndex)].matches(note, velocity))
        {
            return false;
        }

        return true;
    }

    [[nodiscard]] int getZoneRoundRobinGroup(const ZoneRef& zone) const noexcept
    {
        if (zone.roundRobinGroup > 0)
            return zone.roundRobinGroup;

        if (isZoneGroupIndexValid(zone) && zone.groupIndex >= 0)
        {
            const auto groupRoundRobin = groups[static_cast<std::size_t>(zone.groupIndex)].roundRobinGroup;
            if (groupRoundRobin > 0)
                return groupRoundRobin;
        }

        return 0;
    }

    [[nodiscard]] int getZoneChokeGroup(const ZoneRef& zone) const noexcept
    {
        if (zone.chokeGroup > 0)
            return zone.chokeGroup;

        if (isZoneGroupIndexValid(zone) && zone.groupIndex >= 0)
        {
            const auto groupChoke = groups[static_cast<std::size_t>(zone.groupIndex)].chokeGroup;
            if (groupChoke > 0)
                return groupChoke;
        }

        return 0;
    }

    [[nodiscard]] RoundRobinMode getZoneRoundRobinMode(const ZoneRef& zone) const noexcept
    {
        if (zone.roundRobinMode == RoundRobinMode::cycleRandom)
            return RoundRobinMode::cycleRandom;

        if (isZoneGroupIndexValid(zone) && zone.groupIndex >= 0)
            return groups[static_cast<std::size_t>(zone.groupIndex)].roundRobinMode;

        return RoundRobinMode::ordered;
    }

    [[nodiscard]] int getZoneRoundRobinLength(const ZoneRef& zone) const noexcept
    {
        return zone.roundRobinLength > 0 ? zone.roundRobinLength : 0;
    }

    [[nodiscard]] ZoneTriggerMode getZoneTriggerMode(const ZoneRef& zone) const noexcept
    {
        if (zone.triggerMode == ZoneTriggerMode::release)
            return ZoneTriggerMode::release;

        if (zone.triggerMode == ZoneTriggerMode::oneShot)
            return ZoneTriggerMode::oneShot;

        if (isZoneGroupIndexValid(zone) && zone.groupIndex >= 0)
            return groups[static_cast<std::size_t>(zone.groupIndex)].triggerMode;

        return ZoneTriggerMode::gate;
    }

    [[nodiscard]] float getZonePan(const ZoneRef& zone) const noexcept
    {
        auto pan = zone.pan;
        if (isZoneGroupIndexValid(zone) && zone.groupIndex >= 0)
            pan += groups[static_cast<std::size_t>(zone.groupIndex)].pan;

        if (pan < -1.0f)
            return -1.0f;

        if (pan > 1.0f)
            return 1.0f;

        return pan;
    }

    [[nodiscard]] float getZoneGainDb(const ZoneRef& zone) const noexcept
    {
        auto gainDb = zone.gainDb;
        if (isZoneGroupIndexValid(zone) && zone.groupIndex >= 0)
            gainDb += groups[static_cast<std::size_t>(zone.groupIndex)].gainDb;

        return gainDb;
    }

    [[nodiscard]] static float computeVelocityFadeGain(const VelocityFadeRange& range,
                                                       const int velocity,
                                                       const bool fadeIn) noexcept
    {
        if (!range.isEnabled())
            return 1.0f;

        if (fadeIn)
        {
            if (velocity <= range.low)
                return 0.0f;
            if (velocity >= range.high)
                return 1.0f;
            return static_cast<float>(velocity - range.low) / static_cast<float>(range.high - range.low);
        }

        if (velocity <= range.low)
            return 1.0f;
        if (velocity >= range.high)
            return 0.0f;
        return 1.0f - (static_cast<float>(velocity - range.low) / static_cast<float>(range.high - range.low));
    }

    [[nodiscard]] float getZoneVelocityGain(const ZoneRef& zone, const int velocity) const noexcept
    {
        auto gain = computeVelocityFadeGain(zone.velocityFadeIn, velocity, true)
            * computeVelocityFadeGain(zone.velocityFadeOut, velocity, false);

        if (isZoneGroupIndexValid(zone) && zone.groupIndex >= 0)
        {
            const auto& group = groups[static_cast<std::size_t>(zone.groupIndex)];
            gain *= computeVelocityFadeGain(group.velocityFadeIn, velocity, true)
                * computeVelocityFadeGain(group.velocityFadeOut, velocity, false);
        }

        return gain;
    }

    [[nodiscard]] int findFirstMatchingZoneIndex(const int note, const int velocity) const noexcept
    {
        for (std::size_t index = 0; index < zoneCount; ++index)
        {
            if (isZonePlayableMatch(index, note, velocity))
                return static_cast<int>(index);
        }

        return -1;
    }

    [[nodiscard]] int findRoundRobinMatchingZoneIndex(const int note,
                                                      const int velocity,
                                                      const int roundRobinGroup,
                                                      const std::uint32_t roundRobinStep,
                                                      const RoundRobinMode roundRobinMode = RoundRobinMode::ordered) const noexcept
    {
        if (roundRobinGroup <= 0)
            return findFirstMatchingZoneIndex(note, velocity);

        std::size_t candidateCount = 0;
        auto roundRobinLength = 0;

        for (std::size_t index = 0; index < zoneCount; ++index)
        {
            const auto& zone = zones[index];
            if (isZonePlayableMatch(index, note, velocity)
                && getZoneRoundRobinGroup(zone) == roundRobinGroup)
            {
                ++candidateCount;
                const auto candidateLength = getZoneRoundRobinLength(zone);
                if (candidateLength > roundRobinLength)
                    roundRobinLength = candidateLength;
            }
        }

        if (candidateCount == 0)
            return -1;

        const auto selectedOrdinal = roundRobinMode == RoundRobinMode::cycleRandom
            ? cycleRandomCandidateIndex(roundRobinGroup, roundRobinStep, candidateCount)
            : static_cast<std::size_t>(roundRobinStep % static_cast<std::uint32_t>(candidateCount));

        const auto targetPosition = roundRobinMode == RoundRobinMode::ordered && roundRobinLength > 0
            ? static_cast<int>(roundRobinStep % static_cast<std::uint32_t>(roundRobinLength)) + 1
            : 0;
        auto ordinal = std::size_t{ 0 };
        const auto usePreparedOrder = roundRobinMode == RoundRobinMode::ordered && targetPosition == 0;
        for (std::size_t traversalIndex = 0; traversalIndex < zoneCount; ++traversalIndex)
        {
            const auto index = usePreparedOrder
                ? roundRobinOrderedZoneIndices[traversalIndex]
                : traversalIndex;
            const auto& zone = zones[index];
            if (!isZonePlayableMatch(index, note, velocity)
                || getZoneRoundRobinGroup(zone) != roundRobinGroup)
                continue;

            if (targetPosition > 0)
            {
                if (zone.roundRobinPosition == targetPosition)
                    return static_cast<int>(index);
                continue;
            }

            if (ordinal++ == selectedOrdinal)
                return static_cast<int>(index);
        }

        return -1;
    }

    [[nodiscard]] bool roundRobinSortsBefore(const std::size_t leftIndex, const std::size_t rightIndex) const noexcept
    {
        const auto& left = zones[leftIndex];
        const auto& right = zones[rightIndex];
        const auto leftHasPosition = left.roundRobinPosition > 0;
        const auto rightHasPosition = right.roundRobinPosition > 0;

        if (leftHasPosition != rightHasPosition)
            return leftHasPosition;

        if (leftHasPosition && left.roundRobinPosition != right.roundRobinPosition)
            return left.roundRobinPosition < right.roundRobinPosition;

        return leftIndex < rightIndex;
    }

    [[nodiscard]] static std::uint32_t hashRoundRobinValue(std::uint32_t value) noexcept
    {
        value ^= value >> 16;
        value *= 0x7feb352dU;
        value ^= value >> 15;
        value *= 0x846ca68bU;
        value ^= value >> 16;
        return value;
    }

    [[nodiscard]] static std::size_t greatestCommonDivisor(std::size_t left, std::size_t right) noexcept
    {
        while (right != 0)
        {
            const auto next = left % right;
            left = right;
            right = next;
        }

        return left;
    }

    [[nodiscard]] static std::size_t cycleRandomCandidateIndexForCycle(const int roundRobinGroup,
                                                                       const std::size_t cycle,
                                                                       const std::size_t position,
                                                                       const std::size_t candidateCount) noexcept
    {
        if (candidateCount <= 1)
            return 0;

        const auto seed = static_cast<std::uint32_t>(roundRobinGroup * 2654435761u)
            ^ static_cast<std::uint32_t>(cycle * 2246822519u);

        auto offset = static_cast<std::size_t>(hashRoundRobinValue(seed) % static_cast<std::uint32_t>(candidateCount));
        if (position == 0 && cycle > 0)
        {
            const auto previous = cycleRandomCandidateIndexForCycle(
                roundRobinGroup,
                cycle - 1,
                candidateCount - 1,
                candidateCount);
            if (offset == previous)
                offset = (offset + 1) % candidateCount;
        }

        auto stride = 1u + static_cast<std::size_t>(hashRoundRobinValue(seed ^ 0x9e3779b9U)
            % static_cast<std::uint32_t>(candidateCount - 1));
        while (greatestCommonDivisor(stride, candidateCount) != 1)
            stride = (stride % (candidateCount - 1)) + 1;

        return (offset + (position * stride)) % candidateCount;
    }

    [[nodiscard]] static std::size_t cycleRandomCandidateIndex(const int roundRobinGroup,
                                                               const std::uint32_t roundRobinStep,
                                                               const std::size_t candidateCount) noexcept
    {
        if (candidateCount <= 1)
            return 0;

        const auto step = static_cast<std::size_t>(roundRobinStep);
        return cycleRandomCandidateIndexForCycle(
            roundRobinGroup,
            step / candidateCount,
            step % candidateCount,
            candidateCount);
    }
};
} // namespace audiocity::engine
