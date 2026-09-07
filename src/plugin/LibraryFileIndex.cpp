#include "LibraryFileIndex.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
constexpr std::array<std::uint8_t, 8> kIndexMagic{
    'A', 'C', 'T', 'Y', 'L', 'I', 'B', '1'
};
constexpr std::size_t kHeaderBytes = 8 + (6 * sizeof(std::uint32_t));
constexpr std::size_t kMaximumPathBytes = 32 * 1024;
constexpr int kMaximumPathComponents = 256;
constexpr int kMaximumCachedDirectoryDepth = 8;
constexpr std::size_t kMaximumLinkCacheEntries = 16 * 1024;
constexpr std::size_t kScanDeliveryBatchSize = 256;
constexpr auto kScanMaximumDeliveryDelay = std::chrono::milliseconds(175);
constexpr std::uint8_t kInstrumentFlag = 1u;

constexpr std::array<std::uint32_t, 256> makeCrc32Table() noexcept
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < table.size(); ++index)
    {
        auto value = index;
        for (auto bit = 0; bit < 8; ++bit)
            value = (value >> 1u) ^ (0xedb88320u & (0u - (value & 1u)));
        table[index] = value;
    }
    return table;
}

constexpr auto kCrc32Table = makeCrc32Table();

void appendU32(juce::MemoryOutputStream& output, const std::uint32_t value)
{
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 24u)
    };
    output.write(bytes.data(), bytes.size());
}

void appendU64(juce::MemoryOutputStream& output, const std::uint64_t value)
{
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    output.write(bytes.data(), bytes.size());
}

std::uint32_t crc32(const void* const data, const std::size_t size) noexcept
{
    auto crc = std::uint32_t{ 0xffffffffu };
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index)
        crc = kCrc32Table[(crc ^ bytes[index]) & 0xffu] ^ (crc >> 8u);
    return ~crc;
}

class BoundedReader final
{
public:
    BoundedReader(const void* const data, const std::size_t size) noexcept
        : current_(static_cast<const std::uint8_t*>(data)), remaining_(size)
    {
    }

    bool readBytes(const std::size_t count, const std::uint8_t*& bytes) noexcept
    {
        if (count > remaining_)
            return false;

        bytes = current_;
        current_ += count;
        remaining_ -= count;
        return true;
    }

    bool readU8(std::uint8_t& value) noexcept
    {
        const std::uint8_t* bytes = nullptr;
        if (!readBytes(1, bytes))
            return false;
        value = bytes[0];
        return true;
    }

    bool readU32(std::uint32_t& value) noexcept
    {
        const std::uint8_t* bytes = nullptr;
        if (!readBytes(4, bytes))
            return false;

        value = static_cast<std::uint32_t>(bytes[0])
            | (static_cast<std::uint32_t>(bytes[1]) << 8u)
            | (static_cast<std::uint32_t>(bytes[2]) << 16u)
            | (static_cast<std::uint32_t>(bytes[3]) << 24u);
        return true;
    }

    bool readU64(std::uint64_t& value) noexcept
    {
        const std::uint8_t* bytes = nullptr;
        if (!readBytes(8, bytes))
            return false;

        value = 0;
        for (std::size_t index = 0; index < 8; ++index)
            value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8u);
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept { return remaining_; }

private:
    const std::uint8_t* current_ = nullptr;
    std::size_t remaining_ = 0;
};

bool decodeUtf8(const std::uint8_t* const bytes,
                const std::size_t byteCount,
                juce::String& decoded)
{
    if (byteCount == 0 || byteCount > kMaximumPathBytes
        || std::memchr(bytes, 0, byteCount) != nullptr
        || byteCount > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    decoded = juce::String::fromUTF8(
        reinterpret_cast<const char*>(bytes), static_cast<int>(byteCount));
    return static_cast<std::size_t>(decoded.getNumBytesAsUTF8()) == byteCount
        && std::memcmp(decoded.toRawUTF8(), bytes, byteCount) == 0;
}

bool normaliseSafeRelativePath(const juce::String& input, juce::String& normalized)
{
    if (input.isEmpty() || juce::File::isAbsolutePath(input))
        return false;

    const auto slashed = input.replaceCharacter('\\', '/');
    juce::StringArray components;
    components.addTokens(slashed, "/", {});
    components.removeEmptyStrings();
    if (components.isEmpty() || components.size() > kMaximumPathComponents)
        return false;

    for (const auto& component : components)
    {
        if (component == "." || component == ".." || component.length() > 255)
            return false;
    }

    normalized = components.joinIntoString("/");
    return normalized == slashed;
}

juce::String pathKey(juce::String path)
{
    // Windows directories can opt into case-sensitive lookup. Exact-case keys
    // avoid partition collisions, false duplicate rejection, and traversal-cache
    // aliasing there. Case-insensitive volumes may retain redundant spellings,
    // which is preferable to conflating distinct filesystem identities.
    return path.replaceCharacter('\\', '/');
}

enum class CheckedPathStatus : std::uint8_t
{
    safeExisting,
    missing,
    symbolicLink
};

using LinkTraversalCache = std::unordered_map<std::string, CheckedPathStatus>;

bool pathTraversesSymbolicLink(const juce::File& rootFolder,
                               const juce::String& relativePath,
                               LinkTraversalCache& checkedPaths)
{
    juce::StringArray components;
    components.addTokens(relativePath.replaceCharacter('\\', '/'), "/", {});
    components.removeEmptyStrings();

    auto candidate = rootFolder;
    juce::String relativePrefix;
    for (auto componentIndex = 0; componentIndex < components.size(); ++componentIndex)
    {
        const auto& component = components[componentIndex];
        candidate = candidate.getChildFile(component);
        if (relativePrefix.isNotEmpty())
            relativePrefix += "/";
        relativePrefix += component;

        // Cache only a bounded number of directory prefixes. Leaf links are always
        // checked directly, while shared folders (the common 50k-index case) avoid
        // repeated filesystem probes. This remains a point-in-time validation; code
        // opening a file must still tolerate normal filesystem replacement races.
        const auto cacheEligible = componentIndex + 1 < components.size()
            && componentIndex < kMaximumCachedDirectoryDepth;
        const auto cacheKey = cacheEligible ? pathKey(relativePrefix).toStdString() : std::string{};
        if (cacheEligible)
        {
            if (const auto cached = checkedPaths.find(cacheKey); cached != checkedPaths.end())
            {
                if (cached->second == CheckedPathStatus::symbolicLink)
                    return true;

                // The complete cache is revalidated before load/save publishes
                // its result. Until then these statuses are only traversal hints.
                if (cached->second == CheckedPathStatus::missing)
                    return false;
                continue;
            }
        }

        if (candidate.isSymbolicLink())
        {
            if (cacheEligible && checkedPaths.size() < kMaximumLinkCacheEntries)
                checkedPaths.emplace(cacheKey, CheckedPathStatus::symbolicLink);
            return true;
        }
        if (!candidate.exists())
        {
            if (cacheEligible && checkedPaths.size() < kMaximumLinkCacheEntries)
                checkedPaths.emplace(cacheKey, CheckedPathStatus::missing);
            return false;
        }

        if (cacheEligible && checkedPaths.size() < kMaximumLinkCacheEntries)
            checkedPaths.emplace(cacheKey, CheckedPathStatus::safeExisting);
    }

    return false;
}

bool linkTraversalCacheRemainsValid(const juce::File& rootFolder,
                                    const LinkTraversalCache& checkedPaths)
{
    if (rootFolder.isSymbolicLink())
        return false;

    for (const auto& [relativePrefix, status] : checkedPaths)
    {
        const auto candidate = rootFolder.getChildFile(juce::String::fromUTF8(
            relativePrefix.data(), static_cast<int>(relativePrefix.size())));
        if (candidate.isSymbolicLink())
            return false;

        const auto exists = candidate.exists();
        if ((status == CheckedPathStatus::safeExisting && !exists)
            || (status == CheckedPathStatus::missing && exists))
        {
            // Reject any changed prefix instead of trying to infer whether paths
            // skipped below a formerly-missing directory are still safe.
            return false;
        }
    }

    return true;
}

bool pathTraversesSymbolicLink(const juce::File& rootFolder, const juce::String& relativePath)
{
    LinkTraversalCache checkedPaths;
    return pathTraversesSymbolicLink(rootFolder, relativePath, checkedPaths);
}

bool appendUtf8(juce::MemoryOutputStream& output, const juce::String& value)
{
    const auto byteCount = static_cast<std::size_t>(value.getNumBytesAsUTF8());
    if (byteCount == 0 || byteCount > kMaximumPathBytes
        || byteCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return false;
    }

    appendU32(output, static_cast<std::uint32_t>(byteCount));
    return output.write(value.toRawUTF8(), byteCount);
}

bool pruneIndexPartitions(const juce::File& directory,
                          const juce::File& partitionToKeep)
{
    struct Partition
    {
        juce::File file;
        std::uint64_t bytes = 0;
        juce::int64 activityTimeMs = 0;
        bool isPartitionToKeep = false;
    };

    std::vector<Partition> partitions;
    std::uint64_t totalBytes = 0;
    if (directory.isDirectory())
    {
        for (const auto& item : juce::RangedDirectoryIterator(
                 directory, false, "root_*.acli", juce::File::findFiles))
        {
            const auto file = item.getFile();
            const auto signedSize = file.getSize();
            const auto size = signedSize > 0 ? static_cast<std::uint64_t>(signedSize) : 0u;
            if (totalBytes > std::numeric_limits<std::uint64_t>::max() - size)
                totalBytes = std::numeric_limits<std::uint64_t>::max();
            else
                totalBytes += size;

            partitions.push_back({
                file,
                size,
                juce::jmax(file.getLastModificationTime().toMilliseconds(),
                           file.getLastAccessTime().toMilliseconds()),
                file == partitionToKeep
            });
        }
    }

    std::sort(partitions.begin(), partitions.end(), [](const Partition& left, const Partition& right)
    {
        if (left.isPartitionToKeep != right.isPartitionToKeep)
            return left.isPartitionToKeep;
        if (left.activityTimeMs != right.activityTimeMs)
            return left.activityTimeMs > right.activityTimeMs;
        return left.file.getFullPathName() < right.file.getFullPathName();
    });

    auto retainedCount = partitions.size();
    for (auto index = partitions.size(); index > 0
         && (retainedCount > audiocity::plugin::LibraryFileIndexStore::maxRootPartitions
             || totalBytes > audiocity::plugin::LibraryFileIndexStore::maxTotalPartitionBytes);)
    {
        --index;
        auto& partition = partitions[index];
        if (partition.isPartitionToKeep)
            continue;

        if (!partition.file.deleteFile())
            continue;

        --retainedCount;
        totalBytes -= juce::jmin(totalBytes, partition.bytes);
        partition.bytes = 0;
    }

    return retainedCount <= audiocity::plugin::LibraryFileIndexStore::maxRootPartitions
        && totalBytes <= audiocity::plugin::LibraryFileIndexStore::maxTotalPartitionBytes;
}
}

namespace audiocity::plugin
{
LibraryFileIndexStore::LibraryFileIndexStore(juce::File cacheFile)
    : cacheFile_(std::move(cacheFile))
{
}

void LibraryFileIndexStore::setPathValidationObserverForTesting(
    PathValidationObserverForTesting observer)
{
    pathValidationObserverForTesting_ = std::move(observer);
}

juce::File LibraryFileIndexStore::getDefaultCacheDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Audiocity")
        .getChildFile("Cache")
        .getChildFile("library_indices_v1");
}

juce::File LibraryFileIndexStore::getCacheFileForRoot(const juce::File& rootFolder)
{
    const auto rootKey = pathKey(rootFolder.getFullPathName());
    return getDefaultCacheDirectory().getChildFile(
        "root_" + juce::String(rootKey.hashCode64()) + ".acli");
}

std::optional<LibraryFileIndexData> LibraryFileIndexStore::load() const
{
    LibraryFileIndexData data;
    if (!load(data))
        return std::nullopt;
    return data;
}

bool LibraryFileIndexStore::load(LibraryFileIndexData& destination) const
{
    if (!cacheFile_.existsAsFile())
        return false;

    const auto fileSize = cacheFile_.getSize();
    if (fileSize < static_cast<juce::int64>(kHeaderBytes)
        || fileSize > static_cast<juce::int64>(maxFileBytes))
    {
        return false;
    }

    juce::MemoryBlock bytes;
    if (!cacheFile_.loadFileAsData(bytes)
        || bytes.getSize() != static_cast<std::size_t>(fileSize)
        || bytes.getSize() < kHeaderBytes
        || bytes.getSize() > maxFileBytes)
    {
        return false;
    }

    BoundedReader input(bytes.getData(), bytes.getSize());
    const std::uint8_t* magic = nullptr;
    std::uint32_t version = 0;
    std::uint32_t headerBytes = 0;
    std::uint32_t entryCount = 0;
    std::uint32_t payloadBytes = 0;
    std::uint32_t storedCrc = 0;
    std::uint32_t rootPathBytes = 0;
    if (!input.readBytes(kIndexMagic.size(), magic)
        || std::memcmp(magic, kIndexMagic.data(), kIndexMagic.size()) != 0
        || !input.readU32(version)
        || !input.readU32(headerBytes)
        || !input.readU32(entryCount)
        || !input.readU32(payloadBytes)
        || !input.readU32(storedCrc)
        || !input.readU32(rootPathBytes)
        || version != static_cast<std::uint32_t>(formatVersion)
        || headerBytes != kHeaderBytes
        || entryCount > maxEntries
        || payloadBytes != input.remaining()
        || rootPathBytes == 0
        || rootPathBytes > kMaximumPathBytes)
    {
        return false;
    }

    const auto* payload = static_cast<const std::uint8_t*>(bytes.getData()) + kHeaderBytes;
    if (crc32(payload, payloadBytes) != storedCrc)
        return false;

    const std::uint8_t* rootBytes = nullptr;
    juce::String storedRoot;
    if (!input.readBytes(rootPathBytes, rootBytes)
        || !decodeUtf8(rootBytes, rootPathBytes, storedRoot)
        || !juce::File::isAbsolutePath(storedRoot))
    {
        return false;
    }

    const juce::File rootFolder(storedRoot);
    const auto canonicalRoot = rootFolder.getFullPathName();
    if (canonicalRoot.isEmpty() || rootFolder.isSymbolicLink())
        return false;

    LibraryFileIndexData restored;
    restored.libraryRootPath = canonicalRoot;
    restored.entries.reserve(entryCount);
    std::unordered_set<std::string> seenPaths;
    seenPaths.reserve(entryCount);
    LinkTraversalCache checkedPaths;
    checkedPaths.reserve(juce::jmin(
        static_cast<std::size_t>(entryCount), kMaximumLinkCacheEntries));

    for (std::uint32_t index = 0; index < entryCount; ++index)
    {
        std::uint32_t pathBytes = 0;
        const std::uint8_t* pathData = nullptr;
        std::uint64_t sizeBytes = 0;
        std::uint64_t modificationTimeMs = 0;
        std::uint8_t flags = 0;
        juce::String encodedPath;
        juce::String relativePath;
        if (!input.readU32(pathBytes)
            || pathBytes == 0
            || pathBytes > kMaximumPathBytes
            || !input.readBytes(pathBytes, pathData)
            || !decodeUtf8(pathData, pathBytes, encodedPath)
            || !normaliseSafeRelativePath(encodedPath, relativePath)
            || !input.readU64(sizeBytes)
            || !input.readU64(modificationTimeMs)
            || !input.readU8(flags)
            || sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<juce::int64>::max())
            || modificationTimeMs > static_cast<std::uint64_t>(std::numeric_limits<juce::int64>::max())
            || (flags & ~kInstrumentFlag) != 0)
        {
            return false;
        }

        const auto key = pathKey(relativePath).toStdString();
        if (!seenPaths.emplace(key).second)
            return false;

        LibraryFileIndexEntry entry;
        entry.relativePath = relativePath;
        entry.file = rootFolder.getChildFile(relativePath);
        if (!entry.file.isAChildOf(rootFolder)
            || pathTraversesSymbolicLink(rootFolder, relativePath, checkedPaths))
            return false;
        entry.fileName = entry.file.getFileName();
        entry.extensionLower = entry.file.getFileExtension().toLowerCase();
        entry.sizeBytes = static_cast<juce::int64>(sizeBytes);
        entry.modificationTimeMs = static_cast<juce::int64>(modificationTimeMs);
        entry.isInstrument = (flags & kInstrumentFlag) != 0;
        restored.entries.push_back(std::move(entry));
        if (pathValidationObserverForTesting_)
            pathValidationObserverForTesting_(restored.entries.size());
    }

    if (input.remaining() != 0
        || !linkTraversalCacheRemainsValid(rootFolder, checkedPaths))
        return false;

    destination = std::move(restored);
    cacheFile_.setLastAccessTime(juce::Time::getCurrentTime());
    return true;
}

bool LibraryFileIndexStore::save(const LibraryFileIndexData& data,
                                 CancellationCheck shouldCancel) const
{
    return saveWithExpectedRootIdentifier(data, std::move(shouldCancel), 0);
}

bool LibraryFileIndexStore::saveWithExpectedRootIdentifier(
    const LibraryFileIndexData& data,
    CancellationCheck shouldCancel,
    const std::uint64_t expectedRootIdentifier) const
{
    const auto cancellationRequested = [&shouldCancel]()
    {
        return shouldCancel && shouldCancel();
    };

    if (data.entries.size() > maxEntries || data.libraryRootPath.isEmpty()
        || !juce::File::isAbsolutePath(data.libraryRootPath)
        || cancellationRequested())
    {
        return false;
    }

    const juce::File rootFolder(data.libraryRootPath);
    const auto canonicalRoot = rootFolder.getFullPathName();
    const auto initialRootIdentifier = rootFolder.getFileIdentifier();
    const auto rootIdentityIsValid = [&rootFolder, initialRootIdentifier, expectedRootIdentifier]()
    {
        if (!rootFolder.isDirectory() || rootFolder.isSymbolicLink())
            return false;

        const auto currentIdentifier = rootFolder.getFileIdentifier();
        return (expectedRootIdentifier == 0 || currentIdentifier == expectedRootIdentifier)
            && (initialRootIdentifier == 0 || currentIdentifier == initialRootIdentifier);
    };
    if (canonicalRoot.isEmpty()
        || !rootIdentityIsValid()
        || static_cast<std::size_t>(canonicalRoot.getNumBytesAsUTF8()) > kMaximumPathBytes)
    {
        return false;
    }

    juce::MemoryOutputStream payload;
    const auto rootByteCount = static_cast<std::uint32_t>(canonicalRoot.getNumBytesAsUTF8());
    if (!payload.write(canonicalRoot.toRawUTF8(), rootByteCount))
        return false;

    std::unordered_set<std::string> seenPaths;
    seenPaths.reserve(data.entries.size());
    LinkTraversalCache checkedPaths;
    checkedPaths.reserve(juce::jmin(data.entries.size(), kMaximumLinkCacheEntries));
    for (const auto& entry : data.entries)
    {
        if (cancellationRequested())
            return false;

        juce::String relativePath;
        if (!normaliseSafeRelativePath(entry.relativePath, relativePath)
            || !rootFolder.getChildFile(relativePath).isAChildOf(rootFolder)
            || pathTraversesSymbolicLink(rootFolder, relativePath, checkedPaths)
            || entry.sizeBytes < 0
            || entry.modificationTimeMs < 0
            || !seenPaths.emplace(pathKey(relativePath).toStdString()).second
            || !appendUtf8(payload, relativePath))
        {
            return false;
        }

        appendU64(payload, static_cast<std::uint64_t>(entry.sizeBytes));
        appendU64(payload, static_cast<std::uint64_t>(entry.modificationTimeMs));
        payload.writeByte(static_cast<char>(entry.isInstrument ? kInstrumentFlag : 0u));
        if (payload.getDataSize() > maxFileBytes - kHeaderBytes)
            return false;
        if (pathValidationObserverForTesting_)
            pathValidationObserverForTesting_(seenPaths.size());
    }

    if (cancellationRequested()
        || !rootIdentityIsValid()
        || !linkTraversalCacheRemainsValid(rootFolder, checkedPaths)
        || payload.getDataSize() > maxFileBytes - kHeaderBytes
        || payload.getDataSize() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return false;
    }

    juce::MemoryOutputStream encoded;
    encoded.write(kIndexMagic.data(), kIndexMagic.size());
    appendU32(encoded, static_cast<std::uint32_t>(formatVersion));
    appendU32(encoded, static_cast<std::uint32_t>(kHeaderBytes));
    appendU32(encoded, static_cast<std::uint32_t>(data.entries.size()));
    appendU32(encoded, static_cast<std::uint32_t>(payload.getDataSize()));
    appendU32(encoded, crc32(payload.getData(), payload.getDataSize()));
    appendU32(encoded, rootByteCount);
    encoded.write(payload.getData(), payload.getDataSize());
    if (encoded.getDataSize() > maxFileBytes)
        return false;

    const auto parent = cacheFile_.getParentDirectory();
    if (!parent.exists() && !parent.createDirectory())
        return false;

    juce::TemporaryFile temporary(cacheFile_);
    if (cancellationRequested()
        || !rootIdentityIsValid()
        || !temporary.getFile().replaceWithData(encoded.getData(), encoded.getDataSize()))
        return false;
    if (cancellationRequested()
        || !rootIdentityIsValid()
        || !temporary.overwriteTargetFileWithTemporary())
        return false;

    return pruneIndexPartitions(parent, cacheFile_);
}

bool LibraryFileIndexStore::saveCompletedScan(const juce::File& rootFolder,
                                              LibraryFileIndexScanResult&& scan,
                                              CancellationCheck shouldCancel) const
{
    if (!scan.complete())
        return false;

    LibraryFileIndexData data;
    data.libraryRootPath = rootFolder.getFullPathName();
    data.entries = std::move(scan.entries);
    return saveWithExpectedRootIdentifier(
        data, std::move(shouldCancel), scan.rootIdentifier);
}

bool LibraryFileIndexStore::reset() const
{
    return !cacheFile_.exists() || cacheFile_.deleteFile();
}

bool LibraryFileIndex::isSupportedExtension(const juce::String& extensionLower, const bool includeRex)
{
    const auto extension = extensionLower.toLowerCase();
    for (const auto& descriptor : importFormatDescriptors())
    {
        if (!descriptor.requiredFileName.empty())
            continue;

        if (descriptor.availability == ImportFormatAvailability::rexRuntime && !includeRex)
            continue;

        for (std::size_t index = 0; index < descriptor.extensionCount; ++index)
        {
            if (extension.equalsIgnoreCase(descriptor.extensions[index].data()))
                return true;
        }
    }

    return false;
}

bool LibraryFileIndex::isSupportedFile(const juce::File& file, const bool includeRex)
{
    return file.existsAsFile() && isSupportedExtension(file.getFileExtension(), includeRex);
}

bool LibraryFileIndex::isSupportedFile(const juce::File& file,
                                       const ImportFormatCapabilities& capabilities,
                                       const bool includeUnavailable)
{
    if (!file.existsAsFile())
        return false;

    const auto* descriptor = findImportFormatDescriptorForPath(file.getFullPathName());
    return descriptor != nullptr
        && (includeUnavailable || isImportFormatAvailable(*descriptor, capabilities));
}

std::optional<LibraryFileIndexEntry> LibraryFileIndex::createEntryForFile(
    const juce::File& rootFolder,
    const juce::File& file,
    const bool includeRex)
{
    return createEntryForFile(
        rootFolder,
        file,
        ImportFormatCapabilities{ includeRex, true },
        false);
}

std::optional<LibraryFileIndexEntry> LibraryFileIndex::createEntryForFile(
    const juce::File& rootFolder,
    const juce::File& file,
    const ImportFormatCapabilities& capabilities,
    const bool includeUnavailable)
{
    if (!isSupportedFile(file, capabilities, includeUnavailable)
        || !file.isAChildOf(rootFolder)
        || pathTraversesSymbolicLink(rootFolder, file.getRelativePathFrom(rootFolder)))
        return std::nullopt;

    LibraryFileIndexEntry entry;
    entry.file = file;
    entry.relativePath = file.getRelativePathFrom(rootFolder).replaceCharacter('\\', '/');
    entry.fileName = file.getFileName();
    entry.extensionLower = file.getFileExtension().toLowerCase();
    entry.sizeBytes = file.getSize();
    entry.modificationTimeMs = file.getLastModificationTime().toMilliseconds();
    if (const auto* descriptor = findImportFormatDescriptorForPath(file.getFullPathName()))
        entry.isInstrument = descriptor->isInstrument;
    return entry;
}

std::vector<LibraryFileIndexEntry> LibraryFileIndex::scanRoot(const juce::File& rootFolder, const bool includeRex)
{
    auto scan = scanRootIncrementally(
        rootFolder,
        ImportFormatCapabilities{ includeRex, true },
        false);
    return scan.complete() ? std::move(scan.entries) : std::vector<LibraryFileIndexEntry>{};
}

LibraryFileIndexScanResult LibraryFileIndex::scanRootIncrementally(
    const juce::File& rootFolder,
    const ImportFormatCapabilities& capabilities,
    const bool includeUnavailable,
    CancellationCheck shouldCancel,
    EntryBatchCallback onBatch,
    LibraryFileIndexScanLimits limits)
{
    LibraryFileIndexScanResult result;
    if (!rootFolder.isDirectory() || rootFolder.isSymbolicLink())
    {
        result.incompleteReason = LibraryFileIndexScanResult::IncompleteReason::invalidRoot;
        return result;
    }

    limits.maximumEntries = juce::jmin(limits.maximumEntries, LibraryFileIndexStore::maxEntries);
    limits.maximumDirectories = juce::jmax(std::size_t{ 1 }, limits.maximumDirectories);
    limits.maximumDepth = juce::jlimit(0, kMaximumPathComponents, limits.maximumDepth);

    const auto initialRootIdentifier = rootFolder.getFileIdentifier();
    result.rootIdentifier = initialRootIdentifier;
    const auto rootIdentityIsValid = [&rootFolder, initialRootIdentifier]()
    {
        if (!rootFolder.isDirectory() || rootFolder.isSymbolicLink())
            return false;

        const auto currentIdentifier = rootFolder.getFileIdentifier();
        return initialRootIdentifier == 0 || currentIdentifier == initialRootIdentifier;
    };

    result.entries.reserve(4096);
    auto batchStart = std::size_t{ 0 };
    auto lastDelivery = std::chrono::steady_clock::now();

    const auto cancellationRequested = [&shouldCancel]()
    {
        return shouldCancel && shouldCancel();
    };

    const auto markCancelled = [&result]()
    {
        result.cancelled = true;
        result.incompleteReason = LibraryFileIndexScanResult::IncompleteReason::cancelled;
    };

    const auto deliverPendingBatch = [&]()
    {
        if (batchStart >= result.entries.size())
            return true;

        if (cancellationRequested())
        {
            markCancelled();
            return false;
        }
        if (!rootIdentityIsValid())
        {
            result.incompleteReason = LibraryFileIndexScanResult::IncompleteReason::rootChanged;
            return false;
        }

        if (onBatch)
        {
            onBatch(std::span<const LibraryFileIndexEntry>(
                result.entries.data() + batchStart,
                result.entries.size() - batchStart));
        }

        batchStart = result.entries.size();
        lastDelivery = std::chrono::steady_clock::now();
        return true;
    };

    struct PendingDirectory
    {
        juce::File directory;
        int depth = 0;
    };

    std::vector<PendingDirectory> pendingDirectories;
    pendingDirectories.reserve(juce::jmin(std::size_t{ 4096 }, limits.maximumDirectories));
    pendingDirectories.push_back({ rootFolder, 0 });
    auto discoveredDirectories = std::size_t{ 1 };

    while (!pendingDirectories.empty() && result.complete())
    {
        if (cancellationRequested())
        {
            markCancelled();
            break;
        }

        auto pending = std::move(pendingDirectories.back());
        pendingDirectories.pop_back();
        if (pending.depth > 0
            && (pending.directory.isSymbolicLink()
                || !pending.directory.isAChildOf(rootFolder)
                || pathTraversesSymbolicLink(
                    rootFolder, pending.directory.getRelativePathFrom(rootFolder))))
        {
            continue;
        }

        for (const auto& item : juce::RangedDirectoryIterator(
                 pending.directory,
                 false,
                 "*",
                 juce::File::findFilesAndDirectories,
                 juce::File::FollowSymlinks::no))
        {
            if (cancellationRequested())
            {
                markCancelled();
                break;
            }

            if (onBatch && batchStart < result.entries.size()
                && std::chrono::steady_clock::now() - lastDelivery >= kScanMaximumDeliveryDelay
                && !deliverPendingBatch())
            {
                break;
            }

            const auto child = item.getFile();
            if (item.isDirectory())
            {
                if (child.isSymbolicLink())
                    continue;

                const auto childDepth = pending.depth + 1;
                if (childDepth > limits.maximumDepth)
                {
                    result.incompleteReason = LibraryFileIndexScanResult::IncompleteReason::depthLimitReached;
                    break;
                }
                if (discoveredDirectories >= limits.maximumDirectories)
                {
                    result.incompleteReason = LibraryFileIndexScanResult::IncompleteReason::directoryLimitReached;
                    break;
                }

                pendingDirectories.push_back({ child, childDepth });
                ++discoveredDirectories;
                continue;
            }

            auto entry = createEntryForFile(
                rootFolder, child, capabilities, includeUnavailable);
            if (!entry.has_value())
                continue;

            if (result.entries.size() >= limits.maximumEntries)
            {
                result.entryLimitReached = true;
                result.incompleteReason = LibraryFileIndexScanResult::IncompleteReason::entryLimitReached;
                break;
            }

            result.entries.push_back(std::move(*entry));
            const auto now = std::chrono::steady_clock::now();
            if (result.entries.size() - batchStart >= kScanDeliveryBatchSize
                || now - lastDelivery >= kScanMaximumDeliveryDelay)
            {
                if (!deliverPendingBatch())
                    break;
                if (cancellationRequested())
                {
                    markCancelled();
                    break;
                }
            }
        }
    }

    if (result.complete())
    {
        if (cancellationRequested())
            markCancelled();
        else if (!deliverPendingBatch())
            return result;
        else if (cancellationRequested())
            markCancelled();
        else if (!rootIdentityIsValid())
            result.incompleteReason = LibraryFileIndexScanResult::IncompleteReason::rootChanged;
    }

    return result;
}

bool LibraryFileIndex::matchesSearch(const juce::String& fileName,
                                     const juce::String& relativePath,
                                     const juce::String& normalizedQuery)
{
    return normalizedQuery.isEmpty()
        || fileName.containsIgnoreCase(normalizedQuery)
        || relativePath.containsIgnoreCase(normalizedQuery);
}

std::vector<std::size_t> LibraryFileIndex::search(
    const std::vector<LibraryFileIndexEntry>& entries,
    const juce::String& query)
{
    const auto normalizedQuery = query.trim();
    std::vector<std::size_t> matches;
    matches.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        if (matchesSearch(entry.fileName, entry.relativePath, normalizedQuery))
            matches.push_back(index);
    }
    return matches;
}
} // namespace audiocity::plugin
