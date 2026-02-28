#pragma once

#include <optional>

class AudiocityAudioProcessor;

struct ZoneLoopState
{
    int loopStart = 0;
    int loopEnd = 0;
};

class IZoneModel
{
public:
    virtual ~IZoneModel() = default;

    [[nodiscard]] virtual int getZoneCount() const = 0;
    [[nodiscard]] virtual std::optional<ZoneLoopState> getZoneLoopState(int zoneIndex) const = 0;
    virtual bool setZoneLoopState(int zoneIndex, int loopStart, int loopEnd) = 0;
};

class ProcessorZoneModel final : public IZoneModel
{
public:
    explicit ProcessorZoneModel(AudiocityAudioProcessor& processor) noexcept;

    [[nodiscard]] int getZoneCount() const override;
    [[nodiscard]] std::optional<ZoneLoopState> getZoneLoopState(int zoneIndex) const override;
    bool setZoneLoopState(int zoneIndex, int loopStart, int loopEnd) override;

private:
    AudiocityAudioProcessor& processor_;
};