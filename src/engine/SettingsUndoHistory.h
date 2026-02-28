#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace audiocity::engine
{
struct SettingsSnapshot
{
    struct LoopPointSnapshot
    {
        int loopStart = 0;
        int loopEnd = 0;

        [[nodiscard]] bool operator==(const LoopPointSnapshot& other) const noexcept
        {
            return loopStart == other.loopStart && loopEnd == other.loopEnd;
        }
    };

    int preloadSamples = 32768;
    int qualityTierIndex = 1;
    int rrModeIndex = 0;
    int playbackModeIndex = 0;
    bool monoEnabled = false;
    bool legatoEnabled = false;
    float glideSeconds = 0.0f;
    int chokeGroup = 0;
    std::vector<LoopPointSnapshot> importedZoneLoopPoints;

    [[nodiscard]] bool operator==(const SettingsSnapshot& other) const noexcept
    {
        return preloadSamples == other.preloadSamples
            && qualityTierIndex == other.qualityTierIndex
            && rrModeIndex == other.rrModeIndex
            && playbackModeIndex == other.playbackModeIndex
            && monoEnabled == other.monoEnabled
            && legatoEnabled == other.legatoEnabled
            && glideSeconds == other.glideSeconds
                && chokeGroup == other.chokeGroup
                && importedZoneLoopPoints == other.importedZoneLoopPoints;
    }

    [[nodiscard]] bool operator!=(const SettingsSnapshot& other) const noexcept
    {
        return !(*this == other);
    }
};

class SettingsUndoHistory
{
public:
    struct HistoryEntry
    {
        SettingsSnapshot snapshot;
        std::string label;
    };

    explicit SettingsUndoHistory(std::size_t maxEntries = 256) noexcept
        : maxEntries_(maxEntries)
    {
    }

    void clear() noexcept
    {
        undoStack_.clear();
        redoStack_.clear();
        lastCoalesceKey_ = -1;
        lastRecordedAfter_.reset();
    }

    void recordChange(
        const SettingsSnapshot& before,
        const SettingsSnapshot& after,
        const int coalesceKey = -1,
        std::string label = {})
    {
        if (before == after)
            return;

        const auto canCoalesce = coalesceKey >= 0
            && redoStack_.empty()
            && lastCoalesceKey_ == coalesceKey
            && lastRecordedAfter_.has_value()
            && *lastRecordedAfter_ == before;

        if (!canCoalesce)
        {
            undoStack_.push_back({ before, std::move(label) });

            if (undoStack_.size() > maxEntries_)
                undoStack_.erase(undoStack_.begin());
        }
        else if (!label.empty())
        {
            undoStack_.back().label = std::move(label);
        }

        redoStack_.clear();
        lastCoalesceKey_ = coalesceKey;
        lastRecordedAfter_ = after;
    }

    [[nodiscard]] std::optional<SettingsSnapshot> undo(const SettingsSnapshot& current)
    {
        if (undoStack_.empty())
            return std::nullopt;

        const auto previous = undoStack_.back().snapshot;
        undoStack_.pop_back();
        redoStack_.push_back({ current, std::string{} });
        lastCoalesceKey_ = -1;
        lastRecordedAfter_.reset();
        return previous;
    }

    [[nodiscard]] std::optional<SettingsSnapshot> redo(const SettingsSnapshot& current)
    {
        if (redoStack_.empty())
            return std::nullopt;

        const auto next = redoStack_.back().snapshot;
        redoStack_.pop_back();
        undoStack_.push_back({ current, std::string{} });
        lastCoalesceKey_ = -1;
        lastRecordedAfter_.reset();
        return next;
    }

    [[nodiscard]] bool canUndo() const noexcept { return !undoStack_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redoStack_.empty(); }
    [[nodiscard]] std::string undoLabel() const { return canUndo() ? undoStack_.back().label : std::string{}; }
    [[nodiscard]] std::string redoLabel() const { return canRedo() ? redoStack_.back().label : std::string{}; }

private:
    std::size_t maxEntries_ = 256;
    std::vector<HistoryEntry> undoStack_;
    std::vector<HistoryEntry> redoStack_;
    int lastCoalesceKey_ = -1;
    std::optional<SettingsSnapshot> lastRecordedAfter_;
};
}
