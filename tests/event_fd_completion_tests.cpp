#include "mmo/concurrency/completion_queue.h"
#include "mmo/concurrency/event_fd_wakeup.h"

#include "test_support.h"

#include <poll.h>

#include <thread>

namespace {

using mmo::concurrency::CompletionQueue;
using mmo::concurrency::EventFdWakeup;

void WorkerCompletionWakesOwnerThroughEventFd() {
    EventFdWakeup wakeup;
    CompletionQueue completions([&wakeup] { wakeup.Notify(); });
    const std::thread::id owner_thread = std::this_thread::get_id();
    std::thread::id callback_thread;
    bool callback_called = false;

    std::thread worker([&completions, &callback_called, &callback_thread] {
        completions.Push([&callback_called, &callback_thread] {
            callback_called = true;
            callback_thread = std::this_thread::get_id();
        });
    });

    pollfd descriptor{};
    descriptor.fd = wakeup.NativeHandle();
    descriptor.events = POLLIN;
    const int poll_result = poll(&descriptor, 1, 2000);
    worker.join();
    CHECK_EQ(poll_result, 1);
    CHECK_TRUE((descriptor.revents & POLLIN) != 0);
    CHECK_FALSE(callback_called);

    wakeup.Consume();
    CHECK_EQ(completions.Drain(), 1U);
    CHECK_TRUE(callback_called);
    CHECK_EQ(callback_thread, owner_thread);
}

}  // namespace

int main() {
    return test_support::Run({
        {"eventfd wakes owner for worker completion",
         WorkerCompletionWakesOwnerThroughEventFd},
    });
}
