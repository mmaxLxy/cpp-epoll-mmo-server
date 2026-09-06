#include "mmo/client/blocking_client.h"
#include "mmo/data/mysql_player_repository.h"
#include "mmo/data/redis_session_store.h"
#include "mmo/protocol/protobuf_codec.h"
#include "mmo/server/game_server.h"

#include "game_messages.pb.h"
#include "test_support.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

std::string Environment(const char* name, std::string fallback = {}) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::move(fallback) : std::string(value);
}

mmo::data::MySqlConfig MySqlConfigFromEnvironment() {
    mmo::data::MySqlConfig config;
    config.host = Environment("MMO_MYSQL_HOST", "127.0.0.1");
    config.port = static_cast<unsigned int>(
        std::stoul(Environment("MMO_MYSQL_PORT", "3306")));
    config.user = Environment("MMO_MYSQL_USER");
    config.password = Environment("MMO_MYSQL_PASSWORD");
    config.database = Environment("MMO_MYSQL_DATABASE");
    return config;
}

mmo::data::RedisConfig RedisConfigFromEnvironment() {
    mmo::data::RedisConfig config;
    config.host = Environment("MMO_REDIS_HOST", "127.0.0.1");
    config.port = std::stoi(Environment("MMO_REDIS_PORT", "6379"));
    config.key_prefix = "mmo:integration-server:session:";
    return config;
}

template <typename Message>
Message ReceiveType(
    mmo::client::BlockingClient& client,
    mmo::protocol::MessageType expected) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        mmo::protocol::Frame frame;
        const auto status = client.Receive(frame, 500ms);
        if (status == mmo::client::ReceiveStatus::kTimeout) {
            continue;
        }
        CHECK_EQ(status, mmo::client::ReceiveStatus::kFrameReady);
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

void RealDatabaseServerRoundTrip() {
    const std::string account = "integration-server-account";
    mmo::data::MySqlPlayerRepository players(MySqlConfigFromEnvironment());
    mmo::data::RedisSessionStore sessions(RedisConfigFromEnvironment());
    CHECK_TRUE(players.EnsureSchema().Ok());
    CHECK_TRUE(players.DeleteByAccount(account).Ok());

    std::uint64_t player_id = 0;
    {
        mmo::server::ServerConfig config;
        config.port = 0;
        mmo::server::GameServer server(players, sessions, config);
        std::thread server_thread([&] { server.Run(); });
        try {
            mmo::client::BlockingClient client;
            bool connected = false;
            for (int attempt = 0; attempt < 20 && !connected; ++attempt) {
                connected = client.Connect("127.0.0.1", server.BoundPort());
                if (!connected) {
                    std::this_thread::sleep_for(10ms);
                }
            }
            CHECK_TRUE(connected);

            mmo::proto::LoginRequest login;
            login.set_account(account);
            CHECK_TRUE(client.Send(
                mmo::protocol::MessageType::kLoginRequest, login));
            const auto response = ReceiveType<mmo::proto::LoginResponse>(
                client, mmo::protocol::MessageType::kLoginResponse);
            CHECK_TRUE(response.success());
            player_id = response.self().player_id();
            CHECK_TRUE(player_id != 0);

            const auto session = sessions.Load(player_id);
            CHECK_TRUE(session.Ok());
            CHECK_EQ(session.value->token, response.session_token());

            mmo::proto::MoveRequest move;
            move.mutable_position()->set_x(123.0);
            move.mutable_position()->set_y(456.0);
            CHECK_TRUE(client.Send(
                mmo::protocol::MessageType::kMoveRequest, move));
            mmo::proto::Heartbeat heartbeat;
            CHECK_TRUE(client.Send(
                mmo::protocol::MessageType::kHeartbeat, heartbeat));
            (void)ReceiveType<mmo::proto::Heartbeat>(
                client, mmo::protocol::MessageType::kHeartbeat);
            client.Shutdown();

            const auto deadline = std::chrono::steady_clock::now() + 5s;
            bool saved = false;
            while (!saved && std::chrono::steady_clock::now() < deadline) {
                const auto player = players.LoadByAccount(account);
                saved = player.Ok() && player.value->position_x == 123.0 &&
                        player.value->position_y == 456.0;
                if (!saved) {
                    std::this_thread::sleep_for(20ms);
                }
            }
            CHECK_TRUE(saved);
        } catch (...) {
            server.Stop();
            server_thread.join();
            if (player_id != 0) {
                sessions.Remove(player_id);
            }
            players.DeleteByAccount(account);
            throw;
        }
        server.Stop();
        server_thread.join();
    }

    if (player_id != 0) {
        CHECK_TRUE(sessions.Remove(player_id).Ok());
    }
    CHECK_TRUE(players.DeleteByAccount(account).Ok());
}

}  // namespace

int main() {
    if (Environment("MMO_MYSQL_USER").empty() ||
        Environment("MMO_MYSQL_DATABASE").empty()) {
        return 0;
    }
    return test_support::Run({
        {"real database server round trip", RealDatabaseServerRoundTrip},
    });
}
