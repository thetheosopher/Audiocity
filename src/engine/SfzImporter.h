#pragma once

#include "ProgramModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace audiocity::engine
{
struct SfzDiagnostic
{
    enum class Severity
    {
        warning,
        error
    };

    Severity severity = Severity::warning;
    std::string message;
    std::string filePath;
    int line = 0;
};

struct SfzImportResult
{
    Program program;
    std::vector<juce::AudioBuffer<float>> sampleDataByAsset;
    std::vector<SfzDiagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept;
};

class SfzImporter
{
public:
    [[nodiscard]] SfzImportResult importFile(const juce::File& sfzFile) const;
};
} // namespace audiocity::engine
