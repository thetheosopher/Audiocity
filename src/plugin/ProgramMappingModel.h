#pragma once

#include "../engine/ProgramModel.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace audiocity::plugin
{
enum class ProgramZoneOverviewDragMode
{
    none,
    move,
    keyLow,
    keyHigh,
    velocityLow,
    velocityHigh
};

struct ProgramZoneListRow
{
    int zoneIndex = -1;
    int sampleAssetIndex = -1;
    int sampleLength = 0;
    int keyLow = 0;
    int keyHigh = 0;
    int velocityLow = 0;
    int velocityHigh = 0;
    int velocityFadeInLow = -1;
    int velocityFadeInHigh = -1;
    int velocityFadeOutLow = -1;
    int velocityFadeOutHigh = -1;
    int rootMidiNote = 60;
    int sampleStart = 0;
    int sampleEnd = -1;
    int loopStart = -1;
    int loopEnd = -1;
    int roundRobinGroup = 0;
    int roundRobinPosition = 0;
    int chokeGroupId = 0;
    float gainDbValue = 0.0f;
    float panValue = 0.0f;
    audiocity::engine::RoundRobinMode roundRobinModeValue = audiocity::engine::RoundRobinMode::ordered;
    audiocity::engine::ZoneTriggerMode triggerModeValue = audiocity::engine::ZoneTriggerMode::gate;
    audiocity::engine::ZoneLoopMode loopModeValue = audiocity::engine::ZoneLoopMode::noLoop;
    juce::String sampleName;
    juce::String sampleWindow;
    juce::String loopPoints;
    juce::String keyRange;
    juce::String velocityRange;
    juce::String velocityFadeIn;
    juce::String velocityFadeOut;
    juce::String rootNote;
    juce::String triggerMode;
    juce::String loopMode;
    juce::String roundRobin;
    juce::String roundRobinMode;
    juce::String chokeGroup;
    juce::String gainDb;
    juce::String pan;
    juce::String summaryText;
    juce::String detailText;
};

struct ProgramZoneEdit
{
    int zoneIndex = -1;
    int keyLow = audiocity::engine::kMidiNoteMin;
    int keyHigh = audiocity::engine::kMidiNoteMax;
    int velocityLow = audiocity::engine::kVelocityMin;
    int velocityHigh = audiocity::engine::kVelocityMax;
    int velocityFadeInLow = -1;
    int velocityFadeInHigh = -1;
    int velocityFadeOutLow = -1;
    int velocityFadeOutHigh = -1;
    int rootMidiNote = 60;
    int sampleStart = 0;
    int sampleEnd = 0;
    int loopStart = 0;
    int loopEnd = 0;
    float gainDb = 0.0f;
    float pan = 0.0f;
    int roundRobinGroup = 0;
    int roundRobinPosition = 0;
    int chokeGroupId = 0;
    audiocity::engine::ZoneTriggerMode triggerMode = audiocity::engine::ZoneTriggerMode::gate;
    audiocity::engine::ZoneLoopMode loopMode = audiocity::engine::ZoneLoopMode::noLoop;
    audiocity::engine::RoundRobinMode roundRobinMode = audiocity::engine::RoundRobinMode::ordered;
    bool hasGainDb = false;
    bool hasPan = false;
    bool hasSampleStart = false;
    bool hasSampleEnd = false;
    bool hasLoopStart = false;
    bool hasLoopEnd = false;
    bool hasVelocityFadeIn = false;
    bool hasVelocityFadeOut = false;
    bool hasRoundRobinGroup = false;
    bool hasRoundRobinPosition = false;
    bool hasChokeGroupId = false;
    bool hasTriggerMode = false;
    bool hasLoopMode = false;
    bool hasRoundRobinMode = false;
};

[[nodiscard]] std::vector<ProgramZoneListRow> buildProgramZoneListRows(const audiocity::engine::Program& program);
[[nodiscard]] bool applyProgramZoneEdit(audiocity::engine::Program& program, const ProgramZoneEdit& edit);
[[nodiscard]] bool applyProgramZoneEditsAtomic(audiocity::engine::Program& program,
                                               const std::vector<ProgramZoneEdit>& edits);
[[nodiscard]] int createProgramZoneForSampleAsset(audiocity::engine::Program& program,
                                                  int sampleAssetIndex,
                                                  int seedZoneIndex = -1);
[[nodiscard]] int createProgramZone(audiocity::engine::Program& program, int seedZoneIndex = -1);
[[nodiscard]] int duplicateProgramZone(audiocity::engine::Program& program, int zoneIndex);
[[nodiscard]] bool deleteProgramZone(audiocity::engine::Program& program, int zoneIndex);
[[nodiscard]] bool deleteProgramZonesAtomic(audiocity::engine::Program& program,
                                            const std::vector<int>& zoneIndices);
[[nodiscard]] int splitProgramZoneByKey(audiocity::engine::Program& program, int zoneIndex);
[[nodiscard]] int splitProgramSliceAtSample(audiocity::engine::Program& program, int sampleIndex);
[[nodiscard]] int mergeProgramSlicesAtSampleBoundary(audiocity::engine::Program& program, int boundarySample);
[[nodiscard]] bool remapProgramZonesChromatically(audiocity::engine::Program& program,
                                                  const std::vector<int>& zoneIndices,
                                                  int baseMidiNote = 36);
[[nodiscard]] bool mapProgramZonesToRootNotes(audiocity::engine::Program& program,
                                              const std::vector<int>& zoneIndices);
[[nodiscard]] bool spreadProgramZonesAcrossKeyRange(audiocity::engine::Program& program,
                                                    const std::vector<int>& zoneIndices);
[[nodiscard]] bool deriveProgramZoneRootNotesFromKeyRanges(audiocity::engine::Program& program,
                                                           const std::vector<int>& zoneIndices);
[[nodiscard]] juce::Identifier programZoneMappingStateType();
[[nodiscard]] juce::ValueTree createProgramZoneMappingState(const audiocity::engine::Program& program);
[[nodiscard]] std::vector<ProgramZoneEdit> parseProgramZoneMappingState(const juce::ValueTree& state);
[[nodiscard]] bool restoreProgramZoneStructureFromState(audiocity::engine::Program& program, const juce::ValueTree& state);
[[nodiscard]] ProgramZoneEdit makeProgramZoneOverviewEdit(const ProgramZoneListRow& row,
                                                          ProgramZoneOverviewDragMode mode,
                                                          int noteValue,
                                                          int velocityValue,
                                                          int noteDelta = 0,
                                                          int velocityDelta = 0);
} // namespace audiocity::plugin
