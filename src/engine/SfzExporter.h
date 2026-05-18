#pragma once

#include "ProgramModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace audiocity::engine::sfz_export
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
    // When true, sample assets that have decoded audio buffers (or whose source file
    // is present on disk) are copied next to the SFZ under a "Samples" subfolder, and
    // the SFZ references them with relative paths. When false, the exporter writes
    // the original sample path (relative to the SFZ when possible) and never touches
    // the audio data.
    bool copySamples = true;

    // Name of the folder created under the SFZ's parent for copied samples.
    std::string samplesSubfolderName = "Samples";

    // Optional banner line written as the SFZ header comment.
    std::string libraryDisplayName;
};

struct ExportResult
{
    juce::File writtenFile;
    juce::File samplesFolder; // only valid when copySamples produced any files
    int writtenRegionCount = 0;
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

// Pure non-realtime: writes `program` to `destSfz`. Caller must guarantee that
// `sampleDataByAsset.size() == program.sampleAssets.size()` (use an empty buffer
// for assets whose audio is not in memory).
ExportResult exportProgramToSfz(const juce::File& destSfz,
                                const Program& program,
                                const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset,
                                const ExportOptions& options);
} // namespace audiocity::engine::sfz_export
