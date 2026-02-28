#pragma once

#include "sfz/SfzModel.h"

#include <cstdint>
#include <vector>

namespace audiocity::engine
{
class ZoneSelector
{
public:
    enum class RoundRobinMode
    {
        ordered,
        random
    };

    void setZones(std::vector<sfz::Zone> zones);
    [[nodiscard]] const std::vector<sfz::Zone>& zones() const noexcept { return zones_; }

    void setRoundRobinMode(RoundRobinMode mode) noexcept { mode_ = mode; }
    [[nodiscard]] RoundRobinMode getRoundRobinMode() const noexcept { return mode_; }

    [[nodiscard]] int selectZoneIndex(int midiNote, int velocity) noexcept;
    void reset() noexcept;

private:
    [[nodiscard]] std::vector<int> findCandidateIndices(int midiNote, int velocity) const;

    std::vector<sfz::Zone> zones_;
    RoundRobinMode mode_ = RoundRobinMode::ordered;
    std::uint32_t orderedCounter_ = 0;
    std::uint32_t rngState_ = 0x12345678u;
};
}
