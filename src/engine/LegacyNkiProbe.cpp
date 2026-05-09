#include "LegacyNkiProbe.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <string>

namespace audiocity::engine::nki
{
namespace
{
bool isPrintableAscii(const unsigned char value)
{
    return value >= 32 && value <= 126;
}

std::vector<juce::String> extractPrintableStrings(const juce::MemoryBlock& data, const std::size_t minLength)
{
    std::vector<juce::String> strings;
    const auto* bytes = static_cast<const unsigned char*>(data.getData());
    if (bytes == nullptr || data.getSize() == 0)
        return strings;

    std::string current;
    current.reserve(64);

    auto flushCurrent = [&strings, &current, minLength]()
    {
        if (current.size() >= minLength)
            strings.emplace_back(current.c_str());

        current.clear();
    };

    for (std::size_t index = 0; index < data.getSize(); ++index)
    {
        const auto value = bytes[index];
        if (isPrintableAscii(value))
        {
            current.push_back(static_cast<char>(value));
        }
        else
        {
            flushCurrent();
        }
    }

    flushCurrent();
    return strings;
}

bool addUniqueIgnoreCase(juce::StringArray& strings, const juce::String& value)
{
    for (const auto& existing : strings)
    {
        if (existing.equalsIgnoreCase(value))
            return false;
    }

    strings.add(value);
    return true;
}

void addUniqueFile(std::vector<juce::File>& files, const juce::File& candidate)
{
    const auto candidatePath = candidate.getFullPathName();
    if (candidatePath.isEmpty())
        return;

    for (const auto& existing : files)
    {
        if (existing.getFullPathName().equalsIgnoreCase(candidatePath))
            return;
    }

    files.push_back(candidate);
}

juce::String normaliseCandidateReference(const juce::String& source,
                                        const juce::StringArray& knownExtensions)
{
    auto candidate = source.trim().unquoted();
    if (candidate.isEmpty())
        return {};

    auto bestIndex = -1;
    auto bestLength = 0;
    for (const auto& extension : knownExtensions)
    {
        const auto extensionIndex = candidate.indexOfIgnoreCase(extension);
        if (extensionIndex < 0)
            continue;

        if (bestIndex < 0 || extensionIndex < bestIndex)
        {
            bestIndex = extensionIndex;
            bestLength = extension.length();
        }
    }

    if (bestIndex < 0)
        return {};

    candidate = candidate.substring(0, bestIndex + bestLength);

    const auto delimiterIndex = juce::jmax(candidate.lastIndexOfChar('='),
        juce::jmax(candidate.lastIndexOfChar('"'),
            juce::jmax(candidate.lastIndexOfChar('\''), candidate.lastIndexOfChar(' '))));
    if (delimiterIndex >= 0 && delimiterIndex + 1 < candidate.length())
        candidate = candidate.substring(delimiterIndex + 1);

    candidate = candidate.trim().unquoted().replaceCharacter('\\', '/');
    while (candidate.startsWithChar('/'))
        candidate = candidate.substring(1);

    return candidate;
}

void addDiagnostic(ProbeResult& result,
                   const DiagnosticSeverity severity,
                   const juce::String& message)
{
    result.diagnostics.push_back({ severity, message });
}

juce::String extractTaggedValue(const juce::String& source, const juce::StringArray& tags)
{
    const auto trimmed = source.trim().unquoted();
    if (trimmed.isEmpty())
        return {};

    const auto lower = trimmed.toLowerCase();
    for (const auto& tag : tags)
    {
        const auto normalizedTag = tag.toLowerCase();
        if (!lower.startsWith(normalizedTag))
            continue;

        return trimmed.substring(tag.length()).trim().unquoted();
    }

    return {};
}

std::optional<int> extractTaggedInt(const juce::String& source, const juce::StringArray& tags)
{
    const auto value = extractTaggedValue(source, tags);
    if (value.isEmpty() || !value.containsOnly("-0123456789"))
        return std::nullopt;

    return value.getIntValue();
}

std::optional<float> extractTaggedFloat(const juce::String& source, const juce::StringArray& tags)
{
    const auto value = extractTaggedValue(source, tags);
    if (value.isEmpty())
        return std::nullopt;

    const auto text = value.toStdString();
    char* end = nullptr;
    const auto parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str())
        return std::nullopt;

    while (end != nullptr && *end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0)
        ++end;

    if (end == nullptr || *end != '\0')
        return std::nullopt;

    return parsed;
}

float normalisePanValue(const float value)
{
    auto pan = value;
    if (std::abs(pan) > 1.0f)
        pan /= 100.0f;

    return juce::jlimit(-1.0f, 1.0f, pan);
}

std::optional<ZoneLoopMode> extractTaggedLoopMode(const juce::String& source)
{
    if (const auto numericLoop = extractTaggedInt(source, { "loop=", "loop_enabled=", "loopenabled=" });
        numericLoop.has_value())
    {
        return *numericLoop > 0 ? ZoneLoopMode::sustain : ZoneLoopMode::noLoop;
    }

    const auto value = extractTaggedValue(source, { "loop_mode=", "loopmode=" });
    if (value.isEmpty())
        return std::nullopt;

    const auto normalized = value.toLowerCase();
    if (normalized == "loop_continuous" || normalized == "continuous")
        return ZoneLoopMode::continuous;

    if (normalized == "loop_sustain" || normalized == "sustain" || normalized == "loop")
        return ZoneLoopMode::sustain;

    if (normalized == "no_loop" || normalized == "off" || normalized == "none"
        || normalized == "one_shot" || normalized == "oneshot")
    {
        return ZoneLoopMode::noLoop;
    }

    return std::nullopt;
}

std::optional<ZoneTriggerMode> extractTaggedTriggerMode(const juce::String& source)
{
    if (const auto oneShot = extractTaggedInt(source, { "one_shot=", "oneshot=" }); oneShot.has_value())
        return *oneShot > 0 ? ZoneTriggerMode::oneShot : ZoneTriggerMode::gate;

    if (const auto releaseTrigger = extractTaggedInt(source, { "release_trigger=", "releasetrigger=" });
        releaseTrigger.has_value())
    {
        return *releaseTrigger > 0 ? ZoneTriggerMode::release : ZoneTriggerMode::gate;
    }

    const auto value = extractTaggedValue(source, { "trigger=", "trigger_mode=", "triggermode=" });
    if (value.isEmpty())
        return std::nullopt;

    const auto normalized = value.toLowerCase();
    if (normalized == "release")
        return ZoneTriggerMode::release;

    if (normalized == "one_shot" || normalized == "oneshot")
        return ZoneTriggerMode::oneShot;

    if (normalized == "attack" || normalized == "normal" || normalized == "gate")
        return ZoneTriggerMode::gate;

    return std::nullopt;
}

juce::String extractGroupName(const juce::String& source)
{
    static const juce::StringArray tags{ "group=", "group_name=", "groupname=", "grp=" };
    return extractTaggedValue(source, tags);
}

juce::String extractZoneName(const juce::String& source)
{
    static const juce::StringArray tags{ "zone=", "zone_name=", "zonename=" };
    return extractTaggedValue(source, tags);
}

void applyTaggedIntField(const juce::String& source,
                         const juce::StringArray& tags,
                         const int minimum,
                         const int maximum,
                         int& destination)
{
    if (const auto value = extractTaggedInt(source, tags); value.has_value())
        destination = juce::jlimit(minimum, maximum, *value);
}

void applyTaggedFloatField(const juce::String& source,
                           const juce::StringArray& tags,
                           const float minimum,
                           const float maximum,
                           float& destination)
{
    if (const auto value = extractTaggedFloat(source, tags); value.has_value())
        destination = juce::jlimit(minimum, maximum, *value);
}

void commitZoneMetadata(ProbeResult& result,
                        ProbeZoneMetadata& pending,
                        const juce::String& currentGroupName)
{
    if (pending.sampleReference.isEmpty())
        return;

    if (pending.groupName.isEmpty())
        pending.groupName = currentGroupName;

    if (pending.groupName.isNotEmpty())
        addUniqueIgnoreCase(result.groupNames, pending.groupName);

    result.zoneMetadata.push_back(pending);
    pending = {};
}

void enumerateZoneMetadata(const std::vector<juce::String>& printableStrings,
                           const juce::StringArray& sampleExtensions,
                           ProbeResult& result)
{
    ProbeZoneMetadata pending;
    juce::String currentGroupName;

    for (const auto& printable : printableStrings)
    {
        if (const auto groupName = extractGroupName(printable); groupName.isNotEmpty())
        {
            commitZoneMetadata(result, pending, currentGroupName);
            currentGroupName = groupName;
            addUniqueIgnoreCase(result.groupNames, currentGroupName);
            continue;
        }

        if (const auto zoneName = extractZoneName(printable); zoneName.isNotEmpty())
        {
            commitZoneMetadata(result, pending, currentGroupName);
            pending = {};
            pending.zoneName = zoneName;
            pending.groupName = currentGroupName;
            continue;
        }

        applyTaggedIntField(printable, { "lokey=", "lowkey=" }, 0, 127, pending.lowKey);
        applyTaggedIntField(printable, { "hikey=", "highkey=" }, 0, 127, pending.highKey);
        applyTaggedIntField(printable, { "root=", "rootkey=", "root_key=" }, 0, 127, pending.rootKey);
        applyTaggedIntField(printable, { "lovel=", "lowvel=", "lowvelocity=" }, 0, 127, pending.lowVelocity);
        applyTaggedIntField(printable, { "hivel=", "highvel=", "highvelocity=" }, 0, 127, pending.highVelocity);
        applyTaggedIntField(printable,
            { "offset=", "sample_start=", "samplestart=", "start=" },
            0,
            std::numeric_limits<int>::max(),
            pending.sampleStart);
        applyTaggedIntField(printable,
            { "end=", "sample_end=", "sampleend=" },
            0,
            std::numeric_limits<int>::max(),
            pending.sampleEnd);
        applyTaggedIntField(printable,
            { "loop_start=", "loopstart=" },
            0,
            std::numeric_limits<int>::max(),
            pending.loopStart);
        applyTaggedIntField(printable,
            { "loop_end=", "loopend=" },
            0,
            std::numeric_limits<int>::max(),
            pending.loopEnd);
        applyTaggedFloatField(printable,
            { "volume=", "gain=", "vol=" },
            -96.0f,
            24.0f,
            pending.gainDb);
        applyTaggedFloatField(printable,
            { "tune=", "finetune=", "fine_tune=" },
            -2400.0f,
            2400.0f,
            pending.tuneCents);
        applyTaggedIntField(printable, { "transpose=" }, -48, 48, pending.transposeSemitones);
        if (const auto pan = extractTaggedFloat(printable, { "pan=", "balance=" }); pan.has_value())
            pending.pan = normalisePanValue(*pan);
        if (const auto triggerMode = extractTaggedTriggerMode(printable); triggerMode.has_value())
        {
            pending.triggerMode = *triggerMode;
            pending.hasTriggerMode = true;
        }
        if (const auto loopMode = extractTaggedLoopMode(printable); loopMode.has_value())
        {
            pending.loopMode = *loopMode;
            pending.hasLoopMode = true;
        }

        if (const auto sampleReference = normaliseCandidateReference(printable, sampleExtensions);
            sampleReference.isNotEmpty())
        {
            if (pending.sampleReference.isNotEmpty())
                commitZoneMetadata(result, pending, currentGroupName);

            pending.sampleReference = sampleReference;
            if (pending.groupName.isEmpty())
                pending.groupName = currentGroupName;
        }
    }

    commitZoneMetadata(result, pending, currentGroupName);
}

juce::File resolveSampleReference(const juce::File& nkiFile, const juce::String& sampleReference)
{
    auto normalized = sampleReference.trim().replaceCharacter('\\', '/');
    while (normalized.startsWithChar('/'))
        normalized = normalized.substring(1);

    if (normalized.isEmpty())
        return {};

    if (juce::File::isAbsolutePath(normalized))
    {
        const juce::File absoluteCandidate(normalized);
        if (absoluteCandidate.existsAsFile())
            return absoluteCandidate;
    }

    const auto sampleName = juce::File(normalized).getFileName();
    juce::StringArray relativeVariants;
    addUniqueIgnoreCase(relativeVariants, normalized);
    if (normalized.startsWithIgnoreCase("Samples/"))
        addUniqueIgnoreCase(relativeVariants, normalized.substring(8));
    if (sampleName.isNotEmpty())
        addUniqueIgnoreCase(relativeVariants, sampleName);

    const auto nkiDirectory = nkiFile.getParentDirectory();
    const auto parentDirectory = nkiDirectory.getParentDirectory();
    const auto nkiSamplesDirectory = nkiDirectory.getChildFile("Samples");
    const auto parentSamplesDirectory = parentDirectory.getChildFile("Samples");

    std::vector<juce::File> candidateRoots;
    addUniqueFile(candidateRoots, nkiDirectory);
    addUniqueFile(candidateRoots, parentDirectory);
    if (nkiSamplesDirectory.isDirectory())
        addUniqueFile(candidateRoots, nkiSamplesDirectory);
    if (parentSamplesDirectory.isDirectory())
        addUniqueFile(candidateRoots, parentSamplesDirectory);

    for (const auto& root : candidateRoots)
    {
        if (!root.isDirectory())
            continue;

        for (const auto& variant : relativeVariants)
        {
            const auto candidate = root.getChildFile(variant);
            if (candidate.existsAsFile())
                return candidate;
        }
    }

    if (sampleName.isEmpty())
        return {};

    std::vector<juce::File> recursiveSearchRoots;
    if (nkiSamplesDirectory.isDirectory())
        addUniqueFile(recursiveSearchRoots, nkiSamplesDirectory);
    if (parentSamplesDirectory.isDirectory())
        addUniqueFile(recursiveSearchRoots, parentSamplesDirectory);
    if (recursiveSearchRoots.empty())
        addUniqueFile(recursiveSearchRoots, nkiDirectory);

    for (const auto& root : recursiveSearchRoots)
    {
        if (!root.isDirectory())
            continue;

        for (const auto& item : juce::RangedDirectoryIterator(root, true, "*", juce::File::findFiles))
        {
            const auto candidate = item.getFile();
            if (candidate.getFileName().equalsIgnoreCase(sampleName))
                return candidate;
        }
    }

    return {};
}

MidiRange makeKeyRange(const ProbeZoneMetadata& zone) noexcept
{
    if (zone.lowKey >= 0 && zone.highKey >= 0)
        return MidiRange::fromUnordered(zone.lowKey, zone.highKey);

    if (zone.lowKey >= 0)
        return MidiRange::single(zone.lowKey);

    if (zone.highKey >= 0)
        return MidiRange::single(zone.highKey);

    if (zone.rootKey >= 0)
        return MidiRange::single(zone.rootKey);

    return MidiRange::full();
}

VelocityRange makeVelocityRange(const ProbeZoneMetadata& zone) noexcept
{
    if (zone.lowVelocity >= 0 && zone.highVelocity >= 0)
        return VelocityRange::fromUnordered(zone.lowVelocity, zone.highVelocity);

    if (zone.lowVelocity >= 0)
        return VelocityRange::fromUnordered(zone.lowVelocity, kVelocityMax);

    if (zone.highVelocity >= 0)
        return VelocityRange::fromUnordered(kVelocityMin, zone.highVelocity);

    return VelocityRange::full();
}

int chooseRootMidiNote(const ProbeZoneMetadata& zone) noexcept
{
    if (zone.rootKey >= 0)
        return clampMidiNote(zone.rootKey);

    if (zone.lowKey >= 0)
        return clampMidiNote(zone.lowKey);

    if (zone.highKey >= 0)
        return clampMidiNote(zone.highKey);

    return 60;
}

int ensureGroup(Program& program,
                std::map<std::string, int>& groupIndices,
                const ProbeZoneMetadata& zone)
{
    if (zone.groupName.isEmpty())
        return -1;

    const auto key = zone.groupName.toStdString();
    if (const auto found = groupIndices.find(key); found != groupIndices.end())
        return found->second;

    Group group;
    group.name = key;
    program.groups.push_back(group);
    const auto index = static_cast<int>(program.groups.size() - 1);
    groupIndices.emplace(key, index);
    return index;
}

int getOrAddSampleAsset(ImportResult& result,
                        std::map<std::string, int>& sampleIndices,
                        juce::AudioFormatManager& formatManager,
                        const juce::File& sampleFile,
                        const int rootMidiNote)
{
    const auto normalizedPath = sampleFile.getFullPathName().replaceCharacter('\\', '/').toStdString();
    if (const auto found = sampleIndices.find(normalizedPath); found != sampleIndices.end())
        return found->second;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(sampleFile));
    if (reader == nullptr)
    {
        addDiagnostic(result.probe,
            DiagnosticSeverity::warning,
            "NKI import: unsupported or unreadable sample " + sampleFile.getFullPathName());
        return -1;
    }

    const auto sampleLength = static_cast<int>(juce::jlimit<juce::int64>(0,
        static_cast<juce::int64>(std::numeric_limits<int>::max()), reader->lengthInSamples));
    if (sampleLength <= 0 || reader->numChannels <= 0 || reader->sampleRate <= 0.0)
    {
        addDiagnostic(result.probe,
            DiagnosticSeverity::warning,
            "NKI import: decoded sample was empty " + sampleFile.getFullPathName());
        return -1;
    }

    juce::AudioBuffer<float> sampleData(static_cast<int>(reader->numChannels), sampleLength);
    if (!reader->read(&sampleData, 0, sampleLength, 0, true, true))
    {
        addDiagnostic(result.probe,
            DiagnosticSeverity::warning,
            "NKI import: could not decode sample " + sampleFile.getFullPathName());
        return -1;
    }

    SampleAsset asset;
    asset.sourcePath = normalizedPath;
    asset.displayName = sampleFile.getFileName().toStdString();
    asset.lengthSamples = sampleLength;
    asset.numChannels = static_cast<int>(reader->numChannels);
    asset.sampleRateHz = reader->sampleRate;
    asset.rootMidiNote = rootMidiNote;
    asset.bitDepth = static_cast<int>(reader->bitsPerSample);
    asset.embeddedInProgram = false;

    result.program.sampleAssets.push_back(asset);
    result.sampleDataByAsset.push_back(sampleData);
    const auto index = static_cast<int>(result.program.sampleAssets.size() - 1);
    sampleIndices.emplace(normalizedPath, index);
    return index;
}
}

bool ProbeResult::hasErrors() const noexcept
{
    for (const auto& diagnostic : diagnostics)
    {
        if (diagnostic.severity == DiagnosticSeverity::error)
            return true;
    }

    return status == ProbeStatus::invalidFile;
}

ProbeResult probeFile(const juce::File& file)
{
    ProbeResult result;
    result.instrumentName = file.getFileNameWithoutExtension();

    if (!file.existsAsFile())
    {
        addDiagnostic(result, DiagnosticSeverity::error, "NKI probe failed: file not found");
        return result;
    }

    if (!file.getFileExtension().equalsIgnoreCase(".nki"))
    {
        addDiagnostic(result, DiagnosticSeverity::error, "NKI probe failed: unsupported extension");
        return result;
    }

    juce::MemoryBlock fileData;
    if (!file.loadFileAsData(fileData) || fileData.getSize() == 0)
    {
        addDiagnostic(result, DiagnosticSeverity::error, "NKI probe failed: file could not be read");
        return result;
    }

    const auto printableStrings = extractPrintableStrings(fileData, 4);
    constexpr std::size_t minimumSignatureBytes = 16;
    if (fileData.getSize() < minimumSignatureBytes)
        addDiagnostic(result, DiagnosticSeverity::warning, "NKI probe: file is very small and may be truncated");

    const juce::StringArray sampleExtensions{ ".aiff", ".aif", ".wav" };
    const juce::StringArray containerExtensions{ ".nkx", ".nks", ".ncw" };

    for (const auto& printable : printableStrings)
    {
        if (const auto sampleReference = normaliseCandidateReference(printable, sampleExtensions);
            sampleReference.isNotEmpty())
        {
            addUniqueIgnoreCase(result.sampleReferences, sampleReference);
        }

        if (const auto containerReference = normaliseCandidateReference(printable, containerExtensions);
            containerReference.isNotEmpty())
        {
            addUniqueIgnoreCase(result.containerReferences, containerReference);
        }
    }

    enumerateZoneMetadata(printableStrings, sampleExtensions, result);

    for (const auto& sampleReference : result.sampleReferences)
    {
        if (const auto resolvedSample = resolveSampleReference(file, sampleReference); resolvedSample.existsAsFile())
        {
            addUniqueIgnoreCase(result.resolvedSampleFiles,
                resolvedSample.getFullPathName().replaceCharacter('\\', '/'));
        }
        else
        {
            addUniqueIgnoreCase(result.missingSampleReferences, sampleReference);
        }
    }

    if (result.sampleReferences.isEmpty())
    {
        addDiagnostic(result,
            DiagnosticSeverity::warning,
            "NKI probe: no external WAV/AIFF references were detected");
    }
    else
    {
        addDiagnostic(result,
            DiagnosticSeverity::info,
            "NKI probe: extracted " + juce::String(result.sampleReferences.size())
                + " likely external sample reference"
                + (result.sampleReferences.size() == 1 ? "" : "s"));
    }

    if (!result.resolvedSampleFiles.isEmpty())
    {
        addDiagnostic(result,
            DiagnosticSeverity::info,
            "NKI probe: resolved " + juce::String(result.resolvedSampleFiles.size())
                + " external sample file"
                + (result.resolvedSampleFiles.size() == 1 ? "" : "s"));
    }

    if (!result.missingSampleReferences.isEmpty())
    {
        addDiagnostic(result,
            DiagnosticSeverity::warning,
            "NKI probe: could not resolve " + juce::String(result.missingSampleReferences.size())
                + " external sample reference"
                + (result.missingSampleReferences.size() == 1 ? "" : "s"));
    }

    if (!result.zoneMetadata.empty())
    {
        addDiagnostic(result,
            DiagnosticSeverity::info,
            "NKI probe: enumerated " + juce::String(result.zoneMetadata.size())
                + " zone metadata block"
                + (result.zoneMetadata.size() == 1 ? "" : "s"));
    }

    if (!result.groupNames.isEmpty())
    {
        addDiagnostic(result,
            DiagnosticSeverity::info,
            "NKI probe: enumerated " + juce::String(result.groupNames.size())
                + " group"
                + (result.groupNames.size() == 1 ? "" : "s"));
    }

    if (!result.containerReferences.isEmpty())
    {
        addDiagnostic(result,
            DiagnosticSeverity::warning,
            "NKI probe: detected container/compressed sample references (.nkx/.nks/.ncw)");
    }

    if (!result.sampleReferences.isEmpty())
        result.status = ProbeStatus::legacyDiscreteSampleCandidate;
    else if (!result.containerReferences.isEmpty())
        result.status = ProbeStatus::unsupportedContainerReference;
    else
        result.status = ProbeStatus::unsupportedOrUnrecognized;

    return result;
}

ImportResult importFile(const juce::File& file)
{
    return importFile(file, juce::File{});
}

ImportResult importFile(const juce::File& file, const juce::File& extraSearchFolder)
{
    ImportResult result;
    result.probe = probeFile(file);
    result.program.name = result.probe.instrumentName.toStdString();

    if (result.probe.status != ProbeStatus::legacyDiscreteSampleCandidate)
        return result;

    if (result.probe.zoneMetadata.empty())
    {
        addDiagnostic(result.probe,
            DiagnosticSeverity::warning,
            "NKI import: playable import requires enumerated zone metadata");
        return result;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::map<std::string, int> groupIndices;
    std::map<std::string, int> sampleIndices;

    for (const auto& zoneMetadata : result.probe.zoneMetadata)
    {
        if (zoneMetadata.sampleReference.isEmpty())
            continue;

        auto sampleFile = resolveSampleReference(file, zoneMetadata.sampleReference);

        if (!sampleFile.existsAsFile() && extraSearchFolder.isDirectory())
        {
            const auto sampleName = juce::File(
                zoneMetadata.sampleReference.replaceCharacter('\\', '/')).getFileName();
            if (sampleName.isNotEmpty())
            {
                const auto directCandidate = extraSearchFolder.getChildFile(sampleName);
                if (directCandidate.existsAsFile())
                {
                    sampleFile = directCandidate;
                }
                else
                {
                    for (const auto& item : juce::RangedDirectoryIterator(
                             extraSearchFolder, true, "*", juce::File::findFiles))
                    {
                        if (item.getFile().getFileName().equalsIgnoreCase(sampleName))
                        {
                            sampleFile = item.getFile();
                            break;
                        }
                    }
                }
            }
        }

        if (!sampleFile.existsAsFile())
        {
            addDiagnostic(result.probe,
                DiagnosticSeverity::warning,
                "NKI import: skipped zone without resolved sample " + zoneMetadata.sampleReference);
            continue;
        }

        const auto rootMidiNote = chooseRootMidiNote(zoneMetadata);
        const auto sampleAssetIndex = getOrAddSampleAsset(result, sampleIndices, formatManager, sampleFile, rootMidiNote);
        if (sampleAssetIndex < 0)
            continue;

        Zone zone;
        zone.sampleAssetIndex = sampleAssetIndex;
        zone.groupIndex = ensureGroup(result.program, groupIndices, zoneMetadata);
        zone.keyRange = makeKeyRange(zoneMetadata);
        zone.velocityRange = makeVelocityRange(zoneMetadata);
        zone.rootMidiNote = rootMidiNote;
        if (zoneMetadata.sampleStart >= 0)
            zone.sampleStart = juce::jmax(0, zoneMetadata.sampleStart);
        if (zoneMetadata.sampleEnd >= 0)
            zone.sampleEndExclusive = juce::jmax(zone.sampleStart + 1, zoneMetadata.sampleEnd + 1);
        if (zoneMetadata.loopStart >= 0)
            zone.loopStart = juce::jmax(0, zoneMetadata.loopStart);
        if (zoneMetadata.loopEnd >= 0)
        {
            const auto minimumLoopEndExclusive = zone.loopStart >= 0 ? zone.loopStart + 1 : 1;
            zone.loopEndExclusive = juce::jmax(minimumLoopEndExclusive, zoneMetadata.loopEnd + 1);
        }
        zone.gainDb = zoneMetadata.gainDb;
        zone.pan = zoneMetadata.pan;
        zone.tuneCents = zoneMetadata.tuneCents
            + (static_cast<float>(zoneMetadata.transposeSemitones) * 100.0f);
        if (zoneMetadata.hasTriggerMode)
            zone.triggerMode = zoneMetadata.triggerMode;
        if (zoneMetadata.hasLoopMode)
            zone.loopMode = zoneMetadata.loopMode;
        result.program.zones.push_back(zone);
    }

    if (result.program.zones.empty())
    {
        addDiagnostic(result.probe,
            DiagnosticSeverity::warning,
            "NKI import: no playable legacy zones were translated");
    }

    return result;
}

juce::String buildProbeSummary(const ProbeResult& result)
{
    switch (result.status)
    {
        case ProbeStatus::legacyDiscreteSampleCandidate:
        {
            auto summary = "NKI probe: " + juce::String(result.sampleReferences.size())
                + " external sample reference"
                + (result.sampleReferences.size() == 1 ? "" : "s")
                + " found";
            if (!result.zoneMetadata.empty())
                summary += ", " + juce::String(result.zoneMetadata.size()) + " zone metadata block" + (result.zoneMetadata.size() == 1 ? "" : "s");
            if (!result.groupNames.isEmpty())
                summary += " across " + juce::String(result.groupNames.size()) + " group" + (result.groupNames.size() == 1 ? "" : "s");
            summary += ", " + juce::String(result.resolvedSampleFiles.size()) + " resolved";
            if (!result.missingSampleReferences.isEmpty())
                summary += ", " + juce::String(result.missingSampleReferences.size()) + " unresolved";
            summary += "; playable import requires resolved samples and supported zone metadata";
            if (!result.containerReferences.isEmpty())
                summary += " (container refs also detected)";

            return summary;
        }

        case ProbeStatus::unsupportedContainerReference:
            return "NKI probe: container-based or newer format detected";

        case ProbeStatus::unsupportedOrUnrecognized:
            return "NKI probe: no legacy external sample references detected";

        case ProbeStatus::invalidFile:
        default:
            return "NKI probe failed: file not found, unreadable, or unsupported extension";
    }
}
} // namespace audiocity::engine::nki