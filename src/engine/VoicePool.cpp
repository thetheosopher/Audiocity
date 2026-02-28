#include "VoicePool.h"

#include <algorithm>
#include <limits>

namespace audiocity::engine
{
void VoicePool::prepare(const int blockSize) noexcept
{
    preparedBlockSize_ = blockSize;
    static_cast<void>(preparedBlockSize_);
    reset();
}

void VoicePool::reset() noexcept
{
    for (auto& voice : voices_)
    {
        voice.active = false;
        voice.noteNumber = -1;
        voice.currentLevel = 0.0f;
        voice.startOrder = 0;
    }

    startCounter_ = 0;
    stealCount_ = 0;
}

int VoicePool::startVoiceForNote(const int noteNumber) noexcept
{
    auto voiceIndex = findInactiveSlot();

    if (voiceIndex < 0)
    {
        voiceIndex = findStealCandidate();
        ++stealCount_;
    }

    auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
    voice.active = true;
    voice.noteNumber = noteNumber;
    voice.currentLevel = 0.0f;
    voice.startOrder = ++startCounter_;

    return voiceIndex;
}

void VoicePool::stopVoiceAtIndex(const int index) noexcept
{
    if (index < 0 || index >= static_cast<int>(maxVoices))
        return;

    auto& voice = voices_[static_cast<std::size_t>(index)];
    voice.active = false;
    voice.noteNumber = -1;
    voice.currentLevel = 0.0f;
    voice.startOrder = 0;
}

void VoicePool::stopAllVoices() noexcept
{
    for (int i = 0; i < static_cast<int>(maxVoices); ++i)
        stopVoiceAtIndex(i);
}

void VoicePool::setNoteAtIndex(const int index, const int noteNumber) noexcept
{
    if (index < 0 || index >= static_cast<int>(maxVoices))
        return;

    auto& voice = voices_[static_cast<std::size_t>(index)];
    if (!voice.active)
        return;

    voice.noteNumber = noteNumber;
}

int VoicePool::firstActiveVoiceIndex() const noexcept
{
    for (int i = 0; i < static_cast<int>(maxVoices); ++i)
    {
        if (voices_[static_cast<std::size_t>(i)].active)
            return i;
    }

    return -1;
}

int VoicePool::findActiveVoicesForNote(const int noteNumber, int* indices, const int maxIndices) const noexcept
{
    if (indices == nullptr || maxIndices <= 0)
        return 0;

    int count = 0;

    for (int voiceIndex = 0; voiceIndex < static_cast<int>(maxVoices); ++voiceIndex)
    {
        const auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];

        if (!voice.active || voice.noteNumber != noteNumber)
            continue;

        indices[count++] = voiceIndex;

        if (count >= maxIndices)
            break;
    }

    return count;
}

void VoicePool::setCurrentLevel(const int index, const float level) noexcept
{
    if (index < 0 || index >= static_cast<int>(maxVoices))
        return;

    voices_[static_cast<std::size_t>(index)].currentLevel = std::max(0.0f, level);
}

bool VoicePool::isActive(const int index) const noexcept
{
    if (index < 0 || index >= static_cast<int>(maxVoices))
        return false;

    return voices_[static_cast<std::size_t>(index)].active;
}

int VoicePool::noteAt(const int index) const noexcept
{
    if (index < 0 || index >= static_cast<int>(maxVoices))
        return -1;

    return voices_[static_cast<std::size_t>(index)].noteNumber;
}

std::uint64_t VoicePool::startOrderAt(const int index) const noexcept
{
    if (index < 0 || index >= static_cast<int>(maxVoices))
        return 0;

    return voices_[static_cast<std::size_t>(index)].startOrder;
}

int VoicePool::activeVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& voice : voices_)
        count += voice.active ? 1 : 0;

    return count;
}

bool VoicePool::isNoteActive(const int noteNumber) const noexcept
{
    for (const auto& voice : voices_)
    {
        if (voice.active && voice.noteNumber == noteNumber)
            return true;
    }

    return false;
}

int VoicePool::findInactiveSlot() const noexcept
{
    for (int voiceIndex = 0; voiceIndex < static_cast<int>(maxVoices); ++voiceIndex)
    {
        if (!voices_[static_cast<std::size_t>(voiceIndex)].active)
            return voiceIndex;
    }

    return -1;
}

int VoicePool::findStealCandidate() const noexcept
{
    int candidate = 0;
    float minLevel = std::numeric_limits<float>::max();
    std::uint64_t oldestOrder = std::numeric_limits<std::uint64_t>::max();

    for (int voiceIndex = 0; voiceIndex < static_cast<int>(maxVoices); ++voiceIndex)
    {
        const auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];

        if (!voice.active)
            continue;

        if (voice.currentLevel < minLevel)
        {
            minLevel = voice.currentLevel;
            oldestOrder = voice.startOrder;
            candidate = voiceIndex;
            continue;
        }

        if (voice.currentLevel == minLevel && voice.startOrder < oldestOrder)
        {
            oldestOrder = voice.startOrder;
            candidate = voiceIndex;
        }
    }

    return candidate;
}
}
