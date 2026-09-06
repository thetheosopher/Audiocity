#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace audiocity::plugin
{
/** A single owned worker that keeps at most one queued replacement job.

    Submitting a job cooperatively cancels the active job and replaces any job
    that has not started yet. Destruction requests cancellation and joins the
    worker, so no callback can continue against an owner after teardown.
*/
class OwnedJobWorker final
{
public:
    using CancellationFlag = std::atomic<bool>;
    using Job = std::function<void(const CancellationFlag&)>;

    OwnedJobWorker()
        : worker_([this] { run(); })
    {
    }

    ~OwnedJobWorker()
    {
        shutdown();
    }

    OwnedJobWorker(const OwnedJobWorker&) = delete;
    OwnedJobWorker& operator=(const OwnedJobWorker&) = delete;

    [[nodiscard]] bool submit(Job job)
    {
        auto cancellation = std::make_shared<CancellationFlag>(false);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return false;

            requestCancellationLocked();
            pending_ = Entry{ std::move(job), std::move(cancellation) };
        }

        condition_.notify_one();
        return true;
    }

    void cancelAll() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requestCancellationLocked();
            pending_.reset();
        }

        condition_.notify_one();
    }

    void shutdown() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return;

            stopping_ = true;
            requestCancellationLocked();
            pending_.reset();
        }

        condition_.notify_one();
        if (worker_.joinable())
            worker_.join();
    }

private:
    struct Entry
    {
        Job job;
        std::shared_ptr<CancellationFlag> cancellation;
    };

    void requestCancellationLocked() noexcept
    {
        if (activeCancellation_ != nullptr)
            activeCancellation_->store(true, std::memory_order_release);
        if (pending_.has_value())
            pending_->cancellation->store(true, std::memory_order_release);
    }

    void run()
    {
        for (;;)
        {
            std::optional<Entry> entry;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
                if (stopping_)
                    return;

                entry = std::move(pending_);
                pending_.reset();
                activeCancellation_ = entry->cancellation;
            }

            if (!entry->cancellation->load(std::memory_order_acquire))
                entry->job(*entry->cancellation);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (activeCancellation_ == entry->cancellation)
                    activeCancellation_.reset();
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<Entry> pending_;
    std::shared_ptr<CancellationFlag> activeCancellation_;
    bool stopping_ = false;
    std::thread worker_;
};
}
