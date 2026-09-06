#include "mmo/concurrency/bounded_thread_pool.h"

#include <stdexcept>
#include <utility>

namespace mmo::concurrency {

BoundedThreadPool::BoundedThreadPool(
    std::size_t worker_count,
    std::size_t queue_capacity)
    : queue_capacity_(queue_capacity) {
    if (worker_count == 0 || queue_capacity_ == 0) {
        throw std::invalid_argument(
            "worker count and queue capacity must be positive");
    }

    workers_.reserve(worker_count);
    try {
        for (std::size_t index = 0; index < worker_count; ++index) {
            workers_.emplace_back([this] { WorkerLoop(); });
        }
    } catch (...) {
        Shutdown();
        throw;
    }
}

BoundedThreadPool::~BoundedThreadPool() {
    Shutdown();
}

bool BoundedThreadPool::TrySubmit(std::function<void()> task) {
    if (!task) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_tasks_ || tasks_.size() >= queue_capacity_) {
            return false;
        }
        tasks_.push(std::move(task));
    }
    task_available_.notify_one();
    return true;
}

void BoundedThreadPool::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_tasks_ && workers_.empty()) {
            return;
        }
        accepting_tasks_ = false;
    }
    task_available_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

std::size_t BoundedThreadPool::PendingTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

bool BoundedThreadPool::AcceptingTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return accepting_tasks_;
}

void BoundedThreadPool::WorkerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            task_available_.wait(lock, [this] {
                return !tasks_.empty() || !accepting_tasks_;
            });
            if (tasks_.empty() && !accepting_tasks_) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }

        try {
            task();
        } catch (...) {
            // A task owns its error translation. One bad task must not terminate
            // the worker and silently reduce pool capacity.
        }
    }
}

}  // namespace mmo::concurrency
