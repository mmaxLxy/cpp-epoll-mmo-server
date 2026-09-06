#include "mmo/concurrency/completion_queue.h"

#include <utility>

namespace mmo::concurrency {

CompletionQueue::CompletionQueue(Notifier notifier)
    : notifier_(std::move(notifier)) {}

void CompletionQueue::Push(Completion completion) {
    if (!completion) {
        return;
    }

    bool should_notify = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        should_notify = completions_.empty();
        completions_.push(std::move(completion));
    }
    if (should_notify && notifier_) {
        notifier_();
    }
}

std::size_t CompletionQueue::Drain(std::size_t maximum) {
    std::size_t completed = 0;
    while (completed < maximum) {
        Completion completion;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (completions_.empty()) {
                break;
            }
            completion = std::move(completions_.front());
            completions_.pop();
        }
        completion();
        ++completed;
    }
    return completed;
}

std::size_t CompletionQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return completions_.size();
}

}  // namespace mmo::concurrency
