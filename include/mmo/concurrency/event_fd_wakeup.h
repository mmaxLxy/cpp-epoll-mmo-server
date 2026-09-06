#pragma once

namespace mmo::concurrency {

class EventFdWakeup {
public:
    EventFdWakeup();
    ~EventFdWakeup();

    EventFdWakeup(const EventFdWakeup&) = delete;
    EventFdWakeup& operator=(const EventFdWakeup&) = delete;

    int NativeHandle() const noexcept;
    void Notify() noexcept;
    void Consume() noexcept;

private:
    int descriptor_ = -1;
};

}  // namespace mmo::concurrency
