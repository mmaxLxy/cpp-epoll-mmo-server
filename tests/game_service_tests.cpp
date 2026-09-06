#include "mmo/concurrency/bounded_thread_pool.h"
#include "mmo/concurrency/completion_queue.h"
#include "mmo/data/async_data_service.h"
#include "mmo/data/memory_stores.h"
#include "mmo/game/game_service.h"
#include "mmo/protocol/protobuf_codec.h"

#include "game_messages.pb.h"
#include "test_support.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

using namespace std::chrono_literals;
using mmo::concurrency::BoundedThreadPool;
using mmo::concurrency::CompletionQueue;
using mmo::data::AsyncDataService;
using mmo::data::MemoryPlayerRepository;
using mmo::data::MemorySessionStore;
using mmo::game::ConnectionId;
using mmo::game::GameService;
using mmo::game::GameTransport;
using mmo::protocol::Frame;
using mmo::protocol::FrameDecoder;
using mmo::protocol::MessageType;

struct SentMessage {
    ConnectionId connection_id = 0;
    MessageType type = MessageType::kHeartbeat;
    std::string payload;
};

class FakeTransport final : public GameTransport {
public:
    bool Send(
        ConnectionId connection_id,
        MessageType type,
        const google::protobuf::MessageLite& message) override {
        if (!IsConnected(connection_id)) {
            return false;
        }
        sent.push_back(
            SentMessage{connection_id, type, message.SerializeAsString()});
        return true;
    }

    bool IsConnected(ConnectionId connection_id) const override {
        return connected.count(connection_id) != 0;
    }

    void Disconnect(ConnectionId connection_id) override {
        connected.erase(connection_id);
    }

    template <typename Message>
    Message Last(ConnectionId connection_id, MessageType type) const {
        for (auto message = sent.rbegin(); message != sent.rend(); ++message) {
            if (message->connection_id == connection_id &&
                message->type == type) {
                Message parsed;
                CHECK_TRUE(parsed.ParseFromString(message->payload));
                return parsed;
            }
        }
        CHECK_TRUE(false);
        return Message{};
    }

    std::size_t Count(ConnectionId connection_id, MessageType type) const {
        std::size_t count = 0;
        for (const auto& message : sent) {
            if (message.connection_id == connection_id && message.type == type) {
                ++count;
            }
        }
        return count;
    }

    std::unordered_set<ConnectionId> connected;
    std::vector<SentMessage> sent;
};

template <typename Message>
Frame MakeFrame(MessageType type, const Message& message) {
    const auto bytes = mmo::protocol::EncodeProtobufFrame(type, message);
    FrameDecoder decoder;
    decoder.Append(bytes);
    Frame frame;
    CHECK_EQ(decoder.Next(frame), mmo::protocol::DecodeStatus::kFrameReady);
    return frame;
}

template <typename Predicate>
void PumpUntil(CompletionQueue& completions, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        completions.Drain();
        std::this_thread::yield();
    }
    completions.Drain();
    CHECK_TRUE(predicate());
}

void Login(
    GameService& game,
    CompletionQueue& completions,
    ConnectionId connection_id,
    const std::string& account,
    const std::string& token = {}) {
    mmo::proto::LoginRequest request;
    request.set_account(account);
    request.set_session_token(token);
    game.HandleFrame(
        connection_id,
        MakeFrame(MessageType::kLoginRequest, request));
    const std::size_t expected = game.OnlinePlayerCount() + 1;
    PumpUntil(completions, [&] {
        return game.OnlinePlayerCount() == expected;
    });
}

void LoginChatMoveDisconnectAndReconnect() {
    MemoryPlayerRepository players;
    MemorySessionStore sessions;
    BoundedThreadPool workers(2, 32);
    CompletionQueue completions;
    AsyncDataService data_service(players, sessions, workers, completions);
    FakeTransport transport;
    transport.connected = {1, 2};
    GameService game(transport, data_service);

    Login(game, completions, 1, "alice");
    const auto alice_login =
        transport.Last<mmo::proto::LoginResponse>(
            1, MessageType::kLoginResponse);
    CHECK_TRUE(alice_login.success());
    CHECK_FALSE(alice_login.session_token().empty());

    Login(game, completions, 2, "bob");
    const auto bob_login =
        transport.Last<mmo::proto::LoginResponse>(
            2, MessageType::kLoginResponse);
    CHECK_TRUE(bob_login.success());
    CHECK_EQ(bob_login.nearby_players_size(), 1);
    CHECK_EQ(transport.Count(1, MessageType::kPlayerEnter), 1U);

    mmo::proto::ChatRequest chat;
    chat.set_text("hello world");
    game.HandleFrame(1, MakeFrame(MessageType::kChatRequest, chat));
    CHECK_EQ(transport.Count(1, MessageType::kChatBroadcast), 1U);
    CHECK_EQ(transport.Count(2, MessageType::kChatBroadcast), 1U);

    mmo::proto::MoveRequest move;
    move.mutable_position()->set_x(10.0);
    move.mutable_position()->set_y(10.0);
    game.HandleFrame(1, MakeFrame(MessageType::kMoveRequest, move));
    CHECK_EQ(transport.Count(1, MessageType::kPlayerLeave), 1U);
    CHECK_EQ(transport.Count(2, MessageType::kPlayerLeave), 1U);

    transport.connected.erase(1);
    game.OnDisconnected(1);
    PumpUntil(completions, [&] {
        const auto stored = players.LoadByAccount("alice");
        return stored.Ok() && stored.value->position_x == 10.0 &&
               stored.value->position_y == 10.0;
    });
    CHECK_EQ(game.OnlinePlayerCount(), 1U);

    transport.connected.insert(3);
    Login(
        game,
        completions,
        3,
        "alice",
        alice_login.session_token());
    const auto reconnect =
        transport.Last<mmo::proto::LoginResponse>(
            3, MessageType::kLoginResponse);
    CHECK_TRUE(reconnect.success());
    CHECK_EQ(reconnect.self().position().x(), 10.0);
    CHECK_EQ(reconnect.self().position().y(), 10.0);
}

}  // namespace

int main() {
    return test_support::Run({
        {"login chat AOI disconnect and reconnect",
         LoginChatMoveDisconnectAndReconnect},
    });
}
