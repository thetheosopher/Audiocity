#pragma once

#include "../engine/ProgramModel.h"
#include "ProgramMappingModel.h"

#include <juce_data_structures/juce_data_structures.h>

namespace audiocity::plugin
{
struct ImportedProgramDerivedState
{
    juce::String mapSummary;
    std::vector<ProgramZoneListRow> zoneRows;
};

[[nodiscard]] juce::Identifier importedProgramPathStateProperty();
void appendImportedProgramState(juce::ValueTree& state,
                                const juce::String& programPath,
                                const juce::ValueTree& mappingState);
[[nodiscard]] juce::String readImportedProgramStatePath(const juce::ValueTree& state);
[[nodiscard]] juce::ValueTree readImportedProgramMappingState(const juce::ValueTree& state);
[[nodiscard]] ImportedProgramDerivedState buildImportedProgramDerivedState(const audiocity::engine::Program& program);
[[nodiscard]] bool restoreImportedProgramMappingState(audiocity::engine::Program& program,
                                                     const juce::ValueTree& mappingState);
}