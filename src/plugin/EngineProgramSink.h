#pragma once

#include "../engine/EngineCore.h"
#include "ImportedProgramStore.h"

namespace audiocity::plugin
{
/** Publishes programs to the audio engine.

    EngineCore publication generations silence sounding voices on the audio thread before a
    new program is rendered. This sink therefore publishes immutable structure only.
*/
class EngineProgramSink final : public ProgramSink
{
public:
    explicit EngineProgramSink(audiocity::engine::EngineCore& engine) noexcept
        : engine_(engine)
    {
    }

    void republishProgram(const audiocity::engine::Program& program,
                          const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset) override
    {
        juce::String ignoredDiagnostic;
        const auto published = republishProgramChecked(program, sampleDataByAsset, ignoredDiagnostic);
        juce::ignoreUnused(published);
    }

    bool republishProgramChecked(
        const audiocity::engine::Program& program,
        const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset,
        juce::String& diagnostic) override
    {
        const auto result = engine_.setProgram(program, sampleDataByAsset);
        diagnostic = result.diagnostic;
        return static_cast<bool>(result);
    }

    bool republishProgramMetadata(const audiocity::engine::Program& program) override
    {
        return engine_.setProgramMetadata(program);
    }

private:
    audiocity::engine::EngineCore& engine_;
};
}
