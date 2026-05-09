#include "DecentSamplerImporter.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace audiocity::engine::dspreset
{
bool ImportResult::hasErrors() const noexcept
{
    for (const auto& d : diagnostics)
        if (d.severity == ImportDiagnostic::Severity::error)
            return true;
    return false;
}

namespace
{
void addDiagnostic(std::vector<ImportDiagnostic>& diagnostics,
                   const ImportDiagnostic::Severity severity,
                   const juce::String& message)
{
    diagnostics.push_back({ severity, message.toStdString(), 0 });
}

[[nodiscard]] int parseMidiNote(const juce::String& raw, const int defaultValue)
{
    if (raw.isEmpty())
        return defaultValue;

    if (raw.containsOnly("0123456789-+ "))
    {
        const auto value = raw.getIntValue();
        return clampMidiNote(value);
    }

    // Note name like C4 / F#3 / Bb-1
    auto trimmed = raw.trim();
    if (trimmed.isEmpty())
        return defaultValue;

    const auto first = trimmed[0];
    int pitchClass = -1;
    switch (first)
    {
        case 'C': case 'c': pitchClass = 0; break;
        case 'D': case 'd': pitchClass = 2; break;
        case 'E': case 'e': pitchClass = 4; break;
        case 'F': case 'f': pitchClass = 5; break;
        case 'G': case 'g': pitchClass = 7; break;
        case 'A': case 'a': pitchClass = 9; break;
        case 'B': case 'b': pitchClass = 11; break;
        default: return defaultValue;
    }

    int index = 1;
    if (index < trimmed.length())
    {
        const auto accidental = trimmed[index];
        if (accidental == '#') { pitchClass = (pitchClass + 1) % 12; ++index; }
        else if (accidental == 'b' || accidental == 'B') { pitchClass = (pitchClass + 11) % 12; ++index; }
    }

    if (index >= trimmed.length())
        return defaultValue;

    const auto octaveText = trimmed.substring(index);
    if (octaveText.isEmpty() || !octaveText.containsOnly("0123456789-+"))
        return defaultValue;

    const auto octave = octaveText.getIntValue();
    // Convention: C-1 = MIDI 0, C4 = MIDI 60.
    const auto midi = (octave + 1) * 12 + pitchClass;
    return clampMidiNote(midi);
}

[[nodiscard]] juce::File resolveSamplePath(const juce::String& rawPath, const juce::File& presetFolder)
{
    if (rawPath.isEmpty())
        return {};

    juce::String normalized = rawPath.replaceCharacter('\\', '/');
    juce::File direct(normalized);
    if (direct.isAbsolutePath(normalized) && direct.existsAsFile())
        return direct;

    const auto relative = presetFolder.getChildFile(normalized);
    if (relative.existsAsFile())
        return relative;

    const auto siblingSamples = presetFolder.getChildFile("Samples").getChildFile(juce::File(normalized).getFileName());
    if (siblingSamples.existsAsFile())
        return siblingSamples;

    auto parent = presetFolder.getParentDirectory();
    for (int i = 0; i < 2 && parent.exists(); ++i)
    {
        const auto candidate = parent.getChildFile(normalized);
        if (candidate.existsAsFile())
            return candidate;
        const auto candidateInSamples = parent.getChildFile("Samples").getChildFile(juce::File(normalized).getFileName());
        if (candidateInSamples.existsAsFile())
            return candidateInSamples;
        parent = parent.getParentDirectory();
    }

    return {};
}

[[nodiscard]] ZoneTriggerMode parseTrigger(const juce::String& value, const ZoneTriggerMode fallback)
{
    if (value.isEmpty())
        return fallback;
    const auto lower = value.toLowerCase();
    if (lower == "release")
        return ZoneTriggerMode::release;
    if (lower == "first" || lower == "legato" || lower == "normal" || lower == "attack")
        return ZoneTriggerMode::gate;
    return fallback;
}

class Importer
{
public:
    explicit Importer(juce::AudioFormatManager& fm, const juce::File& presetFile)
        : formatManager(fm), presetFolder(presetFile.getParentDirectory()) {}

    void run(const juce::XmlElement& root, ImportResult& result)
    {
        result.program.name = "DecentSampler";
        for (auto* groupsNode : root.getChildWithTagNameIterator("groups"))
            processGroupsContainer(*groupsNode, result);
        // Some presets place <group> directly under root with no <groups> wrapper:
        for (auto* groupNode : root.getChildWithTagNameIterator("group"))
            processGroup(*groupNode, result, /*defaultTrigger*/ ZoneTriggerMode::gate, 0.0f, 0.0f);
        // Some presets also place <sample> at root level.
        for (auto* sampleNode : root.getChildWithTagNameIterator("sample"))
            processSample(*sampleNode, result, /*groupIndex*/ -1, /*defaultTrigger*/ ZoneTriggerMode::gate, 0.0f, 0.0f);

        if (result.program.zones.empty())
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error, "No <sample> elements found in DecentSampler preset");
    }

private:
    void processGroupsContainer(const juce::XmlElement& container, ImportResult& result)
    {
        for (auto* groupNode : container.getChildWithTagNameIterator("group"))
            processGroup(*groupNode, result, ZoneTriggerMode::gate, 0.0f, 0.0f);
    }

    void processGroup(const juce::XmlElement& groupNode, ImportResult& result,
                      const ZoneTriggerMode parentTrigger,
                      const float parentVolumeDb, const float parentPan)
    {
        Group group;
        group.name = groupNode.getStringAttribute("name", "Group " + juce::String(static_cast<int>(result.program.groups.size() + 1))).toStdString();
        const auto groupVolumeDb = parentVolumeDb + parseVolume(groupNode);
        const auto groupPan = juce::jlimit(-1.0f, 1.0f, parentPan + parsePan(groupNode));
        group.gainDb = groupVolumeDb;
        group.pan = groupPan;
        const auto groupTrigger = parseTrigger(groupNode.getStringAttribute("trigger"), parentTrigger);
        group.triggerMode = groupTrigger;

        result.program.groups.push_back(group);
        const auto groupIndex = static_cast<int>(result.program.groups.size() - 1);

        for (auto* sampleNode : groupNode.getChildWithTagNameIterator("sample"))
            processSample(*sampleNode, result, groupIndex, groupTrigger, groupVolumeDb, groupPan);

        // Nested groups (rare) - flatten.
        for (auto* nested : groupNode.getChildWithTagNameIterator("group"))
            processGroup(*nested, result, groupTrigger, groupVolumeDb, groupPan);

        if (groupNode.getChildByName("effects") != nullptr || groupNode.getChildByName("modulators") != nullptr)
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::warning,
                          "Group <effects>/<modulators> blocks are not imported (group: " + juce::String(group.name) + ")");
    }

    void processSample(const juce::XmlElement& sampleNode, ImportResult& result,
                       const int groupIndex, const ZoneTriggerMode parentTrigger,
                       const float parentVolumeDb, const float parentPan)
    {
        const auto rawPath = sampleNode.getStringAttribute("path");
        if (rawPath.isEmpty())
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::warning, "Sample element missing 'path' attribute - skipping");
            return;
        }

        const auto resolved = resolveSamplePath(rawPath, presetFolder);
        if (!resolved.existsAsFile())
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error, "DecentSampler sample not found: " + rawPath);
            return;
        }

        const auto rootNote = parseMidiNote(sampleNode.getStringAttribute("rootNote"), 60);
        const auto loNote = parseMidiNote(sampleNode.getStringAttribute("loNote"), rootNote);
        const auto hiNote = parseMidiNote(sampleNode.getStringAttribute("hiNote"), rootNote);
        const auto loVel = juce::jlimit(0, 127, sampleNode.getIntAttribute("loVel", 0));
        const auto hiVel = juce::jlimit(0, 127, sampleNode.getIntAttribute("hiVel", 127));

        const auto assetIndex = getOrAddSampleAsset(resolved, rootNote, result);
        if (assetIndex < 0)
            return;

        Zone zone;
        zone.sampleAssetIndex = assetIndex;
        zone.groupIndex = groupIndex;
        zone.keyRange = MidiRange::fromUnordered(loNote, hiNote);
        zone.velocityRange = VelocityRange::fromUnordered(loVel, hiVel);
        zone.rootMidiNote = rootNote;

        const auto sampleStart = sampleNode.getIntAttribute("start", 0);
        const auto sampleEnd = sampleNode.getIntAttribute("end", -1);
        zone.sampleStart = juce::jmax(0, sampleStart);
        zone.sampleEndExclusive = sampleEnd > 0 ? sampleEnd : -1;

        const auto loopStart = sampleNode.getIntAttribute("loopStart", -1);
        const auto loopEnd = sampleNode.getIntAttribute("loopEnd", -1);
        const auto loopEnabledAttr = sampleNode.getStringAttribute("loopEnabled").toLowerCase();
        const auto loopEnabled = loopEnabledAttr == "true" || loopEnabledAttr == "1";
        if (loopEnabled && loopStart >= 0 && loopEnd > loopStart)
        {
            zone.loopStart = loopStart;
            zone.loopEndExclusive = loopEnd;
            zone.loopMode = ZoneLoopMode::continuous;
        }

        const auto sampleVolumeDb = parentVolumeDb + parseVolume(sampleNode);
        const auto samplePan = juce::jlimit(-1.0f, 1.0f, parentPan + parsePan(sampleNode));
        zone.gainDb = sampleVolumeDb;
        zone.pan = samplePan;

        const auto tuning = static_cast<float>(sampleNode.getDoubleAttribute("tuning", 0.0));
        const auto coarse = static_cast<int>(std::trunc(tuning));
        const auto cents = (tuning - static_cast<float>(coarse)) * 100.0f;
        zone.tuneCents = cents;
        zone.rootMidiNote = clampMidiNote(rootNote - coarse);

        zone.triggerMode = parseTrigger(sampleNode.getStringAttribute("trigger"), parentTrigger);

        result.program.zones.push_back(zone);
    }

    [[nodiscard]] static float parseVolume(const juce::XmlElement& node)
    {
        const auto raw = node.getStringAttribute("volume");
        if (raw.isEmpty())
            return 0.0f;
        // DecentSampler expresses volume either as "0dB" / "-6dB" or as a linear scalar.
        const auto trimmed = raw.trim();
        const auto lower = trimmed.toLowerCase();
        if (lower.endsWith("db"))
        {
            return static_cast<float>(lower.dropLastCharacters(2).getDoubleValue());
        }
        const auto linear = static_cast<float>(trimmed.getDoubleValue());
        if (linear > 0.0f)
            return juce::Decibels::gainToDecibels(linear, -120.0f);
        return 0.0f;
    }

    [[nodiscard]] static float parsePan(const juce::XmlElement& node)
    {
        if (!node.hasAttribute("pan"))
            return 0.0f;
        // Spec uses -100..100 (percent) for pan.
        const auto raw = static_cast<float>(node.getDoubleAttribute("pan", 0.0));
        if (std::abs(raw) <= 1.0f + 0.0001f)
            return juce::jlimit(-1.0f, 1.0f, raw);
        return juce::jlimit(-1.0f, 1.0f, raw / 100.0f);
    }

    [[nodiscard]] int getOrAddSampleAsset(const juce::File& sampleFile, const int rootMidiNote, ImportResult& result)
    {
        const auto path = sampleFile.getFullPathName().toStdString();
        const auto found = sampleAssetIndices.find(path);
        if (found != sampleAssetIndices.end())
            return found->second;

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(sampleFile));
        if (reader == nullptr)
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                          "Unsupported or unreadable DecentSampler sample " + sampleFile.getFullPathName());
            return -1;
        }

        SampleAsset asset;
        asset.sourcePath = path;
        asset.displayName = sampleFile.getFileName().toStdString();
        asset.lengthSamples = static_cast<int>(juce::jlimit<juce::int64>(0, std::numeric_limits<int>::max(), reader->lengthInSamples));
        asset.numChannels = static_cast<int>(reader->numChannels);
        asset.sampleRateHz = reader->sampleRate;
        asset.rootMidiNote = rootMidiNote;
        asset.bitDepth = static_cast<int>(reader->bitsPerSample);
        asset.embeddedInProgram = false;

        if (!asset.hasAudio())
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                          "DecentSampler sample has no readable audio " + sampleFile.getFullPathName());
            return -1;
        }

        juce::AudioBuffer<float> sampleData(asset.numChannels, asset.lengthSamples);
        if (!reader->read(&sampleData, 0, asset.lengthSamples, 0, true, true))
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                          "Could not decode DecentSampler sample " + sampleFile.getFullPathName());
            return -1;
        }

        result.program.sampleAssets.push_back(asset);
        result.sampleDataByAsset.push_back(sampleData);
        const auto index = static_cast<int>(result.program.sampleAssets.size() - 1);
        sampleAssetIndices[path] = index;
        return index;
    }

    juce::AudioFormatManager& formatManager;
    juce::File presetFolder;
    std::map<std::string, int> sampleAssetIndices;
};
} // namespace

ImportResult importFile(const juce::File& dspresetFile)
{
    ImportResult result;

    if (!dspresetFile.existsAsFile())
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "DecentSampler preset not found: " + dspresetFile.getFullPathName());
        return result;
    }

    auto xml = juce::parseXML(dspresetFile);
    if (xml == nullptr)
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "DecentSampler preset is not valid XML: " + dspresetFile.getFullPathName());
        return result;
    }

    if (!xml->hasTagName("DecentSampler"))
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "Root element is not <DecentSampler> in: " + dspresetFile.getFullPathName());
        return result;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    Importer importer(fm, dspresetFile);
    importer.run(*xml, result);
    return result;
}

juce::String buildImportSummary(const ImportResult& result, const bool imported)
{
    int errorCount = 0;
    int warningCount = 0;
    for (const auto& d : result.diagnostics)
    {
        if (d.severity == ImportDiagnostic::Severity::error)
            ++errorCount;
        else
            ++warningCount;
    }

    juce::String summary = imported
        ? ("DecentSampler imported: " + juce::String(static_cast<int>(result.program.zones.size())) + " zones")
        : juce::String("DecentSampler import failed");

    if (errorCount > 0)
        summary += " (" + juce::String(errorCount) + " errors)";
    if (warningCount > 0)
        summary += " (" + juce::String(warningCount) + " warnings)";

    for (const auto& d : result.diagnostics)
    {
        summary += "\n";
        summary += d.severity == ImportDiagnostic::Severity::error ? "Error: " : "Warning: ";
        summary += juce::String(d.message);
    }

    return summary;
}
} // namespace audiocity::engine::dspreset
