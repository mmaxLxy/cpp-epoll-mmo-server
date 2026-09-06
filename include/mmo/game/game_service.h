#pragma once

#include "mmo/data/async_data_service.h"
#include "mmo/game/aoi_world.h"
#include "mmo/protocol/frame_codec.h"

#include <google/protobuf/message_lite.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace mmo::game {

using ConnectionId = std::uint64_t;

class GameTransport {
public:
    virtual ~GameTransport() = default;

    virtual bool Send(
        ConnectionId connection_id,
        protocol::MessageType type,
        const google::protobuf::MessageLite& message) = 0;
    virtual bool IsConnected(ConnectionId connection_id) const = 0;
    virtual void Disconnect(ConnectionId connection_id) = 0;
};

struct GameConfig {
    AoiConfig aoi{0.0, 1000.0, 0.0, 2000.0, 10, 20};
    std::chrono::seconds session_ttl{300};
    std::size_t maximum_chat_bytes = 256;
};

class GameService {
public:
    GameService(
        GameTransport& transport,
        data::AsyncDataService& data_service,
        GameConfig config = {});

    void HandleFrame(
        ConnectionId connection_id,
        const protocol::Frame& frame);
    void OnDisconnected(ConnectionId connection_id);

    std::size_t OnlinePlayerCount() const noexcept;

private:
    struct OnlinePlayer {
        data::PlayerRecord record;
        ConnectionId connection_id = 0;
        std::string session_token;
    };

    void HandleLogin(ConnectionId connection_id, const protocol::Frame& frame);
    void HandleChat(ConnectionId connection_id, const protocol::Frame& frame);
    void HandleMove(ConnectionId connection_id, const protocol::Frame& frame);
    void HandleHeartbeat(ConnectionId connection_id, const protocol::Frame& frame);

    void ContinueLogin(
        ConnectionId connection_id,
        data::PlayerRecord player,
        std::string requested_token,
        bool newly_created);
    void StoreSessionAndFinish(
        ConnectionId connection_id,
        data::PlayerRecord player,
        bool allow_replacement);
    void FinishLogin(
        ConnectionId connection_id,
        data::PlayerRecord player,
        std::string session_token,
        bool allow_replacement);
    void FailLogin(
        ConnectionId connection_id,
        const std::string& account,
        std::string error);

    data::PlayerRecord NewPlayer(const std::string& account);
    std::string NewSessionToken();

    OnlinePlayer* PlayerForConnection(ConnectionId connection_id);
    const OnlinePlayer* PlayerById(PlayerId player_id) const;

    GameTransport& transport_;
    data::AsyncDataService& data_service_;
    GameConfig config_;
    AoiWorld world_;
    std::unordered_map<PlayerId, OnlinePlayer> online_players_;
    std::unordered_map<ConnectionId, PlayerId> connection_players_;
    std::unordered_map<ConnectionId, std::string> pending_connections_;
    std::unordered_set<std::string> pending_accounts_;
    std::mt19937_64 random_;
    std::uint64_t player_sequence_ = 0;
};

}  // namespace mmo::game
