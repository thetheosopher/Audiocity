#include "BitwigMultisampleImporter.h"

#include "XmlMultisampleImporterUtils.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>

namespace audiocity::engine::bitwig
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
    diagnostics.push_back({ severity, message.toStdString() });
}

[[nodiscard]] ZoneLoopMode parseLoopMode(const juce::String& raw)
{
    const auto lower = raw.toLowerCase();
    if (lower == "loop" || lower == "ping-pong" || lower == "pingpong")
        return ZoneLoopMode::continuous;
    if (lower == "sustain")
        return ZoneLoopMode::sustain;
    return ZoneLoopMode::noLoop;
}

class Importer
{
public:
    Importer(juce::AudioFormatManager& fm, juce::ZipFile& zip)
        : formatManager(fm), zipFile(zip) {}

    void run(const juce::XmlElement& root, ImportResult& result)
    {
        result.program.name = root.getStringAttribute("name", "Bitwig Multisample").toStdString();

        // Bitwig multisamples place <sample> directly under <multisample> with optional <layer>
        // groupings. We treat each <layer> as a Group; samples not inside a layer share an
        // implicit group.
        int implicitGroupIndex = -1;
        for (auto* sampleNode : root.getChildWithTagNameIterator("sample"))
        {
            if (implicitGroupIndex < 0)
            {
                Group g;
                g.name = "Default";
                result.program.groups.push_back(g);
                implicitGroupIndex = static_cast<int>(result.program.groups.size() - 1);
            }
            processSample(*sampleNode, implicitGroupIndex, result);
        }

        for (auto* layerNode : root.getChildWithTagNameIterator("layer"))
        {
            Group g;
            g.name = layerNode->getStringAttribute("name", "Layer " + juce::String(static_cast<int>(result.program.groups.size() + 1))).toStdString();
            result.program.groups.push_back(g);
            const auto groupIndex = static_cast<int>(result.program.groups.size() - 1);
            for (auto* sampleNode : layerNode->getChildWithTagNameIterator("sample"))
                processSample(*sampleNode, groupIndex, result);
        }

        if (result.program.zones.empty())
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                          "No <sample> elements found in Bitwig multisample");
    }

private:
    void processSample(const juce::XmlElement& sampleNode, const int groupIndex, ImportResult& result)
    {
        const auto fileAttr = sampleNode.getStringAttribute("file");
        if (fileAttr.isEmpty())
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::warning,
                          "Sample element missing 'file' attribute - skipping");
            return;
        }

        const auto* keyNode = sampleNode.getChildByName("key");
        const int rootNote = keyNode != nullptr ? juce::jlimit(0, 127, keyNode->getIntAttribute("root", 60)) : 60;
        const int lowKey = keyNode != nullptr ? juce::jlimit(0, 127, keyNode->getIntAttribute("low", rootNote)) : rootNote;
        const int highKey = keyNode != nullptr ? juce::jlimit(0, 127, keyNode->getIntAttribute("high", rootNote)) : rootNote;

        const auto* velNode = sampleNode.getChildByName("velocity");
        const int lowVel = velNode != nullptr ? juce::jlimit(0, 127, velNode->getIntAttribute("low", 0)) : 0;
        const int highVel = velNode != nullptr ? juce::jlimit(0, 127, velNode->getIntAttribute("high", 127)) : 127;

        const auto assetIndex = getOrAddSampleAsset(fileAttr, rootNote, result);
        if (assetIndex < 0)
            return;

        Zone zone;
        zone.sampleAssetIndex = assetIndex;
        zone.groupIndex = groupIndex;
        zone.keyRange = MidiRange::fromUnordered(lowKey, highKey);
        zone.velocityRange = VelocityRange::fromUnordered(lowVel, highVel);
        zone.rootMidiNote = rootNote;

        const auto sampleStart = static_cast<int>(std::round(sampleNode.getDoubleAttribute("sample-start", 0.0)));
        const auto sampleStop = static_cast<int>(std::round(sampleNode.getDoubleAttribute("sample-stop", -1.0)));
        zone.sampleStart = juce::jmax(0, sampleStart);
        zone.sampleEndExclusive = sampleStop > 0 ? sampleStop : -1;

        if (const auto* loopNode = sampleNode.getChildByName("loop"))
        {
            const auto mode = parseLoopMode(loopNode->getStringAttribute("mode", "off"));
            const auto ls = static_cast<int>(std::round(loopNode->getDoubleAttribute("start", -1.0)));
            const auto le = static_cast<int>(std::round(loopNode->getDoubleAttribute("stop", -1.0)));
            if (mode != ZoneLoopMode::noLoop && ls >= 0 && le > ls)
            {
                zone.loopStart = ls;
                zone.loopEndExclusive = le;
                zone.loopMode = mode;
            }
        }

        zone.gainDb = static_cast<float>(sampleNode.getDoubleAttribute("gain", 0.0));
        zone.tuneCents = static_cast<float>(sampleNode.getDoubleAttribute("tune", 0.0)) * 100.0f;

        result.program.zones.push_back(zone);
    }

    [[nodiscard]] int getOrAddSampleAsset(const juce::String& fileName, const int rootMidiNote, ImportResult& result)
    {
        if (!xml_multi::isSafeArchiveRelativePath(fileName))
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                          "Bitwig multisample rejected unsafe audio entry path: " + fileName);
            return -1;
        }

        const auto key = fileName.toStdString();
        const auto found = sampleAssetIndices.find(key);
        if (found != sampleAssetIndices.end())
            return found->second;

        const auto entryIndex = findEntryIndex(fileName);
        if (entryIndex < 0)
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                          "Bitwig multisample missing audio entry: " + fileName);
            return -1;
        }

        std::unique_ptr<juce::InputStream> raw(zipFile.createStreamForEntry(entryIndex));
        if (raw == nullptr)
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                          "Could not open zipped audio entry: " + fileName);
            return -1;
        }

        // Buffer entire entry to memory so JUCE can seek for WAV header parsing.
        juce::MemoryBlock blob;
        raw->readIntoMemoryBlock(blob);
        auto memoryStream = std::make_unique<juce::MemoryInputStream>(blob, true);

        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(std::move(memoryStream)));
        if (reader == nullptr)
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                          "Unsupported audio format inside Bitwig multisample: " + fileName);
            return -1;
        }

        SampleAsset asset;
        asset.sourcePath = key;
        asset.displayName = juce::File::createFileWithoutCheckingPath(fileName).getFileName().toStdString();
        asset.lengthSamples = static_cast<int>(juce::jlimit<juce::int64>(0, std::numeric_limits<int>::max(), reader->lengthInSamples));
        asset.numChannels = static_cast<int>(reader->numChannels);
        asset.sampleRateHz = reader->sampleRate;
        asset.rootMidiNote = rootMidiNote;
        asset.bitDepth = static_cast<int>(reader->bitsPerSample);
        asset.embeddedInProgram = false;

        if (!asset.hasAudio())
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                          "Bitwig multisample audio has no readable samples: " + fileName);
            return -1;
        }

        juce::AudioBuffer<float> sampleData(asset.numChannels, asset.lengthSamples);
        if (!reader->read(&sampleData, 0, asset.lengthSamples, 0, true, true))
        {
            addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                          "Could not decode Bitwig multisample audio: " + fileName);
            return -1;
        }

        result.program.sampleAssets.push_back(asset);
        result.sampleDataByAsset.push_back(sampleData);
        const auto index = static_cast<int>(result.program.sampleAssets.size() - 1);
        sampleAssetIndices[key] = index;
        return index;
    }

    [[nodiscard]] int findEntryIndex(const juce::String& fileName) const
    {
        // Try exact match first, then case-insensitive basename match.
        for (int i = 0; i < zipFile.getNumEntries(); ++i)
        {
            const auto* entry = zipFile.getEntry(i);
            if (entry != nullptr
                && xml_multi::isSafeArchiveRelativePath(entry->filename)
                && entry->filename == fileName)
            {
                return i;
            }
        }
        const auto base = juce::File::createFileWithoutCheckingPath(fileName).getFileName();
        for (int i = 0; i < zipFile.getNumEntries(); ++i)
        {
            const auto* entry = zipFile.getEntry(i);
            if (entry == nullptr) continue;
            if (!xml_multi::isSafeArchiveRelativePath(entry->filename)) continue;
            const auto entryBase = juce::File::createFileWithoutCheckingPath(entry->filename).getFileName();
            if (entryBase.equalsIgnoreCase(base))
                return i;
        }
        return -1;
    }

    juce::AudioFormatManager& formatManager;
    juce::ZipFile& zipFile;
    std::map<std::string, int> sampleAssetIndices;
};
} // namespace

ImportResult importFile(const juce::File& multisampleFile)
{
    ImportResult result;

    if (!multisampleFile.existsAsFile())
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "Bitwig multisample not found: " + multisampleFile.getFullPathName());
        return result;
    }

    juce::ZipFile zip(multisampleFile);
    if (zip.getNumEntries() == 0)
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "Bitwig multisample is empty or not a valid ZIP: " + multisampleFile.getFullPathName());
        return result;
    }

    int manifestIndex = -1;
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* entry = zip.getEntry(i);
        if (entry != nullptr && entry->filename.equalsIgnoreCase("multisample.xml"))
        {
            manifestIndex = i;
            break;
        }
    }
    if (manifestIndex < 0)
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "Bitwig multisample missing 'multisample.xml': " + multisampleFile.getFullPathName());
        return result;
    }

    std::unique_ptr<juce::InputStream> manifestStream(zip.createStreamForEntry(manifestIndex));
    if (manifestStream == nullptr)
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "Could not read 'multisample.xml' inside: " + multisampleFile.getFullPathName());
        return result;
    }
    const auto manifestText = manifestStream->readEntireStreamAsString();
    auto xml = juce::parseXML(manifestText);
    if (xml == nullptr || !xml->hasTagName("multisample"))
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "Invalid Bitwig multisample manifest: " + multisampleFile.getFullPathName());
        return result;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    Importer importer(fm, zip);
    importer.run(*xml, result);
    return result;
}

juce::String buildImportSummary(const ImportResult& result, const bool imported)
{
    int errorCount = 0;
    int warningCount = 0;
    for (const auto& d : result.diagnostics)
    {
        if (d.severity == ImportDiagnostic::Severity::error) ++errorCount;
        else ++warningCount;
    }

    juce::String summary;
    if (imported)
    {
        summary << "Bitwig multisample imported: "
                << static_cast<int>(result.program.zones.size()) << " zone(s), "
                << static_cast<int>(result.program.sampleAssets.size()) << " sample(s)";
    }
    else
    {
        summary << "Bitwig multisample import failed";
    }
    if (warningCount > 0)
        summary << " (" << warningCount << " warning" << (warningCount == 1 ? "" : "s") << ")";
    if (errorCount > 0)
        summary << " (" << errorCount << " error" << (errorCount == 1 ? "" : "s") << ")";

    if (!result.diagnostics.empty())
    {
        for (size_t i = 0; i < result.diagnostics.size() && i < 3; ++i)
            summary << "\n  - " << juce::String(result.diagnostics[i].message);
        if (result.diagnostics.size() > 3)
            summary << "\n  ... " << static_cast<int>(result.diagnostics.size() - 3) << " more";
    }
    return summary;
}
} // namespace audiocity::engine::bitwig
