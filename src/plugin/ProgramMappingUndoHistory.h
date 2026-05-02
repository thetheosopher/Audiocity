#pragma once

#include "../engine/SettingsUndoHistory.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace audiocity::plugin
{
struct ProgramMappingStateSnapshot
{
    juce::ValueTree mappingState;
    juce::String programPath;
    bool hasImportedProgram = false;

    [[nodiscard]] bool operator==(const ProgramMappingStateSnapshot& other) const noexcept
    {
        if (hasImportedProgram != other.hasImportedProgram)
            return false;

        if (!hasImportedProgram)
            return true;

        if (!programPath.equalsIgnoreCase(other.programPath))
            return false;

        if (!mappingState.isValid() || !other.mappingState.isValid())
            return !mappingState.isValid() && !other.mappingState.isValid();

        return mappingState.isEquivalentTo(other.mappingState);
    }

    [[nodiscard]] bool operator!=(const ProgramMappingStateSnapshot& other) const noexcept
    {
        return !(*this == other);
    }
};

class EditorUndoHistory
{
public:
    enum class EntryKind
    {
        settings,
        mapping
    };

    struct HistoryEntry
    {
        EntryKind kind = EntryKind::settings;
        audiocity::engine::SettingsSnapshot settingsSnapshot;
        ProgramMappingStateSnapshot mappingSnapshot;
        std::string label;
    };

    explicit EditorUndoHistory(std::size_t maxEntries = 256) noexcept
        : maxEntries_(maxEntries)
    {
    }

    void clear() noexcept
    {
        undoStack_.clear();
        redoStack_.clear();
        lastCoalesceKey_ = -1;
        lastRecordedAfterSettings_.reset();
    }

    void recordSettingsChange(const audiocity::engine::SettingsSnapshot& before,
                              const audiocity::engine::SettingsSnapshot& after,
                              const int coalesceKey = -1,
                              std::string label = {})
    {
        if (before == after)
            return;

        const auto canCoalesce = coalesceKey >= 0
            && redoStack_.empty()
            && lastCoalesceKey_ == coalesceKey
            && lastRecordedAfterSettings_.has_value()
            && *lastRecordedAfterSettings_ == before
            && !undoStack_.empty()
            && undoStack_.back().kind == EntryKind::settings;

        if (!canCoalesce)
        {
            undoStack_.push_back(makeSettingsEntry(before, std::move(label)));
            if (undoStack_.size() > maxEntries_)
                undoStack_.erase(undoStack_.begin());
        }
        else if (!label.empty())
        {
            undoStack_.back().label = std::move(label);
        }

        redoStack_.clear();
        lastCoalesceKey_ = coalesceKey;
        lastRecordedAfterSettings_ = after;
    }

    void recordMappingChange(const ProgramMappingStateSnapshot& before,
                             const ProgramMappingStateSnapshot& after,
                             std::string label = {})
    {
        if (before == after)
            return;

        undoStack_.push_back(makeMappingEntry(before, std::move(label)));
        if (undoStack_.size() > maxEntries_)
            undoStack_.erase(undoStack_.begin());

        redoStack_.clear();
        lastCoalesceKey_ = -1;
        lastRecordedAfterSettings_.reset();
    }

    [[nodiscard]] std::optional<HistoryEntry> undo(const audiocity::engine::SettingsSnapshot& currentSettings,
                                                   const ProgramMappingStateSnapshot& currentMapping)
    {
        if (undoStack_.empty())
            return std::nullopt;

        auto previous = copyEntry(undoStack_.back());
        undoStack_.pop_back();

        if (previous.kind == EntryKind::settings)
            redoStack_.push_back(makeSettingsEntry(currentSettings, std::string{}));
        else
            redoStack_.push_back(makeMappingEntry(currentMapping, std::string{}));

        lastCoalesceKey_ = -1;
        lastRecordedAfterSettings_.reset();
        return previous;
    }

    [[nodiscard]] std::optional<HistoryEntry> redo(const audiocity::engine::SettingsSnapshot& currentSettings,
                                                   const ProgramMappingStateSnapshot& currentMapping)
    {
        if (redoStack_.empty())
            return std::nullopt;

        auto next = copyEntry(redoStack_.back());
        redoStack_.pop_back();

        if (next.kind == EntryKind::settings)
            undoStack_.push_back(makeSettingsEntry(currentSettings, std::string{}));
        else
            undoStack_.push_back(makeMappingEntry(currentMapping, std::string{}));

        lastCoalesceKey_ = -1;
        lastRecordedAfterSettings_.reset();
        return next;
    }

    [[nodiscard]] bool canUndo() const noexcept { return !undoStack_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redoStack_.empty(); }
    [[nodiscard]] std::string undoLabel() const { return canUndo() ? undoStack_.back().label : std::string{}; }
    [[nodiscard]] std::string redoLabel() const { return canRedo() ? redoStack_.back().label : std::string{}; }

private:
    [[nodiscard]] static juce::ValueTree copyState(const juce::ValueTree& state)
    {
        return state.isValid() ? state.createCopy() : juce::ValueTree{};
    }

    [[nodiscard]] static ProgramMappingStateSnapshot copyMappingSnapshot(const ProgramMappingStateSnapshot& snapshot)
    {
        ProgramMappingStateSnapshot copy;
        copy.hasImportedProgram = snapshot.hasImportedProgram;
        copy.programPath = snapshot.programPath;
        copy.mappingState = copyState(snapshot.mappingState);
        return copy;
    }

    [[nodiscard]] static HistoryEntry makeSettingsEntry(const audiocity::engine::SettingsSnapshot& snapshot,
                                                        std::string label)
    {
        HistoryEntry entry;
        entry.kind = EntryKind::settings;
        entry.settingsSnapshot = snapshot;
        entry.label = std::move(label);
        return entry;
    }

    [[nodiscard]] static HistoryEntry makeMappingEntry(const ProgramMappingStateSnapshot& snapshot,
                                                       std::string label)
    {
        HistoryEntry entry;
        entry.kind = EntryKind::mapping;
        entry.mappingSnapshot = copyMappingSnapshot(snapshot);
        entry.label = std::move(label);
        return entry;
    }

    [[nodiscard]] static HistoryEntry copyEntry(const HistoryEntry& entry)
    {
        if (entry.kind == EntryKind::settings)
            return makeSettingsEntry(entry.settingsSnapshot, entry.label);

        return makeMappingEntry(entry.mappingSnapshot, entry.label);
    }

    std::size_t maxEntries_ = 256;
    std::vector<HistoryEntry> undoStack_;
    std::vector<HistoryEntry> redoStack_;
    int lastCoalesceKey_ = -1;
    std::optional<audiocity::engine::SettingsSnapshot> lastRecordedAfterSettings_;
};
} // namespace audiocity::plugin