#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <mutex>
#include <queue>

namespace mmo::concurrency {

// Worker threads push callbacks; the event-loop thread calls Drain so callbacks
// execute on the owner thread. notifier can write to eventfd in the Linux server.
class CompletionQueue {
public:
    using Completion = std::function<void()>;
    using Notifier = std::function<void()>;

    explicit CompletionQueue(Notifier notifier = {});

    void Push(Completion completion);
    std::size_t Drain(
        std::size_t maximum = std::numeric_limits<std::size_t>::max());
    std::size_t Size() const;

private:
    mutable std::mutex mutex_;
    std::queue<Completion> completions_;
    Notifier notifier_;
};

}  // namespace mmo::concurrency
