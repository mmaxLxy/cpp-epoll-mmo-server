#include "mmo/net/event_loop.h"

#include <sys/epoll.h>

#include <array>
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace mmo::net {

EventLoop::EventLoop()
    : epoll_descriptor_(epoll_create1(EPOLL_CLOEXEC)) {
    if (!epoll_descriptor_) {
        throw std::system_error(
            errno, std::generic_category(), "epoll_create1");
    }
    if (!Add(
            stop_wakeup_.NativeHandle(),
            EPOLLIN,
            [this](std::uint32_t) { stop_wakeup_.Consume(); })) {
        throw std::runtime_error("failed to register event-loop wakeup");
    }
}

bool EventLoop::Add(
    int descriptor,
    std::uint32_t events,
    EventCallback callback) {
    if (descriptor < 0 || !callback || callbacks_.count(descriptor) != 0) {
        return false;
    }
    epoll_event event{};
    event.events = events;
    event.data.fd = descriptor;
    if (epoll_ctl(
            epoll_descriptor_.Get(), EPOLL_CTL_ADD, descriptor, &event) != 0) {
        return false;
    }
    callbacks_.emplace(descriptor, std::move(callback));
    return true;
}

bool EventLoop::Modify(int descriptor, std::uint32_t events) {
    if (callbacks_.count(descriptor) == 0) {
        return false;
    }
    epoll_event event{};
    event.events = events;
    event.data.fd = descriptor;
    return epoll_ctl(
               epoll_descriptor_.Get(), EPOLL_CTL_MOD, descriptor, &event) == 0;
}

void EventLoop::Remove(int descriptor) noexcept {
    callbacks_.erase(descriptor);
    epoll_ctl(epoll_descriptor_.Get(), EPOLL_CTL_DEL, descriptor, nullptr);
}

int EventLoop::PollOnce(int timeout_milliseconds) {
    std::array<epoll_event, 64> events{};
    const int count = epoll_wait(
        epoll_descriptor_.Get(),
        events.data(),
        static_cast<int>(events.size()),
        timeout_milliseconds);
    if (count < 0) {
        if (errno == EINTR) {
            return 0;
        }
        throw std::system_error(errno, std::generic_category(), "epoll_wait");
    }

    for (int index = 0; index < count; ++index) {
        const int descriptor = events[static_cast<std::size_t>(index)].data.fd;
        const auto callback = callbacks_.find(descriptor);
        if (callback != callbacks_.end()) {
            auto invoke = callback->second;
            invoke(events[static_cast<std::size_t>(index)].events);
        }
    }
    return count;
}

void EventLoop::Run() {
    running_.store(true);
    while (running_.load()) {
        PollOnce(-1);
    }
}

void EventLoop::Stop() noexcept {
    running_.store(false);
    stop_wakeup_.Notify();
}

}  // namespace mmo::net
