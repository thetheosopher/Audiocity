#include "PresetJson.h"

#include <juce_data_structures/juce_data_structures.h>

namespace audiocity::plugin
{
juce::String encodePresetXml(const juce::ValueTree& stateTree)
{
    if (auto xml = stateTree.createXml())
        return xml->toString();

    return {};
}

bool decodePresetXml(const juce::String& xmlText, juce::ValueTree& stateTree, juce::String& errorMessage)
{
    errorMessage.clear();

    if (xmlText.isEmpty())
    {
        errorMessage = "Preset file is empty.";
        return false;
    }

    const auto xml = juce::parseXML(xmlText);
    if (xml == nullptr)
    {
        errorMessage = "Preset XML payload is invalid.";
        return false;
    }

    const auto parsedState = juce::ValueTree::fromXml(*xml);
    if (!parsedState.isValid())
    {
        errorMessage = "Preset XML payload did not produce a valid state tree.";
        return false;
    }

    stateTree = parsedState;
    return true;
}
}
