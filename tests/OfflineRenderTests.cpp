#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "../src/engine/EngineCore.h"
#include "../src/engine/ProgramModel.h"
#include "../src/engine/ProgramSnapshot.h"
#include "../src/engine/SettingsUndoHistory.h"
#include "../src/engine/SfzImporter.h"
#include "../src/plugin/CcLearnDial.h"
#include "../src/plugin/LibraryFileIndex.h"
#include "../src/plugin/LibraryMetadata.h"
#include "../src/plugin/PeakPreviewCache.h"
#include "../src/plugin/PresetJson.h"
#include "../src/plugin/ImportedProgramState.h"
#include "../src/plugin/PlayerPadState.h"
#include "../src/plugin/ProgramMappingModel.h"
#include "../src/plugin/ProgramMappingUndoHistory.h"

#include <cmath>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace
{
juce::File fixtureFile(const juce::String& relativePath)
{
    return juce::File(AUDIOCITY_SOURCE_DIR).getChildFile(relativePath);
}

bool buffersAreEqual(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, const float tolerance)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;

    for (int channel = 0; channel < a.getNumChannels(); ++channel)
    {
        const auto* aData = a.getReadPointer(channel);
        const auto* bData = b.getReadPointer(channel);

        for (int sample = 0; sample < a.getNumSamples(); ++sample)
        {
            if (std::abs(aData[sample] - bData[sample]) > tolerance)
                return false;
        }
    }

    return true;
}

juce::AudioBuffer<float> createTestSample(const int length)
{
    juce::AudioBuffer<float> buffer(1, length);

    for (int i = 0; i < length; ++i)
    {
        const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 220.0 / 48000.0);
        buffer.setSample(0, i, 0.35f * std::sin(phase));
    }

    return buffer;
}

juce::AudioBuffer<float> renderSequence(audiocity::engine::EngineCore& engine)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 8;

    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

        if (block == 3)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 64);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            output.copyFrom(channel, block * blockSize, blockBuffer, channel, 0, blockSize);
    }

    return output;
}

bool runDeterminismTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(2048);

    audiocity::engine::EngineCore firstEngine;
    firstEngine.prepare(sampleRate, blockSize, channels);
    firstEngine.setSampleData(sample, sampleRate, 60);

    audiocity::engine::EngineCore secondEngine;
    secondEngine.prepare(sampleRate, blockSize, channels);
    secondEngine.setSampleData(sample, sampleRate, 60);

    const auto first = renderSequence(firstEngine);
    const auto second = renderSequence(secondEngine);

    return buffersAreEqual(first, second, 1.0e-7f);
}

bool runProgramModelRangeAndZoneMatchingTest()
{
    using namespace audiocity::engine;

    const auto reversedKeyRange = MidiRange::fromUnordered(80, 40);
    if (reversedKeyRange.low != 40 || reversedKeyRange.high != 80 || !reversedKeyRange.isValid())
        return false;

    const auto clippedKeyRange = MidiRange::fromUnordered(-12, 140);
    if (clippedKeyRange.low != kMidiNoteMin || clippedKeyRange.high != kMidiNoteMax)
        return false;

    const auto reversedVelocityRange = VelocityRange::fromUnordered(120, 12);
    if (reversedVelocityRange.low != 12 || reversedVelocityRange.high != 120 || !reversedVelocityRange.isValid())
        return false;

    SampleAsset sample;
    sample.sourcePath = "Samples/Kick.wav";
    sample.displayName = "Kick";
    sample.lengthSamples = 2048;
    sample.numChannels = 2;
    sample.sampleRateHz = 48000.0;
    sample.rootMidiNote = 36;

    if (!sample.hasAudio())
        return false;

    Program program;
    program.name = "Mapped Kit";
    program.sampleAssets.push_back(sample);

    Zone kickZone;
    kickZone.sampleAssetIndex = 0;
    kickZone.keyRange = MidiRange::single(36);
    kickZone.velocityRange = VelocityRange::fromUnordered(32, 127);
    kickZone.rootMidiNote = 36;
    program.zones.push_back(kickZone);

    Zone missingSampleZone;
    missingSampleZone.sampleAssetIndex = 9;
    missingSampleZone.keyRange = MidiRange::single(37);
    program.zones.push_back(missingSampleZone);

    if (!program.hasPlayableZones())
        return false;

    if (!program.isZoneSampleIndexValid(program.zones[0]) || program.isZoneSampleIndexValid(program.zones[1]))
        return false;

    if (program.findFirstMatchingZoneIndex(36, 31) != -1)
        return false;

    if (program.findFirstMatchingZoneIndex(36, 32) != 0)
        return false;

    if (program.findFirstMatchingZoneIndex(37, 96) != -1)
        return false;

    return true;
}

bool runProgramSnapshotBuildAndMatchTest()
{
    using namespace audiocity::engine;

    Program program;

    SampleAsset sample;
    sample.lengthSamples = 2048;
    sample.numChannels = 1;
    sample.sampleRateHz = 48000.0;
    sample.rootMidiNote = 40;
    program.sampleAssets.push_back(sample);

    Group group;
    group.keyRange = MidiRange::fromUnordered(36, 48);
    group.velocityRange = VelocityRange::fromUnordered(80, 127);
    program.groups.push_back(group);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.groupIndex = 0;
    zone.keyRange = MidiRange::single(40);
    zone.velocityRange = VelocityRange::fromUnordered(64, 127);
    zone.rootMidiNote = 40;
    program.zones.push_back(zone);

    const auto snapshot = ProgramSnapshot::fromProgram(program);
    if (snapshot.sampleAssetCount != 1 || snapshot.groupCount != 1 || snapshot.zoneCount != 1)
        return false;

    if (snapshot.truncated || !snapshot.hasPlayableZones())
        return false;

    if (snapshot.findFirstMatchingZoneIndex(40, 79) != -1)
        return false;

    if (snapshot.findFirstMatchingZoneIndex(39, 100) != -1)
        return false;

    if (snapshot.findFirstMatchingZoneIndex(40, 100) != 0)
        return false;

    return snapshot.zones[0].rootMidiNote == 40;
}

bool runProgramMappingRowsTest()
{
    using namespace audiocity::engine;

    Program program;
    program.name = "Mapping Test";

    SampleAsset sample;
    sample.displayName = "snare_rr1.wav";
    program.sampleAssets.push_back(sample);

    Group group;
    group.roundRobinGroup = 4;
    group.roundRobinMode = RoundRobinMode::cycleRandom;
    group.chokeGroup = 2;
    group.triggerMode = ZoneTriggerMode::oneShot;
    group.gainDb = -3.0f;
    group.pan = 0.25f;
    program.groups.push_back(group);

    Zone zone;
    zone.groupIndex = 0;
    zone.sampleAssetIndex = 0;
    zone.keyRange = { 38, 40 };
    zone.velocityRange = { 32, 96 };
    zone.velocityFadeIn = VelocityFadeRange::fromUnordered(40, 64);
    zone.velocityFadeOut = VelocityFadeRange::fromUnordered(92, 120);
    zone.rootMidiNote = 39;
    zone.roundRobinPosition = 2;
    zone.loopMode = ZoneLoopMode::sustain;
    zone.gainDb = 1.5f;
    zone.pan = -0.5f;
    zone.triggerMode = ZoneTriggerMode::release;
    program.zones.push_back(zone);

    const auto rows = audiocity::plugin::buildProgramZoneListRows(program);
    if (rows.size() != 1)
        return false;

    const auto& row = rows.front();
    return row.zoneIndex == 0
        && row.keyLow == 38
        && row.keyHigh == 40
        && row.velocityLow == 32
        && row.velocityHigh == 96
        && row.velocityFadeInLow == 40
        && row.velocityFadeInHigh == 64
        && row.velocityFadeOutLow == 92
        && row.velocityFadeOutHigh == 120
        && row.rootMidiNote == 39
        && row.roundRobinGroup == 4
        && row.roundRobinPosition == 2
        && row.chokeGroupId == 2
        && std::abs(row.gainDbValue - (-1.5f)) <= 1.0e-6f
        && std::abs(row.panValue - (-0.25f)) <= 1.0e-6f
        && row.roundRobinModeValue == RoundRobinMode::cycleRandom
        && row.triggerModeValue == ZoneTriggerMode::release
        && row.loopModeValue == ZoneLoopMode::sustain
        && row.sampleName == "snare_rr1.wav"
        && row.keyRange == "38-40"
        && row.velocityRange == "32-96"
        && row.velocityFadeIn == "40-64"
        && row.velocityFadeOut == "92-120"
        && row.rootNote == "39"
        && row.triggerMode == "release"
        && row.loopMode == "sustain"
        && row.roundRobin == "4:2"
        && row.roundRobinMode == "cycle-random"
        && row.chokeGroup == "2"
        && row.gainDb == "-1.5 dB"
        && row.pan == "-25"
        && row.detailText.contains("Vel Fade In: 40-64")
        && row.detailText.contains("Vel Fade Out: 92-120")
        && row.detailText.contains("RR mode: cycle-random")
        && row.detailText.contains("Sample: snare_rr1.wav");
}

bool runProgramMappingEditTest()
{
    using namespace audiocity::engine;

    Program program;

    SampleAsset sample;
    sample.displayName = "tone.wav";
    program.sampleAssets.push_back(sample);

    Group group;
    group.keyRange = MidiRange::single(60);
    group.velocityRange = VelocityRange::fromUnordered(20, 80);
    group.gainDb = -3.0f;
    group.pan = 0.25f;
    program.groups.push_back(group);

    Zone first;
    first.groupIndex = 0;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::single(60);
    first.velocityRange = VelocityRange::fromUnordered(20, 80);
    first.rootMidiNote = 60;
    first.velocityFadeIn = VelocityFadeRange::disabled();
    first.velocityFadeOut = VelocityFadeRange::disabled();
    first.gainDb = 1.0f;
    first.pan = -0.25f;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::single(62);
    second.velocityRange = VelocityRange::fromUnordered(30, 90);
    second.rootMidiNote = 62;
    second.gainDb = 2.0f;
    second.pan = 0.10f;
    second.roundRobinGroup = 3;
    second.roundRobinPosition = 1;
    second.chokeGroup = 5;
    second.triggerMode = ZoneTriggerMode::oneShot;
    second.loopMode = ZoneLoopMode::sustain;
    second.velocityFadeIn = VelocityFadeRange::fromUnordered(16, 48);
    second.velocityFadeOut = VelocityFadeRange::fromUnordered(96, 120);
    program.zones.push_back(second);

    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = 75;
    edit.keyHigh = 72;
    edit.velocityLow = 127;
    edit.velocityHigh = 96;
    edit.velocityFadeInLow = 12;
    edit.velocityFadeInHigh = 72;
    edit.velocityFadeOutLow = 100;
    edit.velocityFadeOutHigh = 118;
    edit.rootMidiNote = 200;
    edit.gainDb = -6.0f;
    edit.pan = 0.5f;
    edit.roundRobinGroup = 9;
    edit.roundRobinPosition = 2;
    edit.roundRobinMode = RoundRobinMode::cycleRandom;
    edit.chokeGroupId = 7;
    edit.triggerMode = ZoneTriggerMode::release;
    edit.loopMode = ZoneLoopMode::continuous;
    edit.hasGainDb = true;
    edit.hasPan = true;
    edit.hasVelocityFadeIn = true;
    edit.hasVelocityFadeOut = true;
    edit.hasRoundRobinGroup = true;
    edit.hasRoundRobinPosition = true;
    edit.hasRoundRobinMode = true;
    edit.hasChokeGroupId = true;
    edit.hasTriggerMode = true;
    edit.hasLoopMode = true;

    if (!audiocity::plugin::applyProgramZoneEdit(program, edit))
        return false;

    audiocity::plugin::ProgramZoneEdit rangeOnlyEdit;
    rangeOnlyEdit.zoneIndex = 1;
    rangeOnlyEdit.keyLow = 64;
    rangeOnlyEdit.keyHigh = 64;
    rangeOnlyEdit.velocityLow = 10;
    rangeOnlyEdit.velocityHigh = 11;
    rangeOnlyEdit.velocityFadeInLow = -1;
    rangeOnlyEdit.velocityFadeInHigh = -1;
    rangeOnlyEdit.velocityFadeOutLow = -1;
    rangeOnlyEdit.velocityFadeOutHigh = -1;
    rangeOnlyEdit.rootMidiNote = 65;
    rangeOnlyEdit.hasVelocityFadeIn = true;
    rangeOnlyEdit.hasVelocityFadeOut = true;
    if (!audiocity::plugin::applyProgramZoneEdit(program, rangeOnlyEdit))
        return false;

    if (audiocity::plugin::applyProgramZoneEdit(program, { 99, 0, 127, 0, 127, 60 }))
        return false;

    const auto& editedZone = program.zones[0];
    const auto& untouchedZone = program.zones[1];
    const auto& updatedGroup = program.groups[0];

    return editedZone.keyRange.low == 72
        && editedZone.keyRange.high == 75
        && editedZone.velocityRange.low == 96
        && editedZone.velocityRange.high == 127
        && editedZone.velocityFadeIn.low == 12
        && editedZone.velocityFadeIn.high == 72
        && editedZone.velocityFadeOut.low == 100
        && editedZone.velocityFadeOut.high == 118
        && editedZone.rootMidiNote == 127
        && std::abs(editedZone.gainDb - (-3.0f)) <= 1.0e-6f
        && std::abs(editedZone.pan - 0.25f) <= 1.0e-6f
        && editedZone.roundRobinGroup == 9
        && editedZone.roundRobinPosition == 2
        && editedZone.roundRobinMode == RoundRobinMode::cycleRandom
        && editedZone.chokeGroup == 7
        && editedZone.triggerMode == ZoneTriggerMode::release
        && editedZone.loopMode == ZoneLoopMode::continuous
        && untouchedZone.keyRange.low == 64
        && untouchedZone.keyRange.high == 64
        && untouchedZone.velocityRange.low == 10
        && untouchedZone.velocityRange.high == 11
        && !untouchedZone.velocityFadeIn.isEnabled()
        && !untouchedZone.velocityFadeOut.isEnabled()
        && untouchedZone.rootMidiNote == 65
        && std::abs(untouchedZone.gainDb - 2.0f) <= 1.0e-6f
        && std::abs(untouchedZone.pan - 0.10f) <= 1.0e-6f
        && untouchedZone.roundRobinGroup == 3
        && untouchedZone.roundRobinPosition == 1
        && untouchedZone.roundRobinMode == RoundRobinMode::ordered
        && untouchedZone.chokeGroup == 5
        && untouchedZone.triggerMode == ZoneTriggerMode::oneShot
        && untouchedZone.loopMode == ZoneLoopMode::sustain
        && updatedGroup.keyRange.low == 64
        && updatedGroup.keyRange.high == 75
        && updatedGroup.velocityRange.low == 10
        && updatedGroup.velocityRange.high == 127;
}

bool runProgramMappingOverviewEditTest()
{
    audiocity::plugin::ProgramZoneListRow row;
    row.zoneIndex = 3;
    row.keyLow = 40;
    row.keyHigh = 44;
    row.velocityLow = 20;
    row.velocityHigh = 40;
    row.rootMidiNote = 42;

    const auto moved = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::move,
        52,
        32,
        10,
        12);
    if (!(moved.zoneIndex == 3
        && moved.keyLow == 50
        && moved.keyHigh == 54
        && moved.velocityLow == 32
        && moved.velocityHigh == 52
        && moved.rootMidiNote == 42))
    {
        return false;
    }

    const auto clippedMove = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::move,
        127,
        127,
        100,
        120);
    if (!(clippedMove.keyLow == 123
        && clippedMove.keyHigh == 127
        && clippedMove.velocityLow == 107
        && clippedMove.velocityHigh == 127))
    {
        return false;
    }

    const auto resizedKeyLow = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::keyLow,
        60,
        20);
    if (!(resizedKeyLow.keyLow == 44 && resizedKeyLow.keyHigh == 44))
        return false;

    const auto resizedKeyHigh = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::keyHigh,
        12,
        20);
    if (!(resizedKeyHigh.keyLow == 40 && resizedKeyHigh.keyHigh == 40))
        return false;

    const auto resizedVelocityLow = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::velocityLow,
        40,
        100);
    if (!(resizedVelocityLow.velocityLow == 40 && resizedVelocityLow.velocityHigh == 40))
        return false;

    const auto resizedVelocityHigh = audiocity::plugin::makeProgramZoneOverviewEdit(
        row,
        audiocity::plugin::ProgramZoneOverviewDragMode::velocityHigh,
        40,
        2);
    return resizedVelocityHigh.velocityLow == 20
        && resizedVelocityHigh.velocityHigh == 20;
}

bool runProgramMappingSampleWindowEditTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "window.wav";
    sample.lengthSamples = 240;
    program.sampleAssets.push_back(sample);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(60);
    zone.velocityRange = VelocityRange::full();
    zone.rootMidiNote = 60;
    zone.sampleStart = 12;
    zone.sampleEndExclusive = 181;
    zone.loopStart = 40;
    zone.loopEndExclusive = 97;
    program.zones.push_back(zone);

    const auto rows = audiocity::plugin::buildProgramZoneListRows(program);
    if (rows.size() != 1)
        return false;

    const auto& row = rows.front();
    if (!(row.sampleLength == 240
        && row.sampleStart == 12
        && row.sampleEnd == 180
        && row.loopStart == 40
        && row.loopEnd == 96
        && row.sampleWindow == "12-180"
        && row.loopPoints == "40-96"
        && row.detailText.contains("Window: 12-180")
        && row.detailText.contains("Loop Points: 40-96")))
    {
        return false;
    }

    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = zone.keyRange.low;
    edit.keyHigh = zone.keyRange.high;
    edit.velocityLow = zone.velocityRange.low;
    edit.velocityHigh = zone.velocityRange.high;
    edit.rootMidiNote = zone.rootMidiNote;
    edit.sampleStart = 200;
    edit.sampleEnd = 500;
    edit.loopStart = 5;
    edit.loopEnd = 400;
    edit.hasSampleStart = true;
    edit.hasSampleEnd = true;
    edit.hasLoopStart = true;
    edit.hasLoopEnd = true;

    if (!audiocity::plugin::applyProgramZoneEdit(program, edit))
        return false;

    return program.zones[0].sampleStart == 200
        && program.zones[0].sampleEndExclusive == 240
        && program.zones[0].loopStart == 200
        && program.zones[0].loopEndExclusive == 240;
}

bool runProgramMappingZoneOperationsTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "ops.wav";
    sample.lengthSamples = 512;
    program.sampleAssets.push_back(sample);

    Group group;
    group.keyRange = MidiRange::fromUnordered(36, 44);
    group.velocityRange = VelocityRange::full();
    program.groups.push_back(group);

    Zone first;
    first.groupIndex = 0;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::fromUnordered(36, 40);
    first.velocityRange = VelocityRange::fromUnordered(20, 100);
    first.rootMidiNote = 38;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::fromUnordered(42, 44);
    second.rootMidiNote = 43;
    program.zones.push_back(second);

    const auto duplicatedIndex = audiocity::plugin::duplicateProgramZone(program, 0);
    if (!(duplicatedIndex == 2
        && program.zones.size() == 3
        && program.zones[2].keyRange.low == 36
        && program.zones[2].keyRange.high == 40))
    {
        return false;
    }

    const auto splitIndex = audiocity::plugin::splitProgramZoneByKey(program, 2);
    if (!(splitIndex == 3
        && program.zones.size() == 4
        && program.zones[2].keyRange.low == 36
        && program.zones[2].keyRange.high == 38
        && program.zones[3].keyRange.low == 39
        && program.zones[3].keyRange.high == 40))
    {
        return false;
    }

    if (!audiocity::plugin::deleteProgramZone(program, 1))
        return false;

    if (audiocity::plugin::deleteProgramZone(program, 99))
        return false;

    if (audiocity::plugin::splitProgramZoneByKey(program, 99) >= 0)
        return false;

    Program singleKeyProgram;
    singleKeyProgram.sampleAssets.push_back(sample);
    singleKeyProgram.groups.push_back(group);
    Zone singleKeyZone = first;
    singleKeyZone.keyRange = MidiRange::single(60);
    singleKeyProgram.zones.push_back(singleKeyZone);

    if (audiocity::plugin::splitProgramZoneByKey(singleKeyProgram, 0) >= 0)
        return false;

    return program.zones.size() == 3
        && program.groups[0].keyRange.low == 36
        && program.groups[0].keyRange.high == 40;
}

bool runProgramMappingCreateZoneTest()
{
    using namespace audiocity::engine;

    Program program;

    SampleAsset firstSample;
    firstSample.displayName = "first.wav";
    firstSample.lengthSamples = 512;
    firstSample.rootMidiNote = 60;
    program.sampleAssets.push_back(firstSample);

    SampleAsset secondSample;
    secondSample.displayName = "second.wav";
    secondSample.lengthSamples = 256;
    secondSample.rootMidiNote = 67;
    program.sampleAssets.push_back(secondSample);

    Group group;
    group.keyRange = MidiRange::fromUnordered(24, 96);
    group.velocityRange = VelocityRange::full();
    program.groups.push_back(group);

    const auto createdIndex = audiocity::plugin::createProgramZone(program);
    if (!(createdIndex == 0
        && program.zones.size() == 1
        && program.zones[0].sampleAssetIndex == 0
        && program.zones[0].groupIndex == 0
        && program.zones[0].keyRange.low == 60
        && program.zones[0].keyRange.high == 60
        && program.zones[0].velocityRange.low == 0
        && program.zones[0].velocityRange.high == 127
        && program.zones[0].sampleStart == 0
        && program.zones[0].sampleEndExclusive == 512))
    {
        return false;
    }

    program.zones[0].sampleAssetIndex = 1;
    program.zones[0].rootMidiNote = 69;
    program.zones[0].keyRange = MidiRange::fromUnordered(60, 72);
    program.zones[0].velocityRange = VelocityRange::fromUnordered(8, 110);
    program.zones[0].sampleStart = 12;
    program.zones[0].sampleEndExclusive = 200;
    program.zones[0].loopStart = 24;
    program.zones[0].loopEndExclusive = 144;
    program.zones[0].tuneCents = 12.5f;

    const auto seededIndex = audiocity::plugin::createProgramZone(program, 0);
    if (!(seededIndex == 1
        && program.zones.size() == 2
        && program.zones[1].sampleAssetIndex == 1
        && program.zones[1].groupIndex == 0
        && program.zones[1].rootMidiNote == 69
        && program.zones[1].keyRange.low == 69
        && program.zones[1].keyRange.high == 69
        && program.zones[1].velocityRange.low == 0
        && program.zones[1].velocityRange.high == 127
        && program.zones[1].sampleStart == 0
        && program.zones[1].sampleEndExclusive == 256
        && program.zones[1].loopStart == -1
        && program.zones[1].loopEndExclusive == -1
        && std::abs(program.zones[1].tuneCents - 12.5f) <= 1.0e-6f))
    {
        return false;
    }

    const auto explicitAssetIndex = audiocity::plugin::createProgramZoneForSampleAsset(program, 0, 0);
    if (!(explicitAssetIndex == 2
        && program.zones.size() == 3
        && program.zones[2].sampleAssetIndex == 0
        && program.zones[2].groupIndex == 0
        && program.zones[2].rootMidiNote == 69
        && program.zones[2].keyRange.low == 69
        && program.zones[2].keyRange.high == 69
        && program.zones[2].sampleEndExclusive == 512))
    {
        return false;
    }

    Program emptyProgram;
    return audiocity::plugin::createProgramZone(emptyProgram) < 0;
}

bool runProgramMappingAtomicBatchEditRollbackTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "batch.wav";
    sample.lengthSamples = 512;
    program.sampleAssets.push_back(sample);

    Zone first;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::single(60);
    first.velocityRange = VelocityRange::full();
    first.rootMidiNote = 60;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::single(61);
    second.rootMidiNote = 61;
    program.zones.push_back(second);

    const auto beforeFailure = audiocity::plugin::createProgramZoneMappingState(program);

    std::vector<audiocity::plugin::ProgramZoneEdit> failingEdits;
    failingEdits.push_back({ 0, 48, 52, 8, 96, -1, -1, -1, -1, 50 });
    failingEdits.push_back({ 99, 0, 127, 0, 127, 60 });

    if (audiocity::plugin::applyProgramZoneEditsAtomic(program, failingEdits))
        return false;

    const auto afterFailure = audiocity::plugin::createProgramZoneMappingState(program);
    if (!beforeFailure.isEquivalentTo(afterFailure))
        return false;

    std::vector<audiocity::plugin::ProgramZoneEdit> successfulEdits;
    successfulEdits.push_back({ 0, 48, 52, 8, 96, -1, -1, -1, -1, 50 });
    successfulEdits.push_back({ 1, 53, 57, 16, 100, -1, -1, -1, -1, 55 });

    if (!audiocity::plugin::applyProgramZoneEditsAtomic(program, successfulEdits))
        return false;

    return program.zones[0].keyRange.low == 48
        && program.zones[0].keyRange.high == 52
        && program.zones[0].velocityRange.low == 8
        && program.zones[0].velocityRange.high == 96
        && program.zones[0].rootMidiNote == 50
        && program.zones[1].keyRange.low == 53
        && program.zones[1].keyRange.high == 57
        && program.zones[1].velocityRange.low == 16
        && program.zones[1].velocityRange.high == 100
        && program.zones[1].rootMidiNote == 55;
}

bool runProgramMappingAtomicBatchDeleteRollbackTest()
{
    using namespace audiocity::engine;

    Program program;
    SampleAsset sample;
    sample.displayName = "batch_delete.wav";
    sample.lengthSamples = 512;
    program.sampleAssets.push_back(sample);

    Zone first;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::single(60);
    first.rootMidiNote = 60;
    program.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::single(61);
    second.rootMidiNote = 61;
    program.zones.push_back(second);

    Zone third = first;
    third.keyRange = MidiRange::single(62);
    third.rootMidiNote = 62;
    program.zones.push_back(third);

    const auto beforeFailure = audiocity::plugin::createProgramZoneMappingState(program);
    if (audiocity::plugin::deleteProgramZonesAtomic(program, { 2, 99 }))
        return false;

    const auto afterFailure = audiocity::plugin::createProgramZoneMappingState(program);
    if (!beforeFailure.isEquivalentTo(afterFailure))
        return false;

    if (!audiocity::plugin::deleteProgramZonesAtomic(program, { 2, 0 }))
        return false;

    return program.zones.size() == 1
        && program.zones.front().rootMidiNote == 61;
}

bool runProgramMappingStateRoundTripTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    SampleAsset sample;
    sample.displayName = "roundtrip.wav";
    sample.lengthSamples = 240;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    group.gainDb = -3.0f;
    group.pan = 0.25f;
    group.roundRobinGroup = 4;
    group.chokeGroup = 2;
    group.triggerMode = ZoneTriggerMode::gate;
    baseProgram.groups.push_back(group);

    Zone zone;
    zone.groupIndex = 0;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(36, 40);
    zone.velocityRange = VelocityRange::fromUnordered(24, 100);
    zone.velocityFadeIn = VelocityFadeRange::fromUnordered(20, 60);
    zone.velocityFadeOut = VelocityFadeRange::fromUnordered(90, 120);
    zone.rootMidiNote = 38;
    zone.sampleStart = 8;
    zone.sampleEndExclusive = 160;
    zone.loopStart = 20;
    zone.loopEndExclusive = 80;
    zone.gainDb = 1.5f;
    zone.pan = -0.5f;
    zone.tuneCents = 7.0f;
    zone.roundRobinGroup = 9;
    zone.roundRobinPosition = 2;
    zone.roundRobinMode = RoundRobinMode::cycleRandom;
    zone.triggerMode = ZoneTriggerMode::oneShot;
    zone.chokeGroup = 11;
    zone.loopMode = ZoneLoopMode::sustain;
    baseProgram.zones.push_back(zone);

    Program editedProgram = baseProgram;
    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = 41;
    edit.keyHigh = 45;
    edit.velocityLow = 32;
    edit.velocityHigh = 96;
    edit.velocityFadeInLow = 40;
    edit.velocityFadeInHigh = 72;
    edit.velocityFadeOutLow = -1;
    edit.velocityFadeOutHigh = -1;
    edit.rootMidiNote = 43;
    edit.sampleStart = 24;
    edit.sampleEnd = 180;
    edit.loopStart = 48;
    edit.loopEnd = 120;
    edit.gainDb = -1.0f;
    edit.pan = -0.25f;
    edit.roundRobinGroup = 7;
    edit.roundRobinPosition = 3;
    edit.roundRobinMode = RoundRobinMode::ordered;
    edit.chokeGroupId = 6;
    edit.triggerMode = ZoneTriggerMode::release;
    edit.loopMode = ZoneLoopMode::continuous;
    edit.hasSampleStart = true;
    edit.hasSampleEnd = true;
    edit.hasLoopStart = true;
    edit.hasLoopEnd = true;
    edit.hasVelocityFadeIn = true;
    edit.hasVelocityFadeOut = true;
    edit.hasGainDb = true;
    edit.hasPan = true;
    edit.hasRoundRobinGroup = true;
    edit.hasRoundRobinPosition = true;
    edit.hasRoundRobinMode = true;
    edit.hasChokeGroupId = true;
    edit.hasTriggerMode = true;
    edit.hasLoopMode = true;

    if (!audiocity::plugin::applyProgramZoneEdit(editedProgram, edit))
        return false;

    const auto state = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    if (!state.isValid() || !state.hasType(audiocity::plugin::programZoneMappingStateType()))
        return false;

    Program restoredProgram = baseProgram;
    if (!audiocity::plugin::restoreProgramZoneStructureFromState(restoredProgram, state))
        return false;

    const auto editedRows = audiocity::plugin::buildProgramZoneListRows(editedProgram);
    const auto restoredRows = audiocity::plugin::buildProgramZoneListRows(restoredProgram);
    if (editedRows.size() != 1 || restoredRows.size() != 1)
        return false;

    const auto& editedZone = editedProgram.zones.front();
    const auto& restoredZone = restoredProgram.zones.front();
    const auto& editedRow = editedRows.front();
    const auto& restoredRow = restoredRows.front();
    return editedZone.sampleAssetIndex == restoredZone.sampleAssetIndex
        && editedZone.groupIndex == restoredZone.groupIndex
        && editedZone.velocityFadeIn.low == restoredZone.velocityFadeIn.low
        && editedZone.velocityFadeIn.high == restoredZone.velocityFadeIn.high
        && editedZone.velocityFadeOut.low == restoredZone.velocityFadeOut.low
        && editedZone.velocityFadeOut.high == restoredZone.velocityFadeOut.high
        && std::abs(editedZone.gainDb - restoredZone.gainDb) <= 1.0e-6f
        && std::abs(editedZone.pan - restoredZone.pan) <= 1.0e-6f
        && std::abs(editedZone.tuneCents - restoredZone.tuneCents) <= 1.0e-6f
        && editedZone.roundRobinGroup == restoredZone.roundRobinGroup
        && editedZone.roundRobinPosition == restoredZone.roundRobinPosition
        && editedZone.roundRobinMode == restoredZone.roundRobinMode
        && editedZone.triggerMode == restoredZone.triggerMode
        && editedZone.chokeGroup == restoredZone.chokeGroup
        && editedZone.loopMode == restoredZone.loopMode
        && editedRow.keyLow == restoredRow.keyLow
        && editedRow.keyHigh == restoredRow.keyHigh
        && editedRow.velocityLow == restoredRow.velocityLow
        && editedRow.velocityHigh == restoredRow.velocityHigh
        && editedRow.velocityFadeInLow == restoredRow.velocityFadeInLow
        && editedRow.velocityFadeInHigh == restoredRow.velocityFadeInHigh
        && editedRow.velocityFadeOutLow == restoredRow.velocityFadeOutLow
        && editedRow.velocityFadeOutHigh == restoredRow.velocityFadeOutHigh
        && editedRow.rootMidiNote == restoredRow.rootMidiNote
        && editedRow.sampleStart == restoredRow.sampleStart
        && editedRow.sampleEnd == restoredRow.sampleEnd
        && editedRow.loopStart == restoredRow.loopStart
        && editedRow.loopEnd == restoredRow.loopEnd
        && std::abs(editedRow.gainDbValue - restoredRow.gainDbValue) <= 1.0e-6f
        && std::abs(editedRow.panValue - restoredRow.panValue) <= 1.0e-6f
        && editedRow.roundRobinGroup == restoredRow.roundRobinGroup
        && editedRow.roundRobinPosition == restoredRow.roundRobinPosition
        && editedRow.roundRobinModeValue == restoredRow.roundRobinModeValue
        && editedRow.chokeGroupId == restoredRow.chokeGroupId
        && editedRow.triggerModeValue == restoredRow.triggerModeValue
        && editedRow.loopModeValue == restoredRow.loopModeValue;
}

bool runProgramMappingStructuralStateRoundTripTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    SampleAsset sample;
    sample.displayName = "structural.wav";
    sample.lengthSamples = 512;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    group.roundRobinGroup = 3;
    group.roundRobinMode = RoundRobinMode::ordered;
    group.velocityRange = VelocityRange::full();
    baseProgram.groups.push_back(group);

    Zone first;
    first.groupIndex = 0;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::fromUnordered(36, 40);
    first.velocityRange = VelocityRange::fromUnordered(10, 90);
    first.rootMidiNote = 38;
    first.roundRobinGroup = 3;
    first.roundRobinPosition = 1;
    baseProgram.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::fromUnordered(41, 48);
    second.rootMidiNote = 45;
    second.roundRobinPosition = 2;
    second.roundRobinMode = RoundRobinMode::cycleRandom;
    second.loopMode = ZoneLoopMode::sustain;
    baseProgram.zones.push_back(second);

    Program editedProgram = baseProgram;
    if (audiocity::plugin::duplicateProgramZone(editedProgram, 0) != 2)
        return false;

    if (audiocity::plugin::splitProgramZoneByKey(editedProgram, 2) != 3)
        return false;

    if (!audiocity::plugin::deleteProgramZone(editedProgram, 1))
        return false;

    editedProgram.zones[1].roundRobinMode = RoundRobinMode::cycleRandom;
    editedProgram.zones[1].roundRobinPosition = 4;
    editedProgram.zones[2].velocityFadeIn = VelocityFadeRange::fromUnordered(18, 64);
    editedProgram.zones[2].velocityFadeOut = VelocityFadeRange::fromUnordered(88, 118);

    const auto state = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    if (!state.isValid())
        return false;

    Program restoredProgram = baseProgram;
    if (!audiocity::plugin::restoreProgramZoneStructureFromState(restoredProgram, state))
        return false;

    const auto restoredState = audiocity::plugin::createProgramZoneMappingState(restoredProgram);
    const auto editedRows = audiocity::plugin::buildProgramZoneListRows(editedProgram);
    const auto restoredRows = audiocity::plugin::buildProgramZoneListRows(restoredProgram);
    return editedRows.size() == 3
        && restoredRows.size() == editedRows.size()
        && state.toXmlString() == restoredState.toXmlString();
}

bool runImportedProgramStateSubtreeRoundTripTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    SampleAsset sample;
    sample.displayName = "imported.wav";
    sample.lengthSamples = 256;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    group.roundRobinGroup = 5;
    group.roundRobinMode = RoundRobinMode::ordered;
    group.triggerMode = ZoneTriggerMode::gate;
    baseProgram.groups.push_back(group);

    Zone zone;
    zone.groupIndex = 0;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(48, 52);
    zone.velocityRange = VelocityRange::fromUnordered(20, 110);
    zone.rootMidiNote = 50;
    zone.roundRobinGroup = 5;
    zone.roundRobinPosition = 3;
    zone.roundRobinLength = 4;
    zone.triggerMode = ZoneTriggerMode::release;
    baseProgram.zones.push_back(zone);

    const auto mappingState = audiocity::plugin::createProgramZoneMappingState(baseProgram);
    juce::ValueTree patchState("patch");
    const juce::String sfzPath = "C:/Library/Kits/ImportedKit.sfz";
    audiocity::plugin::appendImportedProgramState(patchState, sfzPath, mappingState);

    if (audiocity::plugin::readImportedProgramStatePath(patchState) != sfzPath)
        return false;

    const auto extractedMappingState = audiocity::plugin::readImportedProgramMappingState(patchState);
    if (!extractedMappingState.isValid()
        || !extractedMappingState.hasType(audiocity::plugin::programZoneMappingStateType()))
    {
        return false;
    }

    Program restoredProgram = baseProgram;
    restoredProgram.zones.front().roundRobinLength = 0;
    restoredProgram.zones.front().triggerMode = ZoneTriggerMode::gate;
    if (!audiocity::plugin::restoreProgramZoneStructureFromState(restoredProgram, extractedMappingState))
        return false;

    return restoredProgram.zones.front().roundRobinLength == 4
        && restoredProgram.zones.front().triggerMode == ZoneTriggerMode::release
        && extractedMappingState.toXmlString()
            == audiocity::plugin::createProgramZoneMappingState(restoredProgram).toXmlString();
}

bool runImportedProgramStateLegacyReplayFallbackTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    SampleAsset sample;
    sample.displayName = "legacy.wav";
    sample.lengthSamples = 512;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    group.roundRobinGroup = 2;
    group.roundRobinMode = RoundRobinMode::ordered;
    baseProgram.groups.push_back(group);

    Zone zone;
    zone.groupIndex = 0;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(36, 42);
    zone.velocityRange = VelocityRange::fromUnordered(16, 96);
    zone.rootMidiNote = 40;
    baseProgram.zones.push_back(zone);

    Program editedProgram = baseProgram;
    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = 48;
    edit.keyHigh = 60;
    edit.velocityLow = 20;
    edit.velocityHigh = 110;
    edit.velocityFadeInLow = 24;
    edit.velocityFadeInHigh = 64;
    edit.velocityFadeOutLow = 100;
    edit.velocityFadeOutHigh = 120;
    edit.rootMidiNote = 52;
    edit.sampleStart = 0;
    edit.sampleEnd = 511;
    edit.gainDb = -4.0f;
    edit.pan = -0.35f;
    edit.roundRobinGroup = 7;
    edit.roundRobinPosition = 3;
    edit.roundRobinMode = RoundRobinMode::cycleRandom;
    edit.chokeGroupId = 5;
    edit.triggerMode = ZoneTriggerMode::release;
    edit.loopMode = ZoneLoopMode::continuous;
    edit.hasVelocityFadeIn = true;
    edit.hasVelocityFadeOut = true;
    edit.hasSampleStart = true;
    edit.hasSampleEnd = true;
    edit.hasGainDb = true;
    edit.hasPan = true;
    edit.hasRoundRobinGroup = true;
    edit.hasRoundRobinPosition = true;
    edit.hasRoundRobinMode = true;
    edit.hasChokeGroupId = true;
    edit.hasTriggerMode = true;
    edit.hasLoopMode = true;

    if (!audiocity::plugin::applyProgramZoneEdit(editedProgram, edit))
        return false;

    auto legacyState = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    legacyState.setProperty("formatVersion", 1, nullptr);

    Program restoredProgram = baseProgram;
    if (!audiocity::plugin::restoreImportedProgramMappingState(restoredProgram, legacyState))
        return false;

    const auto editedState = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    const auto restoredState = audiocity::plugin::createProgramZoneMappingState(restoredProgram);
    return editedState.toXmlString() == restoredState.toXmlString();
}

bool runImportedProgramRestoreResultSuccessTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    baseProgram.name = "Restore Result";

    SampleAsset sample;
    sample.displayName = "restore.wav";
    sample.lengthSamples = 512;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    group.roundRobinGroup = 4;
    group.roundRobinMode = RoundRobinMode::ordered;
    baseProgram.groups.push_back(group);

    Zone zone;
    zone.groupIndex = 0;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(36, 48);
    zone.velocityRange = VelocityRange::fromUnordered(20, 80);
    zone.rootMidiNote = 40;
    baseProgram.zones.push_back(zone);

    Program editedProgram = baseProgram;
    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = 48;
    edit.keyHigh = 60;
    edit.velocityLow = 30;
    edit.velocityHigh = 110;
    edit.rootMidiNote = 52;
    edit.roundRobinGroup = 6;
    edit.roundRobinPosition = 2;
    edit.triggerMode = ZoneTriggerMode::release;
    edit.hasRoundRobinGroup = true;
    edit.hasRoundRobinPosition = true;
    edit.hasTriggerMode = true;

    if (!audiocity::plugin::applyProgramZoneEdit(editedProgram, edit))
        return false;

    const auto mappingState = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    const auto restoredResult = audiocity::plugin::buildImportedProgramRestoreResult(baseProgram, mappingState);
    if (!restoredResult.has_value())
        return false;

    return restoredResult->hasPublishableZones
        && restoredResult->program.zones.size() == 1
        && restoredResult->program.zones.front().keyRange.low == 48
        && restoredResult->program.zones.front().keyRange.high == 60
        && restoredResult->program.zones.front().triggerMode == ZoneTriggerMode::release
        && restoredResult->derivedState.zoneRows.size() == 1
        && restoredResult->derivedState.zoneRows.front().keyRange == "48-60"
        && restoredResult->derivedState.mapSummary.contains("Program: Restore Result");
}

bool runImportedProgramRestoreResultAtomicFailureTest()
{
    using namespace audiocity::engine;

    Program baseProgram;
    SampleAsset sample;
    sample.displayName = "atomic.wav";
    sample.lengthSamples = 512;
    baseProgram.sampleAssets.push_back(sample);

    Group group;
    baseProgram.groups.push_back(group);

    Zone firstZone;
    firstZone.groupIndex = 0;
    firstZone.sampleAssetIndex = 0;
    firstZone.keyRange = MidiRange::fromUnordered(24, 36);
    firstZone.velocityRange = VelocityRange::fromUnordered(1, 64);
    baseProgram.zones.push_back(firstZone);

    Zone secondZone = firstZone;
    secondZone.keyRange = MidiRange::fromUnordered(37, 48);
    secondZone.velocityRange = VelocityRange::fromUnordered(65, 127);
    baseProgram.zones.push_back(secondZone);

    Program editedProgram = baseProgram;
    audiocity::plugin::ProgramZoneEdit firstEdit;
    firstEdit.zoneIndex = 0;
    firstEdit.keyLow = 30;
    firstEdit.keyHigh = 40;
    firstEdit.velocityLow = 10;
    firstEdit.velocityHigh = 70;
    firstEdit.rootMidiNote = 34;

    audiocity::plugin::ProgramZoneEdit secondEdit;
    secondEdit.zoneIndex = 1;
    secondEdit.keyLow = 50;
    secondEdit.keyHigh = 72;
    secondEdit.velocityLow = 71;
    secondEdit.velocityHigh = 120;
    secondEdit.rootMidiNote = 60;

    if (!audiocity::plugin::applyProgramZoneEdit(editedProgram, firstEdit)
        || !audiocity::plugin::applyProgramZoneEdit(editedProgram, secondEdit))
    {
        return false;
    }

    auto legacyState = audiocity::plugin::createProgramZoneMappingState(editedProgram);
    legacyState.setProperty("formatVersion", 1, nullptr);
    if (legacyState.getNumChildren() < 2)
        return false;

    legacyState.getChild(1).setProperty("zoneIndex", 99, nullptr);
    const auto baseStateXml = audiocity::plugin::createProgramZoneMappingState(baseProgram).toXmlString();
    const auto restoredResult = audiocity::plugin::buildImportedProgramRestoreResult(baseProgram, legacyState);
    const auto afterAttemptXml = audiocity::plugin::createProgramZoneMappingState(baseProgram).toXmlString();

    return !restoredResult.has_value()
        && baseStateXml == afterAttemptXml;
}

bool runImportedProgramDerivedStateSummaryTest()
{
    using namespace audiocity::engine;

    Program program;
    program.name = "Imported Summary";

    SampleAsset sample;
    sample.displayName = "summary.wav";
    sample.lengthSamples = 1024;
    program.sampleAssets.push_back(sample);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(60, 67);
    zone.velocityRange = VelocityRange::fromUnordered(10, 120);
    zone.rootMidiNote = 64;
    zone.roundRobinGroup = 3;
    zone.roundRobinPosition = 2;
    zone.triggerMode = ZoneTriggerMode::release;
    zone.loopMode = ZoneLoopMode::sustain;
    zone.chokeGroup = 8;
    program.zones.push_back(zone);

    const auto derivedState = audiocity::plugin::buildImportedProgramDerivedState(program);
    return derivedState.zoneRows.size() == 1
        && derivedState.zoneRows.front().sampleName == "summary.wav"
        && derivedState.mapSummary.contains("Program: Imported Summary")
        && derivedState.mapSummary.contains("1. summary.wav")
        && derivedState.mapSummary.contains("key 60-67")
        && derivedState.mapSummary.contains("vel 10-120")
        && derivedState.mapSummary.contains("release")
        && derivedState.mapSummary.contains("loop sustain")
        && derivedState.mapSummary.contains("rr 3:2")
        && derivedState.mapSummary.contains("choke 8");
}

bool runSfzImportIncludeDefineDefaultPathTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_import_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto samplesDirectory = tempDirectory.getChildFile("Samples");
    if (!samplesDirectory.createDirectory())
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    const auto sampleFile = samplesDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto includeFile = tempDirectory.getChildFile("regions.sfz");
    const auto rootFile = tempDirectory.getChildFile("root.sfz");

    if (!includeFile.replaceWithText(
            "<group> key=60 lovel=10 hivel=120 pan=-50\n"
            "<region> sample=Tone.wav pitch_keycenter=60 tune=7 transpose=1 offset=4 end=120 loop_start=16 loop_end=31 loop_mode=loop_sustain\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    if (!rootFile.replaceWithText(
            "#define $SAMPLE_DIR Samples\n"
            "<control> default_path=$SAMPLE_DIR/\n"
            "#include \"regions.sfz\"\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(rootFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors())
        return false;

    const auto& program = result.program;
    if (program.sampleAssets.size() != 1
        || result.sampleDataByAsset.size() != 1
        || program.groups.size() != 1
        || program.zones.size() != 1)
    {
        return false;
    }

    const auto& asset = program.sampleAssets[0];
    if (asset.lengthSamples != sampleLength
        || asset.numChannels != 1
        || std::abs(asset.sampleRateHz - sampleRate) > 0.01
        || asset.rootMidiNote != 60)
    {
        return false;
    }

    if (result.sampleDataByAsset[0].getNumChannels() != 1
        || result.sampleDataByAsset[0].getNumSamples() != sampleLength)
    {
        return false;
    }

    const auto& group = program.groups[0];
    if (group.keyRange.low != 60
        || group.keyRange.high != 60
        || group.velocityRange.low != 10
        || group.velocityRange.high != 120
        || std::abs(group.pan - (-0.5f)) > 1.0e-6f)
    {
        return false;
    }

    const auto& zone = program.zones[0];
    if (!(zone.sampleAssetIndex == 0
        && zone.groupIndex == 0
        && zone.keyRange.low == 60
        && zone.keyRange.high == 60
        && zone.velocityRange.low == 10
        && zone.velocityRange.high == 120
        && zone.rootMidiNote == 60
        && zone.sampleStart == 4
        && zone.sampleEndExclusive == 121
        && zone.loopStart == 16
        && zone.loopEndExclusive == 32
        && std::abs(zone.tuneCents - 107.0f) <= 1.0e-6f
        && zone.loopMode == ZoneLoopMode::sustain))
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, 128, 2);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);
    engine.setProgram(program, result.sampleDataByAsset);

    juce::AudioBuffer<float> block(2, 128);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    engine.render(block, midi);

    auto energy = 0.0f;
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const auto* data = block.getReadPointer(channel);
        for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); ++sampleIndex)
            energy += std::abs(data[sampleIndex]);
    }

    return engine.activeVoiceCount() == 1 && energy > 0.01f;
}

bool runSfzImportRoundRobinPlaybackTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_rr_import_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("rr.sfz");
    if (!sfzFile.replaceWithText(
            "<group> key=60\n"
            "<region> sample=Tone.wav seq_position=2\n"
            "<region> sample=Tone.wav seq_position=1\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.zones.size() != 2
        || result.sampleDataByAsset.size() != 1
        || result.program.zones[0].roundRobinPosition != 2
        || result.program.zones[1].roundRobinPosition != 1
        || result.program.zones[0].roundRobinGroup <= 0
        || result.program.zones[0].roundRobinGroup != result.program.zones[1].roundRobinGroup)
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);
    engine.setProgram(result.program, result.sampleDataByAsset);

    auto triggerAndReadZone = [&]()
    {
        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        auto selectedZone = -1;
        const auto states = engine.getVoicePlaybackStates();
        for (const auto& state : states)
        {
            if (state.active)
            {
                selectedZone = state.zoneIndex;
                break;
            }
        }

        engine.panic();
        return selectedZone;
    };

    return triggerAndReadZone() == 1
        && triggerAndReadZone() == 0
        && triggerAndReadZone() == 1;
}

bool runSfzImportSeqLengthPlaybackTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 44100.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_seq_length_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("seq_length.sfz");
    if (!sfzFile.replaceWithText(
            "<group> key=60 seq_length=4\n"
            "<region> sample=Tone.wav seq_position=1\n"
            "<region> sample=Tone.wav seq_position=3\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.zones.size() != 2
        || result.program.zones[0].roundRobinLength != 4
        || result.program.zones[1].roundRobinLength != 4)
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);
    engine.setProgram(result.program, result.sampleDataByAsset);

    auto triggerAndReadZone = [&]()
    {
        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        auto selectedZone = -1;
        const auto states = engine.getVoicePlaybackStates();
        for (const auto& state : states)
        {
            if (state.active)
            {
                selectedZone = state.zoneIndex;
                break;
            }
        }

        engine.panic();
        return selectedZone;
    };

    return triggerAndReadZone() == 0
        && triggerAndReadZone() == -1
        && triggerAndReadZone() == 1
        && triggerAndReadZone() == -1
        && triggerAndReadZone() == 0;
}

bool runSfzImportReleaseTriggerPlaybackTest()
{
    using namespace audiocity::engine;

    constexpr double sampleRate = 44100.0;
    constexpr int sampleLength = 512;
    constexpr int blockSize = 64;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_release_trigger_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("Tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(sampleFile.createOutputStream());
        if (output == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
        {
            tempDirectory.deleteRecursively();
            return false;
        }

        output.release();
        const auto sample = createTestSample(sampleLength);
        if (!writer->writeFromAudioSampleBuffer(sample, 0, sampleLength))
        {
            tempDirectory.deleteRecursively();
            return false;
        }
    }

    const auto sfzFile = tempDirectory.getChildFile("release.sfz");
    if (!sfzFile.replaceWithText(
            "<group> key=60\n"
            "<region> sample=Tone.wav trigger=release\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto result = importer.importFile(sfzFile);
    tempDirectory.deleteRecursively();

    if (result.hasErrors()
        || result.program.zones.size() != 1
        || result.program.zones.front().triggerMode != ZoneTriggerMode::release)
    {
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, 2);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);
    engine.setProgram(result.program, result.sampleDataByAsset);

    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);
    if (engine.activeVoiceCount() != 0)
        return false;

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);
    return engine.activeVoiceCount() == 1 && engine.isNoteActive(60);
}

bool runSfzImporterDiagnosticsTest()
{
    using namespace audiocity::engine;

    auto hasDiagnostic = [](const std::vector<SfzDiagnostic>& diagnostics,
                            const SfzDiagnostic::Severity severity,
                            const std::string& text)
    {
        for (const auto& diagnostic : diagnostics)
        {
            if (diagnostic.severity == severity
                && diagnostic.message.find(text) != std::string::npos)
            {
                return true;
            }
        }

        return false;
    };

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_sfz_diagnostics_test", "");

    if (!tempDirectory.createDirectory())
        return false;

    const auto cycleA = tempDirectory.getChildFile("cycle_a.sfz");
    const auto cycleB = tempDirectory.getChildFile("cycle_b.sfz");
    const auto missingSample = tempDirectory.getChildFile("missing_sample.sfz");

    if (!cycleA.replaceWithText("#include \"cycle_b.sfz\"\n")
        || !cycleB.replaceWithText("#include \"cycle_a.sfz\"\n")
        || !missingSample.replaceWithText(
            "<region> sample=DoesNotExist.wav trigger=legato loop_mode=spin_cycle seq_length=4 unsupported_opcode=12\n"))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    SfzImporter importer;
    const auto cycleResult = importer.importFile(cycleA);
    const auto missingResult = importer.importFile(missingSample);
    tempDirectory.deleteRecursively();

    if (!cycleResult.hasErrors()
        || !hasDiagnostic(cycleResult.diagnostics, SfzDiagnostic::Severity::error, "cycle"))
    {
        return false;
    }

    if (!missingResult.hasErrors()
        || !missingResult.program.zones.empty()
        || !hasDiagnostic(missingResult.diagnostics, SfzDiagnostic::Severity::warning, "Unknown SFZ opcode")
        || !hasDiagnostic(missingResult.diagnostics, SfzDiagnostic::Severity::warning, "Unsupported SFZ trigger value")
        || !hasDiagnostic(missingResult.diagnostics, SfzDiagnostic::Severity::warning, "Unsupported SFZ loop_mode value")
        || !hasDiagnostic(missingResult.diagnostics, SfzDiagnostic::Severity::error, "Missing SFZ sample"))
    {
        return false;
    }

    return true;
}

bool runVoiceStealingEdgeCaseTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.resetStealCount();

    juce::MidiBuffer midi;

    for (int index = 0; index <= static_cast<int>(audiocity::engine::VoicePool::maxVoices); ++index)
        midi.addEvent(juce::MidiMessage::noteOn(1, 36 + index, 1.0f), 0);

    juce::AudioBuffer<float> block(channels, blockSize);
    engine.render(block, midi);

    if (engine.activeVoiceCount() != static_cast<int>(audiocity::engine::VoicePool::maxVoices))
        return false;

    if (engine.stealCount() != 1)
        return false;

    return !engine.isNoteActive(36) && engine.isNoteActive(36 + static_cast<int>(audiocity::engine::VoicePool::maxVoices));
}

bool runPolyphonyLimitControlTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

    audiocity::engine::EngineCore::AdsrSettings sustain;
    sustain.attackSeconds = 0.0001f;
    sustain.decaySeconds = 0.0001f;
    sustain.sustainLevel = 1.0f;
    sustain.releaseSeconds = 0.5f;
    engine.setAmpEnvelope(sustain);

    engine.setPolyphonyLimit(1);

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 1);
        engine.render(block, midi);
    }

    if (engine.activeVoiceCount() != 1 || !engine.isNoteActive(64))
        return false;

    engine.panic();
    engine.setPolyphonyLimit(3);

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, 1.0f), 1);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 2);
        midi.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 3);
        engine.render(block, midi);
    }

    return engine.activeVoiceCount() == 3;
}

float blockEnergy(const juce::AudioBuffer<float>& block)
{
    float energy = 0.0f;

    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const auto* data = block.getReadPointer(channel);
        for (int i = 0; i < block.getNumSamples(); ++i)
            energy += std::abs(data[i]);
    }

    return energy;
}

float channelEnergy(const juce::AudioBuffer<float>& block, const int channel)
{
    if (channel < 0 || channel >= block.getNumChannels())
        return 0.0f;

    float energy = 0.0f;
    const auto* data = block.getReadPointer(channel);
    for (int sampleIndex = 0; sampleIndex < block.getNumSamples(); ++sampleIndex)
        energy += std::abs(data[sampleIndex]);

    return energy;
}

bool runSameOffsetMidiEventOrderTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;

    auto renderEnergy = [](const bool noteOffFirst)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(4096), sampleRate, 60);

        audiocity::engine::EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.001f;
        engine.setAmpEnvelope(amp);

        if (noteOffFirst)
        {
            engine.noteOff(60, 0);
            engine.noteOn(60, 1.0f, 0);
        }
        else
        {
            engine.noteOn(60, 1.0f, 0);
            engine.noteOff(60, 0);
        }

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        engine.render(block, midi);
        return blockEnergy(block);
    };

    const auto releasedImmediately = renderEnergy(false);
    const auto startedAfterIgnoredRelease = renderEnergy(true);

    return releasedImmediately < 1.0e-6f && startedAfterIgnoredRelease > 0.01f;
}

bool runEngineProgramSnapshotZoneSelectionTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(8192), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    Program program;
    SampleAsset sample;
    sample.lengthSamples = 8192;
    sample.numChannels = 1;
    sample.sampleRateHz = sampleRate;
    sample.rootMidiNote = 72;
    program.sampleAssets.push_back(sample);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(72);
    zone.velocityRange = VelocityRange::full();
    zone.rootMidiNote = 72;
    program.zones.push_back(zone);
    engine.setProgram(program);

    if (!engine.hasProgram() || engine.getProgramZoneCount() != 1)
        return false;

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        if (engine.activeVoiceCount() != 0 || blockEnergy(block) > 1.0e-6f)
            return false;
    }

    int activeSampleIndex = -1;
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
        engine.render(block, midi);

        if (engine.activeVoiceCount() != 1 || blockEnergy(block) <= 0.01f)
            return false;

        const auto states = engine.getVoicePlaybackStates();
        for (const auto& state : states)
        {
            if (state.active)
            {
                activeSampleIndex = state.sampleIndex;
                break;
            }
        }
    }

    if (activeSampleIndex < 32 || activeSampleIndex > 96)
        return false;

    engine.panic();
    engine.clearProgram();
    if (engine.hasProgram())
        return false;

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    return engine.activeVoiceCount() == 1 && blockEnergy(block) > 0.01f;
}

bool runEngineProgramSampleAssetBindingTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    Program program;

    SampleAsset silentAsset;
    silentAsset.lengthSamples = 4096;
    silentAsset.numChannels = 1;
    silentAsset.sampleRateHz = sampleRate;
    silentAsset.rootMidiNote = 60;
    program.sampleAssets.push_back(silentAsset);

    SampleAsset toneAsset;
    toneAsset.lengthSamples = 4096;
    toneAsset.numChannels = 1;
    toneAsset.sampleRateHz = sampleRate;
    toneAsset.rootMidiNote = 72;
    program.sampleAssets.push_back(toneAsset);

    Zone silentZone;
    silentZone.sampleAssetIndex = 0;
    silentZone.keyRange = MidiRange::single(60);
    silentZone.velocityRange = VelocityRange::full();
    silentZone.rootMidiNote = 60;
    program.zones.push_back(silentZone);

    Zone toneZone;
    toneZone.sampleAssetIndex = 1;
    toneZone.keyRange = MidiRange::single(72);
    toneZone.velocityRange = VelocityRange::full();
    toneZone.rootMidiNote = 72;
    program.zones.push_back(toneZone);

    juce::AudioBuffer<float> silentSample(1, 4096);
    silentSample.clear();
    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(silentSample);
    samples.push_back(createTestSample(4096));
    engine.setProgram(program, samples);

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        if (engine.activeVoiceCount() != 1 || blockEnergy(block) > 1.0e-6f)
            return false;

        const auto states = engine.getVoicePlaybackStates();
        bool foundSilentZone = false;
        for (const auto& state : states)
            foundSilentZone = foundSilentZone || (state.active && state.zoneIndex == 0 && state.sampleAssetIndex == 0);

        if (!foundSilentZone)
            return false;
    }

    engine.panic();

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
        engine.render(block, midi);

        if (engine.activeVoiceCount() != 1 || blockEnergy(block) <= 0.01f)
            return false;

        const auto states = engine.getVoicePlaybackStates();
        bool foundToneZone = false;
        for (const auto& state : states)
            foundToneZone = foundToneZone || (state.active && state.zoneIndex == 1 && state.sampleAssetIndex == 1);

        if (!foundToneZone)
            return false;
    }

    return true;
}

bool runEngineProgramStereoAssetPlaybackTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    EngineCore::FilterSettings openFilter;
    openFilter.baseCutoffHz = 20000.0f;
    openFilter.envAmountHz = 0.0f;
    openFilter.resonance = 0.0f;
    engine.setFilterSettings(openFilter);

    EngineCore::DcFilterSettings dc;
    dc.enabled = false;
    engine.setDcFilterSettings(dc);

    Program program;
    SampleAsset asset;
    asset.lengthSamples = sampleLength;
    asset.numChannels = 2;
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(60);
    zone.velocityRange = VelocityRange::full();
    zone.rootMidiNote = 60;
    program.zones.push_back(zone);

    juce::AudioBuffer<float> stereoSample(2, sampleLength);
    stereoSample.clear();
    const auto leftTone = createTestSample(sampleLength);
    stereoSample.copyFrom(0, 0, leftTone, 0, 0, sampleLength);

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(stereoSample);
    engine.setProgram(program, samples);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    const auto leftEnergy = channelEnergy(block, 0);
    const auto rightEnergy = channelEnergy(block, 1);

    return leftEnergy > 0.01f && rightEnergy < leftEnergy * 0.05f;
}

bool runEngineProgramRoundRobinZoneSelectionTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    Program program;

    for (int index = 0; index < 3; ++index)
    {
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);
    }

    Group group;
    group.keyRange = MidiRange::single(60);
    group.velocityRange = VelocityRange::full();
    group.roundRobinGroup = 11;
    program.groups.push_back(group);

    Zone secondPosition;
    secondPosition.sampleAssetIndex = 0;
    secondPosition.groupIndex = 0;
    secondPosition.keyRange = MidiRange::single(60);
    secondPosition.velocityRange = VelocityRange::full();
    secondPosition.rootMidiNote = 60;
    secondPosition.roundRobinPosition = 2;
    program.zones.push_back(secondPosition);

    Zone firstPosition = secondPosition;
    firstPosition.sampleAssetIndex = 1;
    firstPosition.roundRobinPosition = 1;
    program.zones.push_back(firstPosition);

    Zone thirdPosition = secondPosition;
    thirdPosition.sampleAssetIndex = 2;
    thirdPosition.roundRobinPosition = 3;
    program.zones.push_back(thirdPosition);

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(createTestSample(sampleLength));
    samples.push_back(createTestSample(sampleLength));
    samples.push_back(createTestSample(sampleLength));
    engine.setProgram(program, samples);

    auto triggerAndReadZone = [&]()
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        const auto states = engine.getVoicePlaybackStates();
        auto zoneIndex = -1;
        for (const auto& state : states)
        {
            if (state.active)
            {
                zoneIndex = state.zoneIndex;
                break;
            }
        }

        engine.panic();
        return zoneIndex;
    };

    if (triggerAndReadZone() != 1)
        return false;

    if (triggerAndReadZone() != 0)
        return false;

    if (triggerAndReadZone() != 2)
        return false;

    if (triggerAndReadZone() != 1)
        return false;

    engine.setProgram(program, samples);
    return triggerAndReadZone() == 1;
}

bool runEngineProgramLayeredZonePlaybackTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    Program program;
    for (int index = 0; index < 2; ++index)
    {
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);
    }

    for (int zoneIndex = 0; zoneIndex < 2; ++zoneIndex)
    {
        Zone zone;
        zone.sampleAssetIndex = zoneIndex;
        zone.keyRange = MidiRange::single(60);
        zone.velocityRange = VelocityRange::full();
        zone.rootMidiNote = 60;
        program.zones.push_back(zone);
    }

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(createTestSample(sampleLength));
    samples.push_back(createTestSample(sampleLength));
    engine.setProgram(program, samples);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    if (engine.activeVoiceCount() != 2)
        return false;

    std::array<bool, 2> activeZones{};
    const auto states = engine.getVoicePlaybackStates();
    for (const auto& state : states)
    {
        if (state.active && state.zoneIndex >= 0 && state.zoneIndex < 2)
            activeZones[static_cast<std::size_t>(state.zoneIndex)] = true;
    }

    if (!activeZones[0] || !activeZones[1])
        return false;

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);

    midi.clear();
    for (int blockIndex = 0; blockIndex < 12; ++blockIndex)
        engine.render(block, midi);

    return engine.activeVoiceCount() == 0;
}

bool runEngineProgramVelocityFadeInTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    auto renderVelocity = [](const float velocity)
    {
        EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

        EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.001f;
        engine.setAmpEnvelope(amp);

        Program program;
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);

        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.keyRange = MidiRange::single(60);
        zone.velocityRange = VelocityRange::full();
        zone.velocityFadeIn = VelocityFadeRange::fromUnordered(32, 96);
        zone.rootMidiNote = 60;
        program.zones.push_back(zone);

        std::vector<juce::AudioBuffer<float>> samples;
        samples.push_back(createTestSample(sampleLength));
        engine.setProgram(program, samples);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, velocity), 0);
        engine.render(block, midi);
        return blockEnergy(block);
    };

    const auto lowVelocityEnergy = renderVelocity(0.25f);
    const auto midVelocityEnergy = renderVelocity(0.50f);
    const auto highVelocityEnergy = renderVelocity(1.0f);

    return lowVelocityEnergy < highVelocityEnergy * 0.05f
        && midVelocityEnergy > highVelocityEnergy * 0.25f
        && midVelocityEnergy < highVelocityEnergy * 0.75f;
}

bool runEngineProgramCycleRandomRoundRobinZoneSelectionTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    auto renderSequence = []()
    {
        EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

        EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.001f;
        engine.setAmpEnvelope(amp);

        Program program;
        for (int index = 0; index < 3; ++index)
        {
            SampleAsset asset;
            asset.lengthSamples = sampleLength;
            asset.numChannels = 1;
            asset.sampleRateHz = sampleRate;
            asset.rootMidiNote = 60;
            program.sampleAssets.push_back(asset);
        }

        Group group;
        group.keyRange = MidiRange::single(60);
        group.velocityRange = VelocityRange::full();
        group.roundRobinGroup = 17;
        group.roundRobinMode = RoundRobinMode::cycleRandom;
        program.groups.push_back(group);

        for (int zoneIndex = 0; zoneIndex < 3; ++zoneIndex)
        {
            Zone zone;
            zone.sampleAssetIndex = zoneIndex;
            zone.groupIndex = 0;
            zone.keyRange = MidiRange::single(60);
            zone.velocityRange = VelocityRange::full();
            zone.rootMidiNote = 60;
            zone.roundRobinPosition = zoneIndex + 1;
            program.zones.push_back(zone);
        }

        std::vector<juce::AudioBuffer<float>> samples;
        samples.push_back(createTestSample(sampleLength));
        samples.push_back(createTestSample(sampleLength));
        samples.push_back(createTestSample(sampleLength));
        engine.setProgram(program, samples);

        std::array<int, 6> sequence{};
        for (auto& selectedZone : sequence)
        {
            juce::AudioBuffer<float> block(channels, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            engine.render(block, midi);

            selectedZone = -1;
            const auto states = engine.getVoicePlaybackStates();
            for (const auto& state : states)
            {
                if (state.active)
                {
                    selectedZone = state.zoneIndex;
                    break;
                }
            }

            engine.panic();
        }

        return sequence;
    };

    const auto first = renderSequence();
    const auto second = renderSequence();
    if (first != second)
        return false;

    for (std::size_t index = 1; index < first.size(); ++index)
    {
        if (first[index] == first[index - 1])
            return false;
    }

    for (std::size_t cycleStart = 0; cycleStart < first.size(); cycleStart += 3)
    {
        std::array<bool, 3> seen{};
        for (std::size_t offset = 0; offset < 3; ++offset)
        {
            const auto zoneIndex = first[cycleStart + offset];
            if (zoneIndex < 0 || zoneIndex >= 3)
                return false;

            seen[static_cast<std::size_t>(zoneIndex)] = true;
        }

        if (!seen[0] || !seen[1] || !seen[2])
            return false;
    }

    return true;
}

bool runEngineProgramChokeGroupTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.5f;
    engine.setAmpEnvelope(amp);

    Program program;

    for (int index = 0; index < 2; ++index)
    {
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60 + (index * 2);
        program.sampleAssets.push_back(asset);
    }

    Group chokeGroup;
    chokeGroup.keyRange = MidiRange::fromUnordered(60, 62);
    chokeGroup.velocityRange = VelocityRange::full();
    chokeGroup.chokeGroup = 9;
    program.groups.push_back(chokeGroup);

    Zone firstZone;
    firstZone.sampleAssetIndex = 0;
    firstZone.groupIndex = 0;
    firstZone.keyRange = MidiRange::single(60);
    firstZone.velocityRange = VelocityRange::full();
    firstZone.rootMidiNote = 60;
    program.zones.push_back(firstZone);

    Zone secondZone = firstZone;
    secondZone.sampleAssetIndex = 1;
    secondZone.keyRange = MidiRange::single(62);
    secondZone.rootMidiNote = 62;
    program.zones.push_back(secondZone);

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(createTestSample(sampleLength));
    samples.push_back(createTestSample(sampleLength));
    engine.setProgram(program, samples);

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
    }

    if (engine.activeVoiceCount() != 1 || !engine.isNoteActive(60))
        return false;

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, 1.0f), 0);
        engine.render(block, midi);
    }

    return engine.activeVoiceCount() == 1
        && !engine.isNoteActive(60)
        && engine.isNoteActive(62);
}

bool runEngineProgramZoneAndGroupPanTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    EngineCore::FilterSettings openFilter;
    openFilter.baseCutoffHz = 20000.0f;
    openFilter.envAmountHz = 0.0f;
    openFilter.resonance = 0.0f;
    engine.setFilterSettings(openFilter);

    EngineCore::DcFilterSettings dc;
    dc.enabled = false;
    engine.setDcFilterSettings(dc);

    Program program;
    SampleAsset asset;
    asset.lengthSamples = sampleLength;
    asset.numChannels = 1;
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Group leftGroup;
    leftGroup.keyRange = MidiRange::single(60);
    leftGroup.velocityRange = VelocityRange::full();
    leftGroup.pan = -1.0f;
    program.groups.push_back(leftGroup);

    Group partialRightGroup;
    partialRightGroup.keyRange = MidiRange::single(62);
    partialRightGroup.velocityRange = VelocityRange::full();
    partialRightGroup.pan = 0.25f;
    program.groups.push_back(partialRightGroup);

    Zone leftZone;
    leftZone.sampleAssetIndex = 0;
    leftZone.groupIndex = 0;
    leftZone.keyRange = MidiRange::single(60);
    leftZone.velocityRange = VelocityRange::full();
    leftZone.rootMidiNote = 60;
    program.zones.push_back(leftZone);

    Zone rightZone = leftZone;
    rightZone.groupIndex = 1;
    rightZone.keyRange = MidiRange::single(62);
    rightZone.rootMidiNote = 62;
    rightZone.pan = 0.75f;
    program.zones.push_back(rightZone);

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(createTestSample(sampleLength));
    engine.setProgram(program, samples);

    auto renderNote = [&](const int note)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
        engine.render(block, midi);
        engine.panic();
        return std::pair<float, float>{ channelEnergy(block, 0), channelEnergy(block, 1) };
    };

    const auto [leftNoteLeftEnergy, leftNoteRightEnergy] = renderNote(60);
    if (leftNoteLeftEnergy <= 0.01f || leftNoteRightEnergy >= leftNoteLeftEnergy * 0.05f)
        return false;

    const auto [rightNoteLeftEnergy, rightNoteRightEnergy] = renderNote(62);
    return rightNoteRightEnergy > 0.01f && rightNoteLeftEnergy < rightNoteRightEnergy * 0.05f;
}

bool runEngineProgramZoneTriggerModeTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    auto configureEngine = [](EngineCore& engine,
                              const ZoneTriggerMode groupTriggerMode,
                              const ZoneTriggerMode zoneTriggerMode)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

        EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.005f;
        engine.setAmpEnvelope(amp);

        Program program;
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);

        Group group;
        group.keyRange = MidiRange::single(60);
        group.velocityRange = VelocityRange::full();
        group.triggerMode = groupTriggerMode;
        program.groups.push_back(group);

        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.groupIndex = 0;
        zone.keyRange = MidiRange::single(60);
        zone.velocityRange = VelocityRange::full();
        zone.rootMidiNote = 60;
        zone.triggerMode = zoneTriggerMode;
        program.zones.push_back(zone);

        std::vector<juce::AudioBuffer<float>> samples;
        samples.push_back(createTestSample(sampleLength));
        engine.setProgram(program, samples);
    };

    auto renderNoteOnAndOff = [](EngineCore& engine, const int releaseBlocks)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        midi.clear();
        for (int blockIndex = 0; blockIndex < releaseBlocks; ++blockIndex)
            engine.render(block, midi);
    };

    auto renderNoteOnOnly = [](EngineCore& engine)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
    };

    auto renderNoteOffOnly = [](EngineCore& engine)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);
    };

    EngineCore zoneOneShot;
    configureEngine(zoneOneShot, ZoneTriggerMode::gate, ZoneTriggerMode::oneShot);
    renderNoteOnAndOff(zoneOneShot, 1);
    if (zoneOneShot.activeVoiceCount() != 1 || !zoneOneShot.isNoteActive(60))
        return false;

    EngineCore groupOneShot;
    configureEngine(groupOneShot, ZoneTriggerMode::oneShot, ZoneTriggerMode::gate);
    renderNoteOnAndOff(groupOneShot, 1);
    if (groupOneShot.activeVoiceCount() != 1 || !groupOneShot.isNoteActive(60))
        return false;

    EngineCore gate;
    configureEngine(gate, ZoneTriggerMode::gate, ZoneTriggerMode::gate);
    renderNoteOnAndOff(gate, 32);

    if (gate.activeVoiceCount() != 0)
        return false;

    EngineCore zoneRelease;
    configureEngine(zoneRelease, ZoneTriggerMode::gate, ZoneTriggerMode::release);
    renderNoteOnOnly(zoneRelease);
    if (zoneRelease.activeVoiceCount() != 0)
        return false;
    renderNoteOffOnly(zoneRelease);
    if (zoneRelease.activeVoiceCount() != 1 || !zoneRelease.isNoteActive(60))
        return false;

    EngineCore groupRelease;
    configureEngine(groupRelease, ZoneTriggerMode::release, ZoneTriggerMode::gate);
    renderNoteOnOnly(groupRelease);
    if (groupRelease.activeVoiceCount() != 0)
        return false;
    renderNoteOffOnly(groupRelease);
    return groupRelease.activeVoiceCount() == 1 && groupRelease.isNoteActive(60);
}

bool runEngineProgramZoneGainAndTuneTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;

    struct RenderResult
    {
        float energy = 0.0f;
        int sampleIndex = -1;
    };

    auto renderZone = [](const float gainDb, const float tuneCents) -> RenderResult
    {
        EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(4096), sampleRate, 60);

        EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.001f;
        engine.setAmpEnvelope(amp);

        Program program;
        SampleAsset asset;
        asset.lengthSamples = 4096;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);

        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.keyRange = MidiRange::single(60);
        zone.velocityRange = VelocityRange::full();
        zone.rootMidiNote = 60;
        zone.gainDb = gainDb;
        zone.tuneCents = tuneCents;
        program.zones.push_back(zone);

        std::vector<juce::AudioBuffer<float>> samples;
        samples.push_back(createTestSample(4096));
        engine.setProgram(program, samples);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        RenderResult result;
        result.energy = blockEnergy(block);

        const auto states = engine.getVoicePlaybackStates();
        for (const auto& state : states)
        {
            if (state.active)
            {
                result.sampleIndex = state.sampleIndex;
                break;
            }
        }

        return result;
    };

    const auto neutral = renderZone(0.0f, 0.0f);
    const auto quiet = renderZone(-12.0f, 0.0f);
    const auto tunedUp = renderZone(0.0f, 1200.0f);

    if (!(neutral.energy > 0.01f))
        return false;

    if (!(quiet.energy < neutral.energy * 0.4f))
        return false;

    return tunedUp.sampleIndex > neutral.sampleIndex + 32;
}

bool runEngineProgramZoneSampleWindowTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int toneStart = 512;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    Program program;
    SampleAsset asset;
    asset.lengthSamples = 4096;
    asset.numChannels = 1;
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(60);
    zone.velocityRange = VelocityRange::full();
    zone.rootMidiNote = 60;
    zone.sampleStart = toneStart;
    zone.sampleEndExclusive = 4096;
    program.zones.push_back(zone);

    juce::AudioBuffer<float> sample(1, 4096);
    sample.clear();
    const auto tone = createTestSample(4096 - toneStart);
    sample.copyFrom(0, toneStart, tone, 0, 0, tone.getNumSamples());

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(sample);
    engine.setProgram(program, samples);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    if (blockEnergy(block) <= 0.01f)
        return false;

    const auto states = engine.getVoicePlaybackStates();
    for (const auto& state : states)
    {
        if (state.active)
            return state.sampleIndex >= toneStart;
    }

    return false;
}

bool runEngineProgramZoneLoopModeTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 128;
    constexpr int zoneStart = 32;
    constexpr int zoneEnd = 96;
    constexpr int loopStart = 48;
    constexpr int loopEnd = 64;

    auto renderTailEnergy = [](const ZoneLoopMode loopMode, const bool releaseBeforeTail)
    {
        EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createTestSample(sampleLength), sampleRate, 60);

        EngineCore::AdsrSettings amp;
        amp.attackSeconds = 0.0001f;
        amp.decaySeconds = 0.001f;
        amp.sustainLevel = 1.0f;
        amp.releaseSeconds = 0.5f;
        engine.setAmpEnvelope(amp);

        EngineCore::FilterSettings openFilter;
        openFilter.baseCutoffHz = 20000.0f;
        openFilter.envAmountHz = 0.0f;
        openFilter.resonance = 0.0f;
        engine.setFilterSettings(openFilter);

        EngineCore::DcFilterSettings dc;
        dc.enabled = false;
        engine.setDcFilterSettings(dc);

        Program program;
        SampleAsset asset;
        asset.lengthSamples = sampleLength;
        asset.numChannels = 1;
        asset.sampleRateHz = sampleRate;
        asset.rootMidiNote = 60;
        program.sampleAssets.push_back(asset);

        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.keyRange = MidiRange::single(60);
        zone.velocityRange = VelocityRange::full();
        zone.rootMidiNote = 60;
        zone.sampleStart = zoneStart;
        zone.sampleEndExclusive = zoneEnd;
        zone.loopStart = loopStart;
        zone.loopEndExclusive = loopEnd;
        zone.loopMode = loopMode;
        program.zones.push_back(zone);

        juce::AudioBuffer<float> sample(1, sampleLength);
        sample.clear();
        for (int index = loopStart; index < loopEnd; ++index)
            sample.setSample(0, index, 0.8f);

        std::vector<juce::AudioBuffer<float>> samples;
        samples.push_back(sample);
        engine.setProgram(program, samples);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        engine.render(block, midi);

        if (releaseBeforeTail)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            engine.render(block, midi);
            midi.clear();
        }

        engine.render(block, midi);
        return blockEnergy(block);
    };

    const auto noLoopHeld = renderTailEnergy(ZoneLoopMode::noLoop, false);
    const auto sustainHeld = renderTailEnergy(ZoneLoopMode::sustain, false);
    const auto sustainReleased = renderTailEnergy(ZoneLoopMode::sustain, true);
    const auto continuousReleased = renderTailEnergy(ZoneLoopMode::continuous, true);

    if (noLoopHeld > 0.1f)
        return false;

    if (sustainHeld < 50.0f)
        return false;

    return continuousReleased > 50.0f && sustainReleased < continuousReleased * 0.5f;
}

juce::AudioBuffer<float> createOneCycleSine(const int sampleCount)
{
    juce::AudioBuffer<float> buffer(1, sampleCount);
    for (int i = 0; i < sampleCount; ++i)
    {
        const auto phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi
            * static_cast<double>(i) / static_cast<double>(juce::jmax(1, sampleCount)));
        buffer.setSample(0, i, std::sin(phase));
    }

    return buffer;
}

juce::AudioBuffer<float> renderHeldNote(audiocity::engine::EngineCore& engine,
                                        const int midiNote,
                                        const int blockSize,
                                        const int blocks,
                                        const int channels)
{
    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 1.0f), 0);

        engine.render(blockBuffer, midi);
        for (int ch = 0; ch < channels; ++ch)
            output.copyFrom(ch, block * blockSize, blockBuffer, ch, 0, blockSize);
    }

    return output;
}

float estimateFrequencyFromPositiveCrossings(const juce::AudioBuffer<float>& audio,
                                             const double sampleRate,
                                             const int skipSamples)
{
    if (audio.getNumChannels() <= 0)
        return 0.0f;

    const auto* data = audio.getReadPointer(0);
    const auto total = audio.getNumSamples();
    const auto start = juce::jlimit(1, juce::jmax(1, total - 1), skipSamples);
    int crossings = 0;

    for (int i = start; i < total; ++i)
    {
        const auto previous = data[i - 1];
        const auto current = data[i];
        if (previous <= 0.0f && current > 0.0f)
            ++crossings;
    }

    const auto measuredSamples = juce::jmax(1, total - start);
    const auto seconds = static_cast<float>(measuredSamples / sampleRate);
    return seconds > 0.0f ? static_cast<float>(crossings) / seconds : 0.0f;
}

bool runGeneratedCyclePitchInvariantAcrossSampleCountsTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr int blocks = 64;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 36;

    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);

    auto renderFrequency = [&](const int cycleSamples) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings fastSustain;
        fastSustain.attackSeconds = 0.0001f;
        fastSustain.decaySeconds = 0.0001f;
        fastSustain.sustainLevel = 1.0f;
        fastSustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(fastSustain);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);
        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);

        const auto rendered = renderHeldNote(engine, rootMidiNote, blockSize, blocks, channels);
        return estimateFrequencyFromPositiveCrossings(rendered, outputSampleRate, blockSize * 2);
    };

    const auto freq64 = renderFrequency(64);
    const auto freq1024 = renderFrequency(1024);
    const auto freq8192 = renderFrequency(8192);

    if (freq64 <= 0.0f || freq1024 <= 0.0f || freq8192 <= 0.0f)
        return false;

    const auto targetError64 = std::abs(freq64 - static_cast<float>(targetHz));
    const auto targetError1024 = std::abs(freq1024 - static_cast<float>(targetHz));
    const auto targetError8192 = std::abs(freq8192 - static_cast<float>(targetHz));
    const auto crossCountDelta = std::abs(freq64 - freq1024);
    const auto crossCountDeltaHigh = std::abs(freq1024 - freq8192);

    return targetError64 < 1.5f
        && targetError1024 < 1.5f
        && targetError8192 < 1.5f
        && crossCountDelta < 0.8f
        && crossCountDeltaHigh < 0.8f;
}

bool runDisplayMinMaxPreservesPolarityTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> shaped(1, 16);
    for (int i = 0; i < 8; ++i)
        shaped.setSample(0, i, -0.95f + static_cast<float>(i) * 0.05f);
    for (int i = 8; i < 16; ++i)
        shaped.setSample(0, i, 0.15f + static_cast<float>(i - 8) * 0.08f);

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(shaped, sampleRate, 60);

    const auto minMax = engine.buildDisplayMinMaxByChannel(4);
    if (minMax.size() != 1 || minMax.front().size() != 4)
        return false;

    const auto& buckets = minMax.front();
    const bool hasNegativeOnlyBucket = buckets[0].maxValue < 0.0f && buckets[1].maxValue < 0.0f;
    const bool hasPositiveOnlyBucket = buckets[2].minValue > 0.0f && buckets[3].minValue > 0.0f;

    return hasNegativeOnlyBucket && hasPositiveOnlyBucket;
}

bool runLoadedSampleMetadataForGeneratedDataTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    juce::AudioBuffer<float> generated(2, 512);
    generated.clear();
    for (int i = 0; i < generated.getNumSamples(); ++i)
    {
        generated.setSample(0, i, std::sin(static_cast<float>(i) * 0.05f));
        generated.setSample(1, i, std::cos(static_cast<float>(i) * 0.05f));
    }

    engine.setSampleData(generated, 32000.0, 60);

    return engine.getLoadedSampleLength() == 512
        && engine.getLoadedSampleChannels() == 2
        && std::abs(engine.getLoadedSampleRateHz() - 32000.0) < 0.01
        && engine.getLoadedSampleBitDepth() < 0;
}

bool runParameterIdSafetyTest()
{
    constexpr int kAaxSafeParamIdLength = 31;

    const auto processorFile = fixtureFile("src/plugin/PluginProcessor.cpp");
    if (!processorFile.existsAsFile())
        return false;

    juce::StringArray lines;
    lines.addLines(processorFile.loadFileAsString());

    std::set<juce::String> trimmedIds;
    int paramIdCount = 0;

    for (const auto& rawLine : lines)
    {
        const auto line = rawLine.trim();
        if (!line.startsWith("constexpr auto kParam"))
            continue;

        const auto firstQuote = line.indexOfChar('"');
        if (firstQuote < 0)
            return false;

        const auto secondQuote = line.indexOfChar(firstQuote + 1, '"');
        if (secondQuote <= firstQuote)
            return false;

        const auto paramId = line.substring(firstQuote + 1, secondQuote);
        ++paramIdCount;

        if (paramId.length() > kAaxSafeParamIdLength)
            return false;

        if (!trimmedIds.insert(paramId.substring(0, kAaxSafeParamIdLength)).second)
            return false;
    }

    return paramIdCount > 0;
}

bool runPlaybackModesTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore::AdsrSettings fastAdsr;
    fastAdsr.attackSeconds = 0.0001f;
    fastAdsr.decaySeconds = 0.001f;
    fastAdsr.sustainLevel = 1.0f;
    fastAdsr.releaseSeconds = 0.005f;

    {
        audiocity::engine::EngineCore gate;
        gate.prepare(sampleRate, blockSize, channels);
        gate.setAmpEnvelope(fastAdsr);
        gate.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);
        gate.setSampleData(createTestSample(4096), sampleRate, 60);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        gate.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        gate.render(block, midi);

        midi.clear();
        for (int i = 0; i < 12; ++i)
            gate.render(block, midi);

        if (gate.activeVoiceCount() != 0)
            return false;
    }

    {
        audiocity::engine::EngineCore oneShot;
        oneShot.prepare(sampleRate, blockSize, channels);
        oneShot.setAmpEnvelope(fastAdsr);
        oneShot.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        oneShot.setSampleData(createTestSample(4096), sampleRate, 60);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        oneShot.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        oneShot.render(block, midi);

        midi.clear();
        oneShot.render(block, midi);

        if (oneShot.activeVoiceCount() == 0)
            return false;
    }

    // Loop mode: without note off, playback should continue beyond sample length
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setAmpEnvelope(fastAdsr);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setSampleData(createTestSample(128), sampleRate, 60);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        engine.render(block, midi);
        engine.render(block, midi);

        if (blockEnergy(block) < 0.2f)
            return false;
    }

    return true;
}

bool runLoopMarkersTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> shaped(1, 64);
    shaped.clear();
    for (int i = 16; i < 32; ++i)
        shaped.setSample(0, i, 0.9f);

    audiocity::engine::EngineCore::AdsrSettings slowRelease;
    slowRelease.attackSeconds = 0.0001f;
    slowRelease.decaySeconds = 0.001f;
    slowRelease.sustainLevel = 1.0f;
    slowRelease.releaseSeconds = 0.5f;

    // Loop markers should keep playback in the [16, 31] region while note is held.
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setAmpEnvelope(slowRelease);
        engine.setSampleData(shaped, sampleRate, 60);
        engine.setLoopPoints(16, 31);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        engine.render(block, midi);
        engine.render(block, midi);

        if (blockEnergy(block) < 10.0f)
            return false;
    }

    // After note-off, loop should stop and voice should enter release.
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setAmpEnvelope(slowRelease);
        engine.setSampleData(shaped, sampleRate, 60);
        engine.setLoopPoints(16, 31);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        // After note-off the voice should eventually stop
        midi.clear();
        for (int i = 0; i < 200; ++i)
            engine.render(block, midi);

        if (engine.activeVoiceCount() != 0)
            return false;
    }

    return true;
}

float maxConsecutiveDelta(const juce::AudioBuffer<float>& block)
{
    float maxDelta = 0.0f;
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
    {
        const auto* data = block.getReadPointer(channel);
        for (int i = 1; i < block.getNumSamples(); ++i)
            maxDelta = juce::jmax(maxDelta, std::abs(data[i] - data[i - 1]));
    }

    return maxDelta;
}

bool runLoopCrossfadeSmoothsBoundaryTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> stepped(1, 256);
    for (int i = 0; i < stepped.getNumSamples(); ++i)
        stepped.setSample(0, i, i < 128 ? -0.9f : 0.9f);

    auto renderBoundaryDelta = [&](const int crossfadeSamples)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(stepped, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setLoopPoints(32, 220);
        engine.setLoopCrossfadeSamples(crossfadeSamples);

        audiocity::engine::EngineCore::AdsrSettings holdAdsr;
        holdAdsr.attackSeconds = 0.0001f;
        holdAdsr.decaySeconds = 0.001f;
        holdAdsr.sustainLevel = 1.0f;
        holdAdsr.releaseSeconds = 0.5f;
        engine.setAmpEnvelope(holdAdsr);

        audiocity::engine::EngineCore::FilterSettings openFilter;
        openFilter.baseCutoffHz = 20000.0f;
        openFilter.envAmountHz = 0.0f;
        openFilter.resonance = 0.0f;
        engine.setFilterSettings(openFilter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 4; ++i)
            engine.render(block, midi);

        return maxConsecutiveDelta(block);
    };

    const auto noCrossfadeDelta = renderBoundaryDelta(0);
    const auto crossfadedDelta = renderBoundaryDelta(24);

    return crossfadedDelta < (noCrossfadeDelta * 0.85f);
}

bool runPanicSilencesAudioImmediatelyTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    if (blockEnergy(block) <= 0.001f)
        return false;

    engine.panic();

    midi.clear();
    engine.render(block, midi);
    const auto postPanicEnergy = blockEnergy(block);

    return postPanicEnergy <= 1.0e-6f;
}

bool runLoadSampleResetsPlaybackAndLoopRangesTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_load_reset_test", ".wav");

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 440.0 / sampleRate);
            buffer.setSample(0, i, 0.3f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
            return false;
    }

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setSampleWindow(64, 192);
    engine.setLoopPoints(80, 160);

    const auto loaded = engine.loadSampleFromFile(tempFile);
    tempFile.deleteFile();

    if (!loaded)
        return false;

    return engine.getSampleWindowStart() == 0
        && engine.getSampleWindowEnd() == sampleLength - 1
        && engine.getLoopStart() == 0
        && engine.getLoopEnd() == sampleLength - 1;
}

bool runLoadSampleClearsProgramSnapshotTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_load_clears_program_test", ".wav");

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 220.0 / sampleRate);
            buffer.setSample(0, i, 0.4f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
            return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    Program program;
    SampleAsset asset;
    asset.lengthSamples = sampleLength;
    asset.numChannels = 1;
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 72;
    program.sampleAssets.push_back(asset);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(72);
    zone.velocityRange = VelocityRange::full();
    zone.rootMidiNote = 72;
    program.zones.push_back(zone);

    std::vector<juce::AudioBuffer<float>> samples;
    samples.push_back(createTestSample(sampleLength));
    engine.setProgram(program, samples);
    if (!engine.hasProgram())
        return false;

    const auto loaded = engine.loadSampleFromFile(tempFile);
    tempFile.deleteFile();
    if (!loaded || engine.hasProgram())
        return false;

    EngineCore::AdsrSettings amp;
    amp.attackSeconds = 0.0001f;
    amp.decaySeconds = 0.001f;
    amp.sustainLevel = 1.0f;
    amp.releaseSeconds = 0.001f;
    engine.setAmpEnvelope(amp);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    return blockEnergy(block) > 0.001f;
}

bool runLoadSampleResetsEnvelopeAndFilterDefaultsTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_load_defaults_test", ".wav");

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 330.0 / sampleRate);
            buffer.setSample(0, i, 0.3f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
            return false;
    }

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    audiocity::engine::EngineCore::AdsrSettings customAmp;
    customAmp.attackSeconds = 0.320f;
    customAmp.decaySeconds = 0.410f;
    customAmp.sustainLevel = 0.23f;
    customAmp.releaseSeconds = 0.580f;
    engine.setAmpEnvelope(customAmp);

    audiocity::engine::EngineCore::AdsrSettings customFilterEnv;
    customFilterEnv.attackSeconds = 0.420f;
    customFilterEnv.decaySeconds = 0.530f;
    customFilterEnv.sustainLevel = 0.64f;
    customFilterEnv.releaseSeconds = 0.710f;
    engine.setFilterEnvelope(customFilterEnv);

    auto customPitchLfo = engine.getPitchLfoSettings();
    customPitchLfo.rateHz = 7.5f;
    customPitchLfo.depthCents = 42.0f;
    engine.setPitchLfoSettings(customPitchLfo);

    auto customFilter = engine.getFilterSettings();
    customFilter.baseCutoffHz = 420.0f;
    customFilter.envAmountHz = 9500.0f;
    customFilter.resonance = 0.77f;
    customFilter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::highPass24;
    customFilter.lfoRateHz = 12.0f;
    customFilter.lfoAmountHz = 3200.0f;
    customFilter.lfoTempoSync = true;
    engine.setFilterSettings(customFilter);

    const auto loaded = engine.loadSampleFromFile(tempFile);
    tempFile.deleteFile();

    if (!loaded)
        return false;

    const auto amp = engine.getAmpEnvelope();
    if (std::abs(amp.attackSeconds - 0.005f) > 1.0e-6f
        || std::abs(amp.decaySeconds - 0.150f) > 1.0e-6f
        || std::abs(amp.sustainLevel - 0.85f) > 1.0e-6f
        || std::abs(amp.releaseSeconds - 0.150f) > 1.0e-6f)
    {
        return false;
    }

    const auto filterEnv = engine.getFilterEnvelope();
    if (std::abs(filterEnv.attackSeconds - 0.001f) > 1.0e-6f
        || std::abs(filterEnv.decaySeconds - 0.120f) > 1.0e-6f
        || std::abs(filterEnv.sustainLevel - 0.0f) > 1.0e-6f
        || std::abs(filterEnv.releaseSeconds - 0.100f) > 1.0e-6f)
    {
        return false;
    }

    const auto filter = engine.getFilterSettings();
    const auto pitchLfo = engine.getPitchLfoSettings();
    return std::abs(filter.baseCutoffHz - 18000.0f) <= 1.0e-6f
        && std::abs(filter.envAmountHz - 0.0f) <= 1.0e-6f
        && std::abs(filter.resonance - 0.0f) <= 1.0e-6f
        && filter.mode == audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12
        && std::abs(filter.lfoRateHz - 0.0f) <= 1.0e-6f
        && std::abs(filter.lfoAmountHz - 0.0f) <= 1.0e-6f
        && !filter.lfoTempoSync
        && std::abs(pitchLfo.rateHz - 0.0f) <= 1.0e-6f
        && std::abs(pitchLfo.depthCents - 0.0f) <= 1.0e-6f;
}

bool runFilterModulationAmountsAreBipolarTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    auto filter = engine.getFilterSettings();
    filter.envAmountHz = -3000.0f;
    filter.velocityAmountHz = 4500.0f;
    engine.setFilterSettings(filter);

    auto applied = engine.getFilterSettings();
    if (std::abs(applied.envAmountHz - (-3000.0f)) > 1.0e-6f
        || std::abs(applied.velocityAmountHz - 4500.0f) > 1.0e-6f)
    {
        return false;
    }

    filter.envAmountHz = -50000.0f;
    filter.velocityAmountHz = 50000.0f;
    engine.setFilterSettings(filter);

    applied = engine.getFilterSettings();
    return std::abs(applied.envAmountHz - (-12000.0f)) <= 1.0e-6f
        && std::abs(applied.velocityAmountHz - 12000.0f) <= 1.0e-6f;
}

bool runEmbeddedLoopMetadataLoadsWithoutRootNoteTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 512;
    constexpr int loopStart = 96;
    constexpr int loopEnd = 320;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_embedded_loop_test", ".wav");

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
            return false;

        juce::StringPairArray metadata;
        metadata.set("loop0start", juce::String(loopStart));
        metadata.set("loop0end", juce::String(loopEnd));

        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(output.get(), sampleRate, 1, 16, metadata, 0));
        if (writer == nullptr)
            return false;

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 220.0 / sampleRate);
            buffer.setSample(0, i, 0.3f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
            return false;
    }

    auto getMetadataIntCaseInsensitive = [](const juce::StringPairArray& metadata, const juce::String& key) -> int
    {
        const auto keys = metadata.getAllKeys();
        for (int i = 0; i < keys.size(); ++i)
        {
            if (keys[i].equalsIgnoreCase(key))
            {
                const auto value = metadata.getValue(keys[i], {}).trim();
                if (value.containsOnly("-0123456789"))
                    return value.getIntValue();
            }
        }

        return -1;
    };

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(tempFile));
    if (reader == nullptr)
    {
        tempFile.deleteFile();
        return false;
    }

    const auto readBackStart = getMetadataIntCaseInsensitive(reader->metadataValues, "Loop0Start");
    const auto readBackEnd = getMetadataIntCaseInsensitive(reader->metadataValues, "Loop0End");
    if (readBackStart < 0 || readBackEnd <= readBackStart)
    {
        tempFile.deleteFile();
        return true;
    }

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    const auto loaded = engine.loadSampleFromFile(tempFile);
    tempFile.deleteFile();

    if (!loaded)
        return false;

    return engine.getSampleWindowStart() == 0
        && engine.getSampleWindowEnd() == sampleLength - 1
        && engine.getLoopStart() == readBackStart
        && engine.getLoopEnd() == readBackEnd
        && engine.getPlaybackMode() == audiocity::engine::EngineCore::PlaybackMode::loop;
}

bool runRexRuntimeFallbackSmokeTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto rexFixture = fixtureFile("third_party/REXSDK_Win_1.9.2/REX Test Protocol Files/120Mono.rx2");
    if (!rexFixture.existsAsFile())
        return true;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    const auto rexRuntimeAvailable = engine.isRexRuntimeAvailable();
    const auto loaded = engine.loadSampleFromFile(rexFixture);

    if (!rexRuntimeAvailable)
        return !loaded;

    if (!loaded || engine.getLoadedSampleLength() <= 0)
        return false;

    const auto fullEnd = juce::jmax(0, engine.getLoadedSampleLength() - 1);
    return engine.getSampleWindowStart() == 0
        && engine.getSampleWindowEnd() == fullEnd
        && engine.getLoopStart() == 0
        && engine.getLoopEnd() == fullEnd
        && engine.getPlaybackMode() == audiocity::engine::EngineCore::PlaybackMode::loop;
}

bool runEditorSampleEditControlsTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> ascending(1, 64);
    for (int i = 0; i < ascending.getNumSamples(); ++i)
        ascending.setSample(0, i, static_cast<float>(i) / 63.0f);

    audiocity::engine::EngineCore::AdsrSettings flatAdsr;
    flatAdsr.attackSeconds = 0.0001f;
    flatAdsr.decaySeconds = 0.0001f;
    flatAdsr.sustainLevel = 1.0f;
    flatAdsr.releaseSeconds = 0.001f;

    audiocity::engine::EngineCore::FilterSettings openFilter;
    openFilter.baseCutoffHz = 18000.0f;
    openFilter.envAmountHz = 0.0f;

    auto configureEngine = [&](audiocity::engine::EngineCore& engine)
    {
        engine.prepare(sampleRate, blockSize, channels);
        engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::cpu);
        engine.setAmpEnvelope(flatAdsr);
        engine.setFilterSettings(openFilter);
        engine.setSampleData(ascending, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setSampleWindow(8, 39);
    };

    auto renderWithEdits = [&](const bool reverse, const int fadeIn, const int fadeOut)
    {
        audiocity::engine::EngineCore engine;
        configureEngine(engine);
        engine.setReversePlayback(reverse);
        engine.setFadeSamples(fadeIn, fadeOut);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto forward = renderWithEdits(false, 0, 0);
    const auto reversed = renderWithEdits(true, 0, 0);

    float forwardEarly = 0.0f;
    float forwardLate = 0.0f;
    float reverseEarly = 0.0f;
    float reverseLate = 0.0f;

    for (int i = 4; i < 12; ++i)
    {
        forwardEarly += std::abs(forward.getSample(0, i));
        reverseEarly += std::abs(reversed.getSample(0, i));
    }

    for (int i = 20; i < 28; ++i)
    {
        forwardLate += std::abs(forward.getSample(0, i));
        reverseLate += std::abs(reversed.getSample(0, i));
    }

    if (!(forwardLate > forwardEarly * 1.25f))
        return false;

    if (!(reverseEarly > reverseLate * 1.25f))
        return false;

    const auto faded = renderWithEdits(false, 10, 10);

    float noFadeHead = 0.0f;
    float fadedHead = 0.0f;
    float noFadeTail = 0.0f;
    float fadedTail = 0.0f;

    for (int i = 1; i < 8; ++i)
    {
        noFadeHead += std::abs(forward.getSample(0, i));
        fadedHead += std::abs(faded.getSample(0, i));
    }

    for (int i = 24; i < 31; ++i)
    {
        noFadeTail += std::abs(forward.getSample(0, i));
        fadedTail += std::abs(faded.getSample(0, i));
    }

    if (!(fadedHead < noFadeHead * 0.65f))
        return false;

    if (!(fadedTail < noFadeTail * 0.75f))
        return false;

    return true;
}

bool runPolyphonicDifferentNotesLayerWhenMonoOffTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setMonoMode(false);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 16);

    juce::AudioBuffer<float> block(channels, blockSize);
    engine.render(block, midi);

    return engine.activeVoiceCount() >= 2 && engine.isNoteActive(60) && engine.isNoteActive(64);
}

bool runMonoLegatoUsesSingleVoiceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setMonoMode(true);
    engine.setLegatoMode(true);

    audiocity::engine::EngineCore::AdsrSettings fastAdsr;
    fastAdsr.attackSeconds = 0.0001f;
    fastAdsr.decaySeconds = 0.001f;
    fastAdsr.sustainLevel = 1.0f;
    fastAdsr.releaseSeconds = 0.004f;
    engine.setAmpEnvelope(fastAdsr);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;

    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
    engine.render(block, midi);

    if (engine.activeVoiceCount() != 1 || !engine.isNoteActive(72) || engine.isNoteActive(60))
        return false;

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 72), 0);
    engine.render(block, midi);

    midi.clear();
    for (int i = 0; i < 10; ++i)
        engine.render(block, midi);

    return engine.activeVoiceCount() == 0;
}

bool runPolyphonicSameNoteReleaseTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setMonoMode(false);
    engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

    audiocity::engine::EngineCore::AdsrSettings adsr;
    adsr.attackSeconds = 0.0001f;
    adsr.decaySeconds = 0.001f;
    adsr.sustainLevel = 1.0f;
    adsr.releaseSeconds = 0.02f;
    engine.setAmpEnvelope(adsr);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;

    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 32);
    engine.render(block, midi);

    if (engine.activeVoiceCount() != 1)
        return false;

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);

    midi.clear();
    for (int i = 0; i < 120; ++i)
        engine.render(block, midi);

    return engine.activeVoiceCount() == 0;
}

bool runDenseLoopModeOverflowDoesNotStickNotesTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int denseEventCount = 1600;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setMonoMode(false);
    engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

    audiocity::engine::EngineCore::AdsrSettings adsr;
    adsr.attackSeconds = 0.0001f;
    adsr.decaySeconds = 0.001f;
    adsr.sustainLevel = 1.0f;
    adsr.releaseSeconds = 0.01f;
    engine.setAmpEnvelope(adsr);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < denseEventCount; ++i)
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    for (int i = 0; i < denseEventCount; ++i)
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);

    engine.render(block, midi);

    midi.clear();
    for (int i = 0; i < 260; ++i)
        engine.render(block, midi);

    return engine.activeVoiceCount() == 0;
}

bool runQueueSaturatedByPitchBendStillReleasesNoteOffTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int saturationEvents = 1600;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

    audiocity::engine::EngineCore::AdsrSettings adsr;
    adsr.attackSeconds = 0.0001f;
    adsr.decaySeconds = 0.001f;
    adsr.sustainLevel = 1.0f;
    adsr.releaseSeconds = 0.01f;
    engine.setAmpEnvelope(adsr);

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;

    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    engine.render(block, midi);
    if (engine.activeVoiceCount() == 0)
        return false;

    midi.clear();
    for (int i = 0; i < saturationEvents; ++i)
        midi.addEvent(juce::MidiMessage::pitchWheel(1, (i % 2 == 0) ? 16383 : 0), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    engine.render(block, midi);

    midi.clear();
    for (int i = 0; i < 260; ++i)
        engine.render(block, midi);

    return engine.activeVoiceCount() == 0;
}

juce::AudioBuffer<float> renderLegatoTransitionBuffer(const float glideSeconds)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 6;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);
    engine.setMonoMode(true);
    engine.setLegatoMode(true);
    engine.setGlideSeconds(glideSeconds);

    juce::AudioBuffer<float> rendered(channels, blockSize * blocks);

    for (int blockIndex = 0; blockIndex < blocks; ++blockIndex)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        if (blockIndex == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

        if (blockIndex == 1)
            midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);

        if (blockIndex == 4)
            midi.addEvent(juce::MidiMessage::noteOff(1, 72), 0);

        engine.render(block, midi);

        for (int channel = 0; channel < channels; ++channel)
            rendered.copyFrom(channel, blockIndex * blockSize, block, channel, 0, blockSize);
    }

    return rendered;
}

bool runGlideChangesLegatoTransitionTest()
{
    const auto immediate = renderLegatoTransitionBuffer(0.0f);
    const auto gliding = renderLegatoTransitionBuffer(0.05f);
    return !buffersAreEqual(immediate, gliding, 1.0e-6f);
}

bool runPreloadSegmentationDeterminismTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto sample = createTestSample(8192);

    audiocity::engine::EngineCore fullPreload;
    fullPreload.prepare(sampleRate, blockSize, channels);
    fullPreload.setPreloadSamples(16384);
    fullPreload.setSampleData(sample, sampleRate, 60);

    audiocity::engine::EngineCore segmented;
    segmented.prepare(sampleRate, blockSize, channels);
    segmented.setPreloadSamples(512);
    segmented.setSampleData(sample, sampleRate, 60);

    const auto a = renderSequence(fullPreload);
    const auto b = renderSequence(segmented);
    return buffersAreEqual(a, b, 1.0e-6f);
}

juce::AudioBuffer<float> renderSequenceWithOptionalPreloadChange(
    audiocity::engine::EngineCore& engine,
    const bool applyChange,
    const int changedPreloadSamples)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 8;

    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

        if (block == 5)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 32);

        if (applyChange && block == 2)
            engine.setPreloadSamples(changedPreloadSamples);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            output.copyFrom(channel, block * blockSize, blockBuffer, channel, 0, blockSize);
    }

    return output;
}

bool runRuntimePreloadChangeStabilityTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto sample = createTestSample(8192);

    audiocity::engine::EngineCore reference;
    reference.prepare(sampleRate, blockSize, channels);
    reference.setPreloadSamples(4096);
    reference.setSampleData(sample, sampleRate, 60);

    audiocity::engine::EngineCore changed;
    changed.prepare(sampleRate, blockSize, channels);
    changed.setPreloadSamples(4096);
    changed.setSampleData(sample, sampleRate, 60);

    const auto stable = renderSequenceWithOptionalPreloadChange(reference, false, 512);
    const auto withChange = renderSequenceWithOptionalPreloadChange(changed, true, 512);
    return buffersAreEqual(stable, withChange, 1.0e-6f);
}

juce::AudioBuffer<float> renderLoopSequenceWithOptionalPreloadChange(
    audiocity::engine::EngineCore& engine,
    const bool applyChange,
    const int changedPreloadSamples)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 10;

    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

        if (applyChange && block == 4)
            engine.setPreloadSamples(changedPreloadSamples);

        if (block == 8)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 64);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            output.copyFrom(channel, block * blockSize, blockBuffer, channel, 0, blockSize);
    }

    return output;
}

juce::AudioBuffer<float> renderSequenceWithOptionalSampleReload(
    audiocity::engine::EngineCore& engine,
    const juce::AudioBuffer<float>& sample,
    const double sampleRate,
    const bool applyReload)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 8;

    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

        if (block == 5)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 32);

        if (applyReload && block == 2)
            engine.setSampleData(sample, sampleRate, 60);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            output.copyFrom(channel, block * blockSize, blockBuffer, channel, 0, blockSize);
    }

    return output;
}

bool runRuntimeSampleReloadStabilityTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto sample = createTestSample(8192);

    audiocity::engine::EngineCore reference;
    reference.prepare(sampleRate, blockSize, channels);
    reference.setSampleData(sample, sampleRate, 60);

    audiocity::engine::EngineCore changed;
    changed.prepare(sampleRate, blockSize, channels);
    changed.setSampleData(sample, sampleRate, 60);

    const auto stable = renderSequenceWithOptionalSampleReload(reference, sample, sampleRate, false);
    const auto withReload = renderSequenceWithOptionalSampleReload(changed, sample, sampleRate, true);
    return buffersAreEqual(stable, withReload, 1.0e-6f);
}

bool runLoopModeRuntimePreloadChangeStabilityTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto shaped = juce::AudioBuffer<float>(1, 96);
    shaped.clear();
    for (int i = 20; i < 56; ++i)
        shaped.setSample(0, i, 0.7f * std::sin(static_cast<float>(i) * 0.3f));

    audiocity::engine::EngineCore::AdsrSettings slowRelease;
    slowRelease.attackSeconds = 0.0001f;
    slowRelease.decaySeconds = 0.001f;
    slowRelease.sustainLevel = 1.0f;
    slowRelease.releaseSeconds = 0.4f;

    audiocity::engine::EngineCore reference;
    reference.prepare(sampleRate, blockSize, channels);
    reference.setAmpEnvelope(slowRelease);
    reference.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
    reference.setPreloadSamples(2048);
    reference.setSampleData(shaped, sampleRate, 60);
    reference.setLoopPoints(20, 55);

    audiocity::engine::EngineCore changed;
    changed.prepare(sampleRate, blockSize, channels);
    changed.setAmpEnvelope(slowRelease);
    changed.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
    changed.setPreloadSamples(2048);
    changed.setSampleData(shaped, sampleRate, 60);
    changed.setLoopPoints(20, 55);

    const auto stable = renderLoopSequenceWithOptionalPreloadChange(reference, false, 256);
    const auto withChange = renderLoopSequenceWithOptionalPreloadChange(changed, true, 256);
    return buffersAreEqual(stable, withChange, 1.0e-6f);
}

bool runSegmentRebuildCounterTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);

    const auto baseCount = engine.getSegmentRebuildCount();

    const auto sample = createTestSample(4096);
    engine.setSampleData(sample, sampleRate, 60);
    const auto afterLoadCount = engine.getSegmentRebuildCount();

    if (afterLoadCount <= baseCount)
        return false;

    engine.setPreloadSamples(512);
    const auto afterPreloadChangeCount = engine.getSegmentRebuildCount();

    return afterPreloadChangeCount > afterLoadCount;
}

bool runProgramPreloadMetricsAndRebuildTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setPreloadSamples(2048);

    Program program;

    SampleAsset firstAsset;
    firstAsset.displayName = "layer_a.wav";
    firstAsset.sampleRateHz = sampleRate;
    firstAsset.rootMidiNote = 60;
    program.sampleAssets.push_back(firstAsset);

    SampleAsset secondAsset = firstAsset;
    secondAsset.displayName = "layer_b.wav";
    program.sampleAssets.push_back(secondAsset);

    Zone firstZone;
    firstZone.sampleAssetIndex = 0;
    firstZone.keyRange = MidiRange::single(60);
    firstZone.rootMidiNote = 60;
    program.zones.push_back(firstZone);

    Zone secondZone = firstZone;
    secondZone.sampleAssetIndex = 1;
    secondZone.keyRange = MidiRange::single(61);
    secondZone.rootMidiNote = 61;
    program.zones.push_back(secondZone);

    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    sampleDataByAsset.push_back(createTestSample(4096));
    sampleDataByAsset.push_back(createTestSample(1536));

    engine.setProgram(program, sampleDataByAsset);
    const auto afterLoadCount = engine.getSegmentRebuildCount();

    if (engine.getLoadedPreloadSamples() != 3584)
        return false;

    if (engine.getLoadedStreamSamples() != 2048)
        return false;

    engine.setPreloadSamples(512);
    const auto afterPreloadChangeCount = engine.getSegmentRebuildCount();

    return afterPreloadChangeCount > afterLoadCount
        && engine.getLoadedPreloadSamples() == 1024
        && engine.getLoadedStreamSamples() == 4608;
}

bool runSingleSampleFileStreamingPreloadMetricsTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleLength = 4096;

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_single_stream_metrics", ".wav");

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();

        juce::AudioBuffer<float> buffer(1, sampleLength);
        for (int i = 0; i < sampleLength; ++i)
        {
            const float phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 220.0 / sampleRate);
            buffer.setSample(0, i, 0.35f * std::sin(phase));
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, sampleLength))
            return false;
    }

    auto cleanup = [&]() { tempFile.deleteFile(); };

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setPreloadSamples(256);

    if (!engine.loadSampleFromFile(tempFile))
    {
        cleanup();
        return false;
    }

    auto renderBlock = [&](const bool noteOn) -> float
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        if (noteOn)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

        engine.render(block, midi);
        return blockEnergy(block);
    };

    if (engine.getLoadedPreloadSamples() != 256
        || engine.getLoadedStreamSamples() != (sampleLength - 256))
    {
        cleanup();
        return false;
    }

    const auto afterLoadCount = engine.getSegmentRebuildCount();
    if (renderBlock(true) <= 0.001f)
    {
        cleanup();
        return false;
    }

    renderBlock(false);
    renderBlock(false);

    if (engine.getStreamPrimeRequestCount() != 4
        || engine.getStreamPrimeCacheHitCount() != 3
        || engine.getStreamPrimeCacheMissCount() != 1
        || engine.getStreamPrimeServiceCount() != 0)
    {
        cleanup();
        return false;
    }

    engine.serviceStreamPriming();
    renderBlock(false);

    if (engine.getStreamPrimeRequestCount() != 5
        || engine.getStreamPrimeCacheHitCount() != 4
        || engine.getStreamPrimeCacheMissCount() != 1
        || engine.getStreamPrimeServiceCount() != 1)
    {
        cleanup();
        return false;
    }

    engine.panic();
    engine.setPreloadSamples(512);

    if (engine.getSegmentRebuildCount() <= afterLoadCount
        || engine.getLoadedPreloadSamples() != 512
        || engine.getLoadedStreamSamples() != (sampleLength - 512)
        || engine.getStreamPrimeRequestCount() != 0
        || engine.getStreamPrimeCacheHitCount() != 0
        || engine.getStreamPrimeCacheMissCount() != 0
        || engine.getStreamPrimeServiceCount() != 0)
    {
        cleanup();
        return false;
    }

    if (renderBlock(true) <= 0.001f)
    {
        cleanup();
        return false;
    }

    renderBlock(false);
    renderBlock(false);
    renderBlock(false);
    renderBlock(false);

    if (engine.getStreamPrimeRequestCount() != 6
        || engine.getStreamPrimeCacheHitCount() != 5
        || engine.getStreamPrimeCacheMissCount() != 1
        || engine.getStreamPrimeServiceCount() != 0)
    {
        cleanup();
        return false;
    }

    engine.serviceStreamPriming();
    renderBlock(false);

    const auto passed = engine.getStreamPrimeRequestCount() == 7
        && engine.getStreamPrimeCacheHitCount() == 6
        && engine.getStreamPrimeCacheMissCount() == 1
        && engine.getStreamPrimeServiceCount() == 1;

    cleanup();
    return passed;
}

bool runProgramStreamPrimingAndCacheMetricsTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_stream_prime_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("stream.wav");
    const auto sample = createTestSample(4096);

    auto writeWavFile = [&](const juce::File& fileToWrite) -> bool
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(fileToWrite.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();
        return writer->writeFromAudioSampleBuffer(sample, 0, sample.getNumSamples());
    };

    if (!writeWavFile(sampleFile))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setPreloadSamples(256);

    Program program;
    SampleAsset asset;
    asset.sourcePath = sampleFile.getFullPathName().toStdString();
    asset.displayName = "stream.wav";
    asset.lengthSamples = sample.getNumSamples();
    asset.numChannels = sample.getNumChannels();
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    const std::array<int, 5> zoneStarts{ 256, 768, 1280, 1792, 2304 };
    for (int zoneIndex = 0; zoneIndex < static_cast<int>(zoneStarts.size()); ++zoneIndex)
    {
        Zone zone;
        zone.sampleAssetIndex = 0;
        zone.keyRange = MidiRange::single(60 + zoneIndex);
        zone.rootMidiNote = 60 + zoneIndex;
        zone.sampleStart = zoneStarts[static_cast<std::size_t>(zoneIndex)];
        zone.sampleEndExclusive = sample.getNumSamples();
        program.zones.push_back(zone);
    }

    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    sampleDataByAsset.push_back(sample);
    engine.setProgram(program, sampleDataByAsset);

    const auto baseRequests = engine.getStreamPrimeRequestCount();
    const auto baseHits = engine.getStreamPrimeCacheHitCount();
    const auto baseMisses = engine.getStreamPrimeCacheMissCount();
    const auto baseServices = engine.getStreamPrimeServiceCount();

    juce::AudioBuffer<float> firstBlock(channels, blockSize);
    juce::MidiBuffer firstMidi;
    firstMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    engine.render(firstBlock, firstMidi);

    if ((engine.getStreamPrimeRequestCount() - baseRequests) != 2
        || (engine.getStreamPrimeCacheHitCount() - baseHits) != 1
        || (engine.getStreamPrimeCacheMissCount() - baseMisses) != 1)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    engine.serviceStreamPriming();
    if ((engine.getStreamPrimeServiceCount() - baseServices) != 1)
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    juce::AudioBuffer<float> secondBlock(channels, blockSize);
    juce::MidiBuffer secondMidi;
    secondMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    engine.render(secondBlock, secondMidi);

    const auto passed = (engine.getStreamPrimeRequestCount() - baseRequests) == 4
        && (engine.getStreamPrimeCacheHitCount() - baseHits) == 2
        && (engine.getStreamPrimeCacheMissCount() - baseMisses) == 2
        && (engine.getStreamPrimeServiceCount() - baseServices) == 1;

    tempDirectory.deleteRecursively();
    return passed;
}

bool runProgramStreamLookaheadPrimingTest()
{
    using namespace audiocity::engine;

    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_stream_lookahead_test", "");
    if (!tempDirectory.createDirectory())
        return false;

    const auto sampleFile = tempDirectory.getChildFile("lookahead.wav");
    const auto sample = createTestSample(4096);

    auto writeWavFile = [&](const juce::File& fileToWrite) -> bool
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> output(fileToWrite.createOutputStream());
        if (output == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(), sampleRate, 1, 16, {}, 0));
        if (writer == nullptr)
            return false;

        output.release();
        return writer->writeFromAudioSampleBuffer(sample, 0, sample.getNumSamples());
    };

    if (!writeWavFile(sampleFile))
    {
        tempDirectory.deleteRecursively();
        return false;
    }

    EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setPreloadSamples(256);

    Program program;
    SampleAsset asset;
    asset.sourcePath = sampleFile.getFullPathName().toStdString();
    asset.displayName = "lookahead.wav";
    asset.lengthSamples = sample.getNumSamples();
    asset.numChannels = sample.getNumChannels();
    asset.sampleRateHz = sampleRate;
    asset.rootMidiNote = 60;
    program.sampleAssets.push_back(asset);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::single(60);
    zone.rootMidiNote = 60;
    zone.sampleStart = 256;
    zone.sampleEndExclusive = sample.getNumSamples();
    program.zones.push_back(zone);

    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    sampleDataByAsset.push_back(sample);
    engine.setProgram(program, sampleDataByAsset);

    const auto baseRequests = engine.getStreamPrimeRequestCount();
    const auto baseHits = engine.getStreamPrimeCacheHitCount();
    const auto baseMisses = engine.getStreamPrimeCacheMissCount();
    const auto baseServices = engine.getStreamPrimeServiceCount();

    for (int blockIndex = 0; blockIndex < 5; ++blockIndex)
    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        if (blockIndex == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

        if (blockIndex == 4)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 32);

        engine.render(block, midi);
        engine.serviceStreamPriming();
    }

    const auto passed = (engine.getStreamPrimeRequestCount() - baseRequests) >= 6
        && (engine.getStreamPrimeCacheHitCount() - baseHits) >= 4
        && (engine.getStreamPrimeCacheMissCount() - baseMisses) >= 2
        && (engine.getStreamPrimeServiceCount() - baseServices) >= 2;

    tempDirectory.deleteRecursively();
    return passed;
}

bool runQualityTierDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> sample(1, 2048);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto value = static_cast<float>((i % 31) / 31.0);
        sample.setSample(0, i, value * 0.5f - 0.25f);
    }

    audiocity::engine::EngineCore cpu;
    cpu.prepare(sampleRate, blockSize, channels);
    cpu.setQualityTier(audiocity::engine::EngineCore::QualityTier::cpu);
    cpu.setSampleData(sample, sampleRate, 60);

    audiocity::engine::EngineCore fidelity;
    fidelity.prepare(sampleRate, blockSize, channels);
    fidelity.setQualityTier(audiocity::engine::EngineCore::QualityTier::fidelity);
    fidelity.setSampleData(sample, sampleRate, 60);

    juce::AudioBuffer<float> cpuBlock(channels, blockSize);
    juce::AudioBuffer<float> fidelityBlock(channels, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 0);

    cpu.render(cpuBlock, midi);
    fidelity.render(fidelityBlock, midi);

    return !buffersAreEqual(cpuBlock, fidelityBlock, 1.0e-7f);
}

juce::AudioBuffer<float> renderQualityTierSequence(audiocity::engine::EngineCore& engine)
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 6;

    juce::AudioBuffer<float> output(channels, blockSize * blocks);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.95f), 0);

        if (block == 3)
            midi.addEvent(juce::MidiMessage::noteOff(1, 67), 64);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            output.copyFrom(channel, block * blockSize, blockBuffer, channel, 0, blockSize);
    }

    return output;
}

bool runQualityTierDeterminismTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> sample(1, 2048);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto value = static_cast<float>((i % 29) / 29.0);
        sample.setSample(0, i, value * 0.7f - 0.35f);
    }

    auto renderPairForTier = [&](const audiocity::engine::EngineCore::QualityTier tier)
    {
        audiocity::engine::EngineCore first;
        first.prepare(sampleRate, blockSize, channels);
        first.setQualityTier(tier);
        first.setSampleData(sample, sampleRate, 60);

        audiocity::engine::EngineCore second;
        second.prepare(sampleRate, blockSize, channels);
        second.setQualityTier(tier);
        second.setSampleData(sample, sampleRate, 60);

        const auto a = renderQualityTierSequence(first);
        const auto b = renderQualityTierSequence(second);
        return buffersAreEqual(a, b, 1.0e-7f);
    };

    return renderPairForTier(audiocity::engine::EngineCore::QualityTier::cpu)
        && renderPairForTier(audiocity::engine::EngineCore::QualityTier::fidelity)
        && renderPairForTier(audiocity::engine::EngineCore::QualityTier::ultra);
}

double computeAverage(const std::vector<float>& values, const int startIndex, const int endIndexExclusive)
{
    if (values.empty())
        return 0.0;

    const auto start = juce::jlimit(0, static_cast<int>(values.size()), startIndex);
    const auto endExclusive = juce::jlimit(start, static_cast<int>(values.size()), endIndexExclusive);

    double sum = 0.0;
    int count = 0;
    for (int i = start; i < endExclusive; ++i)
    {
        sum += static_cast<double>(values[static_cast<std::size_t>(i)]);
        ++count;
    }

    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

bool runCpuQualityEnergyDriftSmokeTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 128;
    constexpr double sampleRate = 48000.0;
    constexpr double maxCpuVsFidelityDriftDelta = 0.15;
    constexpr double hardMaxNormalizedDrift = 1.0;

    juce::AudioBuffer<float> shaped(1, 96);
    shaped.clear();
    for (int i = 20; i < 56; ++i)
        shaped.setSample(0, i, 0.75f * std::sin(static_cast<float>(i) * 0.33f));

    audiocity::engine::EngineCore::AdsrSettings stableAdsr;
    stableAdsr.attackSeconds = 0.0001f;
    stableAdsr.decaySeconds = 0.001f;
    stableAdsr.sustainLevel = 1.0f;
    stableAdsr.releaseSeconds = 0.2f;

    auto computeDrift = [&](const audiocity::engine::EngineCore::QualityTier tier)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setQualityTier(tier);
        engine.setAmpEnvelope(stableAdsr);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setSampleData(shaped, sampleRate, 60);
        engine.setLoopPoints(20, 55);

        std::vector<float> energies;
        energies.reserve(static_cast<std::size_t>(blocks));

        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

            engine.render(blockBuffer, midi);
            energies.push_back(blockEnergy(blockBuffer));
        }

        const auto earlyMean = computeAverage(energies, 16, 48);
        const auto lateMean = computeAverage(energies, blocks - 32, blocks);

        if (!(earlyMean > 1.0e-6) || !std::isfinite(earlyMean) || !std::isfinite(lateMean))
            return std::numeric_limits<double>::infinity();

        return std::abs(lateMean - earlyMean) / earlyMean;
    };

    const auto cpuDrift = computeDrift(audiocity::engine::EngineCore::QualityTier::cpu);
    const auto fidelityDrift = computeDrift(audiocity::engine::EngineCore::QualityTier::fidelity);

    if (!std::isfinite(cpuDrift) || !std::isfinite(fidelityDrift))
        return false;

    if (cpuDrift > hardMaxNormalizedDrift)
        return false;

    return cpuDrift <= fidelityDrift + maxCpuVsFidelityDriftDelta;
}

bool runRuntimeQualitySwitchSmokeTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 14;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> sample(1, 3072);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto phase = static_cast<float>(2.0 * juce::MathConstants<double>::pi * i * 330.0 / sampleRate);
        sample.setSample(0, i, 0.35f * std::sin(phase));
    }

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::fidelity);
    engine.setSampleData(sample, sampleRate, 60);

    double totalEnergy = 0.0;
    for (int block = 0; block < blocks; ++block)
    {
        if (block == 4)
            engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::ultra);
        if (block == 8)
            engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::cpu);
        if (block == 11)
            engine.setQualityTier(audiocity::engine::EngineCore::QualityTier::fidelity);

        juce::AudioBuffer<float> blockBuffer(channels, blockSize);
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.9f), 0);
        if (block == 12)
            midi.addEvent(juce::MidiMessage::noteOff(1, 67), 32);

        engine.render(blockBuffer, midi);

        for (int channel = 0; channel < blockBuffer.getNumChannels(); ++channel)
        {
            const auto* data = blockBuffer.getReadPointer(channel);
            for (int i = 0; i < blockBuffer.getNumSamples(); ++i)
            {
                const auto sampleValue = data[i];
                if (!std::isfinite(sampleValue))
                    return false;

                totalEnergy += std::abs(static_cast<double>(sampleValue));
            }
        }
    }

    return totalEnergy > 0.01 && totalEnergy < 20000.0;
}

bool runEditorUndoHistoryMixedOrderTest()
{
    using namespace audiocity::engine;

    Program initialProgram;
    SampleAsset sample;
    sample.displayName = "undo.wav";
    sample.lengthSamples = 512;
    initialProgram.sampleAssets.push_back(sample);

    Zone first;
    first.sampleAssetIndex = 0;
    first.keyRange = MidiRange::single(60);
    first.rootMidiNote = 60;
    initialProgram.zones.push_back(first);

    Zone second = first;
    second.keyRange = MidiRange::single(61);
    second.rootMidiNote = 61;
    initialProgram.zones.push_back(second);

    const auto initialState = audiocity::plugin::createProgramZoneMappingState(initialProgram);

    auto editedProgram = initialProgram;
    audiocity::plugin::ProgramZoneEdit edit;
    edit.zoneIndex = 0;
    edit.keyLow = 48;
    edit.keyHigh = 52;
    edit.velocityLow = 8;
    edit.velocityHigh = 96;
    edit.rootMidiNote = 50;
    if (!audiocity::plugin::applyProgramZoneEdit(editedProgram, edit))
        return false;

    const auto editedState = audiocity::plugin::createProgramZoneMappingState(editedProgram);

    auto finalProgram = editedProgram;
    if (!audiocity::plugin::deleteProgramZone(finalProgram, 1))
        return false;

    const auto finalState = audiocity::plugin::createProgramZoneMappingState(finalProgram);

    audiocity::engine::SettingsSnapshot initialSettings;
    initialSettings.preloadSamples = 32768;
    initialSettings.sampleWindowStart = 0;

    auto changedSettings = initialSettings;
    changedSettings.preloadSamples = 4096;
    changedSettings.sampleWindowStart = 12;

    audiocity::plugin::ProgramMappingStateSnapshot initialMapping;
    initialMapping.hasImportedProgram = true;
    initialMapping.programPath = "C:/Library/undo.sfz";
    initialMapping.mappingState = initialState;

    auto editedMapping = initialMapping;
    editedMapping.mappingState = editedState;

    auto finalMapping = initialMapping;
    finalMapping.mappingState = finalState;

    audiocity::plugin::EditorUndoHistory history;
    history.recordSettingsChange(initialSettings, changedSettings, 1, "Edit Settings");
    history.recordMappingChange(editedMapping, finalMapping, "Delete Mapping Zone");

    if (!history.canUndo() || history.canRedo() || history.undoLabel() != "Delete Mapping Zone")
        return false;

    auto currentSettings = changedSettings;
    auto currentMapping = finalMapping;
    const auto undo1 = history.undo(currentSettings, currentMapping);
    if (!undo1.has_value() || undo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (undo1->mappingSnapshot != editedMapping)
        return false;

    currentMapping = undo1->mappingSnapshot;
    const auto undo2 = history.undo(currentSettings, currentMapping);
    if (!undo2.has_value() || undo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::settings)
        return false;

    if (!(undo2->settingsSnapshot == initialSettings))
        return false;

    if (history.canUndo() || !history.canRedo())
        return false;

    currentSettings = undo2->settingsSnapshot;
    const auto redo1 = history.redo(currentSettings, currentMapping);
    if (!redo1.has_value() || redo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::settings)
        return false;

    if (!(redo1->settingsSnapshot == changedSettings))
        return false;

    currentSettings = redo1->settingsSnapshot;
    const auto redo2 = history.redo(currentSettings, currentMapping);
    if (!redo2.has_value() || redo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (redo2->mappingSnapshot != finalMapping)
        return false;

    return !history.canRedo() && history.canUndo();
}

bool runEditorUndoHistoryCoalesceAndLabelsTest()
{
    audiocity::engine::SettingsSnapshot a;
    a.preloadSamples = 32768;

    auto b = a;
    b.preloadSamples = 24000;

    auto c = b;
    c.preloadSamples = 16000;

    audiocity::plugin::EditorUndoHistory history;
    history.recordSettingsChange(a, b, 1, "Edit Settings");
    history.recordSettingsChange(b, c, 1, "Edit Settings");

    if (history.undoLabel() != "Edit Settings")
        return false;

    auto currentMapping = audiocity::plugin::ProgramMappingStateSnapshot{};
    auto currentSettings = c;
    const auto undo = history.undo(currentSettings, currentMapping);
    if (!undo.has_value() || undo->kind != audiocity::plugin::EditorUndoHistory::EntryKind::settings)
        return false;

    if (!(undo->settingsSnapshot == a))
        return false;

    if (!history.undoLabel().empty())
        return false;

    return history.canRedo();
}

bool runEditorUndoHistoryCreateZoneTest()
{
    using namespace audiocity::engine;

    Program initialProgram;
    SampleAsset sample;
    sample.displayName = "create.wav";
    sample.lengthSamples = 512;
    sample.rootMidiNote = 60;
    initialProgram.sampleAssets.push_back(sample);

    audiocity::plugin::ProgramMappingStateSnapshot initialMapping;
    initialMapping.hasImportedProgram = true;
    initialMapping.programPath = "C:/Library/create.sfz";
    initialMapping.mappingState = audiocity::plugin::createProgramZoneMappingState(initialProgram);

    auto createdProgram = initialProgram;
    if (audiocity::plugin::createProgramZone(createdProgram) != 0)
        return false;

    auto createdMapping = initialMapping;
    createdMapping.mappingState = audiocity::plugin::createProgramZoneMappingState(createdProgram);

    audiocity::engine::SettingsSnapshot initialSettings;
    initialSettings.preloadSamples = 32768;

    auto changedSettings = initialSettings;
    changedSettings.preloadSamples = 8192;

    audiocity::plugin::EditorUndoHistory history;
    history.recordSettingsChange(initialSettings, changedSettings, 1, "Edit Settings");
    history.recordMappingChange(initialMapping, createdMapping, "Create Mapping Zone");

    if (!history.canUndo() || history.undoLabel() != "Create Mapping Zone")
        return false;

    auto currentSettings = changedSettings;
    auto currentMapping = createdMapping;
    const auto undo1 = history.undo(currentSettings, currentMapping);
    if (!undo1.has_value() || undo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (undo1->mappingSnapshot != initialMapping || history.undoLabel() != "Edit Settings")
        return false;

    currentMapping = undo1->mappingSnapshot;
    const auto undo2 = history.undo(currentSettings, currentMapping);
    if (!undo2.has_value() || undo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::settings)
        return false;

    if (!(undo2->settingsSnapshot == initialSettings))
        return false;

    currentSettings = undo2->settingsSnapshot;
    const auto redo1 = history.redo(currentSettings, currentMapping);
    if (!redo1.has_value() || redo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::settings)
        return false;

    if (!(redo1->settingsSnapshot == changedSettings))
        return false;

    currentSettings = redo1->settingsSnapshot;
    const auto redo2 = history.redo(currentSettings, currentMapping);
    if (!redo2.has_value() || redo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    return redo2->mappingSnapshot == createdMapping && !history.canRedo() && history.canUndo();
}

bool runEditorUndoHistoryDuplicateAndSplitTest()
{
    using namespace audiocity::engine;

    Program initialProgram;
    SampleAsset sample;
    sample.displayName = "undo.wav";
    sample.lengthSamples = 512;
    initialProgram.sampleAssets.push_back(sample);

    Zone zone;
    zone.sampleAssetIndex = 0;
    zone.keyRange = MidiRange::fromUnordered(36, 47);
    zone.rootMidiNote = 42;
    initialProgram.zones.push_back(zone);

    audiocity::plugin::ProgramMappingStateSnapshot initialMapping;
    initialMapping.hasImportedProgram = true;
    initialMapping.programPath = "C:/Library/undo.sfz";
    initialMapping.mappingState = audiocity::plugin::createProgramZoneMappingState(initialProgram);

    auto duplicatedProgram = initialProgram;
    const auto duplicatedIndex = audiocity::plugin::duplicateProgramZone(duplicatedProgram, 0);
    if (duplicatedIndex != 1)
        return false;

    auto duplicatedMapping = initialMapping;
    duplicatedMapping.mappingState = audiocity::plugin::createProgramZoneMappingState(duplicatedProgram);

    auto splitProgram = duplicatedProgram;
    const auto splitIndex = audiocity::plugin::splitProgramZoneByKey(splitProgram, duplicatedIndex);
    if (splitIndex != 2)
        return false;

    auto splitMapping = initialMapping;
    splitMapping.mappingState = audiocity::plugin::createProgramZoneMappingState(splitProgram);

    audiocity::plugin::EditorUndoHistory history;
    history.recordMappingChange(initialMapping, duplicatedMapping, "Duplicate Mapping Zone");
    history.recordMappingChange(duplicatedMapping, splitMapping, "Split Mapping Zone");

    if (!history.canUndo() || history.canRedo() || history.undoLabel() != "Split Mapping Zone")
        return false;

    auto currentSettings = audiocity::engine::SettingsSnapshot{};
    auto currentMapping = splitMapping;
    const auto undo1 = history.undo(currentSettings, currentMapping);
    if (!undo1.has_value() || undo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (undo1->mappingSnapshot != duplicatedMapping || history.undoLabel() != "Duplicate Mapping Zone")
        return false;

    currentMapping = undo1->mappingSnapshot;
    const auto undo2 = history.undo(currentSettings, currentMapping);
    if (!undo2.has_value() || undo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (undo2->mappingSnapshot != initialMapping)
        return false;

    currentMapping = undo2->mappingSnapshot;
    const auto redo1 = history.redo(currentSettings, currentMapping);
    if (!redo1.has_value() || redo1->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (redo1->mappingSnapshot != duplicatedMapping)
        return false;

    currentMapping = redo1->mappingSnapshot;
    const auto redo2 = history.redo(currentSettings, currentMapping);
    if (!redo2.has_value() || redo2->kind != audiocity::plugin::EditorUndoHistory::EntryKind::mapping)
        return false;

    if (redo2->mappingSnapshot != splitMapping)
        return false;

    return !history.canRedo() && history.canUndo();
}

bool runSettingsUndoHistoryTest()
{
    audiocity::engine::SettingsUndoHistory history;
    audiocity::engine::SettingsSnapshot initial;
    initial.preloadSamples = 32768;
    initial.qualityTierIndex = 1;
    initial.playbackModeIndex = 0;
    initial.pitchBendRangeSemitones = 2.0f;
    initial.monoEnabled = false;
    initial.legatoEnabled = false;
    initial.glideSeconds = 0.0f;
    initial.polyphonyLimit = 64;
    initial.sampleWindowStart = 0;

    auto changedPreload = initial;
    changedPreload.preloadSamples = 4096;

    auto changedTierAndMapping = changedPreload;
    changedTierAndMapping.qualityTierIndex = 0;
    changedTierAndMapping.playbackModeIndex = 1;
    changedTierAndMapping.pitchBendRangeSemitones = 12.0f;
    changedTierAndMapping.monoEnabled = true;
    changedTierAndMapping.legatoEnabled = true;
    changedTierAndMapping.glideSeconds = 0.04f;
    changedTierAndMapping.polyphonyLimit = 8;
    changedTierAndMapping.sampleWindowStart = 2;

    history.recordChange(initial, changedPreload);
    history.recordChange(changedPreload, changedTierAndMapping);

    if (!history.canUndo() || history.canRedo())
        return false;

    auto current = changedTierAndMapping;
    const auto firstUndo = history.undo(current);
    if (!firstUndo.has_value() || *firstUndo != changedPreload)
        return false;

    current = *firstUndo;
    const auto secondUndo = history.undo(current);
    if (!secondUndo.has_value() || *secondUndo != initial)
        return false;

    if (history.canUndo() || !history.canRedo())
        return false;

    current = *secondUndo;
    const auto firstRedo = history.redo(current);
    if (!firstRedo.has_value() || *firstRedo != changedPreload)
        return false;

    current = *firstRedo;
    const auto secondRedo = history.redo(current);
    if (!secondRedo.has_value() || *secondRedo != changedTierAndMapping)
        return false;

    return !history.canRedo() && history.canUndo();
}

bool runSettingsUndoHistoryCapacityTest()
{
    audiocity::engine::SettingsUndoHistory history(2);

    audiocity::engine::SettingsSnapshot s0;
    s0.preloadSamples = 32768;
    s0.qualityTierIndex = 1;
    s0.playbackModeIndex = 0;
    s0.monoEnabled = false;
    s0.legatoEnabled = false;
    s0.glideSeconds = 0.0f;
    s0.sampleWindowStart = 0;

    auto s1 = s0;
    s1.preloadSamples = 16384;

    auto s2 = s1;
    s2.preloadSamples = 8192;
    s2.playbackModeIndex = 1;

    auto s3 = s2;
    s3.preloadSamples = 4096;
    s3.qualityTierIndex = 0;
    s3.playbackModeIndex = 2;
    s3.monoEnabled = true;
    s3.glideSeconds = 0.01f;
    s3.sampleWindowStart = 1;

    history.recordChange(s0, s1);
    history.recordChange(s1, s2);
    history.recordChange(s2, s3);

    auto current = s3;

    const auto undo1 = history.undo(current);
    if (!undo1.has_value() || *undo1 != s2)
        return false;

    current = *undo1;
    const auto undo2 = history.undo(current);
    if (!undo2.has_value() || *undo2 != s1)
        return false;

    current = *undo2;
    const auto undo3 = history.undo(current);
    if (undo3.has_value())
        return false;

    return !history.canUndo() && history.canRedo();
}

bool runSettingsUndoHistoryCoalesceTest()
{
    audiocity::engine::SettingsUndoHistory history;

    audiocity::engine::SettingsSnapshot a;
    a.preloadSamples = 32768;

    auto b = a;
    b.preloadSamples = 30000;

    auto c = b;
    c.preloadSamples = 25000;

    auto d = c;
    d.preloadSamples = 22000;

    history.recordChange(a, b, 1);
    history.recordChange(b, c, 1);
    history.recordChange(c, d, 1);

    auto current = d;
    const auto firstUndo = history.undo(current);
    if (!firstUndo.has_value() || *firstUndo != a)
        return false;

    current = *firstUndo;
    const auto secondUndo = history.undo(current);
    if (secondUndo.has_value())
        return false;

    return history.canRedo();
}

bool runSettingsUndoHistoryLabelsTest()
{
    audiocity::engine::SettingsUndoHistory history;

    audiocity::engine::SettingsSnapshot a;
    a.preloadSamples = 32768;

    auto b = a;
    b.preloadSamples = 4096;

    history.recordChange(a, b, -1, "Change Preload Samples");

    if (history.undoLabel() != "Change Preload Samples")
        return false;

    auto current = b;
    const auto undo = history.undo(current);
    if (!undo.has_value() || *undo != a)
        return false;

    if (!history.undoLabel().empty())
        return false;

    return history.canRedo();
}

bool runSettingsUndoHistoryEditorStateTest()
{
    audiocity::engine::SettingsUndoHistory history;

    audiocity::engine::SettingsSnapshot base;
    base.sampleWindowStart = 4;
    base.sampleWindowEnd = 120;
    base.loopStart = 16;
    base.loopEnd = 96;
    base.fadeInSamples = 2;
    base.fadeOutSamples = 2;
    base.reversePlayback = false;

    auto edited = base;
    edited.sampleWindowStart = 20;
    edited.sampleWindowEnd = 80;
    edited.loopStart = 24;
    edited.loopEnd = 72;
    edited.fadeInSamples = 8;
    edited.fadeOutSamples = 10;
    edited.reversePlayback = true;

    history.recordChange(base, edited, -1, "Edit Sample");

    auto current = edited;
    const auto undo = history.undo(current);
    if (!undo.has_value() || *undo != base)
        return false;

    current = *undo;
    const auto redo = history.redo(current);
    if (!redo.has_value() || *redo != edited)
        return false;

    return true;
}

bool runPlayerPadStateUtilityTest()
{
    auto pads = audiocity::plugin::defaultPlayerPadAssignments();
    if (pads[0].noteNumber != 36 || pads[0].velocity != 100)
        return false;
    if (pads[3].noteNumber != 39 || pads[3].velocity != 100)
        return false;
    if (pads[7].noteNumber != 43 || pads[7].velocity != 100)
        return false;

    const auto sanitized = audiocity::plugin::sanitizePlayerPadAssignment({ -12, 999 });
    if (sanitized.noteNumber != 0 || sanitized.velocity != 127)
        return false;

    return true;
}

bool runFilterModeDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> sample(1, 4096);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 120.0 / sampleRate);
        const auto high = 0.35 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 6200.0 / sampleRate);
        sample.setSample(0, i, static_cast<float>(0.4 * low + high));
    }

    auto renderWithMode = [&](const audiocity::engine::EngineCore::FilterSettings::Mode mode)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setAmpEnvelope({ 0.0001f, 0.001f, 1.0f, 0.1f });
        engine.setFilterEnvelope({ 0.0001f, 0.001f, 0.0f, 0.1f });
        engine.setSampleData(sample, sampleRate, 60);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = mode;
        filter.baseCutoffHz = 1200.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.35f;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto lp12 = renderWithMode(audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12);
    const auto lp24 = renderWithMode(audiocity::engine::EngineCore::FilterSettings::Mode::lowPass24);
    const auto hp12 = renderWithMode(audiocity::engine::EngineCore::FilterSettings::Mode::highPass12);

    if (buffersAreEqual(lp12, hp12, 1.0e-5f))
        return false;

    if (buffersAreEqual(lp12, lp24, 1.0e-6f))
        return false;

    return true;
}

bool runFilterModulationDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(4096);

    auto renderWithSettings = [&](const int note, const float velocity, const float keyTracking, const float velocityHz)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 800.0f;
        filter.envAmountHz = 0.0f;
        filter.keyTracking = keyTracking;
        filter.velocityAmountHz = velocityHz;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, note, velocity), 0);
        engine.render(block, midi);
        return block;
    };

    const auto base = renderWithSettings(60, 0.5f, 0.0f, 0.0f);
    const auto keyTracked = renderWithSettings(84, 0.5f, 1.0f, 0.0f);
    const auto velocityTracked = renderWithSettings(60, 1.0f, 0.0f, 6000.0f);

    if (buffersAreEqual(base, keyTracked, 1.0e-5f))
        return false;

    if (buffersAreEqual(base, velocityTracked, 1.0e-5f))
        return false;

    return true;
}

bool runFilterKeytrackPolarityTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 220.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5400.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.25 * low + high));
    }

    auto renderEnergyForKeytrack = [&](const float keytrack)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 900.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.0f;
        filter.keyTracking = keytrack;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);
        return blockEnergy(block);
    };

    const auto inverseEnergy = renderEnergyForKeytrack(-1.0f);
    const auto neutralEnergy = renderEnergyForKeytrack(0.0f);
    const auto positiveEnergy = renderEnergyForKeytrack(1.0f);
    const auto overTrackedEnergy = renderEnergyForKeytrack(2.0f);

    if (!(inverseEnergy < neutralEnergy))
        return false;

    if (!(positiveEnergy > neutralEnergy))
        return false;

    return overTrackedEnergy > positiveEnergy;
}

bool runFilterLfoDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 180.0 / sampleRate);
        const auto high = 0.7 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5200.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.25 * low + high));
    }

    auto renderWithLfoAmount = [&](const float lfoAmountHz)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1100.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.15f;
        filter.lfoRateHz = 4.0f;
        filter.lfoAmountHz = lfoAmountHz;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 7; ++i)
            engine.render(block, midi);

        return block;
    };

    const auto noLfo = renderWithLfoAmount(0.0f);
    const auto withLfo = renderWithLfoAmount(3000.0f);
    return !buffersAreEqual(noLfo, withLfo, 1.0e-6f);
}

bool runPitchLfoVibratoSettingsTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 48;
    constexpr double sampleRate = 48000.0;

    auto renderWithPitchLfo = [&](const float rateHz, const float depthCents)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createOneCycleSine(128), sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        audiocity::engine::EngineCore::AdsrSettings heldAdsr;
        heldAdsr.attackSeconds = 0.0001f;
        heldAdsr.decaySeconds = 0.0001f;
        heldAdsr.sustainLevel = 1.0f;
        heldAdsr.releaseSeconds = 0.5f;
        engine.setAmpEnvelope(heldAdsr);

        auto pitchLfo = engine.getPitchLfoSettings();
        pitchLfo.rateHz = rateHz;
        pitchLfo.depthCents = depthCents;
        engine.setPitchLfoSettings(pitchLfo);

        return renderHeldNote(engine, 60, blockSize, blocks, channels);
    };

    const auto dry = renderWithPitchLfo(0.0f, 0.0f);
    const auto vibrato = renderWithPitchLfo(5.0f, 40.0f);
    if (buffersAreEqual(dry, vibrato, 1.0e-6f))
        return false;

    const auto slowRate = renderWithPitchLfo(1.5f, 35.0f);
    const auto fastRate = renderWithPitchLfo(8.0f, 35.0f);
    if (buffersAreEqual(slowRate, fastRate, 1.0e-6f))
        return false;

    const auto shallowDepth = renderWithPitchLfo(5.0f, 10.0f);
    const auto deepDepth = renderWithPitchLfo(5.0f, 70.0f);
    return !buffersAreEqual(shallowDepth, deepDepth, 1.0e-6f);
}

bool runAmpLfoTremoloSettingsTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr int blocks = 48;
    constexpr double sampleRate = 48000.0;

    auto renderWithAmpLfo = [&](const float rateHz,
                                const float depth,
                                const audiocity::engine::EngineCore::FilterSettings::LfoShape shape)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(createOneCycleSine(128), sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        audiocity::engine::EngineCore::AdsrSettings heldAdsr;
        heldAdsr.attackSeconds = 0.0001f;
        heldAdsr.decaySeconds = 0.0001f;
        heldAdsr.sustainLevel = 1.0f;
        heldAdsr.releaseSeconds = 0.5f;
        engine.setAmpEnvelope(heldAdsr);

        auto ampLfo = engine.getAmpLfoSettings();
        ampLfo.rateHz = rateHz;
        ampLfo.depth = depth;
        ampLfo.shape = shape;
        engine.setAmpLfoSettings(ampLfo);

        return renderHeldNote(engine, 60, blockSize, blocks, channels);
    };

    const auto dry = renderWithAmpLfo(0.0f, 0.0f, audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    const auto tremolo = renderWithAmpLfo(5.0f, 1.0f, audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    if (buffersAreEqual(dry, tremolo, 1.0e-6f))
        return false;

    const auto sineShape = renderWithAmpLfo(4.0f, 0.8f, audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    const auto squareShape = renderWithAmpLfo(4.0f, 0.8f, audiocity::engine::EngineCore::FilterSettings::LfoShape::square);
    if (buffersAreEqual(sineShape, squareShape, 1.0e-6f))
        return false;

    const auto slowRate = renderWithAmpLfo(1.5f, 0.8f, audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    const auto fastRate = renderWithAmpLfo(8.0f, 0.8f, audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    return !buffersAreEqual(slowRate, fastRate, 1.0e-6f);
}

bool runFilterLfoShapeDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 160.0 / sampleRate);
        const auto high = 0.75 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 4800.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.25 * low + high));
    }

    auto renderWithShape = [&](const audiocity::engine::EngineCore::FilterSettings::LfoShape shape)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1000.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.15f;
        filter.lfoRateHz = 5.0f;
        filter.lfoAmountHz = 2800.0f;
        filter.lfoShape = shape;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        return block;
    };

    const auto sine = renderWithShape(audiocity::engine::EngineCore::FilterSettings::LfoShape::sine);
    const auto square = renderWithShape(audiocity::engine::EngineCore::FilterSettings::LfoShape::square);
    return !buffersAreEqual(sine, square, 1.0e-6f);
}

bool runFilterLfoTempoSyncSettingsTest()
{
    audiocity::engine::EngineCore engine;
    engine.prepare(48000.0, 128, 2);

    auto filter = engine.getFilterSettings();
    filter.lfoTempoSync = true;
    filter.lfoSyncDivision = 11;
    filter.lfoRateHz = 7.5f;
    filter.lfoAmountHz = 1200.0f;
    engine.setFilterSettings(filter);

    const auto applied = engine.getFilterSettings();
    if (!applied.lfoTempoSync)
        return false;

    if (applied.lfoSyncDivision != 11)
        return false;

    return std::abs(applied.lfoRateHz - 7.5f) < 1.0e-6f;
}

bool runFilterLfoRetriggerDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 170.0 / sampleRate);
        const auto high = 0.75 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5000.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.25 * low + high));
    }

    auto renderSecondAttack = [&](const bool retrigger)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 950.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 4.0f;
        filter.lfoAmountHz = 2800.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = retrigger;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto retrigSecondAttack = renderSecondAttack(true);
    const auto freeRunSecondAttack = renderSecondAttack(false);
    return !buffersAreEqual(retrigSecondAttack, freeRunSecondAttack, 1.0e-6f);
}

bool runFilterLfoStartPhaseDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 150.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5400.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderSecondAttack = [&](const float startPhaseDegrees)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 900.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 4.0f;
        filter.lfoAmountHz = 3000.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoStartPhaseDegrees = startPhaseDegrees;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto phaseZero = renderSecondAttack(0.0f);
    const auto phaseQuarter = renderSecondAttack(90.0f);
    return !buffersAreEqual(phaseZero, phaseQuarter, 1.0e-6f);
}

bool runFilterLfoFadeInDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 140.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5600.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderSecondAttack = [&](const float fadeInMs)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 900.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 6.0f;
        filter.lfoAmountHz = 3200.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoStartPhaseDegrees = 90.0f;
        filter.lfoFadeInMs = fadeInMs;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto instant = renderSecondAttack(0.0f);
    const auto faded = renderSecondAttack(250.0f);
    return !buffersAreEqual(instant, faded, 1.0e-6f);
}

bool runFilterLfoStartRandomDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 145.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5450.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderSecondAttack = [&](const float randomDegrees)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 900.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 5.0f;
        filter.lfoAmountHz = 3200.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoStartPhaseDegrees = 45.0f;
        filter.lfoStartPhaseRandomDegrees = randomDegrees;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto fixed = renderSecondAttack(0.0f);
    const auto randomised = renderSecondAttack(120.0f);
    return !buffersAreEqual(fixed, randomised, 1.0e-6f);
}

bool runFilterLfoAmountKeytrackingDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 155.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5300.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderHighNoteSecondAttack = [&](const float lfoAmountKeytrack)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1000.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 5.0f;
        filter.lfoAmountHz = 2400.0f;
        filter.lfoAmountKeyTracking = lfoAmountKeytrack;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 84), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto neutral = renderHighNoteSecondAttack(0.0f);
    const auto positiveTracked = renderHighNoteSecondAttack(1.0f);
    return !buffersAreEqual(neutral, positiveTracked, 1.0e-6f);
}

bool runFilterLfoRateKeytrackingDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 150.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5200.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderHighNoteSecondAttack = [&](const float lfoRateKeytrack)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1050.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 2.0f;
        filter.lfoRateKeyTracking = lfoRateKeytrack;
        filter.lfoAmountHz = 2600.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 84), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto neutral = renderHighNoteSecondAttack(0.0f);
    const auto positiveTracked = renderHighNoteSecondAttack(1.0f);
    return !buffersAreEqual(neutral, positiveTracked, 1.0e-6f);
}

bool runFilterLfoRateKeytrackInTempoSyncToggleDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 150.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5100.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderHighNoteSecondAttack = [&](const bool keytrackInSync)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1000.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 3.0f;
        filter.lfoRateKeyTracking = 1.0f;
        filter.lfoAmountHz = 2600.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoTempoSync = true;
        filter.lfoRateKeytrackInTempoSync = keytrackInSync;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 84), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto disabled = renderHighNoteSecondAttack(false);
    const auto enabled = renderHighNoteSecondAttack(true);
    return !buffersAreEqual(disabled, enabled, 1.0e-6f);
}

bool runFilterLfoKeytrackCurveDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 170.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5000.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderHighNoteSecondAttack = [&](const bool linearCurve)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 980.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 2.0f;
        filter.lfoRateKeyTracking = 1.0f;
        filter.lfoAmountHz = 2600.0f;
        filter.lfoAmountKeyTracking = 1.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoKeytrackLinear = linearCurve;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 84), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto exponential = renderHighNoteSecondAttack(false);
    const auto linear = renderHighNoteSecondAttack(true);
    return !buffersAreEqual(exponential, linear, 1.0e-6f);
}

bool runFilterLfoUnipolarDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> brightSample(1, 4096);
    for (int i = 0; i < brightSample.getNumSamples(); ++i)
    {
        const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 165.0 / sampleRate);
        const auto high = 0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5150.0 / sampleRate);
        brightSample.setSample(0, i, static_cast<float>(0.2 * low + high));
    }

    auto renderSecondAttack = [&](const bool unipolar)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(brightSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);

        audiocity::engine::EngineCore::AdsrSettings adsr;
        adsr.attackSeconds = 0.0001f;
        adsr.decaySeconds = 0.001f;
        adsr.sustainLevel = 1.0f;
        adsr.releaseSeconds = 0.01f;
        engine.setAmpEnvelope(adsr);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 1020.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        filter.lfoRateHz = 3.0f;
        filter.lfoAmountHz = 2800.0f;
        filter.lfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape::sine;
        filter.lfoRetrigger = true;
        filter.lfoUnipolar = unipolar;
        engine.setFilterSettings(filter);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 72), 0);
        engine.render(block, midi);

        midi.clear();
        for (int i = 0; i < 8; ++i)
            engine.render(block, midi);

        midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto bipolar = renderSecondAttack(false);
    const auto uni = renderSecondAttack(true);
    return !buffersAreEqual(bipolar, uni, 1.0e-6f);
}

bool runUltraQualityDifferenceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> sample(1, 8192);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto phase = static_cast<float>(i % 32) / 31.0f;
        const auto stair = std::floor(phase * 8.0f) / 8.0f;
        sample.setSample(0, i, stair * 2.0f - 1.0f);
    }

    auto renderTier = [&](const audiocity::engine::EngineCore::QualityTier tier)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setRootMidiNote(48);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setQualityTier(tier);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 79, 1.0f), 0);
        engine.render(block, midi);
        return block;
    };

    const auto fidelity = renderTier(audiocity::engine::EngineCore::QualityTier::fidelity);
    const auto ultra = renderTier(audiocity::engine::EngineCore::QualityTier::ultra);
    return !buffersAreEqual(fidelity, ultra, 1.0e-7f);
}

struct UltraSpectralMetric
{
    double mainBandEnergy = 0.0;
    double artifactEnergy = 0.0;
    double artifactRatio = std::numeric_limits<double>::infinity();
    int targetBin = 0;
    int peakBin = 0;
};

UltraSpectralMetric measureUltraSpectralMetric(const audiocity::engine::EngineCore::QualityTier tier)
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr int fftOrder = 12;
    constexpr int fftSize = 1 << fftOrder;
    constexpr int warmupBlocks = 4;
    constexpr int captureBlocks = fftSize / blockSize;
    constexpr int sampleLength = 32768;
    constexpr double sampleRate = 48000.0;
    constexpr int sourceRootNote = 60;
    constexpr int playedNote = 79;
    constexpr int targetBin = 1450;
    constexpr int mainBandRadius = 2;

    const auto pitchRatio = std::pow(2.0, static_cast<double>(playedNote - sourceRootNote) / 12.0);
    const auto targetFrequencyHz = static_cast<double>(targetBin) * sampleRate / static_cast<double>(fftSize);
    const auto sourceToneFrequencyHz = targetFrequencyHz / pitchRatio;

    juce::AudioBuffer<float> sample(1, sampleLength);
    for (int i = 0; i < sample.getNumSamples(); ++i)
    {
        const auto phase = 2.0 * juce::MathConstants<double>::pi * sourceToneFrequencyHz * static_cast<double>(i) / sampleRate;
        sample.setSample(0, i, static_cast<float>(0.9 * std::sin(phase)));
    }

    audiocity::engine::EngineCore::AdsrSettings adsr;
    adsr.attackSeconds = 0.0001f;
    adsr.decaySeconds = 0.0001f;
    adsr.sustainLevel = 1.0f;
    adsr.releaseSeconds = 0.1f;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(sample, sampleRate, sourceRootNote);
    engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);
    engine.setAmpEnvelope(adsr);
    engine.setQualityTier(tier);

    juce::AudioBuffer<float> renderedBlock(channels, blockSize);
    juce::AudioBuffer<float> captured(1, fftSize);

    for (int blockIndex = 0; blockIndex < warmupBlocks + captureBlocks; ++blockIndex)
    {
        juce::MidiBuffer midi;
        if (blockIndex == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, playedNote, 1.0f), 0);

        engine.render(renderedBlock, midi);

        if (blockIndex >= warmupBlocks)
        {
            const auto capturedOffset = (blockIndex - warmupBlocks) * blockSize;
            captured.copyFrom(0, capturedOffset, renderedBlock, 0, 0, blockSize);
        }
    }

    std::array<float, fftSize * 2> fftData {};
    std::copy(captured.getReadPointer(0), captured.getReadPointer(0) + fftSize, fftData.begin());

    juce::dsp::WindowingFunction<float> window(fftSize,
                                               juce::dsp::WindowingFunction<float>::hann,
                                               true);
    window.multiplyWithWindowingTable(fftData.data(), fftSize);

    juce::dsp::FFT fft(fftOrder);
    fft.performRealOnlyForwardTransform(fftData.data());

    auto magnitudeSquaredAtBin = [&](const int bin)
    {
        const auto real = static_cast<double>(fftData[static_cast<std::size_t>(bin * 2)]);
        const auto imag = static_cast<double>(fftData[static_cast<std::size_t>(bin * 2 + 1)]);
        return real * real + imag * imag;
    };

    UltraSpectralMetric metric;
    metric.targetBin = targetBin;

    double peakMagnitude = 0.0;
    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const auto magnitudeSquared = magnitudeSquaredAtBin(bin);
        if (magnitudeSquared > peakMagnitude)
        {
            peakMagnitude = magnitudeSquared;
            metric.peakBin = bin;
        }
    }

    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const auto magnitudeSquared = magnitudeSquaredAtBin(bin);
        if (std::abs(bin - targetBin) <= mainBandRadius)
            metric.mainBandEnergy += magnitudeSquared;
        else
            metric.artifactEnergy += magnitudeSquared;
    }

    const auto denominator = juce::jmax(metric.mainBandEnergy, 1.0e-12);
    metric.artifactRatio = metric.artifactEnergy / denominator;
    return metric;
}

bool runUltraQualitySpectralTonePreservationTest()
{
    const auto fidelity = measureUltraSpectralMetric(audiocity::engine::EngineCore::QualityTier::fidelity);
    const auto ultra = measureUltraSpectralMetric(audiocity::engine::EngineCore::QualityTier::ultra);

    return std::isfinite(fidelity.mainBandEnergy)
        && std::isfinite(fidelity.artifactRatio)
        && std::isfinite(ultra.mainBandEnergy)
        && std::isfinite(ultra.artifactRatio)
        && std::abs(fidelity.peakBin - fidelity.targetBin) <= 1
        && std::abs(ultra.peakBin - ultra.targetBin) <= 1
        && fidelity.mainBandEnergy > 1.0
        && ultra.mainBandEnergy > 1.0
        && ultra.mainBandEnergy > fidelity.mainBandEnergy * 1.02
        && ultra.artifactEnergy < fidelity.artifactEnergy
        && ultra.artifactRatio < fidelity.artifactRatio * 0.90;
}

bool runReverbMixTailTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(2048);

    auto renderTailEnergy = [&](const float mix)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(mix);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        float tailEnergy = 0.0f;
        for (int i = 0; i < 10; ++i)
        {
            engine.render(block, midi);
            if (i >= 6)
                tailEnergy += blockEnergy(block);
        }

        return tailEnergy;
    };

    const auto dryTail = renderTailEnergy(0.0f);
    const auto wetTail = renderTailEnergy(0.35f);
    return wetTail > dryTail * 1.05f;
}

bool runDelayMixTailTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> impulse(1, 2048);
    impulse.clear();
    impulse.setSample(0, 0, 1.0f);

    auto renderTailEnergy = [&](const float mix)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(impulse, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(0.0f);

        audiocity::engine::EngineCore::DelaySettings delay;
        delay.timeMs = 30.0f;
        delay.feedback = 0.45f;
        delay.mix = mix;
        delay.tempoSync = false;
        engine.setDelaySettings(delay);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        midi.clear();
        float tailEnergy = 0.0f;
        for (int i = 0; i < 28; ++i)
        {
            engine.render(block, midi);
            if (i >= 5)
                tailEnergy += blockEnergy(block);
        }

        return tailEnergy;
    };

    const auto dryTail = renderTailEnergy(0.0f);
    const auto wetTail = renderTailEnergy(0.35f);
    return wetTail > dryTail * 1.15f;
}

bool runDelayTempoSyncRespondsToTempoTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> impulse(1, 4096);
    impulse.clear();
    impulse.setSample(0, 0, 1.0f);

    auto renderLeftChannel = [&](const float bpm)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(impulse, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(0.0f);
        engine.setHostTempoBpm(bpm);

        audiocity::engine::EngineCore::DelaySettings delay;
        delay.timeMs = 600.0f;
        delay.feedback = 0.0f;
        delay.mix = 1.0f;
        delay.tempoSync = true;
        engine.setDelaySettings(delay);

        constexpr int totalBlocks = 420;
        std::vector<float> rendered;
        rendered.resize(static_cast<std::size_t>(totalBlocks * blockSize), 0.0f);

        juce::AudioBuffer<float> block(channels, blockSize);
        for (int blockIndex = 0; blockIndex < totalBlocks; ++blockIndex)
        {
            juce::MidiBuffer midi;
            if (blockIndex == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

            engine.render(block, midi);
            for (int sampleIndex = 0; sampleIndex < blockSize; ++sampleIndex)
            {
                rendered[static_cast<std::size_t>(blockIndex * blockSize + sampleIndex)] = block.getSample(0, sampleIndex);
            }
        }

        return rendered;
    };

    auto windowEnergy = [](const std::vector<float>& signal, const int center, const int radius)
    {
        const auto start = juce::jlimit(0, static_cast<int>(signal.size()) - 1, center - radius);
        const auto end = juce::jlimit(start, static_cast<int>(signal.size()) - 1, center + radius);
        float energy = 0.0f;
        for (int i = start; i <= end; ++i)
            energy += std::abs(signal[static_cast<std::size_t>(i)]);
        return energy;
    };

    const auto rendered120 = renderLeftChannel(120.0f);
    const auto rendered90 = renderLeftChannel(90.0f);

    const auto sampleAt500ms = static_cast<int>(std::round(sampleRate * 0.500));
    const auto sampleAt667ms = static_cast<int>(std::round(sampleRate * (2.0 / 3.0)));
    constexpr int kWindowRadius = 256;

    const auto e120at500 = windowEnergy(rendered120, sampleAt500ms, kWindowRadius);
    const auto e120at667 = windowEnergy(rendered120, sampleAt667ms, kWindowRadius);
    const auto e90at500 = windowEnergy(rendered90, sampleAt500ms, kWindowRadius);
    const auto e90at667 = windowEnergy(rendered90, sampleAt667ms, kWindowRadius);

    if (e120at500 <= 1.0e-5f || e90at667 <= 1.0e-5f)
        return false;

    return e120at500 > e120at667 * 2.0f
        && e90at667 > e90at500 * 2.0f;
}

bool runDcOffsetFilterRemovesBiasTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> dcSample(1, 4096);
    dcSample.clear();
    for (int i = 0; i < dcSample.getNumSamples(); ++i)
        dcSample.setSample(0, i, 0.5f);

    auto renderMeanWithSettings = [&](const bool enabled, const float cutoffHz)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(dcSample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(0.0f);
        engine.setMasterVolume(1.0f);

        audiocity::engine::EngineCore::DcFilterSettings dc;
        dc.enabled = enabled;
        dc.cutoffHz = cutoffHz;
        engine.setDcFilterSettings(dc);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

        std::vector<float> captured;
        captured.reserve(static_cast<std::size_t>(blockSize * 16));
        for (int blockIndex = 0; blockIndex < 16; ++blockIndex)
        {
            engine.render(block, midi);
            midi.clear();
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
                captured.push_back(block.getSample(0, sample));
        }

        constexpr int kSkip = 384;
        double sum = 0.0;
        int count = 0;
        for (int i = kSkip; i < static_cast<int>(captured.size()); ++i)
        {
            sum += static_cast<double>(captured[static_cast<std::size_t>(i)]);
            ++count;
        }

        return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 0.0f;
    };

    const auto meanBypassed = renderMeanWithSettings(false, 10.0f);
    const auto meanFiltered = renderMeanWithSettings(true, 10.0f);

    return std::abs(meanBypassed) > 0.10f
        && std::abs(meanFiltered) < 0.02f;
}

bool runMasterVolumeGainTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(2048);

    auto renderPeakForVolume = [&](const float volume)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(0.0f);
        engine.setMasterVolume(volume);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        float peak = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            peak = juce::jmax(peak, block.getMagnitude(channel, 0, block.getNumSamples()));

        return peak;
    };

    const auto peakUnity = renderPeakForVolume(1.0f);
    const auto peakHalf = renderPeakForVolume(0.5f);
    const auto peakMute = renderPeakForVolume(0.0f);

    if (peakUnity <= 1.0e-6f)
        return false;

    const auto ratio = peakHalf / peakUnity;
    return ratio > 0.45f && ratio < 0.55f && peakMute < 1.0e-7f;
}

bool runPanBalanceTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double sampleRate = 48000.0;

    auto sample = createTestSample(2048);

    auto renderChannelPeaks = [&](const float pan)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(sampleRate, blockSize, channels);
        engine.setSampleData(sample, sampleRate, 60);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::oneShot);
        engine.setReverbMix(0.0f);
        engine.setMasterVolume(1.0f);
        engine.setPan(pan);

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        engine.render(block, midi);

        const auto leftPeak = block.getMagnitude(0, 0, block.getNumSamples());
        const auto rightPeak = block.getMagnitude(1, 0, block.getNumSamples());
        return std::pair<float, float>{ leftPeak, rightPeak };
    };

    const auto [centerLeft, centerRight] = renderChannelPeaks(0.0f);
    const auto [fullLeft, fullLeftRight] = renderChannelPeaks(-1.0f);
    const auto [fullRightLeft, fullRight] = renderChannelPeaks(1.0f);

    if (centerLeft <= 1.0e-6f || centerRight <= 1.0e-6f)
        return false;

    return fullLeft > centerLeft * 0.85f
        && fullLeftRight < centerRight * 0.05f
        && fullRight > centerRight * 0.85f
        && fullRightLeft < centerLeft * 0.05f;
}

bool runAutopanStereoMotionTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 60;
    constexpr int cycleSamples = 512;
    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);
    const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);

    auto renderDiffStats = [&](const float depth)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);
        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setReverbMix(0.0f);
        engine.setPan(0.0f);
        engine.setMasterVolume(1.0f);

        audiocity::engine::EngineCore::DelaySettings delay;
        delay.mix = 0.0f;
        engine.setDelaySettings(delay);

        audiocity::engine::EngineCore::AutopanSettings autopan;
        autopan.rateHz = 2.0f;
        autopan.depth = juce::jlimit(0.0f, 1.0f, depth);
        engine.setAutopanSettings(autopan);

        juce::AudioBuffer<float> block(channels, blockSize);
        double absDiffSum = 0.0;
        int counted = 0;
        float maxDiff = -std::numeric_limits<float>::infinity();
        float minDiff = std::numeric_limits<float>::infinity();

        for (int blockIndex = 0; blockIndex < 90; ++blockIndex)
        {
            juce::MidiBuffer midi;
            if (blockIndex == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            engine.render(block, midi);

            if (blockIndex < 8)
                continue;

            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto diff = block.getSample(0, sample) - block.getSample(1, sample);
                absDiffSum += std::abs(diff);
                ++counted;
                maxDiff = juce::jmax(maxDiff, diff);
                minDiff = juce::jmin(minDiff, diff);
            }
        }

        struct Stats
        {
            float meanAbsDiff = 0.0f;
            float minDiff = 0.0f;
            float maxDiff = 0.0f;
        };

        Stats stats;
        if (counted > 0)
            stats.meanAbsDiff = static_cast<float>(absDiffSum / static_cast<double>(counted));
        stats.minDiff = minDiff;
        stats.maxDiff = maxDiff;
        return stats;
    };

    const auto dry = renderDiffStats(0.0f);
    const auto mod = renderDiffStats(0.85f);

    return dry.meanAbsDiff < 0.003f
        && mod.meanAbsDiff > 0.05f
        && mod.maxDiff > 0.05f
        && mod.minDiff < -0.05f;
}

bool runSaturationDriveAndModeTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 128;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 60;
    constexpr int cycleSamples = 512;
    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);
    const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);

    auto renderSignal = [&](const float drive,
                            const audiocity::engine::EngineCore::SaturationSettings::Mode mode)
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);
        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setReverbMix(0.0f);

        audiocity::engine::EngineCore::DelaySettings delay;
        delay.mix = 0.0f;
        engine.setDelaySettings(delay);

        audiocity::engine::EngineCore::AutopanSettings autopan;
        autopan.depth = 0.0f;
        engine.setAutopanSettings(autopan);

        engine.setPan(0.0f);
        engine.setMasterVolume(1.0f);

        audiocity::engine::EngineCore::SaturationSettings sat;
        sat.drive = drive;
        sat.mode = mode;
        engine.setSaturationSettings(sat);

        std::vector<float> captured;
        captured.reserve(static_cast<std::size_t>(blockSize * 80));
        for (int blockIndex = 0; blockIndex < 80; ++blockIndex)
        {
            juce::AudioBuffer<float> block(channels, blockSize);
            juce::MidiBuffer midi;
            if (blockIndex == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            engine.render(block, midi);

            if (blockIndex < 8)
                continue;

            for (int sample = 0; sample < blockSize; ++sample)
                captured.push_back(block.getSample(0, sample));
        }

        return captured;
    };

    const auto base = renderSignal(0.0f, audiocity::engine::EngineCore::SaturationSettings::Mode::softClip);
    const auto drivenSoft = renderSignal(0.8f, audiocity::engine::EngineCore::SaturationSettings::Mode::softClip);
    const auto drivenHard = renderSignal(0.8f, audiocity::engine::EngineCore::SaturationSettings::Mode::hardClip);
    const auto drivenTape = renderSignal(0.8f, audiocity::engine::EngineCore::SaturationSettings::Mode::tape);
    const auto drivenTube = renderSignal(0.8f, audiocity::engine::EngineCore::SaturationSettings::Mode::tube);

    if (base.empty() || drivenSoft.empty() || drivenHard.empty() || drivenTape.empty() || drivenTube.empty())
        return false;

    auto meanAbsDifference = [](const std::vector<float>& a, const std::vector<float>& b)
    {
        const auto count = juce::jmin(static_cast<int>(a.size()), static_cast<int>(b.size()));
        if (count <= 0)
            return 0.0f;

        double sum = 0.0;
        for (int i = 0; i < count; ++i)
            sum += std::abs(a[static_cast<std::size_t>(i)] - b[static_cast<std::size_t>(i)]);

        return static_cast<float>(sum / static_cast<double>(count));
    };

    const auto diffDrive = meanAbsDifference(base, drivenSoft);
    const auto diffHard = meanAbsDifference(drivenSoft, drivenHard);
    const auto diffTape = meanAbsDifference(drivenSoft, drivenTape);
    const auto diffTube = meanAbsDifference(drivenSoft, drivenTube);

    return diffDrive > 0.01f
        && diffHard > 0.002f
        && diffTape > 0.002f
        && diffTube > 0.002f;
}

bool runTuneCoarseFinePitchShiftTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr int blocks = 56;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 60;
    constexpr int cycleSamples = 512;

    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);
    const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);

    auto renderFrequency = [&](const float coarseSemitones, const float fineCents) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setCoarseTuneSemitones(coarseSemitones);
        engine.setFineTuneCents(fineCents);

        const auto rendered = renderHeldNote(engine, rootMidiNote, blockSize, blocks, channels);
        return estimateFrequencyFromPositiveCrossings(rendered, outputSampleRate, blockSize * 3);
    };

    const auto baseHz = renderFrequency(0.0f, 0.0f);
    const auto coarseUpHz = renderFrequency(12.0f, 0.0f);
    const auto coarseDownHz = renderFrequency(-12.0f, 0.0f);
    const auto fineUpHz = renderFrequency(0.0f, 100.0f);

    if (baseHz <= 0.0f || coarseUpHz <= 0.0f || coarseDownHz <= 0.0f || fineUpHz <= 0.0f)
        return false;

    const auto coarseUpRatio = coarseUpHz / baseHz;
    const auto coarseDownRatio = coarseDownHz / baseHz;
    const auto fineUpRatio = fineUpHz / baseHz;
    const auto expectedFineRatio = std::pow(2.0f, 1.0f / 12.0f);

    return std::abs(coarseUpRatio - 2.0f) < 0.12f
        && std::abs(coarseDownRatio - 0.5f) < 0.06f
        && std::abs(fineUpRatio - expectedFineRatio) < 0.04f;
}

bool runPitchBendRangeAndRealtimeModulationTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr int blocks = 56;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 60;
    constexpr int cycleSamples = 512;

    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);
    const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);

    auto renderFrequency = [&](const float bendRangeSemitones, const bool bendUp) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        engine.setPitchBendRangeSemitones(bendRangeSemitones);

        juce::AudioBuffer<float> output(channels, blockSize * blocks);
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            if (bendUp && block == 8)
                midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);

            engine.render(blockBuffer, midi);

            for (int ch = 0; ch < channels; ++ch)
                output.copyFrom(ch, block * blockSize, blockBuffer, ch, 0, blockSize);
        }

        return estimateFrequencyFromPositiveCrossings(output, outputSampleRate, blockSize * 20);
    };

    const auto baseHz = renderFrequency(2.0f, false);
    const auto bend2Hz = renderFrequency(2.0f, true);
    const auto bend12Hz = renderFrequency(12.0f, true);

    if (baseHz <= 0.0f || bend2Hz <= 0.0f || bend12Hz <= 0.0f)
        return false;

    const auto ratio2 = bend2Hz / baseHz;
    const auto ratio12 = bend12Hz / baseHz;
    const auto expected2 = std::pow(2.0f, 2.0f / 12.0f);
    const auto expected12 = 2.0f;

    return std::abs(ratio2 - expected2) < 0.05f
        && std::abs(ratio12 - expected12) < 0.12f
        && ratio12 > ratio2 * 1.5f;
}

bool runModWheelRoutingRealtimeTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr int blocks = 56;
    constexpr double outputSampleRate = 48000.0;
    constexpr int rootMidiNote = 60;
    constexpr int cycleSamples = 512;

    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(rootMidiNote);
    const auto sourceSampleRate = targetHz * static_cast<double>(cycleSamples);

    auto renderPitchFrequency = [&](const float modWheelToPitchCents, const bool applyWheel) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        auto routing = engine.getModulationRoutingSettings();
        routing.modWheelToPitchCents = modWheelToPitchCents;
        engine.setModulationRoutingSettings(routing);

        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        juce::AudioBuffer<float> output(channels, blockSize * blocks);
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            if (applyWheel && block == 8)
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);

            engine.render(blockBuffer, midi);

            for (int ch = 0; ch < channels; ++ch)
                output.copyFrom(ch, block * blockSize, blockBuffer, ch, 0, blockSize);
        }

        return estimateFrequencyFromPositiveCrossings(output, outputSampleRate, blockSize * 20);
    };

    auto renderAmpEnergy = [&](const float modWheelToAmp, const bool applyWheel) -> float
    {
        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        auto routing = engine.getModulationRoutingSettings();
        routing.modWheelToAmp = modWheelToAmp;
        engine.setModulationRoutingSettings(routing);

        engine.setSampleData(createOneCycleSine(cycleSamples), sourceSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        float accumulatedEnergy = 0.0f;
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            if (applyWheel && block == 8)
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);

            engine.render(blockBuffer, midi);
            if (block >= 16)
                accumulatedEnergy += blockEnergy(blockBuffer);
        }

        return accumulatedEnergy;
    };

    auto renderFilterEnergy = [&](const float modWheelToFilterHz, const bool applyWheel) -> float
    {
        juce::AudioBuffer<float> brightSample(1, 4096);
        for (int i = 0; i < brightSample.getNumSamples(); ++i)
        {
            const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * i * 180.0 / outputSampleRate);
            const auto high = 0.7 * std::sin(2.0 * juce::MathConstants<double>::pi * i * 5200.0 / outputSampleRate);
            brightSample.setSample(0, i, static_cast<float>(0.25 * low + high));
        }

        audiocity::engine::EngineCore engine;
        engine.prepare(outputSampleRate, blockSize, channels);

        audiocity::engine::EngineCore::AdsrSettings sustain;
        sustain.attackSeconds = 0.0001f;
        sustain.decaySeconds = 0.0001f;
        sustain.sustainLevel = 1.0f;
        sustain.releaseSeconds = 0.25f;
        engine.setAmpEnvelope(sustain);

        audiocity::engine::EngineCore::FilterSettings filter;
        filter.mode = audiocity::engine::EngineCore::FilterSettings::Mode::lowPass12;
        filter.baseCutoffHz = 900.0f;
        filter.envAmountHz = 0.0f;
        filter.resonance = 0.1f;
        engine.setFilterSettings(filter);

        auto routing = engine.getModulationRoutingSettings();
        routing.modWheelToFilterHz = modWheelToFilterHz;
        engine.setModulationRoutingSettings(routing);

        engine.setSampleData(brightSample, outputSampleRate, rootMidiNote);
        engine.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);

        float accumulatedEnergy = 0.0f;
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> blockBuffer(channels, blockSize);
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, rootMidiNote, 1.0f), 0);

            if (applyWheel && block == 8)
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);

            engine.render(blockBuffer, midi);
            if (block >= 16)
                accumulatedEnergy += blockEnergy(blockBuffer);
        }

        return accumulatedEnergy;
    };

    const auto basePitchHz = renderPitchFrequency(200.0f, false);
    const auto wheelPitchHz = renderPitchFrequency(200.0f, true);
    if (basePitchHz <= 0.0f || wheelPitchHz <= 0.0f)
        return false;

    const auto ampBaseEnergy = renderAmpEnergy(-1.0f, false);
    const auto ampWheelEnergy = renderAmpEnergy(-1.0f, true);
    const auto filterBaseEnergy = renderFilterEnergy(6000.0f, false);
    const auto filterWheelEnergy = renderFilterEnergy(6000.0f, true);

    return wheelPitchHz > basePitchHz * 1.08f
        && ampBaseEnergy > 0.1f
        && ampWheelEnergy < ampBaseEnergy * 0.2f
        && filterWheelEnergy > filterBaseEnergy * 1.15f;
}

bool runVoicePlaybackStateSnapshotTest()
{
    constexpr int channels = 2;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;

    audiocity::engine::EngineCore engine;
    engine.prepare(sampleRate, blockSize, channels);
    engine.setSampleData(createTestSample(4096), sampleRate, 60);

    {
        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 32);
        engine.render(block, midi);
    }

    const auto states = engine.getVoicePlaybackStates();
    int activeCount = 0;
    int positivePositions = 0;
    for (const auto& state : states)
    {
        if (!state.active)
        {
            if (state.sampleIndex >= 0)
                return false;
            continue;
        }

        ++activeCount;
        if (state.sampleIndex > 0)
            ++positivePositions;
    }

    if (activeCount != 2 || positivePositions == 0)
        return false;

    engine.panic();
    const auto afterPanic = engine.getVoicePlaybackStates();
    for (const auto& state : afterPanic)
    {
        if (state.active || state.sampleIndex >= 0)
            return false;
    }

    return true;
}

bool runCcLearnDialUserClearCallbackTest()
{
    CcLearnDial dial("Test", 0.0, 1.0, 0.01, {}, 0.5);
    bool callbackInvoked = false;
    dial.onCcClearedByUser = [&callbackInvoked]
    {
        callbackInvoked = true;
    };

    dial.assignCc(74);
    dial.clearCcByUser();

    return callbackInvoked && dial.getAssignedCc() == -1 && !dial.isCcLearnArmed();
}

bool runSettingsSnapshotCaptureFieldsAffectEqualityTest()
{
    audiocity::engine::SettingsSnapshot baseline;
    baseline.captureTargetSampleRate = 0;
    baseline.captureChannelMode = 0;
    baseline.captureBitDepth = 16;
    baseline.captureInputGain = 1.0f;

    auto changed = baseline;
    changed.captureTargetSampleRate = 48000;
    if (changed == baseline)
        return false;

    changed = baseline;
    changed.captureChannelMode = 3;
    if (changed == baseline)
        return false;

    changed = baseline;
    changed.captureBitDepth = 24;
    if (changed == baseline)
        return false;

    changed = baseline;
    changed.captureInputGain = 1.25f;
    if (changed == baseline)
        return false;

    return true;
}

bool runSettingsUndoHistoryTracksCaptureSettingsTest()
{
    audiocity::engine::SettingsUndoHistory history;
    audiocity::engine::SettingsSnapshot before;
    audiocity::engine::SettingsSnapshot after = before;
    after.captureTargetSampleRate = 44100;
    after.captureChannelMode = 1;
    after.captureBitDepth = 24;
    after.captureInputGain = 1.4f;

    history.recordChange(before, after, -1, "Capture Settings");
    if (!history.canUndo())
        return false;

    const auto undoSnapshot = history.undo(after);
    if (!undoSnapshot.has_value())
        return false;

    if (undoSnapshot->captureTargetSampleRate != before.captureTargetSampleRate
        || undoSnapshot->captureChannelMode != before.captureChannelMode
        || undoSnapshot->captureBitDepth != before.captureBitDepth
        || undoSnapshot->captureInputGain != before.captureInputGain)
    {
        return false;
    }

    const auto redoSnapshot = history.redo(before);
    if (!redoSnapshot.has_value())
        return false;

    return redoSnapshot->captureTargetSampleRate == after.captureTargetSampleRate
        && redoSnapshot->captureChannelMode == after.captureChannelMode
        && redoSnapshot->captureBitDepth == after.captureBitDepth
        && redoSnapshot->captureInputGain == after.captureInputGain;
}

bool runPresetXmlRoundTripWithEmbeddedSampleDataTest()
{
    constexpr auto kPatchRoot = "AudiocityPatch";

    juce::ValueTree state(kPatchRoot);
    state.setProperty("samplePath", juce::String("C:/Samples/Kick.wav"), nullptr);
    state.setProperty("rootMidiNote", 60, nullptr);
    state.setProperty("playbackMode", 2, nullptr);

    std::vector<float> generatedWave{ 0.0f, 0.25f, -0.25f, 0.75f, -0.5f };
    juce::MemoryBlock waveBytes(generatedWave.size() * sizeof(float));
    std::memcpy(waveBytes.getData(), generatedWave.data(), waveBytes.getSize());
    state.setProperty("generatedWaveformData", juce::var(waveBytes), nullptr);

    const auto xml = audiocity::plugin::encodePresetXml(state);
    if (xml.isEmpty())
        return false;

    juce::ValueTree decoded;
    juce::String error;
    if (!audiocity::plugin::decodePresetXml(xml, decoded, error))
        return false;

    if (!decoded.isValid() || !decoded.hasType(kPatchRoot))
        return false;

    if (decoded.getProperty("samplePath").toString() != "C:/Samples/Kick.wav")
        return false;

    if (static_cast<int>(decoded.getProperty("rootMidiNote", -1)) != 60)
        return false;

    const auto* decodedBinary = decoded.getProperty("generatedWaveformData").getBinaryData();
    if (decodedBinary == nullptr || decodedBinary->getSize() != waveBytes.getSize())
        return false;

    return std::memcmp(decodedBinary->getData(), waveBytes.getData(), waveBytes.getSize()) == 0;
}

bool runPresetXmlRejectsInvalidPayloadTest()
{
    juce::ValueTree decoded;
    juce::String error;
    const auto ok = audiocity::plugin::decodePresetXml("this is not xml", decoded, error);
    return !ok && error.isNotEmpty();
}

bool runLibraryMetadataFavoritesAndRecentsTest()
{
    audiocity::plugin::LibraryMetadata metadata;
    const juce::String kick = "C:/Library/Drums/Kick.wav";
    const juce::String kickWindows = "C:\\Library\\Drums\\Kick.wav";
    const juce::String snare = "C:/Library/Drums/Snare.wav";

    metadata.setFavorite(kick, true);
    metadata.setFavorite(kickWindows, true);
    if (!metadata.isFavorite(kickWindows) || metadata.getFavoritePaths().size() != 1)
        return false;

    metadata.setFavorite(kickWindows, false);
    if (metadata.isFavorite(kick) || !metadata.getFavoritePaths().isEmpty())
        return false;

    metadata.markRecent(kick);
    metadata.markRecent(snare);
    metadata.markRecent(kickWindows);
    if (metadata.getRecentPaths().size() != 2
        || metadata.recentRank(kick) != 0
        || metadata.recentRank(snare) != 1)
    {
        return false;
    }

    juce::StringArray tags;
    tags.add("Drums");
    tags.add("#OneShot");
    tags.add("drums");
    metadata.setTags(kick, tags);
    if (!metadata.hasTag(kickWindows, "oneshot")
        || metadata.getTags(kick).size() != 2)
    {
        return false;
    }

    const auto allTags = metadata.getAllTags();
    if (allTags.size() != 2
        || !allTags[0].equalsIgnoreCase("Drums")
        || !allTags[1].equalsIgnoreCase("OneShot"))
    {
        return false;
    }

    juce::StringArray emptyTags;
    metadata.setTags(kick, emptyTags);
    if (!metadata.getTags(kick).isEmpty())
        return false;

    for (int i = 0; i < audiocity::plugin::LibraryMetadata::maxRecentItems + 4; ++i)
        metadata.markRecent("C:/Library/Generated/Item" + juce::String(i) + ".wav");

    return metadata.getRecentPaths().size() == audiocity::plugin::LibraryMetadata::maxRecentItems
        && !metadata.isRecent(snare);
}

bool runLibraryMetadataValueTreeRoundTripTest()
{
    audiocity::plugin::LibraryMetadata metadata;
    const juce::String favorite = "C:/Library/Bass/Sub.wav";
    const juce::String newest = "C:/Library/Keys/EPiano.wav";
    const juce::String older = "C:/Library/Keys/Clav.wav";

    metadata.setFavorite(favorite, true);
    metadata.markRecent(older);
    metadata.markRecent(newest);
    juce::StringArray tags;
    tags.add("Keys");
    tags.add("Warm");
    metadata.setTags(newest, tags);

    const auto restored = audiocity::plugin::LibraryMetadata::fromValueTree(metadata.toValueTree());
    if (!restored.isFavorite(favorite)
        || restored.recentRank(newest) != 0
        || restored.recentRank(older) != 1
        || !restored.hasTag(newest, "warm"))
    {
        return false;
    }

    const auto empty = audiocity::plugin::LibraryMetadata::fromValueTree(juce::ValueTree("notLibraryMetadata"));
    return empty.getFavoritePaths().isEmpty() && empty.getRecentPaths().isEmpty();
}

bool runLibraryMetadataBookmarksTest()
{
    audiocity::plugin::LibraryMetadata metadata;
    const juce::String drums = "C:\\Library\\Drums";
    const juce::String drumsNormalized = "C:/Library/Drums";
    const juce::String synths = "C:/Library/Synths";

    metadata.addBookmark(drums);
    metadata.addBookmark(synths);
    metadata.addBookmark(drumsNormalized);

    auto bookmarks = metadata.getBookmarkPaths();
    if (bookmarks.size() != 2
        || !metadata.isBookmark(drumsNormalized)
        || !bookmarks[0].equalsIgnoreCase(drumsNormalized)
        || !bookmarks[1].equalsIgnoreCase(synths))
    {
        return false;
    }

    metadata.removeBookmark("c:/library/drums");
    if (metadata.isBookmark(drums) || metadata.getBookmarkPaths().size() != 1)
        return false;

    metadata.addBookmark(drums);
    for (int i = 0; i < audiocity::plugin::LibraryMetadata::maxBookmarkItems + 4; ++i)
        metadata.addBookmark("C:/Library/Generated/Root" + juce::String(i));

    bookmarks = metadata.getBookmarkPaths();
    if (bookmarks.size() != audiocity::plugin::LibraryMetadata::maxBookmarkItems
        || metadata.isBookmark(synths)
        || metadata.isBookmark(drums))
    {
        return false;
    }

    const auto restored = audiocity::plugin::LibraryMetadata::fromValueTree(metadata.toValueTree());
    const auto restoredBookmarks = restored.getBookmarkPaths();
    if (restoredBookmarks.size() != bookmarks.size())
        return false;

    for (int index = 0; index < bookmarks.size(); ++index)
    {
        if (!restoredBookmarks[index].equalsIgnoreCase(bookmarks[index]))
            return false;
    }

    return true;
}

bool runLibraryFileIndexScanTest()
{
    using audiocity::plugin::LibraryFileIndex;

    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("audiocity_library_index_test", "");
    if (!tempRoot.createDirectory())
        return false;

    const auto nested = tempRoot.getChildFile("Nested");
    if (!nested.createDirectory()
        || !tempRoot.getChildFile("Kick.WAV").replaceWithText("")
        || !tempRoot.getChildFile("Patch.sfz").replaceWithText("<region> sample=Kick.WAV\n")
        || !tempRoot.getChildFile("Loop.rx2").replaceWithText("")
        || !tempRoot.getChildFile("Ignore.txt").replaceWithText("")
        || !nested.getChildFile("Snare.aif").replaceWithText(""))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    if (!LibraryFileIndex::isSupportedExtension(".RX2", true)
        || LibraryFileIndex::isSupportedExtension(".RX2", false)
        || LibraryFileIndex::isSupportedExtension(".txt", true))
    {
        tempRoot.deleteRecursively();
        return false;
    }

    const auto withoutRex = LibraryFileIndex::scanRoot(tempRoot, false);
    const auto withRex = LibraryFileIndex::scanRoot(tempRoot, true);

    auto hasRelativePath = [](const std::vector<audiocity::plugin::LibraryFileIndexEntry>& entries,
                              const juce::String& relativePath)
    {
        for (const auto& entry : entries)
        {
            if (entry.relativePath == relativePath)
                return true;
        }

        return false;
    };

    auto hasInstrument = [](const std::vector<audiocity::plugin::LibraryFileIndexEntry>& entries)
    {
        for (const auto& entry : entries)
        {
            if (entry.isInstrument && entry.extensionLower == ".sfz")
                return true;
        }

        return false;
    };

    const auto ok = withoutRex.size() == 3
        && withRex.size() == 4
        && hasRelativePath(withoutRex, "Nested/Snare.aif")
        && !hasRelativePath(withoutRex, "Loop.rx2")
        && hasRelativePath(withRex, "Loop.rx2")
        && hasInstrument(withoutRex);

    tempRoot.deleteRecursively();
    return ok;
}

bool runPeakPreviewCacheRoundTripTest()
{
    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("audiocity_peak_cache_tests");
    tempRoot.createDirectory();

    const auto cacheFile = tempRoot.getChildFile("peak_preview_cache.xml");
    audiocity::plugin::PeakPreviewCacheStore store(cacheFile);
    store.reset();

    audiocity::plugin::PeakPreviewCacheData saveData;
    saveData.libraryRootPath = "C:/Library/Samples";
    saveData.entries["c:/library/samples/kick.wav"] = {
        2048,
        { 0.1f, 0.5f, 0.9f },
        "SR: 48000 Hz  Ch: 1",
        "Acidized",
        "Loop: 100-800"
    };

    if (!store.save(saveData))
        return false;

    const auto loaded = store.load();
    if (!loaded.libraryRootPath.equalsIgnoreCase(saveData.libraryRootPath))
        return false;

    const auto it = loaded.entries.find("c:/library/samples/kick.wav");
    if (it == loaded.entries.end())
        return false;

    const auto& entry = it->second;
    if (entry.fileSizeBytes != 2048)
        return false;
    if (entry.metadataLine != "SR: 48000 Hz  Ch: 1")
        return false;
    if (entry.loopFormatBadge != "Acidized")
        return false;
    if (entry.loopMetadataLine != "Loop: 100-800")
        return false;
    if (entry.peaks.size() != 3)
        return false;

    return std::abs(entry.peaks[0] - 0.1f) < 1.0e-5f
        && std::abs(entry.peaks[1] - 0.5f) < 1.0e-5f
        && std::abs(entry.peaks[2] - 0.9f) < 1.0e-5f;
}

bool runPeakPreviewCacheResetClearsFileTest()
{
    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("audiocity_peak_cache_tests");
    tempRoot.createDirectory();

    const auto cacheFile = tempRoot.getChildFile("peak_preview_cache_reset.xml");
    audiocity::plugin::PeakPreviewCacheStore store(cacheFile);
    store.reset();

    audiocity::plugin::PeakPreviewCacheData saveData;
    saveData.libraryRootPath = "C:/Library/A";
    saveData.entries["c:/library/a/snare.wav"] = {
        100,
        { 0.2f },
        "meta",
        {},
        {}
    };

    if (!store.save(saveData))
        return false;
    if (!cacheFile.existsAsFile())
        return false;

    if (!store.reset())
        return false;

    const auto loaded = store.load();
    return loaded.libraryRootPath.isEmpty() && loaded.entries.empty();
}
}

int main()
{
    if (!runDeterminismTest())
        return 1;

    if (!runProgramModelRangeAndZoneMatchingTest())
        return 73;

    if (!runProgramSnapshotBuildAndMatchTest())
        return 75;

    if (!runProgramMappingRowsTest())
        return 97;

    if (!runProgramMappingEditTest())
        return 98;

    if (!runProgramMappingOverviewEditTest())
        return 99;

    if (!runProgramMappingSampleWindowEditTest())
        return 100;

    if (!runProgramMappingZoneOperationsTest())
        return 102;

    if (!runProgramMappingAtomicBatchEditRollbackTest())
        return 111;

    if (!runProgramMappingAtomicBatchDeleteRollbackTest())
        return 112;

    if (!runProgramMappingStateRoundTripTest())
        return 101;

    if (!runProgramMappingStructuralStateRoundTripTest())
        return 103;

    if (!runImportedProgramStateSubtreeRoundTripTest())
        return 106;

    if (!runImportedProgramStateLegacyReplayFallbackTest())
        return 107;

    if (!runImportedProgramRestoreResultSuccessTest())
        return 108;

    if (!runImportedProgramRestoreResultAtomicFailureTest())
        return 109;

    if (!runImportedProgramDerivedStateSummaryTest())
        return 110;

    if (!runSfzImportIncludeDefineDefaultPathTest())
        return 87;

    if (!runSfzImportRoundRobinPlaybackTest())
        return 88;

    if (!runSfzImportSeqLengthPlaybackTest())
        return 104;

    if (!runSfzImportReleaseTriggerPlaybackTest())
        return 105;

    if (!runSfzImporterDiagnosticsTest())
        return 89;

    if (!runSameOffsetMidiEventOrderTest())
        return 74;

    if (!runEngineProgramSnapshotZoneSelectionTest())
        return 76;

    if (!runEngineProgramSampleAssetBindingTest())
        return 77;

    if (!runEngineProgramStereoAssetPlaybackTest())
        return 81;

    if (!runEngineProgramRoundRobinZoneSelectionTest())
        return 82;

    if (!runEngineProgramLayeredZonePlaybackTest())
        return 90;

    if (!runEngineProgramVelocityFadeInTest())
        return 91;

    if (!runEngineProgramCycleRandomRoundRobinZoneSelectionTest())
        return 85;

    if (!runEngineProgramChokeGroupTest())
        return 83;

    if (!runEngineProgramZoneAndGroupPanTest())
        return 84;

    if (!runEngineProgramZoneTriggerModeTest())
        return 86;

    if (!runEngineProgramZoneGainAndTuneTest())
        return 78;

    if (!runEngineProgramZoneSampleWindowTest())
        return 79;

    if (!runEngineProgramZoneLoopModeTest())
        return 80;

    if (!runVoiceStealingEdgeCaseTest())
        return 2;

    if (!runPolyphonyLimitControlTest())
        return 52;

    if (!runPlaybackModesTest())
        return 3;

    if (!runLoopMarkersTest())
        return 4;

    if (!runLoadSampleResetsPlaybackAndLoopRangesTest())
        return 44;

    if (!runLoadSampleClearsProgramSnapshotTest())
        return 92;

    if (!runLoadSampleResetsEnvelopeAndFilterDefaultsTest())
        return 45;

    if (!runFilterModulationAmountsAreBipolarTest())
        return 70;

    if (!runEmbeddedLoopMetadataLoadsWithoutRootNoteTest())
        return 64;

    if (!runRexRuntimeFallbackSmokeTest())
        return 62;

    if (!runCcLearnDialUserClearCallbackTest())
        return 63;

    if (!runGeneratedCyclePitchInvariantAcrossSampleCountsTest())
        return 46;

    if (!runDisplayMinMaxPreservesPolarityTest())
        return 48;

    if (!runLoadedSampleMetadataForGeneratedDataTest())
        return 69;

    if (!runParameterIdSafetyTest())
        return 72;

    if (!runEditorSampleEditControlsTest())
        return 5;

    if (!runPolyphonicDifferentNotesLayerWhenMonoOffTest())
        return 43;

    if (!runMonoLegatoUsesSingleVoiceTest())
        return 7;

    if (!runPolyphonicSameNoteReleaseTest())
        return 24;

    if (!runDenseLoopModeOverflowDoesNotStickNotesTest())
        return 55;

    if (!runQueueSaturatedByPitchBendStillReleasesNoteOffTest())
        return 56;

    if (!runGlideChangesLegatoTransitionTest())
        return 8;

    if (!runPreloadSegmentationDeterminismTest())
        return 9;

    if (!runRuntimePreloadChangeStabilityTest())
        return 10;

    if (!runRuntimeSampleReloadStabilityTest())
        return 11;

    if (!runLoopModeRuntimePreloadChangeStabilityTest())
        return 12;

    if (!runSegmentRebuildCounterTest())
        return 13;

    if (!runProgramPreloadMetricsAndRebuildTest())
        return 14;

    if (!runSingleSampleFileStreamingPreloadMetricsTest())
        return 170;

    if (!runProgramStreamPrimingAndCacheMetricsTest())
        return 171;

    if (!runProgramStreamLookaheadPrimingTest())
        return 172;

    if (!runProgramMappingCreateZoneTest())
        return 176;

    if (!runQualityTierDifferenceTest())
        return 15;

    if (!runQualityTierDeterminismTest())
        return 16;

    if (!runCpuQualityEnergyDriftSmokeTest())
        return 17;

    if (!runRuntimeQualitySwitchSmokeTest())
        return 18;

    if (!runEditorUndoHistoryMixedOrderTest())
        return 173;

    if (!runEditorUndoHistoryCoalesceAndLabelsTest())
        return 174;

    if (!runEditorUndoHistoryDuplicateAndSplitTest())
        return 175;

    if (!runEditorUndoHistoryCreateZoneTest())
        return 177;

    if (!runSettingsUndoHistoryTest())
        return 18;

    if (!runSettingsUndoHistoryCapacityTest())
        return 19;

    if (!runSettingsUndoHistoryCoalesceTest())
        return 20;

    if (!runSettingsUndoHistoryLabelsTest())
        return 21;

    if (!runSettingsUndoHistoryEditorStateTest())
        return 22;

    if (!runSettingsSnapshotCaptureFieldsAffectEqualityTest())
        return 65;

    if (!runSettingsUndoHistoryTracksCaptureSettingsTest())
        return 66;

    if (!runPresetXmlRoundTripWithEmbeddedSampleDataTest())
        return 67;

    if (!runPresetXmlRejectsInvalidPayloadTest())
        return 68;

    if (!runLibraryMetadataFavoritesAndRecentsTest())
        return 93;

    if (!runLibraryMetadataValueTreeRoundTripTest())
        return 94;

    if (!runLibraryMetadataBookmarksTest())
        return 95;

    if (!runLibraryFileIndexScanTest())
        return 96;

    if (!runPeakPreviewCacheRoundTripTest())
        return 70;

    if (!runPeakPreviewCacheResetClearsFileTest())
        return 71;

    if (!runPlayerPadStateUtilityTest())
        return 23;

    if (!runFilterModeDifferenceTest())
        return 25;

    if (!runFilterModulationDifferenceTest())
        return 26;

    if (!runFilterKeytrackPolarityTest())
        return 30;

    if (!runFilterLfoDifferenceTest())
        return 31;

    if (!runPitchLfoVibratoSettingsTest())
        return 53;

    if (!runFilterLfoShapeDifferenceTest())
        return 32;

    if (!runAmpLfoTremoloSettingsTest())
        return 52;

    if (!runFilterLfoTempoSyncSettingsTest())
        return 33;

    if (!runFilterLfoRetriggerDifferenceTest())
        return 34;

    if (!runFilterLfoStartPhaseDifferenceTest())
        return 35;

    if (!runFilterLfoFadeInDifferenceTest())
        return 36;

    if (!runFilterLfoStartRandomDifferenceTest())
        return 37;

    if (!runFilterLfoAmountKeytrackingDifferenceTest())
        return 38;

    if (!runFilterLfoRateKeytrackingDifferenceTest())
        return 39;

    if (!runFilterLfoRateKeytrackInTempoSyncToggleDifferenceTest())
        return 40;

    if (!runFilterLfoKeytrackCurveDifferenceTest())
        return 41;

    if (!runFilterLfoUnipolarDifferenceTest())
        return 42;

    if (!runUltraQualityDifferenceTest())
        return 27;

    if (!runUltraQualitySpectralTonePreservationTest())
        return 178;

    if (!runReverbMixTailTest())
        return 28;

    if (!runDelayMixTailTest())
        return 57;

    if (!runDelayTempoSyncRespondsToTempoTest())
        return 58;

    if (!runDcOffsetFilterRemovesBiasTest())
        return 59;

    if (!runMasterVolumeGainTest())
        return 47;

    if (!runPanBalanceTest())
        return 51;

    if (!runAutopanStereoMotionTest())
        return 60;

    if (!runSaturationDriveAndModeTest())
        return 61;

    if (!runTuneCoarseFinePitchShiftTest())
        return 49;

    if (!runPitchBendRangeAndRealtimeModulationTest())
        return 50;

    if (!runModWheelRoutingRealtimeTest())
        return 179;

    if (!runVoicePlaybackStateSnapshotTest())
        return 54;

    if (!runLoopCrossfadeSmoothsBoundaryTest())
        return 29;

    return 0;
}
