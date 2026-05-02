#include "ImportedProgramState.h"

#include "ProgramMappingModel.h"

namespace audiocity::plugin
{
namespace
{
const juce::Identifier kImportedProgramPathStateProperty{ "sfzProgramPath" };
}

juce::Identifier importedProgramPathStateProperty()
{
    return kImportedProgramPathStateProperty;
}

void appendImportedProgramState(juce::ValueTree& state,
                                const juce::String& programPath,
                                const juce::ValueTree& mappingState)
{
    if (!state.isValid() || programPath.isEmpty())
        return;

    state.setProperty(kImportedProgramPathStateProperty, programPath, nullptr);
    if (mappingState.isValid() && mappingState.hasType(programZoneMappingStateType()))
        state.appendChild(mappingState.createCopy(), nullptr);
}

juce::String readImportedProgramStatePath(const juce::ValueTree& state)
{
    if (!state.isValid())
        return {};

    return state.getProperty(kImportedProgramPathStateProperty).toString();
}

juce::ValueTree readImportedProgramMappingState(const juce::ValueTree& state)
{
    if (!state.isValid())
        return {};

    return state.getChildWithName(programZoneMappingStateType());
}
}