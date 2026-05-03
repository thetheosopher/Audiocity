#pragma once

#include "../engine/ProgramModel.h"
#include "ProgramMappingModel.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>

namespace audiocity::plugin
{
enum class ImportedProgramFormat
{
    unknown,
    sfz,
    rex,
    sampleSlices
};

struct ImportedProgramDerivedState
{
    juce::String mapSummary;
    std::vector<ProgramZoneListRow> zoneRows;
};

struct ImportedProgramRestoreResult
{
    audiocity::engine::Program program;
    ImportedProgramDerivedState derivedState;
    bool hasPublishableZones = false;
};

[[nodiscard]] juce::Identifier importedProgramPathStateProperty();
[[nodiscard]] ImportedProgramFormat detectImportedProgramFormat(const juce::String& programPath);
[[nodiscard]] ImportedProgramFormat readImportedProgramStateFormat(const juce::ValueTree& state);
[[nodiscard]] juce::String importedProgramFormatBadge(ImportedProgramFormat format);
[[nodiscard]] juce::String importedProgramFormatBadge(const juce::String& programPath);
void appendImportedProgramState(juce::ValueTree& state,
                                const juce::String& programPath,
                                const juce::ValueTree& mappingState,
                                ImportedProgramFormat format = ImportedProgramFormat::unknown);
[[nodiscard]] juce::String readImportedProgramStatePath(const juce::ValueTree& state);
[[nodiscard]] juce::ValueTree readImportedProgramMappingState(const juce::ValueTree& state);
[[nodiscard]] ImportedProgramDerivedState buildImportedProgramDerivedState(const audiocity::engine::Program& program);
[[nodiscard]] std::optional<ImportedProgramRestoreResult> buildImportedProgramRestoreResult(
    const audiocity::engine::Program& baseProgram,
    const juce::ValueTree& mappingState);
[[nodiscard]] bool restoreImportedProgramMappingState(audiocity::engine::Program& program,
                                                     const juce::ValueTree& mappingState);
}