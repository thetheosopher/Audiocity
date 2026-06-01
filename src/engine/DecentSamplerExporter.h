#pragma once

#include "ProgramModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace audiocity::engine::dspreset_export
{
struct ExportDiagnostic
{
    enum class Severity
    {
        warning,
        error
    };

    Severity severity = Severity::warning;
    std::string message;
};

struct ExportOptions
{
    bool copySamples = true;
    std::string samplesSubfolderName = "Samples";
    std::string libraryDisplayName;
};

struct ExportResult
{
    juce::File writtenFile;
    juce::File samplesFolder;
    int writtenSampleCount = 0;
    int copiedSampleCount = 0;
    std::vector<ExportDiagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept
    {
        for (const auto& d : diagnostics)
        {
            if (d.severity == ExportDiagnostic::Severity::error)
                return true;
        }
        return false;
    }
};

ExportResult exportProgramToDecentSampler(const juce::File& destPreset,
                                          const Program& program,
                                          const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset,
                                          const ExportOptions& options);
} // namespace audiocity::engine::dspreset_export