#pragma once

#include "ProgramModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <vector>

namespace audiocity::engine::nki
{
enum class ProbeStatus
{
    invalidFile,
    legacyDiscreteSampleCandidate,
    unsupportedContainerReference,
    unsupportedOrUnrecognized
};

enum class DiagnosticSeverity
{
    info,
    warning,
    error
};

struct ProbeDiagnostic
{
    DiagnosticSeverity severity = DiagnosticSeverity::info;
    juce::String message;
};

struct ProbeZoneMetadata
{
    juce::String groupName;
    juce::String zoneName;
    juce::String sampleReference;
    int lowKey = -1;
    int highKey = -1;
    int rootKey = -1;
    int lowVelocity = -1;
    int highVelocity = -1;
    int sampleStart = -1;
    int sampleEnd = -1;
    int loopStart = -1;
    int loopEnd = -1;
    float gainDb = 0.0f;
    float pan = 0.0f;
    float tuneCents = 0.0f;
    int transposeSemitones = 0;
    ZoneTriggerMode triggerMode = ZoneTriggerMode::gate;
    bool hasTriggerMode = false;
    ZoneLoopMode loopMode = ZoneLoopMode::noLoop;
    bool hasLoopMode = false;
};

struct ProbeResult
{
    ProbeStatus status = ProbeStatus::invalidFile;
    juce::String instrumentName;
    juce::StringArray sampleReferences;
    juce::StringArray groupNames;
    juce::StringArray resolvedSampleFiles;
    juce::StringArray missingSampleReferences;
    juce::StringArray containerReferences;
    std::vector<ProbeZoneMetadata> zoneMetadata;
    std::vector<ProbeDiagnostic> diagnostics;

    [[nodiscard]] bool isLegacyDiscreteSampleCandidate() const noexcept
    {
        return status == ProbeStatus::legacyDiscreteSampleCandidate;
    }

    [[nodiscard]] bool hasErrors() const noexcept;
};

struct ImportResult
{
    ProbeResult probe;
    Program program;
    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;

    [[nodiscard]] bool hasErrors() const noexcept
    {
        return probe.hasErrors();
    }

    [[nodiscard]] bool hasPlayableProgram() const noexcept
    {
        return program.hasPlayableZones() && !sampleDataByAsset.empty();
    }
};

[[nodiscard]] ProbeResult probeFile(const juce::File& file);
[[nodiscard]] ImportResult importFile(const juce::File& file);
[[nodiscard]] juce::String buildProbeSummary(const ProbeResult& result);
} // namespace audiocity::engine::nki