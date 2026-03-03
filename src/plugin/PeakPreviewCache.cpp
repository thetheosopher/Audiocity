#include "PeakPreviewCache.h"

#include <sstream>

namespace
{
constexpr auto kRootTag = "peakPreviewCache";
constexpr auto kEntryTag = "entry";
constexpr auto kPeaksTag = "peaks";
constexpr auto kVersion = 1;

juce::String normalisePathForComparison(const juce::String& path)
{
   #if JUCE_WINDOWS
    return path.replaceCharacter('\\', '/').toLowerCase();
   #else
    return path.replaceCharacter('\\', '/');
   #endif
}

bool pathsMatch(const juce::String& a, const juce::String& b)
{
    return normalisePathForComparison(a) == normalisePathForComparison(b);
}

juce::String encodePeaks(const std::vector<float>& peaks)
{
    juce::StringArray values;
    values.ensureStorageAllocated(static_cast<int>(peaks.size()));

    for (const auto peak : peaks)
        values.add(juce::String(juce::jlimit(0.0f, 1.0f, peak), 6));

    return values.joinIntoString(" ");
}

std::vector<float> decodePeaks(const juce::String& encoded)
{
    std::vector<float> peaks;
    juce::StringArray values;
    values.addTokens(encoded, " ", {});

    peaks.reserve(static_cast<std::size_t>(values.size()));
    for (const auto& value : values)
        peaks.push_back(juce::jlimit(0.0f, 1.0f, static_cast<float>(value.getDoubleValue())));

    return peaks;
}
}

namespace audiocity::plugin
{
PeakPreviewCacheStore::PeakPreviewCacheStore(juce::File cacheFile)
    : cacheFile_(std::move(cacheFile))
{
}

juce::File PeakPreviewCacheStore::getDefaultCacheFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Audiocity")
        .getChildFile("Cache")
        .getChildFile("peak_preview_cache.xml");
}

PeakPreviewCacheData PeakPreviewCacheStore::load() const
{
    PeakPreviewCacheData data;

    if (!cacheFile_.existsAsFile())
        return data;

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(cacheFile_));
    if (xml == nullptr || !xml->hasTagName(kRootTag))
        return data;

    data.libraryRootPath = xml->getStringAttribute("libraryRoot", {});

    for (auto* entryXml : xml->getChildIterator())
    {
        if (!entryXml->hasTagName(kEntryTag))
            continue;

        const auto path = entryXml->getStringAttribute("path", {});
        if (path.isEmpty())
            continue;

        PeakPreviewCacheEntry entry;
        entry.fileSizeBytes = entryXml->getStringAttribute("size", "0").getLargeIntValue();
        entry.metadataLine = entryXml->getStringAttribute("metadata", {});
        entry.loopFormatBadge = entryXml->getStringAttribute("loopBadge", {});
        entry.loopMetadataLine = entryXml->getStringAttribute("loopMeta", {});

        if (const auto* peaksXml = entryXml->getChildByName(kPeaksTag))
            entry.peaks = decodePeaks(peaksXml->getAllSubText().trim());

        data.entries.emplace(path.toStdString(), std::move(entry));
    }

    return data;
}

bool PeakPreviewCacheStore::save(const PeakPreviewCacheData& data) const
{
    const auto parent = cacheFile_.getParentDirectory();
    if (!parent.exists() && !parent.createDirectory())
        return false;

    juce::XmlElement root(kRootTag);
    root.setAttribute("version", kVersion);
    root.setAttribute("libraryRoot", data.libraryRootPath);

    for (const auto& [path, entry] : data.entries)
    {
        auto* entryXml = root.createNewChildElement(kEntryTag);
        entryXml->setAttribute("path", path);
        entryXml->setAttribute("size", juce::String(entry.fileSizeBytes));
        entryXml->setAttribute("metadata", entry.metadataLine);
        entryXml->setAttribute("loopBadge", entry.loopFormatBadge);
        entryXml->setAttribute("loopMeta", entry.loopMetadataLine);

        auto* peaksXml = entryXml->createNewChildElement(kPeaksTag);
        peaksXml->addTextElement(encodePeaks(entry.peaks));
    }

    return cacheFile_.replaceWithText(root.toString(), false, false, "\n");
}

bool PeakPreviewCacheStore::reset() const
{
    return !cacheFile_.exists() || cacheFile_.deleteFile();
}

std::string makePeakPreviewCacheKey(const juce::File& file)
{
    return normalisePathForComparison(file.getFullPathName()).toStdString();
}
}
