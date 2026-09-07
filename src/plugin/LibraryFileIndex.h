#pragma once

#include "ImportFormatRegistry.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace audiocity::plugin
{
struct LibraryFileIndexEntry
{
    juce::File file;
    juce::String relativePath;
    juce::String fileName;
    juce::String extensionLower;
    juce::int64 sizeBytes = 0;
    juce::int64 modificationTimeMs = 0;
    bool isInstrument = false;
};

/** A cheap, per-root library index snapshot.

    Only relative paths and filesystem metadata are persisted. The derived File,
    filename, and extension fields are reconstructed against libraryRootPath when
    a snapshot is loaded.
*/
struct LibraryFileIndexData
{
    juce::String libraryRootPath;
    std::vector<LibraryFileIndexEntry> entries;
};

struct LibraryFileIndexScanResult
{
    enum class IncompleteReason
    {
        none,
        cancelled,
        invalidRoot,
        rootChanged,
        entryLimitReached,
        directoryLimitReached,
        depthLimitReached
    };

    std::vector<LibraryFileIndexEntry> entries;
    bool cancelled = false;
    bool entryLimitReached = false;
    std::uint64_t rootIdentifier = 0;
    IncompleteReason incompleteReason = IncompleteReason::none;

    [[nodiscard]] bool complete() const noexcept
    {
        return incompleteReason == IncompleteReason::none;
    }
};

struct LibraryFileIndexScanLimits
{
    std::size_t maximumEntries = 100000;
    std::size_t maximumDirectories = 65536;
    int maximumDepth = 256;
};

/** Versioned, bounded persistent storage for a LibraryFileIndexData snapshot.

    Loads are all-or-nothing: a missing, truncated, corrupt, oversized, or unsafe
    cache leaves the caller's destination untouched. Saves reject snapshots that
    exceed the configured limits and replace the target atomically.
*/
class LibraryFileIndexStore final
{
public:
    using PathValidationObserverForTesting = std::function<void(std::size_t)>;
    using CancellationCheck = std::function<bool()>;

    static constexpr std::size_t maxEntries = 100000;
    static constexpr std::size_t maxFileBytes = 32 * 1024 * 1024;
    static constexpr std::size_t maxRootPartitions = 32;
    static constexpr std::size_t maxTotalPartitionBytes = 128 * 1024 * 1024;
    static constexpr int formatVersion = 1;

    explicit LibraryFileIndexStore(juce::File cacheFile);

    [[nodiscard]] static juce::File getDefaultCacheDirectory();
    [[nodiscard]] static juce::File getCacheFileForRoot(const juce::File& rootFolder);

    [[nodiscard]] std::optional<LibraryFileIndexData> load() const;
    [[nodiscard]] bool load(LibraryFileIndexData& destination) const;
    bool save(const LibraryFileIndexData& data, CancellationCheck shouldCancel = {}) const;
    bool saveCompletedScan(const juce::File& rootFolder,
                           LibraryFileIndexScanResult&& scan,
                           CancellationCheck shouldCancel = {}) const;
    bool reset() const;

    void setPathValidationObserverForTesting(PathValidationObserverForTesting observer);

private:
    bool saveWithExpectedRootIdentifier(const LibraryFileIndexData& data,
                                        CancellationCheck shouldCancel,
                                        std::uint64_t expectedRootIdentifier) const;

    juce::File cacheFile_;
    PathValidationObserverForTesting pathValidationObserverForTesting_;
};

class LibraryFileIndex
{
public:
    using CancellationCheck = std::function<bool()>;
    using EntryBatchCallback = std::function<void(std::span<const LibraryFileIndexEntry>)>;

    [[nodiscard]] static bool isSupportedExtension(const juce::String& extensionLower, bool includeRex);
    [[nodiscard]] static bool isSupportedFile(const juce::File& file, bool includeRex);
    [[nodiscard]] static bool isSupportedFile(const juce::File& file,
                                              const ImportFormatCapabilities& capabilities,
                                              bool includeUnavailable);
    [[nodiscard]] static std::optional<LibraryFileIndexEntry> createEntryForFile(
        const juce::File& rootFolder,
        const juce::File& file,
        bool includeRex);
    [[nodiscard]] static std::optional<LibraryFileIndexEntry> createEntryForFile(
        const juce::File& rootFolder,
        const juce::File& file,
        const ImportFormatCapabilities& capabilities,
        bool includeUnavailable);

    /** Scans a real library tree and publishes bounded batches as they become available.

        The callback span is valid only for the duration of the callback. Cancellation is
        checked between directory entries and after every delivery. Partial entries are
        returned only to let callers discard or diagnose cancelled work; they must not be
        persisted as a complete index.
    */
    [[nodiscard]] static LibraryFileIndexScanResult scanRootIncrementally(
        const juce::File& rootFolder,
        const ImportFormatCapabilities& capabilities,
        bool includeUnavailable,
        CancellationCheck shouldCancel = {},
        EntryBatchCallback onBatch = {},
        LibraryFileIndexScanLimits limits = {});

    [[nodiscard]] static std::vector<LibraryFileIndexEntry> scanRoot(const juce::File& rootFolder, bool includeRex);

    /** Matches a caller-normalized (trimmed) query against the searchable fields. */
    [[nodiscard]] static bool matchesSearch(const juce::String& fileName,
                                            const juce::String& relativePath,
                                            const juce::String& normalizedQuery);
    [[nodiscard]] static std::vector<std::size_t> search(
        const std::vector<LibraryFileIndexEntry>& entries,
        const juce::String& query);
};
} // namespace audiocity::plugin
