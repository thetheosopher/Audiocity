#pragma once

#include <array>
#include <cstddef>

namespace audiocity::engine
{
class VoicePool
{
public:
    static constexpr std::size_t maxVoices = 64;

    void prepare(int blockSize) noexcept;
    void reset() noexcept;

    [[nodiscard]] int activeVoiceCount() const noexcept;

private:
    struct VoiceState
    {
        bool active = false;
        float gain = 0.0f;
    };

    std::array<VoiceState, maxVoices> voices_{};
    int preparedBlockSize_ = 0;
};
}
