#include "ImportedProgramState.h"

#include "../engine/Sf2Importer.h"
#include "ImportFormatRegistry.h"
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
AUDIOCITY_IMPORTED_PROGRAM_IDENTIFIER(kImportedProgramSelectionIndexStateProperty, "importedProgramSelectionIndex")
AUDIOCITY_IMPORTED_PROGRAM_IDENTIFIER(kLegacyImportedProgramPathStateProperty, "sfzProgramPath")

#define kImportedProgramPathStateProperty kImportedProgramPathStatePropertyIdentifier()
#define kImportedProgramFormatStateProperty kImportedProgramFormatStatePropertyIdentifier()
#define kImportedProgramSelectionIndexStateProperty kImportedProgramSelectionIndexStatePropertyIdentifier()
#define kLegacyImportedProgramPathStateProperty kLegacyImportedProgramPathStatePropertyIdentifier()

juce::String formatImportedProgramFormat(const ImportedProgramFormat format)
{
    return importedProgramFormatToken(format);
}

ImportedProgramFormat parseImportedProgramFormat(const juce::String& formatText)
{
    return parseImportedProgramFormatToken(formatText);
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

juce::String buildImportedProgramChoiceLabel(const audiocity::engine::sf2::PresetInfo& preset,
                                            const int fallbackIndex)
{
    if (preset.name.isNotEmpty())
        return preset.name;

    return "Preset " + juce::String(fallbackIndex + 1);
}

juce::String buildImportedProgramChoiceDetail(const audiocity::engine::sf2::PresetInfo& preset)
{
    return "Bank " + juce::String(preset.bank) + ", Program " + juce::String(preset.program);
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
    if (const auto* descriptor = findImportFormatDescriptorForPath(programPath);
        descriptor != nullptr && descriptor->isInstrument)
    {
        return descriptor->format;
    }

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
    if (format == ImportedProgramFormat::sampleSlices)
        return "SLICE";

    if (const auto* descriptor = findImportFormatDescriptor(format))
        return descriptor->badge.data();

    return "PROGRAM";
}

juce::String importedProgramFormatBadge(const juce::String& programPath)
{
    return importedProgramFormatBadge(detectImportedProgramFormat(programPath));
}

juce::String importedProgramFormatDescription(const ImportedProgramFormat format)
{
    if (format == ImportedProgramFormat::sampleSlices)
        return "Transient slice program";

    if (const auto* descriptor = findImportFormatDescriptor(format))
        return descriptor->description.data();

    return "Instrument library";
}

juce::String importedProgramFormatDescription(const juce::String& programPath)
{
    return importedProgramFormatDescription(detectImportedProgramFormat(programPath));
}

ImportedProgramChoiceProbe probeImportedProgramChoices(const juce::File& programFile)
{
    ImportedProgramChoiceProbe probe;
    probe.format = detectImportedProgramFormat(programFile.getFullPathName());

    switch (probe.format)
    {
        case ImportedProgramFormat::sf2:
        {
            const auto sf2Probe = audiocity::engine::sf2::probeFile(programFile);
            for (std::size_t index = 0; index < sf2Probe.availablePresets.size(); ++index)
            {
                const auto& preset = sf2Probe.availablePresets[index];
                ImportedProgramChoice choice;
                choice.choiceIndex = static_cast<int>(index);
                choice.label = buildImportedProgramChoiceLabel(preset, static_cast<int>(index));
                choice.detail = buildImportedProgramChoiceDetail(preset);
                probe.choices.push_back(std::move(choice));
            }
            break;
        }

        case ImportedProgramFormat::unknown:
        case ImportedProgramFormat::sfz:
        case ImportedProgramFormat::rex:
        case ImportedProgramFormat::sampleSlices:
        case ImportedProgramFormat::nki:
        case ImportedProgramFormat::decentSampler:
        case ImportedProgramFormat::bitwigMultisample:
        case ImportedProgramFormat::mpcKeygroup:
        case ImportedProgramFormat::bento1010:
        case ImportedProgramFormat::talSampler:
        case ImportedProgramFormat::tx16wx:
        case ImportedProgramFormat::korgMultisample:
        case ImportedProgramFormat::abletonSampler:
        case ImportedProgramFormat::distingExPreset:
        case ImportedProgramFormat::korgKmp:
        case ImportedProgramFormat::logicExs24:
        case ImportedProgramFormat::nnxt:
        default:
            break;
    }

    return probe;
}

void appendImportedProgramState(juce::ValueTree& state,
                                const juce::String& programPath,
                                const juce::ValueTree& mappingState,
                                const ImportedProgramFormat format,
                                const int selectionIndex,
                                const ImportedAssetManifest& assetManifest)
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

    if (selectionIndex >= 0)
        state.setProperty(kImportedProgramSelectionIndexStateProperty, selectionIndex, nullptr);

    if (auto manifestState = createImportedAssetManifestState(assetManifest); manifestState.isValid())
        state.appendChild(manifestState, nullptr);

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

int readImportedProgramStateSelectionIndex(const juce::ValueTree& state)
{
    if (!state.isValid())
        return -1;

    return juce::jmax(-1,
                      static_cast<int>(state.getProperty(kImportedProgramSelectionIndexStateProperty, -1)));
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
