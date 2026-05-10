#pragma once

#include "ProgramModel.h"
#include "XmlMultisampleImporterUtils.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace audiocity::engine
{
// Each importer below follows the established Audiocity pattern:
//   ImportResult importFile(juce::File);
//   juce::String  buildImportSummary(const ImportResult&, bool imported);
// They all reuse audiocity::engine::xml_multi helpers for path resolution,
// audio decoding, and summary formatting.

#define AUDIOCITY_DECLARE_XML_IMPORTER(NAMESPACE, FORMAT_LABEL)                          \
    namespace NAMESPACE                                                                  \
    {                                                                                    \
        using Diagnostic = audiocity::engine::xml_multi::Diagnostic;                     \
        struct ImportResult                                                              \
        {                                                                                \
            Program program;                                                             \
            std::vector<juce::AudioBuffer<float>> sampleDataByAsset;                     \
            std::vector<Diagnostic> diagnostics;                                         \
            [[nodiscard]] bool hasErrors() const noexcept                                \
            {                                                                            \
                return audiocity::engine::xml_multi::hasErrors(diagnostics);             \
            }                                                                            \
            [[nodiscard]] bool hasPlayableProgram() const noexcept                       \
            {                                                                            \
                return program.hasPlayableZones() && !sampleDataByAsset.empty();         \
            }                                                                            \
        };                                                                               \
        [[nodiscard]] ImportResult importFile(const juce::File& file);                   \
        [[nodiscard]] juce::String buildImportSummary(const ImportResult& result,        \
                                                      bool imported);                    \
    }

AUDIOCITY_DECLARE_XML_IMPORTER(mpc,           "MPC keygroup")
AUDIOCITY_DECLARE_XML_IMPORTER(bento,         "1010music preset")
AUDIOCITY_DECLARE_XML_IMPORTER(talsmpl,       "TAL Sampler preset")
AUDIOCITY_DECLARE_XML_IMPORTER(tx16wx,        "TX16Wx preset")
AUDIOCITY_DECLARE_XML_IMPORTER(korgmulti,     "Korg multisample")
AUDIOCITY_DECLARE_XML_IMPORTER(ableton,       "Ableton sampler preset")

#undef AUDIOCITY_DECLARE_XML_IMPORTER
} // namespace audiocity::engine
