#include "mmo/client/blocking_client.h"

#include "mmo/protocol/protobuf_codec.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>

namespace mmo::client {

bool BlockingClient::Connect(
    const std::string& address,
    std::uint16_t port) {
    if (descriptor_) {
        shutdown(descriptor_.Get(), SHUT_RDWR);
        descriptor_.Reset();
    }
    shutdown_.store(false);
    net::UniqueFd descriptor(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!descriptor) {
        return false;
    }
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) != 1 ||
        connect(
            descriptor.Get(),
            reinterpret_cast<const sockaddr*>(&endpoint),
            sizeof(endpoint)) != 0) {
        return false;
    }
    descriptor_ = std::move(descriptor);
    decoder_.Reset();
    return true;
}

bool BlockingClient::Send(
    protocol::MessageType type,
    const google::protobuf::MessageLite& message) {
    if (!Connected()) {
        return false;
    }
    std::vector<std::uint8_t> bytes;
    try {
        bytes = protocol::EncodeProtobufFrame(type, message);
    } catch (...) {
        return false;
    }

    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t sent = send(
            descriptor_.Get(),
            bytes.data() + offset,
            bytes.size() - offset,
            MSG_NOSIGNAL);
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

ReceiveStatus BlockingClient::Receive(
    protocol::Frame& frame,
    std::chrono::milliseconds timeout) {
    if (!Connected()) {
        return ReceiveStatus::kClosed;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const auto decode_status = decoder_.Next(frame);
        if (decode_status == protocol::DecodeStatus::kFrameReady) {
            return ReceiveStatus::kFrameReady;
        }
        if (decode_status != protocol::DecodeStatus::kNeedMoreData) {
            return ReceiveStatus::kProtocolError;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            return ReceiveStatus::kTimeout;
        }
        pollfd event{};
        event.fd = descriptor_.Get();
        event.events = POLLIN;
        const int poll_result = poll(
            &event,
            1,
            static_cast<int>(remaining.count()));
        if (poll_result == 0) {
            return ReceiveStatus::kTimeout;
        }
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ReceiveStatus::kIoError;
        }
        if ((event.revents & (POLLERR | POLLNVAL)) != 0) {
            return ReceiveStatus::kIoError;
        }

        std::array<std::uint8_t, 16U * 1024U> buffer{};
        const ssize_t received = recv(
            descriptor_.Get(), buffer.data(), buffer.size(), 0);
        if (received > 0) {
            decoder_.Append(buffer.data(), static_cast<std::size_t>(received));
            continue;
        }
        if (received == 0) {
            return ReceiveStatus::kClosed;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        return ReceiveStatus::kIoError;
    }
}

void BlockingClient::Shutdown() noexcept {
    if (descriptor_ && !shutdown_.exchange(true)) {
        shutdown(descriptor_.Get(), SHUT_RDWR);
    }
}

bool BlockingClient::Connected() const noexcept {
    return static_cast<bool>(descriptor_) && !shutdown_.load();
}

}  // namespace mmo::client
