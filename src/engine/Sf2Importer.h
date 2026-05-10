#pragma once

#include "ProgramModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace audiocity::engine::sf2
{
struct ImportDiagnostic
{
    enum class Severity
    {
        warning,
        error
    };

    Severity severity = Severity::warning;
    std::string message;
};

struct PresetInfo
{
    juce::String name;
    int bank = 0;
    int program = 0;
};

struct ProbeResult
{
    std::vector<ImportDiagnostic> diagnostics;
    std::vector<PresetInfo> availablePresets;

    [[nodiscard]] bool hasErrors() const noexcept;
};

struct ImportResult
{
    Program program;
    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    std::vector<ImportDiagnostic> diagnostics;
    std::vector<PresetInfo> availablePresets;
    int chosenPresetIndex = -1;

    [[nodiscard]] bool hasErrors() const noexcept;

    [[nodiscard]] bool hasPlayableProgram() const noexcept
    {
        return program.hasPlayableZones() && !sampleDataByAsset.empty();
    }
};

// importFile imports the first preset (lowest bank, then preset number) of the SF2 file.
[[nodiscard]] ImportResult importFile(const juce::File& sf2File);

// probeFile parses the SoundFont container and enumerates available presets
// without decoding sample payloads.
[[nodiscard]] ProbeResult probeFile(const juce::File& sf2File);

// importFilePreset imports the preset selected by zero-based index into the
// availablePresets list of a previous probe; returns errors if out of range.
[[nodiscard]] ImportResult importFilePreset(const juce::File& sf2File, int presetIndex);

[[nodiscard]] juce::String buildImportSummary(const ImportResult& result, bool imported);
} // namespace audiocity::engine::sf2
