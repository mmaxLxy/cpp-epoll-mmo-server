#include "mmo/concurrency/event_fd_wakeup.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <system_error>

namespace mmo::concurrency {

EventFdWakeup::EventFdWakeup()
    : descriptor_(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
    if (descriptor_ < 0) {
        throw std::system_error(errno, std::generic_category(), "eventfd");
    }
}

EventFdWakeup::~EventFdWakeup() {
    if (descriptor_ >= 0) {
        close(descriptor_);
    }
}

int EventFdWakeup::NativeHandle() const noexcept {
    return descriptor_;
}

void EventFdWakeup::Notify() noexcept {
    const std::uint64_t increment = 1;
    while (write(descriptor_, &increment, sizeof(increment)) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return;
    }
}

void EventFdWakeup::Consume() noexcept {
    std::uint64_t count = 0;
    while (read(descriptor_, &count, sizeof(count)) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return;
    }
}

}  // namespace mmo::concurrency
