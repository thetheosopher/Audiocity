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
// its own active-reader counter in RtSnapshotCell so all three can read concurrently without
// contending with each other or with the writer.
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
// Each fixed reader role publishes the exact raw pointer protected by its outermost guard. Nested
// guards reuse that pointer. An acquisition flag closes the interval between announcing the outer
// guard and publishing its hazard; the writer simply defers reclamation across that bounded window.
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
            cell_->readerAcquiring_[slot_].store(true, std::memory_order_seq_cst);
            const auto previousDepth = cell_->readerDepths_[slot_].fetch_add(1, std::memory_order_seq_cst);
            if (previousDepth == 0)
            {
                ptr_ = cell_->current_.load(std::memory_order_seq_cst);
                cell_->readerHazards_[slot_].store(ptr_, std::memory_order_seq_cst);
            }
            else
            {
                ptr_ = cell_->readerHazards_[slot_].load(std::memory_order_seq_cst);
            }
            cell_->readerAcquiring_[slot_].store(false, std::memory_order_seq_cst);
        }

        void release() noexcept
        {
            if (cell_ != nullptr)
            {
                const auto depth = cell_->readerDepths_[slot_].load(std::memory_order_seq_cst);
                if (depth == 1)
                {
                    cell_->readerReleasing_[slot_].store(true, std::memory_order_seq_cst);
                    cell_->readerHazards_[slot_].store(nullptr, std::memory_order_seq_cst);
                }
                cell_->readerDepths_[slot_].fetch_sub(1, std::memory_order_seq_cst);
                if (depth == 1)
                    cell_->readerReleasing_[slot_].store(false, std::memory_order_seq_cst);
                cell_ = nullptr;
                ptr_ = nullptr;
            }
        }

        const RtSnapshotCell* cell_ = nullptr;
        std::size_t slot_ = 0;
        const T* ptr_ = nullptr;
    };

    // Any thread; fixed atomic work with no retries, locks, allocation, or reclamation. `role`
    // must be the caller's fixed thread role -- see RtReaderRole.
    [[nodiscard]] Reader read(const RtReaderRole role) const noexcept { return Reader(*this, role); }

    // Writer side only (the message thread, for every EngineCore use). Not itself safe against
    // concurrent publish() calls on the same cell.
    void publish(std::shared_ptr<const T> next)
    {
        // Acquire ownership before exposing the raw pointer. vector growth may throw; in that
        // case current_ and every previously-published generation remain untouched.
        const auto* const raw = next.get();
        limbo_.push_back(std::move(next));
        current_.store(raw, std::memory_order_seq_cst);
        reclaim();
    }

    // Writer-side diagnostics also provide an explicit quiescent reclamation point.
    [[nodiscard]] std::size_t retainedOwnerCountForWriter()
    {
        reclaim();
        return limbo_.size();
    }

    template <class Visitor>
    void visitRetainedOwnersForWriter(Visitor&& visitor)
    {
        reclaim();
        for (const auto& owner : limbo_)
            if (owner)
                visitor(*owner);
    }

private:
    void reclaim()
    {
        for (std::size_t slot = 0; slot < readerHazards_.size(); ++slot)
        {
            if (readerAcquiring_[slot].load(std::memory_order_seq_cst)
                || readerReleasing_[slot].load(std::memory_order_seq_cst))
                return;
        }

        limbo_.erase(std::remove_if(limbo_.begin(), limbo_.end(),
            [this](const std::shared_ptr<const T>& owner)
            {
                const auto* raw = owner.get();
                if (raw != nullptr && raw == current_.load(std::memory_order_seq_cst))
                    return false;
                for (const auto& hazard : readerHazards_)
                    if (raw != nullptr && raw == hazard.load(std::memory_order_seq_cst))
                        return false;
                return true;
            }),
            limbo_.end());
    }

    mutable std::atomic<const T*> current_{ nullptr };
    mutable std::array<std::atomic<const T*>, static_cast<std::size_t>(RtReaderRole::count)> readerHazards_{};
    mutable std::array<std::atomic<std::size_t>, static_cast<std::size_t>(RtReaderRole::count)> readerDepths_{};
    mutable std::array<std::atomic<bool>, static_cast<std::size_t>(RtReaderRole::count)> readerAcquiring_{};
    mutable std::array<std::atomic<bool>, static_cast<std::size_t>(RtReaderRole::count)> readerReleasing_{};
    std::vector<std::shared_ptr<const T>> limbo_;
};

} // namespace audiocity::engine
