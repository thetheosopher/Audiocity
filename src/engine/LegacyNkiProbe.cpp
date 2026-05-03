#include "LegacyNkiProbe.h"

#include <juce_audio_formats/juce_audio_formats.h>

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

        if (const auto sampleReference = normaliseCandidateReference(printable, sampleExtensions);
            sampleReference.isNotEmpty())
        {
            commitZoneMetadata(result, pending, currentGroupName);
            pending.sampleReference = sampleReference;
            if (pending.groupName.isEmpty())
                pending.groupName = currentGroupName;
            commitZoneMetadata(result, pending, currentGroupName);
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

        const auto sampleFile = resolveSampleReference(file, zoneMetadata.sampleReference);
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