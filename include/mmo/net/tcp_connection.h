#pragma once

#include "mmo/net/unique_fd.h"
#include "mmo/protocol/frame_codec.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mmo::net {

enum class IoStatus {
    kOpen,
    kPeerClosed,
    kProtocolError,
    kIoError,
    kSlowConsumer,
};

class TcpConnection {
public:
    explicit TcpConnection(
        UniqueFd descriptor,
        std::size_t maximum_pending_bytes = 4U * 1024U * 1024U);

    int NativeHandle() const noexcept;
    IoStatus ReadAvailable(std::vector<protocol::Frame>& frames);
    bool QueueBytes(std::vector<std::uint8_t> bytes);
    IoStatus Flush();

    bool WantsWrite() const noexcept;
    std::size_t PendingBytes() const noexcept;

private:
    void CompactOutput();

    UniqueFd descriptor_;
    protocol::FrameDecoder decoder_;
    std::vector<std::uint8_t> output_;
    std::size_t output_offset_ = 0;
    std::size_t maximum_pending_bytes_ = 0;
};

}  // namespace mmo::net
