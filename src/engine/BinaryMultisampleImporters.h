#pragma once

#include "ProgramModel.h"
#include "XmlMultisampleImporterUtils.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <vector>

namespace audiocity::engine
{
// Binary / text-INI / KMP / EXS24 / NN-XT importers. Each format follows the
// established Audiocity importer pattern and reuses xml_multi helpers for path
// resolution, audio decoding, and summary formatting.

#define AUDIOCITY_DECLARE_BINARY_IMPORTER(NAMESPACE, FORMAT_LABEL)                       \
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

AUDIOCITY_DECLARE_BINARY_IMPORTER(distingex,  "disting EX preset")
AUDIOCITY_DECLARE_BINARY_IMPORTER(korgkmp,    "Korg KMP")
AUDIOCITY_DECLARE_BINARY_IMPORTER(exs24,      "Logic EXS24 instrument")
AUDIOCITY_DECLARE_BINARY_IMPORTER(nnxt,       "Reason NN-XT instrument")

#undef AUDIOCITY_DECLARE_BINARY_IMPORTER
} // namespace audiocity::engine
