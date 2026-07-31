#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace audiocity::engine
{

// Which thread role is reading a cell. EngineCore has exactly one live thread per role at a time:
// the real-time audio thread (inside render() and its private helpers), the dedicated
// stream-priming worker thread (AudiocityAudioProcessor::streamPrimeWorker_), and the message
// thread (everything else -- UI-driven setters/getters, ImportedProgramStore, etc). Each role gets
// its own hazard slot in RtSnapshotCell so all three can read concurrently without contending with
// each other or with the writer.
enum class RtReaderRole : std::size_t
{
    audio = 0,
    worker = 1,
    message = 2,
    count = 3
};

// Publishes shared_ptr<const T> snapshots from a single, externally-serialized writer (the message
// thread, for every EngineCore use) to one reader per RtReaderRole, without ever taking a reader
// through std::atomic<std::shared_ptr<T>>::load()'s internal spinlock. That operation is NOT
// lock-free on any known standard library -- confirmed on MSVC, where is_always_lock_free is false
// and load() takes an internal spinlock (_Lock_and_load) -- which would violate
// docs/02-real-time-rules.md's "no locks on the audio thread" rule.
//
// This is a minimal hazard-pointer scheme: a reader publishes which raw pointer it is about to use
// *before* dereferencing it (with a re-check to close the obvious race), and the writer defers
// freeing any generation still protected by a hazard slot. Unlike a fixed-size retirement ring
// (see DiskSampleStreamSource::cacheStatePtr_/cacheStateOwners_), this is safe no matter how long a
// reader holds the returned Reader guard (e.g. an entire host-controlled render() block, which can
// be arbitrarily large) or how many times the writer publishes while that guard is alive.
template <class T>
class RtSnapshotCell
{
public:
    // Move-only RAII guard. Do not store beyond the scope that requested it -- the protected
    // generation is only guaranteed alive while the guard exists.
    class Reader
    {
    public:
        Reader() noexcept = default;
        Reader(Reader&& other) noexcept { *this = std::move(other); }

        Reader& operator=(Reader&& other) noexcept
        {
            if (this != &other)
            {
                release();
                cell_ = other.cell_;
                slot_ = other.slot_;
                ptr_ = other.ptr_;
                other.cell_ = nullptr;
                other.ptr_ = nullptr;
            }
            return *this;
        }

        Reader(const Reader&) = delete;
        Reader& operator=(const Reader&) = delete;
        ~Reader() noexcept { release(); }

        [[nodiscard]] const T* get() const noexcept { return ptr_; }
        const T* operator->() const noexcept { return ptr_; }
        const T& operator*() const noexcept { return *ptr_; }
        explicit operator bool() const noexcept { return ptr_ != nullptr; }

    private:
        friend class RtSnapshotCell;

        Reader(const RtSnapshotCell& cell, const RtReaderRole role) noexcept
            : cell_(&cell), slot_(static_cast<std::size_t>(role))
        {
            const T* candidate;
            for (;;)
            {
                candidate = cell_->current_.load(std::memory_order_acquire);
                cell_->hazards_[slot_].store(candidate, std::memory_order_release);

                // The writer could have retired `candidate` in the window between our two loads
                // above; re-read current_ and retry until it agrees with what we just protected,
                // so the hazard is guaranteed published before the writer could possibly free it.
                if (cell_->current_.load(std::memory_order_acquire) == candidate)
                    break;
            }
            ptr_ = candidate;
        }

        void release() noexcept
        {
            if (cell_ != nullptr)
                cell_->hazards_[slot_].store(nullptr, std::memory_order_release);
        }

        const RtSnapshotCell* cell_ = nullptr;
        std::size_t slot_ = 0;
        const T* ptr_ = nullptr;
    };

    // Any thread; wait-free (bounded retries, no locks, no allocation). `role` must be the
    // caller's fixed thread role -- see RtReaderRole.
    [[nodiscard]] Reader read(const RtReaderRole role) const noexcept { return Reader(*this, role); }

    // Writer side only (the message thread, for every EngineCore use). Not itself safe against
    // concurrent publish() calls on the same cell.
    void publish(std::shared_ptr<const T> next)
    {
        current_.store(next.get(), std::memory_order_release);
        limbo_.push_back(std::move(next));
        reclaim();
    }

private:
    void reclaim()
    {
        limbo_.erase(std::remove_if(limbo_.begin(), limbo_.end(),
            [this](const std::shared_ptr<const T>& owner)
            {
                const auto* raw = owner.get();
                if (raw == current_.load(std::memory_order_acquire))
                    return false;

                for (auto& hazard : hazards_)
                {
                    if (hazard.load(std::memory_order_acquire) == raw)
                        return false;
                }

                return true;
            }),
            limbo_.end());
    }

    mutable std::atomic<const T*> current_{ nullptr };
    mutable std::array<std::atomic<const T*>, static_cast<std::size_t>(RtReaderRole::count)> hazards_{};
    std::vector<std::shared_ptr<const T>> limbo_;
};

} // namespace audiocity::engine
