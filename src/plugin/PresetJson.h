#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace audiocity::plugin
{
[[nodiscard]] juce::String encodePresetXml(const juce::ValueTree& stateTree);
bool decodePresetXml(const juce::String& xmlText, juce::ValueTree& stateTree, juce::String& errorMessage);
}
