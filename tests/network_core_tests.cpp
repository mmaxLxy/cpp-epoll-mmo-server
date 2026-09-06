#include "mmo/net/event_loop.h"
#include "mmo/net/tcp_connection.h"
#include "mmo/protocol/frame_codec.h"

#include "test_support.h"

#include <sys/epoll.h>
#include <sys/socket.h>

#include <array>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

namespace {

using mmo::net::EventLoop;
using mmo::net::IoStatus;
using mmo::net::TcpConnection;
using mmo::net::UniqueFd;
using mmo::protocol::EncodeFrame;
using mmo::protocol::Frame;
using mmo::protocol::MessageType;

std::pair<UniqueFd, UniqueFd> SocketPair() {
    std::array<int, 2> descriptors{};
    CHECK_EQ(
        socketpair(
            AF_UNIX,
            SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
            0,
            descriptors.data()),
        0);
    return {UniqueFd(descriptors[0]), UniqueFd(descriptors[1])};
}

void EventLoopDispatchesReadableDescriptor() {
    auto pair = SocketPair();
    EventLoop loop;
    bool readable = false;
    CHECK_TRUE(loop.Add(
        pair.first.Get(),
        EPOLLIN,
        [&](std::uint32_t events) { readable = (events & EPOLLIN) != 0; }));
    const std::uint8_t byte = 7;
    CHECK_EQ(send(pair.second.Get(), &byte, sizeof(byte), 0), 1);
    CHECK_EQ(loop.PollOnce(1000), 1);
    CHECK_TRUE(readable);
}

void ConnectionDecodesMultipleFrames() {
    auto pair = SocketPair();
    const auto first = EncodeFrame(MessageType::kHeartbeat, {});
    const auto second = EncodeFrame(MessageType::kChatRequest, {'h', 'i'});
    std::vector<std::uint8_t> bytes(first);
    bytes.insert(bytes.end(), second.begin(), second.end());
    CHECK_EQ(
        send(pair.second.Get(), bytes.data(), bytes.size(), 0),
        static_cast<ssize_t>(bytes.size()));

    TcpConnection connection(std::move(pair.first));
    std::vector<Frame> frames;
    CHECK_EQ(connection.ReadAvailable(frames), IoStatus::kOpen);
    CHECK_EQ(frames.size(), 2U);
    CHECK_EQ(frames[0].message_id,
             static_cast<std::uint32_t>(MessageType::kHeartbeat));
    CHECK_EQ(frames[1].payload, std::vector<std::uint8_t>({'h', 'i'}));
}

void ConnectionFlushesQueuedBytes() {
    auto pair = SocketPair();
    const int peer = pair.second.Get();
    TcpConnection connection(std::move(pair.first));
    CHECK_TRUE(connection.QueueBytes({1, 2, 3, 4}));
    CHECK_EQ(connection.Flush(), IoStatus::kOpen);
    CHECK_FALSE(connection.WantsWrite());

    std::array<std::uint8_t, 4> received{};
    CHECK_EQ(recv(peer, received.data(), received.size(), 0), 4);
    CHECK_EQ(received, (std::array<std::uint8_t, 4>{1, 2, 3, 4}));
}

void PendingOutputLimitRejectsSlowConsumer() {
    auto pair = SocketPair();
    TcpConnection connection(std::move(pair.first), 4);
    CHECK_TRUE(connection.QueueBytes({1, 2, 3, 4}));
    CHECK_FALSE(connection.QueueBytes({5}));
    CHECK_EQ(connection.PendingBytes(), 4U);
}

void PartialWriteKeepsUnsentBytesForLater() {
    auto pair = SocketPair();
    const int small_buffer = 4096;
    CHECK_EQ(
        setsockopt(
            pair.first.Get(),
            SOL_SOCKET,
            SO_SNDBUF,
            &small_buffer,
            sizeof(small_buffer)),
        0);
    const int peer = pair.second.Get();
    std::vector<std::uint8_t> payload(1024U * 1024U, 0x5A);
    TcpConnection connection(std::move(pair.first), 2U * 1024U * 1024U);
    CHECK_TRUE(connection.QueueBytes(payload));
    CHECK_EQ(connection.Flush(), IoStatus::kOpen);
    CHECK_TRUE(connection.WantsWrite());

    std::size_t received_total = 0;
    std::array<std::uint8_t, 16U * 1024U> received{};
    for (int attempt = 0;
         attempt < 10000 && (connection.WantsWrite() || received_total < payload.size());
         ++attempt) {
        while (true) {
            const ssize_t count = recv(
                peer, received.data(), received.size(), 0);
            if (count > 0) {
                received_total += static_cast<std::size_t>(count);
                continue;
            }
            break;
        }
        CHECK_EQ(connection.Flush(), IoStatus::kOpen);
    }
    CHECK_FALSE(connection.WantsWrite());

    while (received_total < payload.size()) {
        const ssize_t count = recv(peer, received.data(), received.size(), 0);
        if (count > 0) {
            received_total += static_cast<std::size_t>(count);
        } else {
            break;
        }
    }
    CHECK_EQ(received_total, payload.size());
}

}  // namespace

int main() {
    return test_support::Run({
        {"epoll dispatches readable descriptor",
         EventLoopDispatchesReadableDescriptor},
        {"connection decodes multiple frames", ConnectionDecodesMultipleFrames},
        {"connection flushes queued bytes", ConnectionFlushesQueuedBytes},
        {"pending output limit rejects slow consumer",
         PendingOutputLimitRejectsSlowConsumer},
        {"partial write keeps unsent bytes", PartialWriteKeepsUnsentBytesForLater},
    });
}
