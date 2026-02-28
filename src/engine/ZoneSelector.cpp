#include "ZoneSelector.h"

#include <algorithm>

namespace audiocity::engine
{
void ZoneSelector::setZones(std::vector<sfz::Zone> zones)
{
    zones_ = std::move(zones);
    reset();
}

int ZoneSelector::selectZoneIndex(const int midiNote, const int velocity) noexcept
{
    const auto candidates = findCandidateIndices(midiNote, velocity);
    if (candidates.empty())
        return -1;

    if (mode_ == RoundRobinMode::ordered)
    {
        const auto index = orderedCounter_ % static_cast<std::uint32_t>(candidates.size());
        ++orderedCounter_;
        return candidates[static_cast<std::size_t>(index)];
    }

    rngState_ = rngState_ * 1664525u + 1013904223u;
    const auto randomIndex = rngState_ % static_cast<std::uint32_t>(candidates.size());
    return candidates[static_cast<std::size_t>(randomIndex)];
}

void ZoneSelector::reset() noexcept
{
    orderedCounter_ = 0;
    rngState_ = 0x12345678u;
}

std::vector<int> ZoneSelector::findCandidateIndices(const int midiNote, const int velocity) const
{
    std::vector<int> indices;

    for (int i = 0; i < static_cast<int>(zones_.size()); ++i)
    {
        const auto& zone = zones_[static_cast<std::size_t>(i)];

        const auto keyMatch = midiNote >= zone.lowKey && midiNote <= zone.highKey;
        const auto velocityMatch = velocity >= zone.lowVelocity && velocity <= zone.highVelocity;

        if (keyMatch && velocityMatch)
            indices.push_back(i);
    }

    std::stable_sort(indices.begin(), indices.end(), [this](const int left, const int right)
    {
        const auto& l = zones_[static_cast<std::size_t>(left)];
        const auto& r = zones_[static_cast<std::size_t>(right)];
        return l.rrGroup < r.rrGroup;
    });

    return indices;
}
}
