#include "ProgramMappingModel.h"

#include <utility>

namespace audiocity::plugin
{
namespace
{
constexpr auto kMinZoneGainDb = -96.0f;
constexpr auto kMaxZoneGainDb = 24.0f;
constexpr auto kMaxMappingGroupId = 128;
constexpr int kProgramZoneMappingStateFormatVersion = 2;
const juce::Identifier kProgramZoneMappingStateType{ "sfzMappingEdits" };
const juce::Identifier kProgramZoneMappingZoneType{ "zone" };
const juce::Identifier kProgramZoneMappingStateFormatKey{ "formatVersion" };
const juce::Identifier kZoneIndex{ "zoneIndex" };
const juce::Identifier kZoneSampleAssetIndex{ "sampleAssetIndex" };
const juce::Identifier kZoneGroupIndex{ "groupIndex" };
const juce::Identifier kKeyLow{ "keyLow" };
const juce::Identifier kKeyHigh{ "keyHigh" };
const juce::Identifier kVelocityLow{ "velocityLow" };
const juce::Identifier kVelocityHigh{ "velocityHigh" };
const juce::Identifier kZoneVelocityFadeInLow{ "velocityFadeInLow" };
const juce::Identifier kZoneVelocityFadeInHigh{ "velocityFadeInHigh" };
const juce::Identifier kZoneVelocityFadeOutLow{ "velocityFadeOutLow" };
const juce::Identifier kZoneVelocityFadeOutHigh{ "velocityFadeOutHigh" };
const juce::Identifier kZoneRootMidiNote{ "rootMidiNote" };
const juce::Identifier kZoneSampleStart{ "sampleStart" };
const juce::Identifier kZoneSampleEnd{ "sampleEnd" };
const juce::Identifier kZoneSampleEndExclusive{ "sampleEndExclusive" };
const juce::Identifier kZoneLoopStart{ "zoneLoopStart" };
const juce::Identifier kZoneLoopEnd{ "zoneLoopEnd" };
const juce::Identifier kZoneLoopEndExclusive{ "zoneLoopEndExclusive" };
const juce::Identifier kZoneGainDb{ "gainDb" };
const juce::Identifier kZoneRawGainDb{ "zoneGainDbRaw" };
const juce::Identifier kZonePan{ "zonePan" };
const juce::Identifier kZoneRawPan{ "zonePanRaw" };
const juce::Identifier kZoneTuneCents{ "tuneCents" };
const juce::Identifier kZoneRoundRobinGroup{ "roundRobinGroup" };
const juce::Identifier kZoneRawRoundRobinGroup{ "zoneRoundRobinGroupRaw" };
const juce::Identifier kZoneRoundRobinPosition{ "roundRobinPosition" };
const juce::Identifier kZoneRoundRobinLength{ "roundRobinLength" };
const juce::Identifier kZoneRoundRobinMode{ "roundRobinMode" };
const juce::Identifier kZoneChokeGroup{ "chokeGroup" };
const juce::Identifier kZoneRawChokeGroup{ "zoneChokeGroupRaw" };
const juce::Identifier kZoneTriggerMode{ "triggerMode" };
const juce::Identifier kZoneRawTriggerMode{ "zoneTriggerModeRaw" };
const juce::Identifier kZoneLoopMode{ "loopMode" };

juce::String formatRange(const int low, const int high)
{
    if (low == high)
        return juce::String(low);

    return juce::String(low) + "-" + juce::String(high);
}

juce::String formatLoopMode(const audiocity::engine::ZoneLoopMode mode)
{
    switch (mode)
    {
        case audiocity::engine::ZoneLoopMode::sustain: return "sustain";
        case audiocity::engine::ZoneLoopMode::continuous: return "continuous";
        case audiocity::engine::ZoneLoopMode::noLoop:
        default: return "off";
    }
}

juce::String formatTriggerMode(const audiocity::engine::ZoneTriggerMode mode)
{
    switch (mode)
    {
        case audiocity::engine::ZoneTriggerMode::oneShot:
            return "one-shot";
        case audiocity::engine::ZoneTriggerMode::release:
            return "release";
        case audiocity::engine::ZoneTriggerMode::gate:
        default:
            return "gate";
    }
}

juce::String formatGainDb(const float gainDb)
{
    return juce::String(gainDb, 1) + " dB";
}

juce::String formatVelocityFade(const audiocity::engine::VelocityFadeRange& range)
{
    return range.isEnabled() ? formatRange(range.low, range.high) : "off";
}

juce::String formatPan(const float pan)
{
    return juce::String(juce::jlimit(-100.0f, 100.0f, pan * 100.0f), 0);
}

juce::String formatRoundRobinMode(const audiocity::engine::RoundRobinMode mode)
{
    return mode == audiocity::engine::RoundRobinMode::cycleRandom ? "cycle-random" : "ordered";
}

bool isValidGroupIndex(const audiocity::engine::Program& program, const int groupIndex)
{
    return groupIndex >= 0 && static_cast<std::size_t>(groupIndex) < program.groups.size();
}

bool isValidSampleAssetIndex(const audiocity::engine::Program& program, const int sampleAssetIndex)
{
    return sampleAssetIndex >= 0 && static_cast<std::size_t>(sampleAssetIndex) < program.sampleAssets.size();
}

int findDefaultSampleAssetIndex(const audiocity::engine::Program& program, const int preferredSampleAssetIndex)
{
    if (isValidSampleAssetIndex(program, preferredSampleAssetIndex))
        return preferredSampleAssetIndex;

    for (std::size_t index = 0; index < program.sampleAssets.size(); ++index)
    {
        if (program.sampleAssets[index].hasAudio())
            return static_cast<int>(index);
    }

    return program.sampleAssets.empty() ? -1 : 0;
}

int clampOverviewNote(const int value)
{
    return juce::jlimit(audiocity::engine::kMidiNoteMin, audiocity::engine::kMidiNoteMax, value);
}

int clampOverviewVelocity(const int value)
{
    return juce::jlimit(audiocity::engine::kVelocityMin, audiocity::engine::kVelocityMax, value);
}

juce::String getSampleName(const audiocity::engine::Program& program, const int sampleAssetIndex)
{
    if (sampleAssetIndex < 0 || static_cast<std::size_t>(sampleAssetIndex) >= program.sampleAssets.size())
        return "sample " + juce::String(sampleAssetIndex);

    const auto& asset = program.sampleAssets[static_cast<std::size_t>(sampleAssetIndex)];
    if (!asset.displayName.empty())
        return juce::String::fromUTF8(asset.displayName.c_str());

    if (!asset.sourcePath.empty())
        return juce::File(juce::String::fromUTF8(asset.sourcePath.c_str())).getFileName();

    return "sample " + juce::String(sampleAssetIndex);
}

int resolveSampleLength(const audiocity::engine::Program& program, const audiocity::engine::Zone& zone)
{
    if (isValidSampleAssetIndex(program, zone.sampleAssetIndex))
        return juce::jmax(1, program.sampleAssets[static_cast<std::size_t>(zone.sampleAssetIndex)].lengthSamples);

    if (zone.sampleEndExclusive > zone.sampleStart)
        return zone.sampleEndExclusive;

    return juce::jmax(1, zone.sampleStart + 1);
}

int resolveSampleEndInclusive(const audiocity::engine::Zone& zone, const int sampleLength)
{
    const auto maxSampleIndex = juce::jmax(0, sampleLength - 1);
    if (zone.sampleEndExclusive > zone.sampleStart)
        return juce::jlimit(zone.sampleStart, maxSampleIndex, zone.sampleEndExclusive - 1);

    return maxSampleIndex;
}

std::pair<int, int> resolveLoopRange(const audiocity::engine::Zone& zone,
                                     const int sampleStart,
                                     const int sampleEnd)
{
    if (zone.loopStart < 0 || zone.loopEndExclusive <= zone.loopStart)
        return { -1, -1 };

    const auto loopStart = juce::jlimit(sampleStart, sampleEnd, zone.loopStart);
    const auto loopEnd = juce::jlimit(loopStart, sampleEnd, zone.loopEndExclusive - 1);
    return { loopStart, loopEnd };
}

juce::String formatOptionalRange(const int low, const int high)
{
    if (low < 0 || high < low)
        return "-";

    return formatRange(low, high);
}

audiocity::engine::VelocityFadeRange restoreVelocityFadeRange(const juce::ValueTree& child,
                                                              const juce::Identifier& lowKey,
                                                              const juce::Identifier& highKey)
{
    const auto low = static_cast<int>(child.getProperty(lowKey, -1));
    const auto high = static_cast<int>(child.getProperty(highKey, -1));
    if (low < 0 || high < 0)
        return audiocity::engine::VelocityFadeRange::disabled();

    return audiocity::engine::VelocityFadeRange::fromUnordered(low, high);
}

audiocity::engine::ZoneTriggerMode resolveTriggerMode(const audiocity::engine::Program& program,
                                                       const audiocity::engine::Zone& zone)
{
    if (zone.triggerMode == audiocity::engine::ZoneTriggerMode::release)
        return audiocity::engine::ZoneTriggerMode::release;

    if (zone.triggerMode == audiocity::engine::ZoneTriggerMode::oneShot)
        return audiocity::engine::ZoneTriggerMode::oneShot;

    if (isValidGroupIndex(program, zone.groupIndex))
        return program.groups[static_cast<std::size_t>(zone.groupIndex)].triggerMode;

    return audiocity::engine::ZoneTriggerMode::gate;
}

int resolveRoundRobinGroup(const audiocity::engine::Program& program, const audiocity::engine::Zone& zone)
{
    if (zone.roundRobinGroup > 0)
        return zone.roundRobinGroup;

    if (isValidGroupIndex(program, zone.groupIndex))
        return program.groups[static_cast<std::size_t>(zone.groupIndex)].roundRobinGroup;

    return 0;
}

audiocity::engine::RoundRobinMode resolveRoundRobinMode(const audiocity::engine::Program& program,
                                                        const audiocity::engine::Zone& zone)
{
    if (zone.roundRobinMode == audiocity::engine::RoundRobinMode::cycleRandom)
        return audiocity::engine::RoundRobinMode::cycleRandom;

    if (isValidGroupIndex(program, zone.groupIndex))
        return program.groups[static_cast<std::size_t>(zone.groupIndex)].roundRobinMode;

    return audiocity::engine::RoundRobinMode::ordered;
}

int resolveChokeGroup(const audiocity::engine::Program& program, const audiocity::engine::Zone& zone)
{
    if (zone.chokeGroup > 0)
        return zone.chokeGroup;

    if (isValidGroupIndex(program, zone.groupIndex))
        return program.groups[static_cast<std::size_t>(zone.groupIndex)].chokeGroup;

    return 0;
}

float resolveGainDb(const audiocity::engine::Program& program, const audiocity::engine::Zone& zone)
{
    auto gainDb = zone.gainDb;
    if (isValidGroupIndex(program, zone.groupIndex))
        gainDb += program.groups[static_cast<std::size_t>(zone.groupIndex)].gainDb;

    return gainDb;
}

float resolvePan(const audiocity::engine::Program& program, const audiocity::engine::Zone& zone)
{
    auto pan = zone.pan;
    if (isValidGroupIndex(program, zone.groupIndex))
        pan += program.groups[static_cast<std::size_t>(zone.groupIndex)].pan;

    return juce::jlimit(-1.0f, 1.0f, pan);
}

void recomputeGroupRangeFromZones(audiocity::engine::Program& program, const int groupIndex)
{
    if (!isValidGroupIndex(program, groupIndex))
        return;

    auto hasZones = false;
    auto keyLow = audiocity::engine::kMidiNoteMax;
    auto keyHigh = audiocity::engine::kMidiNoteMin;
    auto velocityLow = audiocity::engine::kVelocityMax;
    auto velocityHigh = audiocity::engine::kVelocityMin;

    for (const auto& zone : program.zones)
    {
        if (zone.groupIndex != groupIndex)
            continue;

        hasZones = true;
        keyLow = juce::jmin(keyLow, zone.keyRange.low);
        keyHigh = juce::jmax(keyHigh, zone.keyRange.high);
        velocityLow = juce::jmin(velocityLow, zone.velocityRange.low);
        velocityHigh = juce::jmax(velocityHigh, zone.velocityRange.high);
    }

    if (!hasZones)
        return;

    auto& group = program.groups[static_cast<std::size_t>(groupIndex)];
    group.keyRange = audiocity::engine::MidiRange::fromUnordered(keyLow, keyHigh);
    group.velocityRange = audiocity::engine::VelocityRange::fromUnordered(velocityLow, velocityHigh);
}
} // namespace

std::vector<ProgramZoneListRow> buildProgramZoneListRows(const audiocity::engine::Program& program)
{
    std::vector<ProgramZoneListRow> rows;
    rows.reserve(program.zones.size());

    for (std::size_t zoneIndex = 0; zoneIndex < program.zones.size(); ++zoneIndex)
    {
        const auto& zone = program.zones[zoneIndex];
        const auto roundRobinGroup = resolveRoundRobinGroup(program, zone);
        const auto chokeGroup = resolveChokeGroup(program, zone);

        ProgramZoneListRow row;
        row.zoneIndex = static_cast<int>(zoneIndex);
        row.sampleAssetIndex = zone.sampleAssetIndex;
        row.sampleLength = resolveSampleLength(program, zone);
        row.keyLow = zone.keyRange.low;
        row.keyHigh = zone.keyRange.high;
        row.velocityLow = zone.velocityRange.low;
        row.velocityHigh = zone.velocityRange.high;
        row.velocityFadeInLow = zone.velocityFadeIn.isEnabled() ? zone.velocityFadeIn.low : -1;
        row.velocityFadeInHigh = zone.velocityFadeIn.isEnabled() ? zone.velocityFadeIn.high : -1;
        row.velocityFadeOutLow = zone.velocityFadeOut.isEnabled() ? zone.velocityFadeOut.low : -1;
        row.velocityFadeOutHigh = zone.velocityFadeOut.isEnabled() ? zone.velocityFadeOut.high : -1;
        row.rootMidiNote = zone.rootMidiNote;
        row.sampleStart = juce::jlimit(0, juce::jmax(0, row.sampleLength - 1), zone.sampleStart);
        row.sampleEnd = resolveSampleEndInclusive(zone, row.sampleLength);
        const auto loopRange = resolveLoopRange(zone, row.sampleStart, row.sampleEnd);
        row.loopStart = loopRange.first;
        row.loopEnd = loopRange.second;
        row.roundRobinGroup = roundRobinGroup;
        row.roundRobinPosition = zone.roundRobinPosition;
        row.chokeGroupId = chokeGroup;
        row.gainDbValue = resolveGainDb(program, zone);
        row.panValue = resolvePan(program, zone);
        row.roundRobinModeValue = resolveRoundRobinMode(program, zone);
        row.triggerModeValue = resolveTriggerMode(program, zone);
        row.loopModeValue = zone.loopMode;
        row.sampleName = getSampleName(program, zone.sampleAssetIndex);
        row.sampleWindow = formatRange(row.sampleStart, row.sampleEnd);
        row.loopPoints = formatOptionalRange(row.loopStart, row.loopEnd);
        row.keyRange = formatRange(zone.keyRange.low, zone.keyRange.high);
        row.velocityRange = formatRange(zone.velocityRange.low, zone.velocityRange.high);
        row.velocityFadeIn = formatVelocityFade(zone.velocityFadeIn);
        row.velocityFadeOut = formatVelocityFade(zone.velocityFadeOut);
        row.rootNote = juce::String(zone.rootMidiNote);
        row.triggerMode = formatTriggerMode(resolveTriggerMode(program, zone));
        row.loopMode = formatLoopMode(zone.loopMode);
        row.roundRobin = roundRobinGroup > 0
            ? (juce::String(roundRobinGroup) + ":" + juce::String(zone.roundRobinPosition))
            : "-";
        row.roundRobinMode = formatRoundRobinMode(row.roundRobinModeValue);
        row.chokeGroup = chokeGroup > 0 ? juce::String(chokeGroup) : "-";
        row.gainDb = formatGainDb(row.gainDbValue);
        row.pan = formatPan(row.panValue);
        row.summaryText = "#" + juce::String(row.zoneIndex + 1)
            + "  " + row.sampleName
            + "  key " + row.keyRange
            + "  vel " + row.velocityRange;
        row.detailText = "Sample: " + row.sampleName
            + "\nWindow: " + row.sampleWindow
            + "\nLoop Points: " + row.loopPoints
            + "\nKey: " + row.keyRange
            + "\nVelocity: " + row.velocityRange
            + "\nVel Fade In: " + row.velocityFadeIn
            + "\nVel Fade Out: " + row.velocityFadeOut
            + "\nRoot: " + row.rootNote
            + "\nTrigger: " + row.triggerMode
            + "\nLoop: " + row.loopMode
            + "\nRound robin: " + row.roundRobin
            + "\nRR mode: " + row.roundRobinMode
            + "\nChoke: " + row.chokeGroup
            + "\nGain: " + row.gainDb
            + "\nPan: " + row.pan;
        rows.push_back(std::move(row));
    }

    return rows;
}

bool applyProgramZoneEdit(audiocity::engine::Program& program, const ProgramZoneEdit& edit)
{
    if (edit.zoneIndex < 0 || static_cast<std::size_t>(edit.zoneIndex) >= program.zones.size())
        return false;

    auto& zone = program.zones[static_cast<std::size_t>(edit.zoneIndex)];
    zone.keyRange = audiocity::engine::MidiRange::fromUnordered(edit.keyLow, edit.keyHigh);
    zone.velocityRange = audiocity::engine::VelocityRange::fromUnordered(edit.velocityLow, edit.velocityHigh);

    if (edit.hasVelocityFadeIn)
    {
        zone.velocityFadeIn = (edit.velocityFadeInLow < audiocity::engine::kVelocityMin
            || edit.velocityFadeInHigh < audiocity::engine::kVelocityMin)
            ? audiocity::engine::VelocityFadeRange::disabled()
            : audiocity::engine::VelocityFadeRange::fromUnordered(edit.velocityFadeInLow, edit.velocityFadeInHigh);
    }

    if (edit.hasVelocityFadeOut)
    {
        zone.velocityFadeOut = (edit.velocityFadeOutLow < audiocity::engine::kVelocityMin
            || edit.velocityFadeOutHigh < audiocity::engine::kVelocityMin)
            ? audiocity::engine::VelocityFadeRange::disabled()
            : audiocity::engine::VelocityFadeRange::fromUnordered(edit.velocityFadeOutLow, edit.velocityFadeOutHigh);
    }

    zone.rootMidiNote = audiocity::engine::clampMidiNote(edit.rootMidiNote);

    const auto sampleLength = resolveSampleLength(program, zone);
    const auto maxSampleIndex = juce::jmax(0, sampleLength - 1);

    if (edit.hasSampleStart || edit.hasSampleEnd)
    {
        auto nextSampleStart = juce::jlimit(0,
                                            maxSampleIndex,
                                            edit.hasSampleStart ? edit.sampleStart : zone.sampleStart);
        auto nextSampleEnd = juce::jlimit(nextSampleStart,
                                          maxSampleIndex,
                                          edit.hasSampleEnd ? edit.sampleEnd : resolveSampleEndInclusive(zone, sampleLength));
        zone.sampleStart = nextSampleStart;
        zone.sampleEndExclusive = nextSampleEnd + 1;
    }

    if (edit.hasLoopStart || edit.hasLoopEnd)
    {
        const auto sampleStart = zone.sampleStart;
        const auto sampleEnd = resolveSampleEndInclusive(zone, sampleLength);
        const auto currentLoopRange = resolveLoopRange(zone, sampleStart, sampleEnd);
        auto nextLoopStart = juce::jlimit(sampleStart,
                                          sampleEnd,
                                          edit.hasLoopStart
                                              ? edit.loopStart
                                              : (currentLoopRange.first >= 0 ? currentLoopRange.first : sampleStart));
        auto nextLoopEnd = juce::jlimit(nextLoopStart,
                                        sampleEnd,
                                        edit.hasLoopEnd
                                            ? edit.loopEnd
                                            : (currentLoopRange.second >= 0 ? currentLoopRange.second : sampleEnd));
        zone.loopStart = nextLoopStart;
        zone.loopEndExclusive = nextLoopEnd + 1;
    }

    if (edit.hasGainDb)
    {
        const auto groupGainDb = isValidGroupIndex(program, zone.groupIndex)
            ? program.groups[static_cast<std::size_t>(zone.groupIndex)].gainDb
            : 0.0f;
        zone.gainDb = juce::jlimit(kMinZoneGainDb, kMaxZoneGainDb, edit.gainDb - groupGainDb);
    }

    if (edit.hasPan)
    {
        const auto groupPan = isValidGroupIndex(program, zone.groupIndex)
            ? program.groups[static_cast<std::size_t>(zone.groupIndex)].pan
            : 0.0f;
        zone.pan = juce::jlimit(-1.0f, 1.0f, edit.pan - groupPan);
    }

    if (edit.hasRoundRobinGroup)
        zone.roundRobinGroup = juce::jlimit(0, kMaxMappingGroupId, edit.roundRobinGroup);

    if (edit.hasRoundRobinPosition)
        zone.roundRobinPosition = juce::jlimit(0, kMaxMappingGroupId, edit.roundRobinPosition);

    if (edit.hasRoundRobinMode)
        zone.roundRobinMode = edit.roundRobinMode;

    if (edit.hasChokeGroupId)
        zone.chokeGroup = juce::jlimit(0, kMaxMappingGroupId, edit.chokeGroupId);

    if (edit.hasTriggerMode)
        zone.triggerMode = edit.triggerMode;

    if (edit.hasLoopMode)
        zone.loopMode = edit.loopMode;

    recomputeGroupRangeFromZones(program, zone.groupIndex);
    return true;
}

bool applyProgramZoneEditsAtomic(audiocity::engine::Program& program,
                                 const std::vector<ProgramZoneEdit>& edits)
{
    if (edits.empty())
        return false;

    auto updatedProgram = program;
    for (const auto& edit : edits)
    {
        if (!applyProgramZoneEdit(updatedProgram, edit))
            return false;
    }

    program = std::move(updatedProgram);
    return true;
}

int createProgramZoneForSampleAsset(audiocity::engine::Program& program,
                                    const int sampleAssetIndex,
                                    const int seedZoneIndex)
{
    const auto hasSeedZone = seedZoneIndex >= 0 && static_cast<std::size_t>(seedZoneIndex) < program.zones.size();
    const auto* seedZone = hasSeedZone ? &program.zones[static_cast<std::size_t>(seedZoneIndex)] : nullptr;
    const auto resolvedSampleAssetIndex = findDefaultSampleAssetIndex(
        program,
        sampleAssetIndex >= 0 ? sampleAssetIndex : (seedZone != nullptr ? seedZone->sampleAssetIndex : -1));
    if (!isValidSampleAssetIndex(program, resolvedSampleAssetIndex))
        return -1;

    const auto& sampleAsset = program.sampleAssets[static_cast<std::size_t>(resolvedSampleAssetIndex)];

    audiocity::engine::Zone newZone;
    newZone.sampleAssetIndex = resolvedSampleAssetIndex;
    newZone.groupIndex = seedZone != nullptr && isValidGroupIndex(program, seedZone->groupIndex)
        ? seedZone->groupIndex
        : (program.groups.size() == 1 ? 0 : -1);
    newZone.rootMidiNote = audiocity::engine::clampMidiNote(seedZone != nullptr ? seedZone->rootMidiNote : sampleAsset.rootMidiNote);
    newZone.keyRange = audiocity::engine::MidiRange::single(newZone.rootMidiNote);
    newZone.velocityRange = audiocity::engine::VelocityRange::full();
    newZone.velocityFadeIn = audiocity::engine::VelocityFadeRange::disabled();
    newZone.velocityFadeOut = audiocity::engine::VelocityFadeRange::disabled();
    newZone.sampleStart = 0;
    newZone.sampleEndExclusive = sampleAsset.lengthSamples > 0 ? sampleAsset.lengthSamples : -1;
    newZone.loopStart = -1;
    newZone.loopEndExclusive = -1;
    newZone.gainDb = 0.0f;
    newZone.pan = 0.0f;
    newZone.tuneCents = seedZone != nullptr ? seedZone->tuneCents : 0.0f;
    newZone.roundRobinGroup = 0;
    newZone.roundRobinPosition = 0;
    newZone.roundRobinLength = 0;
    newZone.roundRobinMode = audiocity::engine::RoundRobinMode::ordered;
    newZone.triggerMode = audiocity::engine::ZoneTriggerMode::gate;
    newZone.chokeGroup = 0;
    newZone.loopMode = audiocity::engine::ZoneLoopMode::noLoop;

    program.zones.push_back(newZone);
    recomputeGroupRangeFromZones(program, newZone.groupIndex);
    return static_cast<int>(program.zones.size() - 1);
}

int createProgramZone(audiocity::engine::Program& program, const int seedZoneIndex)
{
    return createProgramZoneForSampleAsset(program, -1, seedZoneIndex);
}

int duplicateProgramZone(audiocity::engine::Program& program, const int zoneIndex)
{
    if (zoneIndex < 0 || static_cast<std::size_t>(zoneIndex) >= program.zones.size())
        return -1;

    const auto duplicatedZone = program.zones[static_cast<std::size_t>(zoneIndex)];
    program.zones.push_back(duplicatedZone);
    recomputeGroupRangeFromZones(program, duplicatedZone.groupIndex);
    return static_cast<int>(program.zones.size() - 1);
}

bool deleteProgramZone(audiocity::engine::Program& program, const int zoneIndex)
{
    if (zoneIndex < 0 || static_cast<std::size_t>(zoneIndex) >= program.zones.size())
        return false;

    const auto groupIndex = program.zones[static_cast<std::size_t>(zoneIndex)].groupIndex;
    program.zones.erase(program.zones.begin() + zoneIndex);
    recomputeGroupRangeFromZones(program, groupIndex);
    return true;
}

bool deleteProgramZonesAtomic(audiocity::engine::Program& program,
                              const std::vector<int>& zoneIndices)
{
    if (zoneIndices.empty())
        return false;

    auto sortedZoneIndices = zoneIndices;
    std::sort(sortedZoneIndices.begin(), sortedZoneIndices.end(), std::greater<int>());
    sortedZoneIndices.erase(std::unique(sortedZoneIndices.begin(), sortedZoneIndices.end()), sortedZoneIndices.end());

    auto updatedProgram = program;
    for (const auto zoneIndex : sortedZoneIndices)
    {
        if (!deleteProgramZone(updatedProgram, zoneIndex))
            return false;
    }

    program = std::move(updatedProgram);
    return true;
}

int splitProgramZoneByKey(audiocity::engine::Program& program, const int zoneIndex)
{
    if (zoneIndex < 0 || static_cast<std::size_t>(zoneIndex) >= program.zones.size())
        return -1;

    auto& originalZone = program.zones[static_cast<std::size_t>(zoneIndex)];
    if (originalZone.keyRange.high <= originalZone.keyRange.low)
        return -1;

    const auto groupIndex = originalZone.groupIndex;
    const auto splitPoint = originalZone.keyRange.low
        + ((originalZone.keyRange.high - originalZone.keyRange.low) / 2);
    auto splitZone = originalZone;
    originalZone.keyRange = audiocity::engine::MidiRange::fromUnordered(originalZone.keyRange.low, splitPoint);
    splitZone.keyRange = audiocity::engine::MidiRange::fromUnordered(splitPoint + 1, splitZone.keyRange.high);
    program.zones.push_back(splitZone);
    recomputeGroupRangeFromZones(program, groupIndex);
    return static_cast<int>(program.zones.size() - 1);
}

bool remapProgramZonesChromatically(audiocity::engine::Program& program,
                                    const std::vector<int>& zoneIndices,
                                    const int baseMidiNote)
{
    if (zoneIndices.empty())
        return false;

    std::vector<int> uniqueZoneIndices;
    uniqueZoneIndices.reserve(zoneIndices.size());
    for (const auto zoneIndex : zoneIndices)
    {
        if (zoneIndex < 0 || static_cast<std::size_t>(zoneIndex) >= program.zones.size())
            return false;

        auto alreadyQueued = false;
        for (const auto queuedZoneIndex : uniqueZoneIndices)
        {
            if (queuedZoneIndex == zoneIndex)
            {
                alreadyQueued = true;
                break;
            }
        }

        if (!alreadyQueued)
            uniqueZoneIndices.push_back(zoneIndex);
    }

    if (uniqueZoneIndices.empty())
        return false;

    const auto firstMappedNote = audiocity::engine::clampMidiNote(baseMidiNote);
    const auto availableNotes = audiocity::engine::kMidiNoteMax - firstMappedNote + 1;
    if (static_cast<int>(uniqueZoneIndices.size()) > availableNotes)
        return false;

    auto updatedProgram = program;
    for (std::size_t order = 0; order < uniqueZoneIndices.size(); ++order)
    {
        const auto mappedNote = firstMappedNote + static_cast<int>(order);
        auto& zone = updatedProgram.zones[static_cast<std::size_t>(uniqueZoneIndices[order])];
        zone.keyRange = audiocity::engine::MidiRange::single(mappedNote);
        zone.rootMidiNote = mappedNote;
        recomputeGroupRangeFromZones(updatedProgram, zone.groupIndex);
    }

    program = std::move(updatedProgram);
    return true;
}

juce::Identifier programZoneMappingStateType()
{
    return kProgramZoneMappingStateType;
}

juce::ValueTree createProgramZoneMappingState(const audiocity::engine::Program& program)
{
    auto mappingEdits = juce::ValueTree(kProgramZoneMappingStateType);
    mappingEdits.setProperty(kProgramZoneMappingStateFormatKey, kProgramZoneMappingStateFormatVersion, nullptr);
    const auto rows = buildProgramZoneListRows(program);

    for (std::size_t zoneIndex = 0; zoneIndex < program.zones.size(); ++zoneIndex)
    {
        const auto& zone = program.zones[zoneIndex];
        const auto& row = rows[zoneIndex];
        auto child = juce::ValueTree(kProgramZoneMappingZoneType);
        child.setProperty(kZoneIndex, static_cast<int>(zoneIndex), nullptr);
        child.setProperty(kZoneSampleAssetIndex, zone.sampleAssetIndex, nullptr);
        child.setProperty(kZoneGroupIndex, zone.groupIndex, nullptr);
        child.setProperty(kKeyLow, zone.keyRange.low, nullptr);
        child.setProperty(kKeyHigh, zone.keyRange.high, nullptr);
        child.setProperty(kVelocityLow, zone.velocityRange.low, nullptr);
        child.setProperty(kVelocityHigh, zone.velocityRange.high, nullptr);
        child.setProperty(kZoneVelocityFadeInLow, zone.velocityFadeIn.low, nullptr);
        child.setProperty(kZoneVelocityFadeInHigh, zone.velocityFadeIn.high, nullptr);
        child.setProperty(kZoneVelocityFadeOutLow, zone.velocityFadeOut.low, nullptr);
        child.setProperty(kZoneVelocityFadeOutHigh, zone.velocityFadeOut.high, nullptr);
        child.setProperty(kZoneRootMidiNote, zone.rootMidiNote, nullptr);
        child.setProperty(kZoneSampleStart, row.sampleStart, nullptr);
        child.setProperty(kZoneSampleEnd, row.sampleEnd, nullptr);
        child.setProperty(kZoneSampleEndExclusive, zone.sampleEndExclusive, nullptr);
        if (row.loopStart >= 0 && row.loopEnd >= row.loopStart)
        {
            child.setProperty(kZoneLoopStart, row.loopStart, nullptr);
            child.setProperty(kZoneLoopEnd, row.loopEnd, nullptr);
        }
        child.setProperty(kZoneLoopEndExclusive, zone.loopEndExclusive, nullptr);
        child.setProperty(kZoneGainDb, row.gainDbValue, nullptr);
        child.setProperty(kZoneRawGainDb, zone.gainDb, nullptr);
        child.setProperty(kZonePan, row.panValue, nullptr);
        child.setProperty(kZoneRawPan, zone.pan, nullptr);
        child.setProperty(kZoneTuneCents, zone.tuneCents, nullptr);
        child.setProperty(kZoneRoundRobinGroup, row.roundRobinGroup, nullptr);
        child.setProperty(kZoneRawRoundRobinGroup, zone.roundRobinGroup, nullptr);
        child.setProperty(kZoneRoundRobinPosition, row.roundRobinPosition, nullptr);
        child.setProperty(kZoneRoundRobinLength, zone.roundRobinLength, nullptr);
        child.setProperty(kZoneRoundRobinMode, static_cast<int>(zone.roundRobinMode), nullptr);
        child.setProperty(kZoneChokeGroup, row.chokeGroupId, nullptr);
        child.setProperty(kZoneRawChokeGroup, zone.chokeGroup, nullptr);
        child.setProperty(kZoneTriggerMode, static_cast<int>(row.triggerModeValue), nullptr);
        child.setProperty(kZoneRawTriggerMode, static_cast<int>(zone.triggerMode), nullptr);
        child.setProperty(kZoneLoopMode, static_cast<int>(row.loopModeValue), nullptr);
        mappingEdits.appendChild(child, nullptr);
    }

    return mappingEdits;
}

std::vector<ProgramZoneEdit> parseProgramZoneMappingState(const juce::ValueTree& state)
{
    std::vector<ProgramZoneEdit> edits;
    if (!state.isValid() || !state.hasType(kProgramZoneMappingStateType))
        return edits;

    edits.reserve(static_cast<std::size_t>(state.getNumChildren()));
    for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
    {
        const auto child = state.getChild(childIndex);
        if (!child.hasType(kProgramZoneMappingZoneType))
            continue;

        ProgramZoneEdit edit;
        edit.zoneIndex = static_cast<int>(child.getProperty(kZoneIndex, -1));
        edit.keyLow = static_cast<int>(child.getProperty(kKeyLow, audiocity::engine::kMidiNoteMin));
        edit.keyHigh = static_cast<int>(child.getProperty(kKeyHigh, audiocity::engine::kMidiNoteMax));
        edit.velocityLow = static_cast<int>(child.getProperty(kVelocityLow, audiocity::engine::kVelocityMin));
        edit.velocityHigh = static_cast<int>(child.getProperty(kVelocityHigh, audiocity::engine::kVelocityMax));
        if (child.hasProperty(kZoneVelocityFadeInLow) && child.hasProperty(kZoneVelocityFadeInHigh))
        {
            edit.velocityFadeInLow = static_cast<int>(child.getProperty(kZoneVelocityFadeInLow, -1));
            edit.velocityFadeInHigh = static_cast<int>(child.getProperty(kZoneVelocityFadeInHigh, -1));
            edit.hasVelocityFadeIn = true;
        }
        if (child.hasProperty(kZoneVelocityFadeOutLow) && child.hasProperty(kZoneVelocityFadeOutHigh))
        {
            edit.velocityFadeOutLow = static_cast<int>(child.getProperty(kZoneVelocityFadeOutLow, -1));
            edit.velocityFadeOutHigh = static_cast<int>(child.getProperty(kZoneVelocityFadeOutHigh, -1));
            edit.hasVelocityFadeOut = true;
        }
        edit.rootMidiNote = static_cast<int>(child.getProperty(kZoneRootMidiNote, 60));
        if (child.hasProperty(kZoneSampleStart))
        {
            edit.sampleStart = static_cast<int>(child.getProperty(kZoneSampleStart, 0));
            edit.hasSampleStart = true;
        }
        if (child.hasProperty(kZoneSampleEnd))
        {
            edit.sampleEnd = static_cast<int>(child.getProperty(kZoneSampleEnd, 0));
            edit.hasSampleEnd = true;
        }
        if (child.hasProperty(kZoneLoopStart))
        {
            edit.loopStart = static_cast<int>(child.getProperty(kZoneLoopStart, 0));
            edit.hasLoopStart = true;
        }
        if (child.hasProperty(kZoneLoopEnd))
        {
            edit.loopEnd = static_cast<int>(child.getProperty(kZoneLoopEnd, 0));
            edit.hasLoopEnd = true;
        }
        if (child.hasProperty(kZoneGainDb))
        {
            edit.gainDb = static_cast<float>(child.getProperty(kZoneGainDb, 0.0));
            edit.hasGainDb = true;
        }
        if (child.hasProperty(kZonePan))
        {
            edit.pan = static_cast<float>(child.getProperty(kZonePan, 0.0));
            edit.hasPan = true;
        }
        if (child.hasProperty(kZoneRoundRobinGroup))
        {
            edit.roundRobinGroup = static_cast<int>(child.getProperty(kZoneRoundRobinGroup, 0));
            edit.hasRoundRobinGroup = true;
        }
        if (child.hasProperty(kZoneRoundRobinPosition))
        {
            edit.roundRobinPosition = static_cast<int>(child.getProperty(kZoneRoundRobinPosition, 0));
            edit.hasRoundRobinPosition = true;
        }
        if (child.hasProperty(kZoneRoundRobinMode))
        {
            edit.roundRobinMode = static_cast<int>(child.getProperty(kZoneRoundRobinMode, 0)) == 1
                ? audiocity::engine::RoundRobinMode::cycleRandom
                : audiocity::engine::RoundRobinMode::ordered;
            edit.hasRoundRobinMode = true;
        }
        if (child.hasProperty(kZoneChokeGroup))
        {
            edit.chokeGroupId = static_cast<int>(child.getProperty(kZoneChokeGroup, 0));
            edit.hasChokeGroupId = true;
        }
        if (child.hasProperty(kZoneTriggerMode))
        {
            const auto storedTriggerMode = static_cast<int>(child.getProperty(kZoneTriggerMode, 0));
            edit.triggerMode = storedTriggerMode == 2
                ? audiocity::engine::ZoneTriggerMode::release
                : (storedTriggerMode == 1
                    ? audiocity::engine::ZoneTriggerMode::oneShot
                    : audiocity::engine::ZoneTriggerMode::gate);
            edit.hasTriggerMode = true;
        }
        if (child.hasProperty(kZoneLoopMode))
        {
            const auto storedLoopMode = static_cast<int>(child.getProperty(kZoneLoopMode, 0));
            edit.loopMode = storedLoopMode == 1
                ? audiocity::engine::ZoneLoopMode::sustain
                : (storedLoopMode == 2 ? audiocity::engine::ZoneLoopMode::continuous
                                       : audiocity::engine::ZoneLoopMode::noLoop);
            edit.hasLoopMode = true;
        }

        edits.push_back(edit);
    }

    return edits;
}

bool restoreProgramZoneStructureFromState(audiocity::engine::Program& program, const juce::ValueTree& state)
{
    if (!state.isValid() || !state.hasType(kProgramZoneMappingStateType))
        return false;

    const auto formatVersion = static_cast<int>(state.getProperty(kProgramZoneMappingStateFormatKey, 1));
    if (formatVersion < kProgramZoneMappingStateFormatVersion)
        return false;

    std::vector<audiocity::engine::Zone> restoredZones;
    restoredZones.reserve(static_cast<std::size_t>(state.getNumChildren()));

    for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
    {
        const auto child = state.getChild(childIndex);
        if (!child.hasType(kProgramZoneMappingZoneType))
            continue;

        audiocity::engine::Zone zone;
        if (childIndex < static_cast<int>(program.zones.size()))
            zone = program.zones[static_cast<std::size_t>(childIndex)];

        zone.sampleAssetIndex = static_cast<int>(child.getProperty(kZoneSampleAssetIndex, -1));
        zone.groupIndex = static_cast<int>(child.getProperty(kZoneGroupIndex, -1));
        if (!isValidSampleAssetIndex(program, zone.sampleAssetIndex))
            continue;
        if (zone.groupIndex >= static_cast<int>(program.groups.size()))
            continue;

        zone.keyRange = audiocity::engine::MidiRange::fromUnordered(
            static_cast<int>(child.getProperty(kKeyLow, audiocity::engine::kMidiNoteMin)),
            static_cast<int>(child.getProperty(kKeyHigh, audiocity::engine::kMidiNoteMax)));
        zone.velocityRange = audiocity::engine::VelocityRange::fromUnordered(
            static_cast<int>(child.getProperty(kVelocityLow, audiocity::engine::kVelocityMin)),
            static_cast<int>(child.getProperty(kVelocityHigh, audiocity::engine::kVelocityMax)));
        zone.velocityFadeIn = restoreVelocityFadeRange(child, kZoneVelocityFadeInLow, kZoneVelocityFadeInHigh);
        zone.velocityFadeOut = restoreVelocityFadeRange(child, kZoneVelocityFadeOutLow, kZoneVelocityFadeOutHigh);
        zone.rootMidiNote = audiocity::engine::clampMidiNote(static_cast<int>(child.getProperty(kZoneRootMidiNote, 60)));
        zone.sampleStart = juce::jmax(0, static_cast<int>(child.getProperty(kZoneSampleStart, 0)));
        if (child.hasProperty(kZoneSampleEndExclusive))
            zone.sampleEndExclusive = static_cast<int>(child.getProperty(kZoneSampleEndExclusive, -1));
        else if (child.hasProperty(kZoneSampleEnd))
            zone.sampleEndExclusive = static_cast<int>(child.getProperty(kZoneSampleEnd, -1)) + 1;
        zone.loopStart = static_cast<int>(child.getProperty(kZoneLoopStart, -1));
        if (child.hasProperty(kZoneLoopEndExclusive))
            zone.loopEndExclusive = static_cast<int>(child.getProperty(kZoneLoopEndExclusive, -1));
        else if (child.hasProperty(kZoneLoopEnd))
            zone.loopEndExclusive = static_cast<int>(child.getProperty(kZoneLoopEnd, -1)) + 1;
        zone.gainDb = static_cast<float>(child.getProperty(kZoneRawGainDb, child.getProperty(kZoneGainDb, 0.0f)));
        zone.pan = static_cast<float>(child.getProperty(kZoneRawPan, child.getProperty(kZonePan, 0.0f)));
        zone.tuneCents = static_cast<float>(child.getProperty(kZoneTuneCents, 0.0f));
        zone.roundRobinGroup = static_cast<int>(child.getProperty(kZoneRawRoundRobinGroup,
            child.getProperty(kZoneRoundRobinGroup, 0)));
        zone.roundRobinPosition = static_cast<int>(child.getProperty(kZoneRoundRobinPosition, 0));
        zone.roundRobinLength = static_cast<int>(child.getProperty(kZoneRoundRobinLength, zone.roundRobinLength));
        zone.roundRobinMode = static_cast<int>(child.getProperty(kZoneRoundRobinMode, 0)) == 1
            ? audiocity::engine::RoundRobinMode::cycleRandom
            : audiocity::engine::RoundRobinMode::ordered;
        const auto storedTriggerMode = static_cast<int>(child.getProperty(kZoneRawTriggerMode,
            child.getProperty(kZoneTriggerMode, 0)));
        zone.triggerMode = storedTriggerMode == 2
            ? audiocity::engine::ZoneTriggerMode::release
            : (storedTriggerMode == 1
                ? audiocity::engine::ZoneTriggerMode::oneShot
                : audiocity::engine::ZoneTriggerMode::gate);
        zone.chokeGroup = static_cast<int>(child.getProperty(kZoneRawChokeGroup,
            child.getProperty(kZoneChokeGroup, 0)));
        zone.loopMode = static_cast<int>(child.getProperty(kZoneLoopMode, 0)) == 1
            ? audiocity::engine::ZoneLoopMode::sustain
            : (static_cast<int>(child.getProperty(kZoneLoopMode, 0)) == 2
                ? audiocity::engine::ZoneLoopMode::continuous
                : audiocity::engine::ZoneLoopMode::noLoop);
        restoredZones.push_back(zone);
    }

    program.zones = std::move(restoredZones);
    for (int groupIndex = 0; groupIndex < static_cast<int>(program.groups.size()); ++groupIndex)
        recomputeGroupRangeFromZones(program, groupIndex);
    return true;
}

ProgramZoneEdit makeProgramZoneOverviewEdit(const ProgramZoneListRow& row,
                                            const ProgramZoneOverviewDragMode mode,
                                            const int noteValue,
                                            const int velocityValue,
                                            const int noteDelta,
                                            const int velocityDelta)
{
    ProgramZoneEdit edit;
    edit.zoneIndex = row.zoneIndex;
    edit.keyLow = row.keyLow;
    edit.keyHigh = row.keyHigh;
    edit.velocityLow = row.velocityLow;
    edit.velocityHigh = row.velocityHigh;
    edit.rootMidiNote = row.rootMidiNote;

    switch (mode)
    {
        case ProgramZoneOverviewDragMode::move:
        {
            const auto width = juce::jmax(0, row.keyHigh - row.keyLow);
            const auto height = juce::jmax(0, row.velocityHigh - row.velocityLow);
            auto nextKeyLow = clampOverviewNote(row.keyLow + noteDelta);
            auto nextKeyHigh = nextKeyLow + width;
            if (nextKeyHigh > audiocity::engine::kMidiNoteMax)
            {
                nextKeyHigh = audiocity::engine::kMidiNoteMax;
                nextKeyLow = juce::jmax(audiocity::engine::kMidiNoteMin, nextKeyHigh - width);
            }

            auto nextVelocityLow = clampOverviewVelocity(row.velocityLow + velocityDelta);
            auto nextVelocityHigh = nextVelocityLow + height;
            if (nextVelocityHigh > audiocity::engine::kVelocityMax)
            {
                nextVelocityHigh = audiocity::engine::kVelocityMax;
                nextVelocityLow = juce::jmax(audiocity::engine::kVelocityMin, nextVelocityHigh - height);
            }

            edit.keyLow = nextKeyLow;
            edit.keyHigh = nextKeyHigh;
            edit.velocityLow = nextVelocityLow;
            edit.velocityHigh = nextVelocityHigh;
            break;
        }

        case ProgramZoneOverviewDragMode::keyLow:
            edit.keyLow = juce::jmin(clampOverviewNote(noteValue), row.keyHigh);
            break;

        case ProgramZoneOverviewDragMode::keyHigh:
            edit.keyHigh = juce::jmax(row.keyLow, clampOverviewNote(noteValue));
            break;

        case ProgramZoneOverviewDragMode::velocityLow:
            edit.velocityLow = juce::jmin(clampOverviewVelocity(velocityValue), row.velocityHigh);
            break;

        case ProgramZoneOverviewDragMode::velocityHigh:
            edit.velocityHigh = juce::jmax(row.velocityLow, clampOverviewVelocity(velocityValue));
            break;

        case ProgramZoneOverviewDragMode::none:
        default:
            break;
    }

    return edit;
}
} // namespace audiocity::plugin
