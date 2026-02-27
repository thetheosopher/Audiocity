#include "ZoneMap.h"

namespace audiocity::engine
{
void ZoneMap::clear() noexcept
{
    zoneCount_ = 0;
}

bool ZoneMap::addZone(const Zone& zone) noexcept
{
    if (zoneCount_ >= maxZones)
        return false;

    zones_[zoneCount_++] = zone;
    return true;
}

int ZoneMap::size() const noexcept
{
    return zoneCount_;
}
}
