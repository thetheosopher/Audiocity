#include "BrowserIndex.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kWaveformPeakCount = 256;

bool hasSupportedAudioExtension(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac";
}

std::vector<float> generatePeaks(const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};

    std::vector<float> peaks(static_cast<std::size_t>(kWaveformPeakCount), 0.0f);

    const auto length = static_cast<std::int64_t>(reader->lengthInSamples);
    const auto samplesPerBin = juce::jmax<std::int64_t>(1, length / kWaveformPeakCount);

    juce::AudioBuffer<float> tempBuffer(static_cast<int>(reader->numChannels), static_cast<int>(samplesPerBin));

    for (int bin = 0; bin < kWaveformPeakCount; ++bin)
    {
        const auto start = static_cast<std::int64_t>(bin) * samplesPerBin;
        if (start >= length)
            break;

        const auto count = static_cast<int>(juce::jmin(samplesPerBin, length - start));
        if (!reader->read(&tempBuffer, 0, count, start, true, true))
            continue;

        float maxAbs = 0.0f;
        for (int channel = 0; channel < tempBuffer.getNumChannels(); ++channel)
        {
            const auto* data = tempBuffer.getReadPointer(channel);
            for (int i = 0; i < count; ++i)
                maxAbs = juce::jmax(maxAbs, std::abs(data[i]));
        }

        peaks[static_cast<std::size_t>(bin)] = juce::jlimit(0.0f, 1.0f, maxAbs);
    }

    return peaks;
}
}

class BrowserIndex::ScanJob final : public juce::ThreadPoolJob
{
public:
    ScanJob(BrowserIndex& owner, juce::StringArray watchedFolders)
        : juce::ThreadPoolJob("BrowserScanJob"), owner_(owner), watchedFolders_(std::move(watchedFolders))
    {
    }

    JobStatus runJob() override
    {
        std::vector<Entry> scannedEntries;

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        for (const auto& folderPath : watchedFolders_)
        {
            juce::File folder(folderPath);
            if (!folder.isDirectory())
                continue;

            for (const auto& fileInfo : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles))
            {
                if (shouldExit())
                    return jobHasFinished;

                const auto file = fileInfo.getFile();
                if (!hasSupportedAudioExtension(file))
                    continue;

                Entry entry;
                entry.path = file.getFullPathName();
                entry.fileName = file.getFileName();
                entry.sizeBytes = file.getSize();
                entry.modified = file.getLastModificationTime();

                std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
                if (reader != nullptr)
                {
                    entry.sampleRate = reader->sampleRate;
                    entry.channels = static_cast<int>(reader->numChannels);
                    entry.durationSeconds = reader->sampleRate > 0.0
                        ? static_cast<double>(reader->lengthInSamples) / reader->sampleRate
                        : 0.0;
                }

                scannedEntries.push_back(std::move(entry));
            }
        }

        owner_.applyScanResults(std::move(scannedEntries));
        return jobHasFinished;
    }

private:
    BrowserIndex& owner_;
    juce::StringArray watchedFolders_;
};

class BrowserIndex::PeakJob final : public juce::ThreadPoolJob
{
public:
    PeakJob(BrowserIndex& owner, juce::String path)
        : juce::ThreadPoolJob("BrowserPeakJob"), owner_(owner), path_(std::move(path))
    {
    }

    JobStatus runJob() override
    {
        if (shouldExit())
            return jobHasFinished;

        const auto peaks = generatePeaks(juce::File(path_));
        owner_.applyPeaks(path_, peaks);
        return jobHasFinished;
    }

private:
    BrowserIndex& owner_;
    juce::String path_;
};

BrowserIndex::BrowserIndex() = default;

BrowserIndex::~BrowserIndex()
{
    workerPool_.removeAllJobs(true, 1000);
}

void BrowserIndex::setOnUpdated(std::function<void()> callback)
{
    const juce::ScopedLock lock(stateLock_);
    onUpdated_ = std::move(callback);
}

void BrowserIndex::addWatchedFolder(const juce::File& folder)
{
    if (!folder.isDirectory())
        return;

    {
        const juce::ScopedLock lock(stateLock_);
        watchedFolders_.addIfNotAlreadyThere(folder.getFullPathName());
    }

    scheduleScan();
}

void BrowserIndex::setWatchedFolders(const juce::StringArray& folders)
{
    {
        const juce::ScopedLock lock(stateLock_);
        watchedFolders_.clear();

        for (const auto& path : folders)
        {
            const juce::File folder(path);
            if (folder.isDirectory())
                watchedFolders_.addIfNotAlreadyThere(folder.getFullPathName());
        }
    }

    scheduleScan();
}

void BrowserIndex::rescan()
{
    scheduleScan();
}

juce::StringArray BrowserIndex::getWatchedFolders() const
{
    const juce::ScopedLock lock(stateLock_);
    return watchedFolders_;
}

void BrowserIndex::setSearchText(const juce::String& text)
{
    {
        const juce::ScopedLock lock(stateLock_);
        searchText_ = text.trim();
    }

    triggerAsyncUpdate();
}

juce::String BrowserIndex::getSearchText() const
{
    const juce::ScopedLock lock(stateLock_);
    return searchText_;
}

std::vector<BrowserIndex::EntrySnapshot> BrowserIndex::getSearchResults() const
{
    juce::String needle;
    {
        const juce::ScopedLock lock(stateLock_);
        needle = searchText_.toLowerCase();
    }

    auto results = makeSnapshots([&](const Entry& entry)
    {
        return needle.isEmpty() || entry.fileName.toLowerCase().contains(needle) || entry.path.toLowerCase().contains(needle);
    });

    std::sort(results.begin(), results.end(), [](const EntrySnapshot& a, const EntrySnapshot& b)
    {
        return a.fileName.compareNatural(b.fileName) < 0;
    });

    return results;
}

std::vector<BrowserIndex::EntrySnapshot> BrowserIndex::getFavorites() const
{
    auto favorites = makeSnapshots([](const Entry& entry) { return entry.favorite; });
    std::sort(favorites.begin(), favorites.end(), [](const EntrySnapshot& a, const EntrySnapshot& b)
    {
        return a.fileName.compareNatural(b.fileName) < 0;
    });
    return favorites;
}

std::vector<BrowserIndex::EntrySnapshot> BrowserIndex::getRecent(const int maxItems) const
{
    auto recent = makeSnapshots([](const Entry& entry) { return entry.recentOrder > 0; });

    std::sort(recent.begin(), recent.end(), [](const EntrySnapshot& a, const EntrySnapshot& b)
    {
        return a.recentOrder > b.recentOrder;
    });

    if (maxItems > 0 && static_cast<int>(recent.size()) > maxItems)
        recent.resize(static_cast<std::size_t>(maxItems));

    return recent;
}

bool BrowserIndex::toggleFavorite(const juce::String& path)
{
    bool newValue = false;

    {
        const juce::ScopedLock lock(stateLock_);
        const auto it = entries_.find(path);
        if (it == entries_.end())
            return false;

        it->second.favorite = !it->second.favorite;
        newValue = it->second.favorite;

        if (newValue)
            persistedFavorites_.insert(path);
        else
            persistedFavorites_.erase(path);
    }

    triggerAsyncUpdate();
    return newValue;
}

void BrowserIndex::markRecent(const juce::String& path)
{
    {
        const juce::ScopedLock lock(stateLock_);
        const auto it = entries_.find(path);
        if (it == entries_.end())
            return;

        it->second.recentOrder = ++recentCounter_;
        persistedRecentOrder_[path] = it->second.recentOrder;
    }

    triggerAsyncUpdate();
}

void BrowserIndex::setFavoritePaths(const juce::StringArray& paths)
{
    {
        const juce::ScopedLock lock(stateLock_);
        persistedFavorites_.clear();

        for (const auto& path : paths)
            persistedFavorites_.insert(path);

        for (auto& [_, entry] : entries_)
            entry.favorite = persistedFavorites_.count(entry.path) > 0;
    }

    triggerAsyncUpdate();
}

void BrowserIndex::setRecentPaths(const juce::StringArray& mostRecentFirstPaths)
{
    {
        const juce::ScopedLock lock(stateLock_);

        persistedRecentOrder_.clear();
        recentCounter_ = static_cast<std::uint64_t>(mostRecentFirstPaths.size());

        for (int i = 0; i < mostRecentFirstPaths.size(); ++i)
        {
            const auto order = recentCounter_ - static_cast<std::uint64_t>(i);
            persistedRecentOrder_[mostRecentFirstPaths[i]] = order;
        }

        for (auto& [_, entry] : entries_)
        {
            const auto it = persistedRecentOrder_.find(entry.path);
            entry.recentOrder = it == persistedRecentOrder_.end() ? 0 : it->second;
        }
    }

    triggerAsyncUpdate();
}

juce::StringArray BrowserIndex::getFavoritePaths() const
{
    juce::StringArray out;
    const juce::ScopedLock lock(stateLock_);

    for (const auto& path : persistedFavorites_)
        out.add(path);

    return out;
}

juce::StringArray BrowserIndex::getRecentPaths(const int maxItems) const
{
    juce::StringArray out;

    std::vector<std::pair<juce::String, std::uint64_t>> sorted;
    {
        const juce::ScopedLock lock(stateLock_);
        sorted.reserve(persistedRecentOrder_.size());

        for (const auto& [path, order] : persistedRecentOrder_)
            sorted.push_back({ path, order });
    }

    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right)
    {
        return left.second > right.second;
    });

    for (int i = 0; i < static_cast<int>(sorted.size()); ++i)
    {
        if (maxItems > 0 && i >= maxItems)
            break;

        out.add(sorted[static_cast<std::size_t>(i)].first);
    }

    return out;
}

std::vector<float> BrowserIndex::getPeaks(const juce::String& path) const
{
    const juce::ScopedLock lock(stateLock_);

    const auto it = entries_.find(path);
    if (it == entries_.end())
        return {};

    return it->second.peaks;
}

void BrowserIndex::scheduleScan()
{
    juce::StringArray watched;
    {
        const juce::ScopedLock lock(stateLock_);
        watched = watchedFolders_;
    }

    workerPool_.addJob(new ScanJob(*this, watched), true);
}

void BrowserIndex::schedulePeakJobs(const std::vector<juce::String>& paths)
{
    for (const auto& path : paths)
        workerPool_.addJob(new PeakJob(*this, path), true);
}

void BrowserIndex::applyScanResults(std::vector<Entry> scannedEntries)
{
    std::vector<juce::String> peakPaths;

    {
        const juce::ScopedLock lock(stateLock_);

        std::map<juce::String, Entry> previous = std::move(entries_);
        entries_.clear();

        for (auto& scanned : scannedEntries)
        {
            const auto previousIt = previous.find(scanned.path);
            if (previousIt != previous.end())
            {
                scanned.favorite = previousIt->second.favorite;
                scanned.recentOrder = previousIt->second.recentOrder;
                scanned.peaks = previousIt->second.peaks;
                scanned.peaksReady = previousIt->second.peaksReady;
            }

            if (persistedFavorites_.count(scanned.path) > 0)
                scanned.favorite = true;

            const auto recentIt = persistedRecentOrder_.find(scanned.path);
            if (recentIt != persistedRecentOrder_.end())
                scanned.recentOrder = recentIt->second;

            if (!scanned.peaksReady)
                peakPaths.push_back(scanned.path);

            entries_[scanned.path] = std::move(scanned);
        }
    }

    schedulePeakJobs(peakPaths);
    triggerAsyncUpdate();
}

void BrowserIndex::applyPeaks(const juce::String& path, std::vector<float> peaks)
{
    {
        const juce::ScopedLock lock(stateLock_);
        const auto it = entries_.find(path);
        if (it == entries_.end())
            return;

        it->second.peaks = std::move(peaks);
        it->second.peaksReady = !it->second.peaks.empty();
    }

    triggerAsyncUpdate();
}

void BrowserIndex::handleAsyncUpdate()
{
    std::function<void()> callback;

    {
        const juce::ScopedLock lock(stateLock_);
        callback = onUpdated_;
    }

    if (callback)
        callback();
}

std::vector<BrowserIndex::EntrySnapshot> BrowserIndex::makeSnapshots(const std::function<bool(const Entry&)>& filter) const
{
    std::vector<EntrySnapshot> out;
    const juce::ScopedLock lock(stateLock_);

    for (const auto& [_, entry] : entries_)
    {
        if (!filter(entry))
            continue;

        EntrySnapshot snapshot;
        snapshot.path = entry.path;
        snapshot.fileName = entry.fileName;
        snapshot.durationSeconds = entry.durationSeconds;
        snapshot.sizeBytes = entry.sizeBytes;
        snapshot.favorite = entry.favorite;
        snapshot.recentOrder = entry.recentOrder;

        out.push_back(std::move(snapshot));
    }

    return out;
}
