#include "PlayerPadState.h"

namespace audiocity::plugin
{
std::array<PlayerPadAssignment, kPlayerPadCount> defaultPlayerPadAssignments() noexcept
{
    std::array<PlayerPadAssignment, kPlayerPadCount> pads{};
    for (int i = 0; i < kPlayerPadCount; ++i)
        pads[static_cast<std::size_t>(i)] = sanitizePlayerPadAssignment({ 36 + i, 100 });

    return pads;
}

PlayerPadAssignment sanitizePlayerPadAssignment(const PlayerPadAssignment& assignment) noexcept
{
    return {
        juce::jlimit(0, 127, assignment.noteNumber),
        juce::jlimit(1, 127, assignment.velocity)
    };
}
}