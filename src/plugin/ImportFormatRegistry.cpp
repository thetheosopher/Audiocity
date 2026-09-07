#include "ImportFormatRegistry.h"

#include "../engine/AudioFileSupport.h"

#include <algorithm>

namespace audiocity::plugin
{
namespace
{
using Availability = ImportFormatAvailability;
using Format = ImportedProgramFormat;

constexpr std::array<ImportFormatDescriptor, 18> kDescriptors{{
    { Format::unknown, {}, "AUDIO", "Audio sample", { ".wav", ".aif", ".aiff", ".flac", ".ogg" }, 5, false, Availability::always, {}, {}, true, true },
    { Format::unknown, {}, "NCW", "NCW sample", { ".ncw", {}, {} }, 1, false, Availability::ncwConverter,
      "NCW support requires AUDIOCITY_NCW_CONVERTER_COMMAND to name a converter command.", {}, true, true },
    { Format::sfz, "sfz", "SFZ", "SFZ instrument", { ".sfz", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::rex, "rex", "REX", "REX loop", { ".rex", ".rx2", {} }, 2, true, Availability::rexRuntime,
      "REX/RX2 support requires the REX Shared Library runtime beside Audiocity.", {} },
    { Format::nki, "nki", "NKI", "NKI instrument (legacy subset)", { ".nki", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::sf2, "sf2", "SF2", "SoundFont bank", { ".sf2", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::decentSampler, "decentSampler", "DSPRESET", "DecentSampler preset", { ".dspreset", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::bitwigMultisample, "bitwigMultisample", "MULTI", "Bitwig multisample", { ".multisample", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::mpcKeygroup, "mpcKeygroup", "XPM", "MPC keygroup program", { ".xpm", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::bento1010, "bento1010", "BENTO", "1010music preset", { ".xml", {}, {} }, 1, true, Availability::always, {}, "preset.xml" },
    { Format::talSampler, "talSampler", "TALS", "TAL Sampler preset", { ".talsmpl", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::tx16wx, "tx16wx", "TX16W", "TX16Wx program", { ".txprog", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::korgMultisample, "korgMultisample", "KORG", "Korg multisample", { ".korgmultisample", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::abletonSampler, "abletonSampler", "ADV", "Ableton sampler preset", { ".adv", ".adg", {} }, 2, true, Availability::always, {}, {} },
    { Format::distingExPreset, "distingExPreset", "DEX", "disting EX preset", { ".dexpreset", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::korgKmp, "korgKmp", "KMP", "Korg KMP program", { ".kmp", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::logicExs24, "logicExs24", "EXS", "EXS24 instrument", { ".exs", {}, {} }, 1, true, Availability::always, {}, {} },
    { Format::nnxt, "nnxt", "NNXT", "NN-XT instrument (diagnostic only)", { ".sxt", {}, {} }, 1, true,
      Availability::diagnosticOnly,
      "Reason NN-XT .sxt files are recognized, but import is not yet supported.", {}, false, false }
}};

bool matchesExtension(const ImportFormatDescriptor& descriptor, const juce::String& extension) noexcept
{
    for (std::size_t index = 0; index < descriptor.extensionCount; ++index)
    {
        if (extension.equalsIgnoreCase(descriptor.extensions[index].data()))
            return true;
    }

    return false;
}
}

std::span<const ImportFormatDescriptor> importFormatDescriptors() noexcept
{
    return kDescriptors;
}

const ImportFormatDescriptor* findImportFormatDescriptorForPath(const juce::String& path) noexcept
{
    const juce::File file(path);
    const auto fileName = file.getFileName();
    const auto extension = file.getFileExtension();

    for (const auto& descriptor : kDescriptors)
    {
        if (!matchesExtension(descriptor, extension))
            continue;

        if (!descriptor.requiredFileName.empty()
            && !fileName.equalsIgnoreCase(descriptor.requiredFileName.data()))
        {
            continue;
        }

        return &descriptor;
    }

    return nullptr;
}

const ImportFormatDescriptor* findImportFormatDescriptor(const ImportedProgramFormat format) noexcept
{
    if (format == ImportedProgramFormat::sampleSlices)
        return nullptr;

    for (const auto& descriptor : kDescriptors)
    {
        if (descriptor.isInstrument && descriptor.format == format)
            return &descriptor;
    }

    return nullptr;
}

ImportedProgramFormat parseImportedProgramFormatToken(const juce::String& token) noexcept
{
    const auto normalized = token.trim().trimCharactersAtStart(".");
    if (normalized.equalsIgnoreCase("sampleSlices"))
        return ImportedProgramFormat::sampleSlices;

    for (const auto& descriptor : kDescriptors)
    {
        if (!descriptor.isInstrument)
            continue;

        if (normalized.equalsIgnoreCase(descriptor.stateToken.data()))
            return descriptor.format;

        for (std::size_t index = 0; index < descriptor.extensionCount; ++index)
        {
            const auto extensionToken = juce::String(descriptor.extensions[index].data()).trimCharactersAtStart(".");
            if (normalized.equalsIgnoreCase(extensionToken))
                return descriptor.format;
        }
    }

    if (normalized.equalsIgnoreCase("1010music"))
        return ImportedProgramFormat::bento1010;
    if (normalized.equalsIgnoreCase("exs24"))
        return ImportedProgramFormat::logicExs24;

    return ImportedProgramFormat::unknown;
}

juce::String importedProgramFormatToken(const ImportedProgramFormat format)
{
    if (format == ImportedProgramFormat::sampleSlices)
        return "sampleSlices";

    if (const auto* descriptor = findImportFormatDescriptor(format))
        return descriptor->stateToken.data();

    return {};
}

bool isKnownImportPath(const juce::String& path) noexcept
{
    return findImportFormatDescriptorForPath(path) != nullptr;
}

bool isMappingImportPath(const juce::String& path) noexcept
{
    if (const auto* descriptor = findImportFormatDescriptorForPath(path))
        return descriptor->supportsMappingDrag;

    return false;
}

bool supportsBackgroundImport(const ImportedProgramFormat format) noexcept
{
    if (format == ImportedProgramFormat::unknown)
        return true;
    if (format == ImportedProgramFormat::sampleSlices)
        return false;

    if (const auto* descriptor = findImportFormatDescriptor(format))
        return descriptor->supportsBackgroundImport;

    return false;
}

bool isImportFormatAvailable(const ImportFormatDescriptor& descriptor,
                             const ImportFormatCapabilities& capabilities) noexcept
{
    switch (descriptor.availability)
    {
        case ImportFormatAvailability::rexRuntime:
            return capabilities.rexRuntimeAvailable;
        case ImportFormatAvailability::ncwConverter:
            return capabilities.ncwConverterAvailable;
        case ImportFormatAvailability::diagnosticOnly:
            return false;
        case ImportFormatAvailability::always:
        default:
            return true;
    }
}

bool isImportPathAvailable(const juce::String& path,
                           const ImportFormatCapabilities& capabilities) noexcept
{
    const auto* descriptor = findImportFormatDescriptorForPath(path);
    return descriptor != nullptr && isImportFormatAvailable(*descriptor, capabilities);
}

juce::String importPathUnavailableMessage(const juce::String& path,
                                          const ImportFormatCapabilities& capabilities)
{
    const auto* descriptor = findImportFormatDescriptorForPath(path);
    if (descriptor == nullptr || isImportFormatAvailable(*descriptor, capabilities))
        return {};

    return descriptor->unavailableMessage.data();
}

juce::String buildImportChooserWildcard(const ImportFormatCapabilities& capabilities,
                                        const bool includeUnavailable)
{
    juce::StringArray patterns;
    for (const auto& descriptor : kDescriptors)
    {
        if (!includeUnavailable && !isImportFormatAvailable(descriptor, capabilities))
            continue;

        if (!descriptor.requiredFileName.empty())
        {
            patterns.addIfNotAlreadyThere(descriptor.requiredFileName.data());
            continue;
        }

        for (std::size_t index = 0; index < descriptor.extensionCount; ++index)
            patterns.addIfNotAlreadyThere("*" + juce::String(descriptor.extensions[index].data()));
    }

    return patterns.joinIntoString(";");
}

juce::String buildImportChooserTitle(const ImportFormatCapabilities& capabilities)
{
    auto title = juce::String("Open sample or instrument");
    juce::StringArray unavailable;
    for (const auto& descriptor : kDescriptors)
    {
        if (!isImportFormatAvailable(descriptor, capabilities) && descriptor.badge.size() > 0)
            unavailable.addIfNotAlreadyThere(descriptor.badge.data());
    }

    if (!unavailable.isEmpty())
        title += " (unavailable: " + unavailable.joinIntoString(", ") + ")";

    return title;
}

ImportFormatCapabilities currentImportFormatCapabilities(const bool rexRuntimeAvailable)
{
    return { rexRuntimeAvailable, audiocity::engine::audio_file::isNcwConverterAvailable() };
}
} // namespace audiocity::plugin
