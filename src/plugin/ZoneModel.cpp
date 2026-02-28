#include "ZoneModel.h"

#include "PluginProcessor.h"

ProcessorZoneModel::ProcessorZoneModel(AudiocityAudioProcessor& processor) noexcept
    : processor_(processor)
{
}

int ProcessorZoneModel::getZoneCount() const
{
    return static_cast<int>(processor_.getImportedZones().size());
}

std::optional<ZoneLoopState> ProcessorZoneModel::getZoneLoopState(const int zoneIndex) const
{
    const auto& zones = processor_.getImportedZones();
    if (zoneIndex < 0 || zoneIndex >= static_cast<int>(zones.size()))
        return std::nullopt;

    const auto& zone = zones[static_cast<std::size_t>(zoneIndex)];
    return ZoneLoopState{ zone.loopStart, zone.loopEnd };
}

bool ProcessorZoneModel::setZoneLoopState(const int zoneIndex, const int loopStart, const int loopEnd)
{
    return processor_.updateImportedZoneLoopPoints(zoneIndex, loopStart, loopEnd);
}