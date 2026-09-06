#pragma once

#include "mmo/concurrency/bounded_thread_pool.h"
#include "mmo/concurrency/completion_queue.h"
#include "mmo/concurrency/event_fd_wakeup.h"
#include "mmo/data/async_data_service.h"
#include "mmo/data/player_repository.h"
#include "mmo/data/session_store.h"
#include "mmo/game/game_service.h"
#include "mmo/net/event_loop.h"
#include "mmo/net/tcp_connection.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace mmo::server {

struct ServerConfig {
    std::string bind_address = "127.0.0.1";
    std::uint16_t port = 9000;
    int listen_backlog = 128;
    std::size_t maximum_connections = 4096;
    std::size_t maximum_pending_bytes_per_connection = 4U * 1024U * 1024U;
    std::size_t database_worker_count = 2;
    std::size_t database_queue_capacity = 1024;
    game::GameConfig game;
};

class GameServer final : public game::GameTransport {
public:
    GameServer(
        data::PlayerRepository& players,
        data::SessionStore& sessions,
        ServerConfig config = {});
    ~GameServer() override;

    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

    void Run();
    void Stop() noexcept;
    std::uint16_t BoundPort() const noexcept;

    bool Send(
        game::ConnectionId connection_id,
        protocol::MessageType type,
        const google::protobuf::MessageLite& message) override;
    bool IsConnected(game::ConnectionId connection_id) const override;
    void Disconnect(game::ConnectionId connection_id) override;

private:
    struct Client {
        game::ConnectionId id = 0;
        net::TcpConnection connection;
    };

    void OpenListener();
    void AcceptReady();
    void ClientReady(int descriptor, std::uint32_t events);
    void UpdateInterest(Client& client);
    void CloseConnection(game::ConnectionId connection_id);
    void CloseMarkedConnections();

    ServerConfig config_;
    net::EventLoop event_loop_;
    net::UniqueFd listener_;
    concurrency::EventFdWakeup database_wakeup_;
    concurrency::CompletionQueue completions_;
    concurrency::BoundedThreadPool workers_;
    data::AsyncDataService data_service_;
    game::GameService game_service_;
    std::unordered_map<int, std::unique_ptr<Client>> clients_;
    std::unordered_map<game::ConnectionId, int> connection_descriptors_;
    std::unordered_set<game::ConnectionId> marked_for_close_;
    game::ConnectionId next_connection_id_ = 0;
    std::uint16_t bound_port_ = 0;
    std::atomic<bool> stopped_{false};
};

}  // namespace mmo::server
