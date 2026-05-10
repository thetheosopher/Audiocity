#pragma once

#include "ProgramModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace audiocity::engine::bitwig
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

struct ImportResult
{
    Program program;
    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    std::vector<ImportDiagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept;

    [[nodiscard]] bool hasPlayableProgram() const noexcept
    {
        return program.hasPlayableZones() && !sampleDataByAsset.empty();
    }
};

[[nodiscard]] ImportResult importFile(const juce::File& multisampleFile);
[[nodiscard]] juce::String buildImportSummary(const ImportResult& result, bool imported);
} // namespace audiocity::engine::bitwig
