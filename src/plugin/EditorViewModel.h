#pragma once

#include "CommandStack.h"
#include "SelectionModel.h"

class EditorViewModel
{
public:
    EditorViewModel(SelectionModel& selectionModel, IZoneModel& zoneModel, CommandStack& commandStack) noexcept
        : selectionModel_(selectionModel), zoneModel_(zoneModel), commandStack_(commandStack)
    {
    }

    void setSelectedZoneIndex(const int zoneIndex)
    {
        if (zoneIndex < 0 || zoneIndex >= zoneModel_.getZoneCount())
        {
            selectionModel_.clear();
            return;
        }

        selectionModel_.setSelectedZoneIndex(zoneIndex);
    }

    [[nodiscard]] int getSelectedZoneIndex() const noexcept
    {
        return selectionModel_.getSelectedZoneIndex();
    }

    [[nodiscard]] std::optional<ZoneLoopState> getSelectedZoneLoopState() const
    {
        const auto selected = selectionModel_.getSelectedZoneIndex();
        if (selected < 0)
            return std::nullopt;

        return zoneModel_.getZoneLoopState(selected);
    }

    [[nodiscard]] bool applySelectedZoneLoopPoints(const int loopStart, const int loopEnd)
    {
        const auto selected = selectionModel_.getSelectedZoneIndex();
        if (selected < 0)
            return false;

        auto current = zoneModel_.getZoneLoopState(selected);
        if (!current.has_value())
            return false;

        auto applied = false;

        if (loopStart != current->loopStart)
        {
            applied = commandStack_.execute(std::make_unique<SetZoneLoopStartCommand>(zoneModel_, selected, loopStart)) || applied;
            current = zoneModel_.getZoneLoopState(selected);
            if (!current.has_value())
                return applied;
        }

        if (loopEnd != current->loopEnd)
            applied = commandStack_.execute(std::make_unique<SetZoneLoopEndCommand>(zoneModel_, selected, loopEnd)) || applied;

        return applied;
    }

    [[nodiscard]] bool undo()
    {
        return commandStack_.undo();
    }

    [[nodiscard]] bool redo()
    {
        return commandStack_.redo();
    }

    [[nodiscard]] bool canUndo() const noexcept { return commandStack_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return commandStack_.canRedo(); }

private:
    SelectionModel& selectionModel_;
    IZoneModel& zoneModel_;
    CommandStack& commandStack_;
};