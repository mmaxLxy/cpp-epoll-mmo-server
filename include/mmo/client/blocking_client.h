#pragma once

#include "mmo/net/unique_fd.h"
#include "mmo/protocol/frame_codec.h"

#include <google/protobuf/message_lite.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace mmo::client {

enum class ReceiveStatus {
    kFrameReady,
    kTimeout,
    kClosed,
    kProtocolError,
    kIoError,
};

class BlockingClient {
public:
    BlockingClient() = default;

    bool Connect(const std::string& address, std::uint16_t port);
    bool Send(
        protocol::MessageType type,
        const google::protobuf::MessageLite& message);
    ReceiveStatus Receive(
        protocol::Frame& frame,
        std::chrono::milliseconds timeout);
    void Shutdown() noexcept;
    bool Connected() const noexcept;

private:
    net::UniqueFd descriptor_;
    protocol::FrameDecoder decoder_;
    std::atomic<bool> shutdown_{false};
};

}  // namespace mmo::client
