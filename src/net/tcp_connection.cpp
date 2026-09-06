#include "mmo/net/tcp_connection.h"

#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <stdexcept>
#include <utility>

namespace mmo::net {

TcpConnection::TcpConnection(
    UniqueFd descriptor,
    std::size_t maximum_pending_bytes)
    : descriptor_(std::move(descriptor)),
      maximum_pending_bytes_(maximum_pending_bytes) {
    if (!descriptor_ || maximum_pending_bytes_ == 0) {
        throw std::invalid_argument("invalid TCP connection configuration");
    }
}

int TcpConnection::NativeHandle() const noexcept {
    return descriptor_.Get();
}

IoStatus TcpConnection::ReadAvailable(
    std::vector<protocol::Frame>& frames) {
    std::array<std::uint8_t, 16U * 1024U> buffer{};
    bool peer_closed = false;
    while (true) {
        const ssize_t received = recv(
            descriptor_.Get(), buffer.data(), buffer.size(), 0);
        if (received > 0) {
            decoder_.Append(buffer.data(), static_cast<std::size_t>(received));
            continue;
        }
        if (received == 0) {
            peer_closed = true;
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        return IoStatus::kIoError;
    }

    while (true) {
        protocol::Frame frame;
        const auto status = decoder_.Next(frame);
        if (status == protocol::DecodeStatus::kFrameReady) {
            frames.push_back(std::move(frame));
            continue;
        }
        if (status != protocol::DecodeStatus::kNeedMoreData) {
            return IoStatus::kProtocolError;
        }
        break;
    }
    return peer_closed ? IoStatus::kPeerClosed : IoStatus::kOpen;
}

bool TcpConnection::QueueBytes(std::vector<std::uint8_t> bytes) {
    if (bytes.empty()) {
        return true;
    }
    CompactOutput();
    if (bytes.size() > maximum_pending_bytes_ - PendingBytes()) {
        return false;
    }
    output_.insert(output_.end(), bytes.begin(), bytes.end());
    return true;
}

IoStatus TcpConnection::Flush() {
    while (WantsWrite()) {
        const ssize_t sent = send(
            descriptor_.Get(),
            output_.data() + output_offset_,
            PendingBytes(),
            MSG_NOSIGNAL);
        if (sent > 0) {
            output_offset_ += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return IoStatus::kOpen;
        }
        return IoStatus::kIoError;
    }
    output_.clear();
    output_offset_ = 0;
    return IoStatus::kOpen;
}

bool TcpConnection::WantsWrite() const noexcept {
    return output_offset_ < output_.size();
}

std::size_t TcpConnection::PendingBytes() const noexcept {
    return output_.size() - output_offset_;
}

void TcpConnection::CompactOutput() {
    if (output_offset_ == 0) {
        return;
    }
    if (output_offset_ == output_.size()) {
        output_.clear();
    } else {
        output_.erase(
            output_.begin(),
            output_.begin() + static_cast<std::ptrdiff_t>(output_offset_));
    }
    output_offset_ = 0;
}

}  // namespace mmo::net
