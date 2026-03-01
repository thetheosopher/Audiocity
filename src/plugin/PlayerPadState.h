#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace audiocity::plugin
{
struct PlayerPadAssignment
{
    int noteNumber = 36;
    int velocity = 100;
};

constexpr int kPlayerPadCount = 8;
constexpr auto kPlayerPads = "playerPads";
constexpr auto kPlayerPad = "playerPad";
constexpr auto kPlayerPadIndex = "index";
constexpr auto kPlayerPadNote = "note";
constexpr auto kPlayerPadVelocity = "velocity";

[[nodiscard]] std::array<PlayerPadAssignment, kPlayerPadCount> defaultPlayerPadAssignments() noexcept;
[[nodiscard]] PlayerPadAssignment sanitizePlayerPadAssignment(const PlayerPadAssignment& assignment) noexcept;
}