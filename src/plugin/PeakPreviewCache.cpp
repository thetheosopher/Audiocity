#include "PeakPreviewCache.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace
{
constexpr auto kRootTag = "peakPreviewCache";
constexpr auto kEntryTag = "entry";
constexpr auto kPeaksTag = "peaks";
constexpr auto kVersion = 2;
constexpr juce::int64 kMaximumCacheFileBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaximumSerializedPayloadBytes = 12 * 1024 * 1024;
constexpr std::size_t kMaximumSerializedEntryBytes = 128 * 1024;
constexpr std::size_t kMaximumRootPartitions = 32;
constexpr juce::int64 kMaximumPartitionDirectoryBytes = 128 * 1024 * 1024;

juce::String normalisePathForComparison(const juce::String& path)
{
   #if JUCE_WINDOWS
    return path.replaceCharacter('\\', '/').toLowerCase();
   #else
    return path.replaceCharacter('\\', '/');
   #endif
}

juce::String encodePeaks(const std::vector<float>& peaks)
{
    juce::StringArray values;
    const auto peakCount = std::min(
        peaks.size(),
        audiocity::plugin::PeakPreviewCacheStore::maxPeaksPerEntry);
    values.ensureStorageAllocated(static_cast<int>(peakCount));

    for (std::size_t index = 0; index < peakCount; ++index)
        values.add(juce::String(juce::jlimit(0.0f, 1.0f, peaks[index]), 6));

    return values.joinIntoString(" ");
}

bool decodePeaks(const juce::String& encoded, std::vector<float>& peaks)
{
    peaks.clear();
    juce::StringArray values;
    values.addTokens(encoded, " ", {});
    if (values.isEmpty())
        return false;

    const auto valueCount = juce::jmin(values.size(),
        static_cast<int>(audiocity::plugin::PeakPreviewCacheStore::maxPeaksPerEntry));
    peaks.reserve(static_cast<std::size_t>(valueCount));
    for (auto index = 0; index < valueCount; ++index)
    {
        const auto& value = values[index];
        const auto* begin = value.toRawUTF8();
        char* end = nullptr;
        errno = 0;
        const auto parsed = std::strtof(begin, &end);
        if (begin == end || end == nullptr || *end != '\0'
            || errno == ERANGE || !std::isfinite(parsed))
        {
            peaks.clear();
            return false;
        }
        peaks.push_back(juce::jlimit(0.0f, 1.0f, parsed));
    }

    return true;
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

juce::File PeakPreviewCacheStore::getCacheFileForRoot(const juce::File& rootFolder)
{
    const auto normalizedRoot = normalisePathForComparison(rootFolder.getFullPathName());
    const auto partition = "root_" + juce::String(normalizedRoot.hashCode64()) + ".xml";
    return getDefaultCacheFile().getParentDirectory()
        .getChildFile("peak_previews_v2")
        .getChildFile(partition);
}

PeakPreviewCacheData PeakPreviewCacheStore::load() const
{
    PeakPreviewCacheData data;

    if (!cacheFile_.existsAsFile() || cacheFile_.getSize() > kMaximumCacheFileBytes)
        return data;

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(cacheFile_));
    if (xml == nullptr || !xml->hasTagName(kRootTag))
        return data;

    const auto version = xml->getIntAttribute("version", 1);
    if (version < 1 || version > kVersion)
        return data;

    data.libraryRootPath = xml->getStringAttribute("libraryRoot", {});

    for (auto* entryXml : xml->getChildIterator())
    {
        if (data.entries.size() >= maxEntries)
            break;

        if (!entryXml->hasTagName(kEntryTag))
            continue;

        const auto path = entryXml->getStringAttribute("path", {});
        if (path.isEmpty())
            continue;

        PeakPreviewCacheEntry entry;
        entry.fileSizeBytes = entryXml->getStringAttribute("size", "0").getLargeIntValue();
        entry.fileModificationTimeMs = entryXml->getStringAttribute("mtime", "0").getLargeIntValue();
        entry.lastAccessTimeMs = entryXml->getStringAttribute("accessed", "0").getLargeIntValue();
        entry.metadataLine = entryXml->getStringAttribute("metadata", {});
        entry.loopFormatBadge = entryXml->getStringAttribute("loopBadge", {});
        entry.loopMetadataLine = entryXml->getStringAttribute("loopMeta", {});

        const auto* peaksXml = entryXml->getChildByName(kPeaksTag);
        if (peaksXml == nullptr
            || !decodePeaks(peaksXml->getAllSubText().trim(), entry.peaks))
        {
            continue;
        }

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

    std::vector<std::pair<const std::string*, const PeakPreviewCacheEntry*>> orderedEntries;
    orderedEntries.reserve(data.entries.size());
    for (const auto& [path, entry] : data.entries)
        orderedEntries.emplace_back(&path, &entry);
    std::sort(orderedEntries.begin(), orderedEntries.end(), [](const auto& left, const auto& right)
    {
        return left.second->lastAccessTimeMs > right.second->lastAccessTimeMs;
    });

    std::size_t serializedPayloadBytes = 0;
    const auto candidateCount = juce::jmin(orderedEntries.size(), maxEntries);
    for (std::size_t index = 0; index < candidateCount; ++index)
    {
        const auto& path = *orderedEntries[index].first;
        const auto& entry = *orderedEntries[index].second;
        auto entryXml = std::make_unique<juce::XmlElement>(kEntryTag);
        entryXml->setAttribute("path", path);
        entryXml->setAttribute("size", juce::String(entry.fileSizeBytes));
        entryXml->setAttribute("mtime", juce::String(entry.fileModificationTimeMs));
        entryXml->setAttribute("accessed", juce::String(entry.lastAccessTimeMs));
        entryXml->setAttribute("metadata", entry.metadataLine);
        entryXml->setAttribute("loopBadge", entry.loopFormatBadge);
        entryXml->setAttribute("loopMeta", entry.loopMetadataLine);

        auto* peaksXml = entryXml->createNewChildElement(kPeaksTag);
        peaksXml->addTextElement(encodePeaks(entry.peaks));

        const auto serializedEntryBytes = static_cast<std::size_t>(
            entryXml->toString().getNumBytesAsUTF8());
        if (serializedEntryBytes > kMaximumSerializedEntryBytes)
            continue;
        if (serializedPayloadBytes + serializedEntryBytes > kMaximumSerializedPayloadBytes)
            break;

        serializedPayloadBytes += serializedEntryBytes;
        root.addChildElement(entryXml.release());
    }

    const auto serialized = root.toString();
    if (serialized.getNumBytesAsUTF8() > kMaximumCacheFileBytes)
        return false;

    juce::TemporaryFile temporary(cacheFile_);
    if (!temporary.getFile().replaceWithText(serialized, false, false, "\n"))
        return false;

    if (!temporary.overwriteTargetFileWithTemporary())
        return false;

    const auto partitionDirectory = getDefaultCacheFile().getParentDirectory()
        .getChildFile("peak_previews_v2");
    if (cacheFile_.getParentDirectory() != partitionDirectory)
        return true;

    std::vector<juce::File> partitions;
    for (const auto& entry : juce::RangedDirectoryIterator(
             partitionDirectory, false, "*.xml", juce::File::findFiles))
    {
        partitions.push_back(entry.getFile());
    }
    std::sort(partitions.begin(), partitions.end(), [this](const auto& left, const auto& right)
    {
        if (left == right)
            return false;
        if (left == cacheFile_)
            return true;
        if (right == cacheFile_)
            return false;
        return left.getLastModificationTime() > right.getLastModificationTime();
    });

    juce::int64 retainedBytes = 0;
    for (std::size_t index = 0; index < partitions.size(); ++index)
    {
        const auto partitionBytes = partitions[index].getSize();
        if (index < kMaximumRootPartitions
            && retainedBytes + partitionBytes <= kMaximumPartitionDirectoryBytes)
        {
            retainedBytes += partitionBytes;
            continue;
        }

        if (!partitions[index].deleteFile())
            return false;
    }

    return true;
}

bool PeakPreviewCacheStore::reset() const
{
    return !cacheFile_.exists() || cacheFile_.deleteFile();
}

std::string makePeakPreviewCacheKey(const juce::File& file)
{
    return normalisePathForComparison(file.getFullPathName()).toStdString();
}

bool isPeakPreviewCacheEntryCurrent(const PeakPreviewCacheEntry& entry,
                                    const juce::File& file) noexcept
{
    return entry.fileSizeBytes == file.getSize()
        && entry.fileModificationTimeMs > 0
        && entry.fileModificationTimeMs == file.getLastModificationTime().toMilliseconds();
}
}
