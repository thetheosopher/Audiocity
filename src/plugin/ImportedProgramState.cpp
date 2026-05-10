#include "ImportedProgramState.h"

#include "../engine/Sf2Importer.h"
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
        case ImportedProgramFormat::bitwigMultisample:
            return "bitwigMultisample";
        case ImportedProgramFormat::mpcKeygroup:
            return "mpcKeygroup";
        case ImportedProgramFormat::bento1010:
            return "bento1010";
        case ImportedProgramFormat::talSampler:
            return "talSampler";
        case ImportedProgramFormat::tx16wx:
            return "tx16wx";
        case ImportedProgramFormat::korgMultisample:
            return "korgMultisample";
        case ImportedProgramFormat::abletonSampler:
            return "abletonSampler";
        case ImportedProgramFormat::distingExPreset:
            return "distingExPreset";
        case ImportedProgramFormat::korgKmp:
            return "korgKmp";
        case ImportedProgramFormat::logicExs24:
            return "logicExs24";
        case ImportedProgramFormat::nnxt:
            return "nnxt";
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

    if (formatText.equalsIgnoreCase("bitwigMultisample") || formatText.equalsIgnoreCase("multisample"))
        return ImportedProgramFormat::bitwigMultisample;

    if (formatText.equalsIgnoreCase("mpcKeygroup") || formatText.equalsIgnoreCase("xpm"))
        return ImportedProgramFormat::mpcKeygroup;

    if (formatText.equalsIgnoreCase("bento1010") || formatText.equalsIgnoreCase("1010music"))
        return ImportedProgramFormat::bento1010;

    if (formatText.equalsIgnoreCase("talSampler") || formatText.equalsIgnoreCase("talsmpl"))
        return ImportedProgramFormat::talSampler;

    if (formatText.equalsIgnoreCase("tx16wx") || formatText.equalsIgnoreCase("txprog"))
        return ImportedProgramFormat::tx16wx;

    if (formatText.equalsIgnoreCase("korgMultisample") || formatText.equalsIgnoreCase("korgmultisample"))
        return ImportedProgramFormat::korgMultisample;

    if (formatText.equalsIgnoreCase("abletonSampler") || formatText.equalsIgnoreCase("adv") || formatText.equalsIgnoreCase("adg"))
        return ImportedProgramFormat::abletonSampler;

    if (formatText.equalsIgnoreCase("distingExPreset") || formatText.equalsIgnoreCase("dexpreset"))
        return ImportedProgramFormat::distingExPreset;

    if (formatText.equalsIgnoreCase("korgKmp") || formatText.equalsIgnoreCase("kmp"))
        return ImportedProgramFormat::korgKmp;

    if (formatText.equalsIgnoreCase("logicExs24") || formatText.equalsIgnoreCase("exs") || formatText.equalsIgnoreCase("exs24"))
        return ImportedProgramFormat::logicExs24;

    if (formatText.equalsIgnoreCase("nnxt") || formatText.equalsIgnoreCase("sxt"))
        return ImportedProgramFormat::nnxt;

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

    if (extension.equalsIgnoreCase(".multisample"))
        return ImportedProgramFormat::bitwigMultisample;

    if (extension.equalsIgnoreCase(".xpm"))
        return ImportedProgramFormat::mpcKeygroup;

    if (extension.equalsIgnoreCase(".talsmpl"))
        return ImportedProgramFormat::talSampler;

    if (extension.equalsIgnoreCase(".txprog"))
        return ImportedProgramFormat::tx16wx;

    if (extension.equalsIgnoreCase(".korgmultisample"))
        return ImportedProgramFormat::korgMultisample;

    if (extension.equalsIgnoreCase(".adv") || extension.equalsIgnoreCase(".adg"))
        return ImportedProgramFormat::abletonSampler;

    if (extension.equalsIgnoreCase(".dexpreset"))
        return ImportedProgramFormat::distingExPreset;

    if (extension.equalsIgnoreCase(".kmp"))
        return ImportedProgramFormat::korgKmp;

    if (extension.equalsIgnoreCase(".exs"))
        return ImportedProgramFormat::logicExs24;

    if (extension.equalsIgnoreCase(".sxt"))
        return ImportedProgramFormat::nnxt;

    // 1010music presets are conventionally named preset.xml
    if (extension.equalsIgnoreCase(".xml")
        && juce::File(programPath).getFileNameWithoutExtension().equalsIgnoreCase("preset"))
        return ImportedProgramFormat::bento1010;

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
        case ImportedProgramFormat::bitwigMultisample:
            return "MULTI";
        case ImportedProgramFormat::mpcKeygroup:
            return "XPM";
        case ImportedProgramFormat::bento1010:
            return "BENTO";
        case ImportedProgramFormat::talSampler:
            return "TALS";
        case ImportedProgramFormat::tx16wx:
            return "TX16W";
        case ImportedProgramFormat::korgMultisample:
            return "KORG";
        case ImportedProgramFormat::abletonSampler:
            return "ADV";
        case ImportedProgramFormat::distingExPreset:
            return "DEX";
        case ImportedProgramFormat::korgKmp:
            return "KMP";
        case ImportedProgramFormat::logicExs24:
            return "EXS";
        case ImportedProgramFormat::nnxt:
            return "NNXT";
        case ImportedProgramFormat::unknown:
        default:
            return "PROGRAM";
    }
}

juce::String importedProgramFormatBadge(const juce::String& programPath)
{
    return importedProgramFormatBadge(detectImportedProgramFormat(programPath));
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
                                const int selectionIndex)
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