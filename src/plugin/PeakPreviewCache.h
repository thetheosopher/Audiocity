#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace audiocity::plugin
{
struct PeakPreviewCacheEntry
{
    std::int64_t fileSizeBytes = 0;
    std::int64_t fileModificationTimeMs = 0;
    std::int64_t lastAccessTimeMs = 0;
    std::vector<float> peaks;
    juce::String metadataLine;
    juce::String loopFormatBadge;
    juce::String loopMetadataLine;
};

struct PeakPreviewCacheData
{
    juce::String libraryRootPath;
    std::unordered_map<std::string, PeakPreviewCacheEntry> entries;
};

class PeakPreviewCacheStore
{
public:
    static constexpr std::size_t maxEntries = 10000;
    static constexpr std::size_t maxPeaksPerEntry = 512;

    explicit PeakPreviewCacheStore(juce::File cacheFile);

    static juce::File getDefaultCacheFile();
    static juce::File getCacheFileForRoot(const juce::File& rootFolder);

    [[nodiscard]] PeakPreviewCacheData load() const;
    bool save(const PeakPreviewCacheData& data) const;
    bool reset() const;

private:
    juce::File cacheFile_;
};

[[nodiscard]] std::string makePeakPreviewCacheKey(const juce::File& file);
[[nodiscard]] bool isPeakPreviewCacheEntryCurrent(const PeakPreviewCacheEntry& entry,
                                                  const juce::File& file) noexcept;
}
