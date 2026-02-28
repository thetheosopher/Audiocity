#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <map>
#include <cstdint>
#include <vector>

class BrowserIndex final : private juce::AsyncUpdater
{
public:
    struct EntrySnapshot
    {
        juce::String path;
        juce::String fileName;
        double durationSeconds = 0.0;
        std::int64_t sizeBytes = 0;
        bool favorite = false;
        std::uint64_t recentOrder = 0;
    };

    BrowserIndex();
    ~BrowserIndex() override;

    void setOnUpdated(std::function<void()> callback);

    void addWatchedFolder(const juce::File& folder);
    void setWatchedFolders(const juce::StringArray& folders);
    void rescan();
    [[nodiscard]] juce::StringArray getWatchedFolders() const;

    void setSearchText(const juce::String& text);
    [[nodiscard]] juce::String getSearchText() const;

    [[nodiscard]] std::vector<EntrySnapshot> getSearchResults() const;
    [[nodiscard]] std::vector<EntrySnapshot> getFavorites() const;
    [[nodiscard]] std::vector<EntrySnapshot> getRecent(int maxItems) const;

    bool toggleFavorite(const juce::String& path);
    void markRecent(const juce::String& path);
    void setFavoritePaths(const juce::StringArray& paths);
    void setRecentPaths(const juce::StringArray& mostRecentFirstPaths);
    [[nodiscard]] juce::StringArray getFavoritePaths() const;
    [[nodiscard]] juce::StringArray getRecentPaths(int maxItems) const;

    [[nodiscard]] std::vector<float> getPeaks(const juce::String& path) const;

private:
    struct Entry
    {
        juce::String path;
        juce::String fileName;
        std::int64_t sizeBytes = 0;
        juce::Time modified;
        double durationSeconds = 0.0;
        int channels = 0;
        double sampleRate = 0.0;

        bool favorite = false;
        std::uint64_t recentOrder = 0;

        std::vector<float> peaks;
        bool peaksReady = false;
        bool peakJobQueued = false;
    };

    class ScanJob;
    class PeakJob;

    void scheduleScan();
    void schedulePeakJobs(const std::vector<juce::String>& paths);

    void applyScanResults(std::vector<Entry> scannedEntries);
    void applyPeaks(const juce::String& path, std::vector<float> peaks);

    void handleAsyncUpdate() override;

    [[nodiscard]] std::vector<EntrySnapshot> makeSnapshots(const std::function<bool(const Entry&)>& filter) const;

    mutable juce::CriticalSection stateLock_;
    juce::ThreadPool workerPool_{ 2 };

    std::function<void()> onUpdated_;

    juce::StringArray watchedFolders_;
    juce::String searchText_;
    std::map<juce::String, Entry> entries_;
    std::uint64_t recentCounter_ = 0;
    std::set<juce::String> persistedFavorites_;
    std::map<juce::String, std::uint64_t> persistedRecentOrder_;
};
