#pragma once

#include <array>
#include <cstdint>

namespace audiocity::engine
{
struct Zone
{
    std::uint8_t lowKey = 0;
    std::uint8_t highKey = 127;
    std::uint8_t lowVelocity = 0;
    std::uint8_t highVelocity = 127;
};

class ZoneMap
{
public:
    static constexpr int maxZones = 128;

    void clear() noexcept;
    bool addZone(const Zone& zone) noexcept;
    [[nodiscard]] int size() const noexcept;

private:
    std::array<Zone, maxZones> zones_{};
    int zoneCount_ = 0;
};
}
