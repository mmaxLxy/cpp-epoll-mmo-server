#include "mmo/game/game_service.h"

#include "mmo/protocol/protobuf_codec.h"

#include "game_messages.pb.h"

#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace mmo::game {
namespace {

void FillView(const data::PlayerRecord& player, mmo::proto::PlayerView* view) {
    view->set_player_id(player.player_id);
    view->set_nickname(player.nickname);
    view->mutable_position()->set_x(player.position_x);
    view->mutable_position()->set_y(player.position_y);
}

}  // namespace

GameService::GameService(
    GameTransport& transport,
    data::AsyncDataService& data_service,
    GameConfig config)
    : transport_(transport),
      data_service_(data_service),
      config_(config),
      world_(config_.aoi),
      random_(std::random_device{}()) {}

void GameService::HandleFrame(
    ConnectionId connection_id,
    const protocol::Frame& frame) {
    const auto type = static_cast<protocol::MessageType>(frame.message_id);
    switch (type) {
        case protocol::MessageType::kLoginRequest:
            HandleLogin(connection_id, frame);
            break;
        case protocol::MessageType::kChatRequest:
            HandleChat(connection_id, frame);
            break;
        case protocol::MessageType::kMoveRequest:
            HandleMove(connection_id, frame);
            break;
        case protocol::MessageType::kHeartbeat:
            HandleHeartbeat(connection_id, frame);
            break;
        default:
            transport_.Disconnect(connection_id);
            break;
    }
}

void GameService::HandleLogin(
    ConnectionId connection_id,
    const protocol::Frame& frame) {
    mmo::proto::LoginRequest request;
    if (!protocol::ParseProtobufPayload(
            frame, protocol::MessageType::kLoginRequest, request) ||
        request.account().empty() || request.account().size() > 64 ||
        connection_players_.count(connection_id) != 0 ||
        pending_connections_.count(connection_id) != 0) {
        FailLogin(connection_id, {}, "invalid or duplicate login request");
        return;
    }
    if (pending_accounts_.count(request.account()) != 0) {
        FailLogin(connection_id, {}, "account login is already pending");
        return;
    }

    const std::string account = request.account();
    const std::string requested_token = request.session_token();
    pending_connections_[connection_id] = account;
    pending_accounts_.insert(account);

    if (!data_service_.LoadPlayer(
            account,
            [this, connection_id, account, requested_token](
                data::DataResult<data::PlayerRecord> result) mutable {
                if (!transport_.IsConnected(connection_id) ||
                    pending_connections_.count(connection_id) == 0) {
                    pending_accounts_.erase(account);
                    pending_connections_.erase(connection_id);
                    return;
                }
                if (result.Ok()) {
                    ContinueLogin(
                        connection_id,
                        std::move(result.value.value()),
                        std::move(requested_token),
                        false);
                    return;
                }
                if (result.status != data::DataStatus::kNotFound) {
                    FailLogin(connection_id, account, "failed to load player");
                    return;
                }

                auto player = NewPlayer(account);
                if (!requested_token.empty()) {
                    FailLogin(connection_id, account, "session does not exist");
                    return;
                }
                auto player_for_callback = player;
                if (!data_service_.SavePlayer(
                        std::move(player),
                        [this,
                         connection_id,
                         account,
                         player = std::move(player_for_callback)](
                            data::OperationResult save_result) mutable {
                            if (!save_result.Ok()) {
                                FailLogin(
                                    connection_id,
                                    account,
                                    "failed to create player");
                                return;
                            }
                            ContinueLogin(
                                connection_id,
                                std::move(player),
                                {},
                                true);
                        })) {
                    FailLogin(connection_id, account, "database queue is full");
                }
            })) {
        FailLogin(connection_id, account, "database queue is full");
    }
}

void GameService::ContinueLogin(
    ConnectionId connection_id,
    data::PlayerRecord player,
    std::string requested_token,
    bool newly_created) {
    const std::string account = player.account;
    if (!transport_.IsConnected(connection_id) ||
        pending_connections_.count(connection_id) == 0) {
        pending_accounts_.erase(account);
        pending_connections_.erase(connection_id);
        return;
    }
    player.last_login_unix = static_cast<std::int64_t>(std::time(nullptr));
    if (requested_token.empty()) {
        StoreSessionAndFinish(
            connection_id, std::move(player), false);
        return;
    }

    const PlayerId player_id = player.player_id;
    if (!data_service_.LoadSession(
            player_id,
            [this,
             connection_id,
             account,
             player = std::move(player),
             requested_token = std::move(requested_token),
             newly_created](data::DataResult<data::SessionRecord> result) mutable {
                if (newly_created || !result.Ok() ||
                    result.value->token != requested_token) {
                    FailLogin(connection_id, account, "invalid or expired session");
                    return;
                }
                StoreSessionAndFinish(
                    connection_id, std::move(player), true);
            })) {
        FailLogin(connection_id, account, "database queue is full");
    }
}

void GameService::StoreSessionAndFinish(
    ConnectionId connection_id,
    data::PlayerRecord player,
    bool allow_replacement) {
    const std::string account = player.account;
    if (!transport_.IsConnected(connection_id) ||
        pending_connections_.count(connection_id) == 0) {
        pending_accounts_.erase(account);
        pending_connections_.erase(connection_id);
        return;
    }
    if (online_players_.count(player.player_id) != 0 && !allow_replacement) {
        FailLogin(connection_id, account, "player is already online");
        return;
    }
    const std::string token = NewSessionToken();
    const data::SessionRecord session{player.player_id, token};
    if (!data_service_.StoreSession(
            session,
            config_.session_ttl,
            [this,
             connection_id,
             account,
             player = std::move(player),
             token,
             allow_replacement](data::OperationResult result) mutable {
                if (!result.Ok()) {
                    FailLogin(connection_id, account, "failed to store session");
                    return;
                }
                FinishLogin(
                    connection_id,
                    std::move(player),
                    std::move(token),
                    allow_replacement);
            })) {
        FailLogin(connection_id, account, "database queue is full");
    }
}

void GameService::FinishLogin(
    ConnectionId connection_id,
    data::PlayerRecord player,
    std::string session_token,
    bool allow_replacement) {
    const std::string account = player.account;
    if (!transport_.IsConnected(connection_id) ||
        pending_connections_.count(connection_id) == 0) {
        pending_accounts_.erase(account);
        pending_connections_.erase(connection_id);
        return;
    }

    const auto existing = online_players_.find(player.player_id);
    if (existing != online_players_.end()) {
        if (!allow_replacement) {
            FailLogin(connection_id, account, "player is already online");
            return;
        }
        player = existing->second.record;
        transport_.Disconnect(existing->second.connection_id);
    }

    if (!world_.AddPlayer(
            player.player_id,
            Position{player.position_x, player.position_y})) {
        FailLogin(connection_id, account, "player position is outside the world");
        return;
    }

    connection_players_[connection_id] = player.player_id;
    online_players_.emplace(
        player.player_id,
        OnlinePlayer{player, connection_id, session_token});
    pending_accounts_.erase(account);
    pending_connections_.erase(connection_id);

    const auto visible = world_.VisiblePlayers(player.player_id).value();
    mmo::proto::LoginResponse response;
    response.set_success(true);
    FillView(player, response.mutable_self());
    response.set_session_token(session_token);
    for (const auto nearby_id : visible) {
        const auto* nearby = PlayerById(nearby_id);
        if (nearby != nullptr) {
            FillView(nearby->record, response.add_nearby_players());
        }
    }
    transport_.Send(
        connection_id, protocol::MessageType::kLoginResponse, response);

    mmo::proto::PlayerEnter entered;
    FillView(player, entered.mutable_player());
    for (const auto nearby_id : visible) {
        const auto* nearby = PlayerById(nearby_id);
        if (nearby != nullptr) {
            transport_.Send(
                nearby->connection_id,
                protocol::MessageType::kPlayerEnter,
                entered);
        }
    }
}

void GameService::HandleChat(
    ConnectionId connection_id,
    const protocol::Frame& frame) {
    auto* sender = PlayerForConnection(connection_id);
    mmo::proto::ChatRequest request;
    if (sender == nullptr ||
        !protocol::ParseProtobufPayload(
            frame, protocol::MessageType::kChatRequest, request) ||
        request.text().empty() ||
        request.text().size() > config_.maximum_chat_bytes) {
        return;
    }

    mmo::proto::ChatBroadcast broadcast;
    broadcast.set_player_id(sender->record.player_id);
    broadcast.set_nickname(sender->record.nickname);
    broadcast.set_text(request.text());
    for (const auto& [player_id, player] : online_players_) {
        (void)player_id;
        transport_.Send(
            player.connection_id,
            protocol::MessageType::kChatBroadcast,
            broadcast);
    }
}

void GameService::HandleMove(
    ConnectionId connection_id,
    const protocol::Frame& frame) {
    auto* mover = PlayerForConnection(connection_id);
    mmo::proto::MoveRequest request;
    if (mover == nullptr ||
        !protocol::ParseProtobufPayload(
            frame, protocol::MessageType::kMoveRequest, request) ||
        !request.has_position()) {
        return;
    }

    const Position position{request.position().x(), request.position().y()};
    const auto delta = world_.MovePlayer(mover->record.player_id, position);
    if (!delta.has_value()) {
        return;
    }
    mover->record.position_x = position.x;
    mover->record.position_y = position.y;

    mmo::proto::PlayerMove moved;
    moved.set_player_id(mover->record.player_id);
    moved.mutable_position()->set_x(position.x);
    moved.mutable_position()->set_y(position.y);
    for (const auto player_id : delta->stayed) {
        const auto* player = PlayerById(player_id);
        if (player != nullptr) {
            transport_.Send(
                player->connection_id,
                protocol::MessageType::kPlayerMove,
                moved);
        }
    }

    mmo::proto::PlayerEnter mover_entered;
    FillView(mover->record, mover_entered.mutable_player());
    for (const auto player_id : delta->entered) {
        const auto* player = PlayerById(player_id);
        if (player == nullptr) {
            continue;
        }
        transport_.Send(
            player->connection_id,
            protocol::MessageType::kPlayerEnter,
            mover_entered);
        mmo::proto::PlayerEnter other_entered;
        FillView(player->record, other_entered.mutable_player());
        transport_.Send(
            connection_id,
            protocol::MessageType::kPlayerEnter,
            other_entered);
    }

    mmo::proto::PlayerLeave mover_left;
    mover_left.set_player_id(mover->record.player_id);
    for (const auto player_id : delta->left) {
        const auto* player = PlayerById(player_id);
        if (player == nullptr) {
            continue;
        }
        transport_.Send(
            player->connection_id,
            protocol::MessageType::kPlayerLeave,
            mover_left);
        mmo::proto::PlayerLeave other_left;
        other_left.set_player_id(player_id);
        transport_.Send(
            connection_id,
            protocol::MessageType::kPlayerLeave,
            other_left);
    }
}

void GameService::HandleHeartbeat(
    ConnectionId connection_id,
    const protocol::Frame& frame) {
    mmo::proto::Heartbeat heartbeat;
    if (protocol::ParseProtobufPayload(
            frame, protocol::MessageType::kHeartbeat, heartbeat)) {
        transport_.Send(
            connection_id, protocol::MessageType::kHeartbeat, heartbeat);
    }
}

void GameService::OnDisconnected(ConnectionId connection_id) {
    const auto pending = pending_connections_.find(connection_id);
    if (pending != pending_connections_.end()) {
        pending_accounts_.erase(pending->second);
        pending_connections_.erase(pending);
    }

    const auto mapping = connection_players_.find(connection_id);
    if (mapping == connection_players_.end()) {
        return;
    }
    const PlayerId player_id = mapping->second;
    const auto online = online_players_.find(player_id);
    if (online == online_players_.end()) {
        connection_players_.erase(mapping);
        return;
    }

    const auto visible = world_.VisiblePlayers(player_id).value();
    const auto record = online->second.record;
    world_.RemovePlayer(player_id);
    online_players_.erase(online);
    connection_players_.erase(mapping);

    mmo::proto::PlayerLeave left;
    left.set_player_id(player_id);
    for (const auto nearby_id : visible) {
        const auto* nearby = PlayerById(nearby_id);
        if (nearby != nullptr) {
            transport_.Send(
                nearby->connection_id,
                protocol::MessageType::kPlayerLeave,
                left);
        }
    }

    data_service_.SavePlayer(record, [](data::OperationResult) {});
    // The Redis key is intentionally left to expire so a short reconnect can
    // present its token. A successful new login replaces it with a fresh TTL.
}

std::size_t GameService::OnlinePlayerCount() const noexcept {
    return online_players_.size();
}

void GameService::FailLogin(
    ConnectionId connection_id,
    const std::string& account,
    std::string error) {
    if (!account.empty()) {
        pending_accounts_.erase(account);
    }
    const auto pending = pending_connections_.find(connection_id);
    if (pending != pending_connections_.end()) {
        pending_accounts_.erase(pending->second);
        pending_connections_.erase(pending);
    }

    if (!transport_.IsConnected(connection_id)) {
        return;
    }
    mmo::proto::LoginResponse response;
    response.set_success(false);
    response.set_error(std::move(error));
    transport_.Send(
        connection_id, protocol::MessageType::kLoginResponse, response);
}

data::PlayerRecord GameService::NewPlayer(const std::string& account) {
    static constexpr std::array<const char*, 12> kNames{
        "Nova", "Atlas", "Echo", "Luna", "Orion", "Pixel",
        "River", "Comet", "Maple", "Mango", "Quartz", "Sparrow"};
    const auto milliseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    const PlayerId player_id =
        (milliseconds << 20U) | (++player_sequence_ & 0xFFFFFU);
    const std::string nickname =
        std::string(kNames[random_() % kNames.size()]) + "-" +
        std::to_string(player_id % 100000U);
    return data::PlayerRecord{
        player_id,
        account,
        nickname,
        (config_.aoi.min_x + config_.aoi.max_x) / 2.0,
        (config_.aoi.min_y + config_.aoi.max_y) / 2.0,
        0.0,
        static_cast<std::int64_t>(std::time(nullptr))};
}

std::string GameService::NewSessionToken() {
    std::ostringstream token;
    token << std::hex << std::setfill('0')
          << std::setw(16) << random_()
          << std::setw(16) << random_();
    return token.str();
}

GameService::OnlinePlayer* GameService::PlayerForConnection(
    ConnectionId connection_id) {
    const auto mapping = connection_players_.find(connection_id);
    if (mapping == connection_players_.end()) {
        return nullptr;
    }
    const auto player = online_players_.find(mapping->second);
    return player == online_players_.end() ? nullptr : &player->second;
}

const GameService::OnlinePlayer* GameService::PlayerById(
    PlayerId player_id) const {
    const auto player = online_players_.find(player_id);
    return player == online_players_.end() ? nullptr : &player->second;
}

}  // namespace mmo::game
