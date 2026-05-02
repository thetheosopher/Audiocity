#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace audiocity::plugin
{
[[nodiscard]] juce::Identifier importedProgramPathStateProperty();
void appendImportedProgramState(juce::ValueTree& state,
                                const juce::String& programPath,
                                const juce::ValueTree& mappingState);
[[nodiscard]] juce::String readImportedProgramStatePath(const juce::ValueTree& state);
[[nodiscard]] juce::ValueTree readImportedProgramMappingState(const juce::ValueTree& state);
}