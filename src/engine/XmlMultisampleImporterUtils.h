#pragma once

#include "ProgramModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace audiocity::engine::xml_multi
{
struct Diagnostic
{
    enum class Severity { warning, error };
    Severity severity = Severity::warning;
    std::string message;
};

inline void addDiagnostic(std::vector<Diagnostic>& out, Diagnostic::Severity sev, const juce::String& msg)
{
    out.push_back({ sev, msg.toStdString() });
}

[[nodiscard]] inline bool hasErrors(const std::vector<Diagnostic>& d) noexcept
{
    for (const auto& e : d) if (e.severity == Diagnostic::Severity::error) return true;
    return false;
}

// Parse "C4", "F#3", "Bb-1", "60" (MIDI 60 by convention C-1 = 0; C4 = 60).
[[nodiscard]] int parseMidiNoteText(const juce::String& raw, int defaultValue);

// Resolve relative/absolute sample reference against the preset folder, with common
// fallbacks (Samples/ sibling, parent-up-2-levels lookup).
[[nodiscard]] juce::File resolveSamplePath(const juce::String& rawPath, const juce::File& presetFolder);

// Load an external WAV/AIFF/etc into a SampleAsset + AudioBuffer using JUCE's reader.
// On failure, pushes an error diagnostic and returns -1.
[[nodiscard]] int loadSampleAssetFromFile(juce::AudioFormatManager& fm,
                                          const juce::File& audioFile,
                                          int rootMidiNote,
                                          Program& program,
                                          std::vector<juce::AudioBuffer<float>>& sampleData,
                                          std::vector<Diagnostic>& diagnostics,
                                          const juce::String& humanLabel);

// Build a one-line summary similar to other importers.
[[nodiscard]] juce::String buildGenericSummary(const juce::String& formatLabel,
                                               const Program& program,
                                               const std::vector<Diagnostic>& diagnostics,
                                               bool imported);
} // namespace audiocity::engine::xml_multi
