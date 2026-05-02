#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace audiocity::engine
{
// Editable sampler program data. The audio thread should consume prepared immutable snapshots built from this model.
constexpr std::uint32_t kProgramModelVersion = 1;
constexpr int kMidiNoteMin = 0;
constexpr int kMidiNoteMax = 127;
constexpr int kVelocityMin = 0;
constexpr int kVelocityMax = 127;

[[nodiscard]] constexpr int clampMidiNote(const int note) noexcept
{
    return note < kMidiNoteMin ? kMidiNoteMin : (note > kMidiNoteMax ? kMidiNoteMax : note);
}

[[nodiscard]] constexpr int clampVelocity(const int velocity) noexcept
{
    return velocity < kVelocityMin ? kVelocityMin : (velocity > kVelocityMax ? kVelocityMax : velocity);
}

struct MidiRange
{
    int low = kMidiNoteMin;
    int high = kMidiNoteMax;

    [[nodiscard]] static constexpr MidiRange full() noexcept
    {
        return {};
    }

    [[nodiscard]] static constexpr MidiRange single(const int note) noexcept
    {
        const auto clipped = clampMidiNote(note);
        return { clipped, clipped };
    }

    [[nodiscard]] static constexpr MidiRange fromUnordered(const int first, const int second) noexcept
    {
        const auto clippedFirst = clampMidiNote(first);
        const auto clippedSecond = clampMidiNote(second);
        return clippedFirst <= clippedSecond ? MidiRange{ clippedFirst, clippedSecond }
                                             : MidiRange{ clippedSecond, clippedFirst };
    }

    [[nodiscard]] constexpr bool contains(const int note) const noexcept
    {
        return note >= low && note <= high;
    }

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return low >= kMidiNoteMin && high <= kMidiNoteMax && low <= high;
    }
};

struct VelocityRange
{
    int low = kVelocityMin;
    int high = kVelocityMax;

    [[nodiscard]] static constexpr VelocityRange full() noexcept
    {
        return {};
    }

    [[nodiscard]] static constexpr VelocityRange single(const int velocity) noexcept
    {
        const auto clipped = clampVelocity(velocity);
        return { clipped, clipped };
    }

    [[nodiscard]] static constexpr VelocityRange fromUnordered(const int first, const int second) noexcept
    {
        const auto clippedFirst = clampVelocity(first);
        const auto clippedSecond = clampVelocity(second);
        return clippedFirst <= clippedSecond ? VelocityRange{ clippedFirst, clippedSecond }
                                             : VelocityRange{ clippedSecond, clippedFirst };
    }

    [[nodiscard]] constexpr bool contains(const int velocity) const noexcept
    {
        return velocity >= low && velocity <= high;
    }

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return low >= kVelocityMin && high <= kVelocityMax && low <= high;
    }
};

struct VelocityFadeRange
{
    int low = -1;
    int high = -1;

    [[nodiscard]] static constexpr VelocityFadeRange disabled() noexcept
    {
        return {};
    }

    [[nodiscard]] static constexpr VelocityFadeRange fromUnordered(const int first, const int second) noexcept
    {
        const auto clippedFirst = clampVelocity(first);
        const auto clippedSecond = clampVelocity(second);
        return clippedFirst <= clippedSecond ? VelocityFadeRange{ clippedFirst, clippedSecond }
                                             : VelocityFadeRange{ clippedSecond, clippedFirst };
    }

    [[nodiscard]] constexpr bool isEnabled() const noexcept
    {
        return low >= kVelocityMin && high <= kVelocityMax && low < high;
    }
};

struct SampleAsset
{
    std::string sourcePath;
    std::string displayName;
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

enum class ZoneLoopMode
{
    noLoop,
    sustain,
    continuous
};

enum class RoundRobinMode
{
    ordered,
    cycleRandom
};

enum class ZoneTriggerMode
{
    gate,
    oneShot,
    release
};

struct Group
{
    std::string name;
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

struct Zone
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

struct Program
{
    std::uint32_t version = kProgramModelVersion;
    std::string name = "Init";
    std::vector<SampleAsset> sampleAssets;
    std::vector<Group> groups;
    std::vector<Zone> zones;

    [[nodiscard]] bool isZoneSampleIndexValid(const Zone& zone) const noexcept
    {
        return zone.sampleAssetIndex >= 0
            && static_cast<std::size_t>(zone.sampleAssetIndex) < sampleAssets.size();
    }

    [[nodiscard]] bool isZoneGroupIndexValid(const Zone& zone) const noexcept
    {
        return zone.groupIndex < 0 || static_cast<std::size_t>(zone.groupIndex) < groups.size();
    }

    [[nodiscard]] bool hasPlayableZones() const noexcept
    {
        for (const auto& zone : zones)
        {
            if (isZoneSampleIndexValid(zone) && sampleAssets[static_cast<std::size_t>(zone.sampleAssetIndex)].hasAudio())
                return true;
        }

        return false;
    }

    [[nodiscard]] int findFirstMatchingZoneIndex(const int note, const int velocity) const noexcept
    {
        for (std::size_t index = 0; index < zones.size(); ++index)
        {
            const auto& zone = zones[index];

            if (isZoneSampleIndexValid(zone)
                && isZoneGroupIndexValid(zone)
                && sampleAssets[static_cast<std::size_t>(zone.sampleAssetIndex)].hasAudio()
                && zone.matches(note, velocity))
            {
                return static_cast<int>(index);
            }
        }

        return -1;
    }
};
} // namespace audiocity::engine
