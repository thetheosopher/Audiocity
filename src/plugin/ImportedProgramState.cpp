#include "ImportedProgramState.h"

#include "ProgramMappingModel.h"

namespace audiocity::plugin
{
namespace
{
#define AUDIOCITY_IMPORTED_PROGRAM_IDENTIFIER(name, text) \
    const juce::Identifier& name##Identifier() \
    { \
        static const juce::Identifier identifier{ text }; \
        return identifier; \
    }

AUDIOCITY_IMPORTED_PROGRAM_IDENTIFIER(kImportedProgramPathStateProperty, "importedProgramPath")
AUDIOCITY_IMPORTED_PROGRAM_IDENTIFIER(kImportedProgramFormatStateProperty, "importedProgramFormat")
AUDIOCITY_IMPORTED_PROGRAM_IDENTIFIER(kLegacyImportedProgramPathStateProperty, "sfzProgramPath")

#define kImportedProgramPathStateProperty kImportedProgramPathStatePropertyIdentifier()
#define kImportedProgramFormatStateProperty kImportedProgramFormatStatePropertyIdentifier()
#define kLegacyImportedProgramPathStateProperty kLegacyImportedProgramPathStatePropertyIdentifier()

juce::String formatImportedProgramFormat(const ImportedProgramFormat format)
{
    switch (format)
    {
        case ImportedProgramFormat::sfz:
            return "sfz";
        case ImportedProgramFormat::rex:
            return "rex";
        case ImportedProgramFormat::sampleSlices:
            return "sampleSlices";
        case ImportedProgramFormat::nki:
            return "nki";
        case ImportedProgramFormat::sf2:
            return "sf2";
        case ImportedProgramFormat::decentSampler:
            return "decentSampler";
        case ImportedProgramFormat::unknown:
        default:
            return {};
    }
}

ImportedProgramFormat parseImportedProgramFormat(const juce::String& formatText)
{
    if (formatText.equalsIgnoreCase("sfz"))
        return ImportedProgramFormat::sfz;

    if (formatText.equalsIgnoreCase("rex"))
        return ImportedProgramFormat::rex;

    if (formatText.equalsIgnoreCase("sampleSlices"))
        return ImportedProgramFormat::sampleSlices;

    if (formatText.equalsIgnoreCase("nki"))
        return ImportedProgramFormat::nki;

    if (formatText.equalsIgnoreCase("sf2"))
        return ImportedProgramFormat::sf2;

    if (formatText.equalsIgnoreCase("decentSampler") || formatText.equalsIgnoreCase("dspreset"))
        return ImportedProgramFormat::decentSampler;

    return ImportedProgramFormat::unknown;
}

juce::String formatRange(const int low, const int high)
{
    if (low == high)
        return juce::String(low);

    return juce::String(low) + "-" + juce::String(high);
}

juce::String formatLoopMode(const audiocity::engine::ZoneLoopMode mode)
{
    switch (mode)
    {
        case audiocity::engine::ZoneLoopMode::sustain:
            return "sustain";
        case audiocity::engine::ZoneLoopMode::continuous:
            return "continuous";
        case audiocity::engine::ZoneLoopMode::noLoop:
        default:
            return "off";
    }
}

juce::String formatTriggerMode(const audiocity::engine::ZoneTriggerMode mode)
{
    switch (mode)
    {
        case audiocity::engine::ZoneTriggerMode::oneShot:
            return "one-shot";
        case audiocity::engine::ZoneTriggerMode::release:
            return "release";
        case audiocity::engine::ZoneTriggerMode::gate:
        default:
            return "gate";
    }
}

juce::String buildImportedProgramMapSummary(const audiocity::engine::Program& program)
{
    juce::StringArray lines;
    lines.add("Program: " + juce::String::fromUTF8(program.name.c_str()));
    lines.add("Zones: " + juce::String(static_cast<int>(program.zones.size()))
        + " | Groups: " + juce::String(static_cast<int>(program.groups.size()))
        + " | Samples: " + juce::String(static_cast<int>(program.sampleAssets.size())));

    constexpr int maxVisibleZones = 12;
    const auto zoneCount = static_cast<int>(program.zones.size());
    for (int zoneIndex = 0; zoneIndex < juce::jmin(zoneCount, maxVisibleZones); ++zoneIndex)
    {
        const auto& zone = program.zones[static_cast<std::size_t>(zoneIndex)];
        juce::String sampleName = "sample " + juce::String(zone.sampleAssetIndex);
        if (zone.sampleAssetIndex >= 0
            && static_cast<std::size_t>(zone.sampleAssetIndex) < program.sampleAssets.size())
        {
            const auto& asset = program.sampleAssets[static_cast<std::size_t>(zone.sampleAssetIndex)];
            if (!asset.displayName.empty())
                sampleName = juce::String::fromUTF8(asset.displayName.c_str());
        }

        auto line = juce::String(zoneIndex + 1) + ". " + sampleName
            + " | key " + formatRange(zone.keyRange.low, zone.keyRange.high)
            + " | vel " + formatRange(zone.velocityRange.low, zone.velocityRange.high)
            + " | root " + juce::String(zone.rootMidiNote)
            + " | " + formatTriggerMode(zone.triggerMode)
            + " | loop " + formatLoopMode(zone.loopMode);

        if (zone.roundRobinGroup > 0 || zone.roundRobinPosition > 0)
            line += " | rr " + juce::String(zone.roundRobinGroup) + ":" + juce::String(zone.roundRobinPosition);

        if (zone.chokeGroup > 0)
            line += " | choke " + juce::String(zone.chokeGroup);

        lines.add(line);
    }

    if (zoneCount > maxVisibleZones)
        lines.add("... " + juce::String(zoneCount - maxVisibleZones) + " more zones");

    return lines.joinIntoString("\n");
}
}

juce::Identifier importedProgramPathStateProperty()
{
    return kImportedProgramPathStateProperty;
}

ImportedProgramFormat detectImportedProgramFormat(const juce::String& programPath)
{
    const auto extension = juce::File(programPath).getFileExtension();
    if (extension.equalsIgnoreCase(".sfz"))
        return ImportedProgramFormat::sfz;

    if (extension.equalsIgnoreCase(".rex") || extension.equalsIgnoreCase(".rx2"))
        return ImportedProgramFormat::rex;

    if (extension.equalsIgnoreCase(".nki"))
        return ImportedProgramFormat::nki;

    if (extension.equalsIgnoreCase(".sf2"))
        return ImportedProgramFormat::sf2;

    if (extension.equalsIgnoreCase(".dspreset"))
        return ImportedProgramFormat::decentSampler;

    return ImportedProgramFormat::unknown;
}

ImportedProgramFormat readImportedProgramStateFormat(const juce::ValueTree& state)
{
    if (!state.isValid())
        return ImportedProgramFormat::unknown;

    if (const auto storedFormat = parseImportedProgramFormat(state.getProperty(kImportedProgramFormatStateProperty).toString());
        storedFormat != ImportedProgramFormat::unknown)
    {
        return storedFormat;
    }

    return detectImportedProgramFormat(readImportedProgramStatePath(state));
}

juce::String importedProgramFormatBadge(const ImportedProgramFormat format)
{
    switch (format)
    {
        case ImportedProgramFormat::sfz:
            return "SFZ";
        case ImportedProgramFormat::rex:
            return "REX";
        case ImportedProgramFormat::sampleSlices:
            return "SLICE";
        case ImportedProgramFormat::nki:
            return "NKI";
        case ImportedProgramFormat::sf2:
            return "SF2";
        case ImportedProgramFormat::decentSampler:
            return "DSPRESET";
        case ImportedProgramFormat::unknown:
        default:
            return "PROGRAM";
    }
}

juce::String importedProgramFormatBadge(const juce::String& programPath)
{
    return importedProgramFormatBadge(detectImportedProgramFormat(programPath));
}

void appendImportedProgramState(juce::ValueTree& state,
                                const juce::String& programPath,
                                const juce::ValueTree& mappingState,
                                const ImportedProgramFormat format)
{
    if (!state.isValid() || programPath.isEmpty())
        return;

    state.setProperty(kImportedProgramPathStateProperty, programPath, nullptr);
    if (const auto formatText = formatImportedProgramFormat(format != ImportedProgramFormat::unknown
            ? format
            : detectImportedProgramFormat(programPath));
        formatText.isNotEmpty())
    {
        state.setProperty(kImportedProgramFormatStateProperty, formatText, nullptr);
    }

    if (mappingState.isValid() && mappingState.hasType(programZoneMappingStateType()))
        state.appendChild(mappingState.createCopy(), nullptr);
}

juce::String readImportedProgramStatePath(const juce::ValueTree& state)
{
    if (!state.isValid())
        return {};

    auto programPath = state.getProperty(kImportedProgramPathStateProperty).toString();
    if (programPath.isEmpty())
        programPath = state.getProperty(kLegacyImportedProgramPathStateProperty).toString();

    return programPath;
}

juce::ValueTree readImportedProgramMappingState(const juce::ValueTree& state)
{
    if (!state.isValid())
        return {};

    return state.getChildWithName(programZoneMappingStateType());
}

ImportedProgramDerivedState buildImportedProgramDerivedState(const audiocity::engine::Program& program)
{
    ImportedProgramDerivedState derivedState;
    derivedState.mapSummary = buildImportedProgramMapSummary(program);
    derivedState.zoneRows = buildProgramZoneListRows(program);
    return derivedState;
}

std::optional<ImportedProgramRestoreResult> buildImportedProgramRestoreResult(
    const audiocity::engine::Program& baseProgram,
    const juce::ValueTree& mappingState)
{
    auto restoredProgram = baseProgram;
    if (!restoreImportedProgramMappingState(restoredProgram, mappingState))
        return std::nullopt;

    ImportedProgramRestoreResult result;
    result.derivedState = buildImportedProgramDerivedState(restoredProgram);
    result.hasPublishableZones = !restoredProgram.zones.empty();
    result.program = std::move(restoredProgram);
    return result;
}

bool restoreImportedProgramMappingState(audiocity::engine::Program& program,
                                        const juce::ValueTree& mappingState)
{
    if (!mappingState.isValid() || !mappingState.hasType(programZoneMappingStateType()))
        return false;

    if (restoreProgramZoneStructureFromState(program, mappingState))
        return true;

    auto restoredAnyEdit = false;
    for (const auto& edit : parseProgramZoneMappingState(mappingState))
    {
        if (!applyProgramZoneEdit(program, edit))
            return false;

        restoredAnyEdit = true;
    }

    return restoredAnyEdit;
}
}