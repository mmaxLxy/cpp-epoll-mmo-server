#pragma once

#include "mmo/concurrency/event_fd_wakeup.h"
#include "mmo/net/unique_fd.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace mmo::net {

class EventLoop {
public:
    using EventCallback = std::function<void(std::uint32_t)>;

    EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    bool Add(int descriptor, std::uint32_t events, EventCallback callback);
    bool Modify(int descriptor, std::uint32_t events);
    void Remove(int descriptor) noexcept;

    int PollOnce(int timeout_milliseconds);
    void Run();
    void Stop() noexcept;

private:
    UniqueFd epoll_descriptor_;
    concurrency::EventFdWakeup stop_wakeup_;
    std::unordered_map<int, EventCallback> callbacks_;
    std::atomic<bool> running_{false};
};

}  // namespace mmo::net
