#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace audiocity::engine
{
class VoicePool
{
public:
    static constexpr std::size_t maxVoices = 64;

    void prepare(int blockSize) noexcept;
    void reset() noexcept;
    void setVoiceLimit(int voiceLimit) noexcept;
    [[nodiscard]] int getVoiceLimit() const noexcept { return voiceLimit_; }

    [[nodiscard]] int startVoiceForNote(int noteNumber) noexcept;
    void stopVoiceAtIndex(int index) noexcept;
    void stopAllVoices() noexcept;
    void setNoteAtIndex(int index, int noteNumber) noexcept;
    [[nodiscard]] int firstActiveVoiceIndex() const noexcept;
    [[nodiscard]] int findActiveVoicesForNote(int noteNumber, int* indices, int maxIndices) const noexcept;

    void setCurrentLevel(int index, float level) noexcept;

    [[nodiscard]] bool isActive(int index) const noexcept;
    [[nodiscard]] int noteAt(int index) const noexcept;
    [[nodiscard]] std::uint64_t startOrderAt(int index) const noexcept;

    [[nodiscard]] int activeVoiceCount() const noexcept;
    [[nodiscard]] int stealCount() const noexcept { return stealCount_; }
    void resetStealCount() noexcept { stealCount_ = 0; }
    [[nodiscard]] bool isNoteActive(int noteNumber) const noexcept;

private:
    struct VoiceSlot
    {
        bool active = false;
        int noteNumber = -1;
        float currentLevel = 0.0f;
        std::uint64_t startOrder = 0;
    };

    [[nodiscard]] int findInactiveSlot() const noexcept;
    [[nodiscard]] int findStealCandidate() const noexcept;

    std::array<VoiceSlot, maxVoices> voices_{};
    int preparedBlockSize_ = 0;
    int voiceLimit_ = static_cast<int>(maxVoices);
    std::uint64_t startCounter_ = 0;
    int stealCount_ = 0;
};
}
