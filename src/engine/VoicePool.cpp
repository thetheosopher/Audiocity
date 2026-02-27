#include "VoicePool.h"

namespace audiocity::engine
{
void VoicePool::prepare(const int blockSize) noexcept
{
    preparedBlockSize_ = blockSize;

    for (auto& voice : voices_)
    {
        voice.active = false;
        voice.gain = 0.0f;
    }
}

void VoicePool::reset() noexcept
{
    for (auto& voice : voices_)
    {
        voice.active = false;
        voice.gain = 0.0f;
    }
}

int VoicePool::activeVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& voice : voices_)
        count += voice.active ? 1 : 0;

    return count;
}
}
