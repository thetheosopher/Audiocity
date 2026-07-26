#pragma once

#include "../engine/EngineCore.h"
#include "ImportedProgramStore.h"

namespace audiocity::plugin
{
/** Publishes programs to the audio engine.

    Silencing sounding voices before adopting a new program is part of publishing, so it
    lives here rather than at the call sites.
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
        engine_.panic();
        engine_.setProgram(program, sampleDataByAsset);
    }

    bool republishProgramMetadata(const audiocity::engine::Program& program) override
    {
        engine_.panic();
        return engine_.setProgramMetadata(program);
    }

private:
    audiocity::engine::EngineCore& engine_;
};
}
