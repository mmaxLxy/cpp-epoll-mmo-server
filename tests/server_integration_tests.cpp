#include "mmo/client/blocking_client.h"
#include "mmo/data/memory_stores.h"
#include "mmo/protocol/protobuf_codec.h"
#include "mmo/server/game_server.h"

#include "game_messages.pb.h"
#include "test_support.h"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;
using mmo::client::BlockingClient;
using mmo::client::ReceiveStatus;
using mmo::protocol::Frame;
using mmo::protocol::MessageType;

template <typename Message>
Message ReceiveType(BlockingClient& client, MessageType expected) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        Frame frame;
        const auto status = client.Receive(frame, 500ms);
        if (status == ReceiveStatus::kTimeout) {
            continue;
        }
        CHECK_EQ(status, ReceiveStatus::kFrameReady);
        if (frame.message_id != static_cast<std::uint32_t>(expected)) {
            continue;
        }
        Message message;
        CHECK_TRUE(mmo::protocol::ParseProtobufPayload(
            frame, expected, message));
        return message;
    }
    CHECK_TRUE(false);
    return Message{};
}

void Login(BlockingClient& client, const std::string& account) {
    mmo::proto::LoginRequest login;
    login.set_account(account);
    CHECK_TRUE(client.Send(MessageType::kLoginRequest, login));
    const auto response =
        ReceiveType<mmo::proto::LoginResponse>(
            client, MessageType::kLoginResponse);
    CHECK_TRUE(response.success());
}

void TwoClientsLoginChatAndCrossAoiGrid() {
    mmo::data::MemoryPlayerRepository players;
    mmo::data::MemorySessionStore sessions;
    mmo::server::ServerConfig config;
    config.port = 0;
    mmo::server::GameServer server(players, sessions, config);
    std::thread server_thread([&] { server.Run(); });
    try {
        BlockingClient alice;
        BlockingClient bob;
        CHECK_TRUE(server.BoundPort() != 0);
        bool alice_connected = false;
        for (int attempt = 0; attempt < 20 && !alice_connected; ++attempt) {
            alice_connected = alice.Connect("127.0.0.1", server.BoundPort());
            if (!alice_connected) {
                std::this_thread::sleep_for(10ms);
            }
        }
        if (!alice_connected) {
            throw std::runtime_error(
                std::string("alice connect failed: ") + std::strerror(errno) +
                " port=" + std::to_string(server.BoundPort()));
        }
        CHECK_TRUE(bob.Connect("127.0.0.1", server.BoundPort()));
        Login(alice, "integration-alice");
        Login(bob, "integration-bob");
        (void)ReceiveType<mmo::proto::PlayerEnter>(
            alice, MessageType::kPlayerEnter);

        mmo::proto::ChatRequest chat;
        chat.set_text("network hello");
        CHECK_TRUE(alice.Send(MessageType::kChatRequest, chat));
        const auto alice_chat =
            ReceiveType<mmo::proto::ChatBroadcast>(
                alice, MessageType::kChatBroadcast);
        const auto bob_chat =
            ReceiveType<mmo::proto::ChatBroadcast>(
                bob, MessageType::kChatBroadcast);
        CHECK_EQ(alice_chat.text(), std::string("network hello"));
        CHECK_EQ(bob_chat.text(), std::string("network hello"));

        mmo::proto::MoveRequest move;
        move.mutable_position()->set_x(10.0);
        move.mutable_position()->set_y(10.0);
        CHECK_TRUE(alice.Send(MessageType::kMoveRequest, move));
        (void)ReceiveType<mmo::proto::PlayerLeave>(
            alice, MessageType::kPlayerLeave);
        (void)ReceiveType<mmo::proto::PlayerLeave>(
            bob, MessageType::kPlayerLeave);

        alice.Shutdown();
        bob.Shutdown();
    } catch (...) {
        server.Stop();
        server_thread.join();
        throw;
    }
    server.Stop();
    server_thread.join();
}

}  // namespace

int main() {
    return test_support::Run({
        {"two TCP clients login chat and cross AOI grid",
         TwoClientsLoginChatAndCrossAoiGrid},
    });
}
