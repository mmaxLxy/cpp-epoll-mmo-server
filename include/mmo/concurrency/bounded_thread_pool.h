#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace mmo::concurrency {

// A non-blocking submit API keeps the epoll thread from waiting when the
// database queue is full. Shutdown drains accepted tasks before joining.
class BoundedThreadPool {
public:
    BoundedThreadPool(std::size_t worker_count, std::size_t queue_capacity);
    ~BoundedThreadPool();

    BoundedThreadPool(const BoundedThreadPool&) = delete;
    BoundedThreadPool& operator=(const BoundedThreadPool&) = delete;

    bool TrySubmit(std::function<void()> task);
    void Shutdown();

    std::size_t PendingTasks() const;
    bool AcceptingTasks() const;

private:
    void WorkerLoop();

    const std::size_t queue_capacity_;
    mutable std::mutex mutex_;
    std::condition_variable task_available_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    bool accepting_tasks_ = true;
};

}  // namespace mmo::concurrency
