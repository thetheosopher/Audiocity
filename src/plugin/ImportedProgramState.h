#pragma once

#include "../engine/ProgramModel.h"
#include "ImportedAssetResolver.h"
#include "ProgramMappingModel.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>
#include <vector>

namespace audiocity::plugin
{
enum class ImportedProgramFormat
{
    unknown,
    sfz,
    rex,
    sampleSlices,
    nki,
    sf2,
    decentSampler,
    bitwigMultisample,
    mpcKeygroup,
    bento1010,
    talSampler,
    tx16wx,
    korgMultisample,
    abletonSampler,
    distingExPreset,
    korgKmp,
    logicExs24,
    nnxt
};

struct ImportedProgramDerivedState
{
    juce::String mapSummary;
    std::vector<ProgramZoneListRow> zoneRows;
};

struct ImportedProgramChoice
{
    int choiceIndex = -1;
    juce::String label;
    juce::String detail;
};

struct ImportedProgramChoiceProbe
{
    ImportedProgramFormat format = ImportedProgramFormat::unknown;
    std::vector<ImportedProgramChoice> choices;

    [[nodiscard]] bool hasMultipleChoices() const noexcept
    {
        return choices.size() > 1;
    }
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
[[nodiscard]] juce::String importedProgramFormatDescription(ImportedProgramFormat format);
[[nodiscard]] juce::String importedProgramFormatDescription(const juce::String& programPath);
void appendImportedProgramState(juce::ValueTree& state,
                                const juce::String& programPath,
                                const juce::ValueTree& mappingState,
                                ImportedProgramFormat format = ImportedProgramFormat::unknown,
                                int selectionIndex = -1,
                                const ImportedAssetManifest& assetManifest = {});
[[nodiscard]] juce::String readImportedProgramStatePath(const juce::ValueTree& state);
[[nodiscard]] int readImportedProgramStateSelectionIndex(const juce::ValueTree& state);
[[nodiscard]] juce::ValueTree readImportedProgramMappingState(const juce::ValueTree& state);
[[nodiscard]] ImportedProgramChoiceProbe probeImportedProgramChoices(const juce::File& programFile);
[[nodiscard]] ImportedProgramDerivedState buildImportedProgramDerivedState(const audiocity::engine::Program& program);
[[nodiscard]] std::optional<ImportedProgramRestoreResult> buildImportedProgramRestoreResult(
    const audiocity::engine::Program& baseProgram,
    const juce::ValueTree& mappingState);
[[nodiscard]] bool restoreImportedProgramMappingState(audiocity::engine::Program& program,
                                                     const juce::ValueTree& mappingState);
}
