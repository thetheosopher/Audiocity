#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace audiocity::plugin
{
struct PeakPreviewCacheEntry
{
    std::int64_t fileSizeBytes = 0;
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
    explicit PeakPreviewCacheStore(juce::File cacheFile);

    static juce::File getDefaultCacheFile();

    [[nodiscard]] PeakPreviewCacheData load() const;
    bool save(const PeakPreviewCacheData& data) const;
    bool reset() const;

private:
    juce::File cacheFile_;
};

[[nodiscard]] std::string makePeakPreviewCacheKey(const juce::File& file);
}
