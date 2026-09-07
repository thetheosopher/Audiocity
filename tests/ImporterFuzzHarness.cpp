#include "engine/BitwigMultisampleImporter.h"
#include "engine/BinaryMultisampleImporters.h"
#include "engine/DecentSamplerImporter.h"
#include "engine/LegacyNkiProbe.h"
#include "engine/Sf2Importer.h"
#include "engine/SfzImporter.h"
#include "engine/XmlMultisampleImporters.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#ifndef AUDIOCITY_FUZZ_FORMAT
#error AUDIOCITY_FUZZ_FORMAT must name the importer exercised by this target
#endif

namespace
{
constexpr std::size_t kMaximumFuzzInputBytes = 4u * 1024u * 1024u;

juce::String extensionForFormat()
{
    const juce::String format(AUDIOCITY_FUZZ_FORMAT);
    if (format == "sfz") return ".sfz";
    if (format == "nki") return ".nki";
    if (format == "sf2") return ".sf2";
    if (format == "bitwig") return ".multisample";
    if (format == "decent") return ".dspreset";
    if (format == "mpc") return ".xpm";
    if (format == "bento") return ".xml";
    if (format == "talsmpl") return ".talsmpl";
    if (format == "tx16wx") return ".txprog";
    if (format == "korgmulti") return ".korgmultisample";
    if (format == "ableton") return ".adv";
    if (format == "distingex") return ".dexpreset";
    if (format == "kmp") return ".kmp";
    if (format == "exs24") return ".exs";
    return ".sxt";
}

juce::File fuzzInputFile()
{
    static const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("audiocity-importer-fuzz-" + juce::String(AUDIOCITY_FUZZ_FORMAT)
            + "-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64())
            + extensionForFormat());
    return file;
}

bool exerciseImporter(const std::uint8_t* data, const std::size_t size)
{
    if (data == nullptr || size == 0 || size > kMaximumFuzzInputBytes)
        return true;

    const auto file = fuzzInputFile();
    if (!file.replaceWithData(data, size))
    {
        std::fprintf(stderr, "Failed to write temporary fuzz input: %s\n",
            file.getFullPathName().toRawUTF8());
        return false;
    }

    const juce::String format(AUDIOCITY_FUZZ_FORMAT);
    if (format == "sfz")
    {
        audiocity::engine::SfzImporter importer;
        juce::ignoreUnused(importer.importFile(file));
    }
    else if (format == "nki")
    {
        juce::ignoreUnused(audiocity::engine::nki::importFile(file));
    }
    else if (format == "sf2")
    {
        juce::ignoreUnused(audiocity::engine::sf2::importFile(file));
    }
    else if (format == "bitwig")
    {
        juce::ignoreUnused(audiocity::engine::bitwig::importFile(file));
    }
    else if (format == "decent")
    {
        juce::ignoreUnused(audiocity::engine::dspreset::importFile(file));
    }
    else if (format == "mpc")
    {
        juce::ignoreUnused(audiocity::engine::mpc::importFile(file));
    }
    else if (format == "bento")
    {
        juce::ignoreUnused(audiocity::engine::bento::importFile(file));
    }
    else if (format == "talsmpl")
    {
        juce::ignoreUnused(audiocity::engine::talsmpl::importFile(file));
    }
    else if (format == "tx16wx")
    {
        juce::ignoreUnused(audiocity::engine::tx16wx::importFile(file));
    }
    else if (format == "korgmulti")
    {
        juce::ignoreUnused(audiocity::engine::korgmulti::importFile(file));
    }
    else if (format == "ableton")
    {
        juce::ignoreUnused(audiocity::engine::ableton::importFile(file));
    }
    else if (format == "distingex")
    {
        juce::ignoreUnused(audiocity::engine::distingex::importFile(file));
    }
    else if (format == "kmp")
    {
        juce::ignoreUnused(audiocity::engine::korgkmp::importFile(file));
    }
    else if (format == "exs24")
    {
        juce::ignoreUnused(audiocity::engine::exs24::importFile(file));
    }
    else
    {
        juce::ignoreUnused(audiocity::engine::nnxt::importFile(file));
    }

    return true;
}
}

#if defined(AUDIOCITY_LIBFUZZER)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    if (!exerciseImporter(data, size))
        std::abort();
    return 0;
}
#else
int main(const int argc, char** argv)
{
    if (argc == 1)
    {
        constexpr std::array<std::array<std::uint8_t, 16>, 5> smokeCorpus{
            std::array<std::uint8_t, 16>{},
            std::array<std::uint8_t, 16>{ 'R', 'I', 'F', 'F', 0xff, 0xff, 0xff, 0x7f, 's', 'f', 'b', 'k' },
            std::array<std::uint8_t, 16>{ 'P', 'K', 3, 4, 0, 0, 0 },
            std::array<std::uint8_t, 16>{ '<', 'A', 'b', 'l', 'e', 't', 'o', 'n', '>' },
            std::array<std::uint8_t, 16>{ 'M', 'S', 'P', '1', 0, 0, 0, 64 }
        };
        for (const auto& input : smokeCorpus)
        {
            if (!exerciseImporter(input.data(), input.size()))
            {
                fuzzInputFile().deleteFile();
                return 2;
            }
        }
        fuzzInputFile().deleteFile();
        return 0;
    }

    for (auto index = 1; index < argc; ++index)
    {
        const juce::File input(argv[index]);
        juce::MemoryBlock bytes;
        if (!input.loadFileAsData(bytes) || bytes.getSize() > kMaximumFuzzInputBytes)
        {
            std::fprintf(stderr, "Could not read bounded fuzz corpus input: %s\n", argv[index]);
            fuzzInputFile().deleteFile();
            return 2;
        }
        if (!exerciseImporter(static_cast<const std::uint8_t*>(bytes.getData()), bytes.getSize()))
        {
            fuzzInputFile().deleteFile();
            return 2;
        }
    }
    fuzzInputFile().deleteFile();
    return 0;
}
#endif
