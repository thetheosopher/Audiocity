#pragma once

#include "ZoneModel.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

class ICommand
{
public:
    virtual ~ICommand() = default;
    [[nodiscard]] virtual bool apply() = 0;
    [[nodiscard]] virtual bool undo() = 0;
    [[nodiscard]] virtual std::string label() const = 0;
};

class CommandStack
{
public:
    explicit CommandStack(const std::size_t maxEntries = 256)
        : maxEntries_(maxEntries)
    {
    }

    [[nodiscard]] bool execute(std::unique_ptr<ICommand> command)
    {
        if (command == nullptr)
            return false;

        if (!command->apply())
            return false;

        undoStack_.push_back(std::move(command));
        if (undoStack_.size() > maxEntries_)
            undoStack_.erase(undoStack_.begin());

        redoStack_.clear();
        return true;
    }

    [[nodiscard]] bool undo()
    {
        if (undoStack_.empty())
            return false;

        auto command = std::move(undoStack_.back());
        undoStack_.pop_back();

        if (!command->undo())
            return false;

        redoStack_.push_back(std::move(command));
        return true;
    }

    [[nodiscard]] bool redo()
    {
        if (redoStack_.empty())
            return false;

        auto command = std::move(redoStack_.back());
        redoStack_.pop_back();

        if (!command->apply())
            return false;

        undoStack_.push_back(std::move(command));
        return true;
    }

    [[nodiscard]] bool canUndo() const noexcept { return !undoStack_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redoStack_.empty(); }
    [[nodiscard]] std::string undoLabel() const { return canUndo() ? undoStack_.back()->label() : std::string{}; }
    [[nodiscard]] std::string redoLabel() const { return canRedo() ? redoStack_.back()->label() : std::string{}; }

private:
    std::size_t maxEntries_ = 256;
    std::vector<std::unique_ptr<ICommand>> undoStack_;
    std::vector<std::unique_ptr<ICommand>> redoStack_;
};

class SetZoneLoopStartCommand final : public ICommand
{
public:
    SetZoneLoopStartCommand(IZoneModel& zoneModel, const int zoneIndex, const int targetLoopStart) noexcept
        : zoneModel_(zoneModel), zoneIndex_(zoneIndex), targetLoopStart_(targetLoopStart)
    {
    }

    [[nodiscard]] bool apply() override
    {
        const auto current = zoneModel_.getZoneLoopState(zoneIndex_);
        if (!current.has_value())
            return false;

        if (!previousStateCaptured_)
        {
            previousState_ = *current;
            previousStateCaptured_ = true;
        }

        appliedState_.loopStart = std::max(0, targetLoopStart_);
        appliedState_.loopEnd = std::max(current->loopEnd, appliedState_.loopStart + 1);
        return zoneModel_.setZoneLoopState(zoneIndex_, appliedState_.loopStart, appliedState_.loopEnd);
    }

    [[nodiscard]] bool undo() override
    {
        if (!previousStateCaptured_)
            return false;

        return zoneModel_.setZoneLoopState(zoneIndex_, previousState_.loopStart, previousState_.loopEnd);
    }

    [[nodiscard]] std::string label() const override { return "Set Zone Loop Start"; }

private:
    IZoneModel& zoneModel_;
    int zoneIndex_ = -1;
    int targetLoopStart_ = 0;
    bool previousStateCaptured_ = false;
    ZoneLoopState previousState_{};
    ZoneLoopState appliedState_{};
};

class SetZoneLoopEndCommand final : public ICommand
{
public:
    SetZoneLoopEndCommand(IZoneModel& zoneModel, const int zoneIndex, const int targetLoopEnd) noexcept
        : zoneModel_(zoneModel), zoneIndex_(zoneIndex), targetLoopEnd_(targetLoopEnd)
    {
    }

    [[nodiscard]] bool apply() override
    {
        const auto current = zoneModel_.getZoneLoopState(zoneIndex_);
        if (!current.has_value())
            return false;

        if (!previousStateCaptured_)
        {
            previousState_ = *current;
            previousStateCaptured_ = true;
        }

        appliedState_.loopStart = current->loopStart;
        appliedState_.loopEnd = std::max(current->loopStart + 1, targetLoopEnd_);
        return zoneModel_.setZoneLoopState(zoneIndex_, appliedState_.loopStart, appliedState_.loopEnd);
    }

    [[nodiscard]] bool undo() override
    {
        if (!previousStateCaptured_)
            return false;

        return zoneModel_.setZoneLoopState(zoneIndex_, previousState_.loopStart, previousState_.loopEnd);
    }

    [[nodiscard]] std::string label() const override { return "Set Zone Loop End"; }

private:
    IZoneModel& zoneModel_;
    int zoneIndex_ = -1;
    int targetLoopEnd_ = 0;
    bool previousStateCaptured_ = false;
    ZoneLoopState previousState_{};
    ZoneLoopState appliedState_{};
};