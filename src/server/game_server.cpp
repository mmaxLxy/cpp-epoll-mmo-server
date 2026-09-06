#include "mmo/server/game_server.h"

#include "mmo/protocol/protobuf_codec.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace mmo::server {

GameServer::GameServer(
    data::PlayerRepository& players,
    data::SessionStore& sessions,
    ServerConfig config)
    : config_(std::move(config)),
      completions_([this] { database_wakeup_.Notify(); }),
      workers_(
          config_.database_worker_count,
          config_.database_queue_capacity),
      data_service_(players, sessions, workers_, completions_),
      game_service_(*this, data_service_, config_.game) {
    OpenListener();
    if (!event_loop_.Add(
            listener_.Get(),
            EPOLLIN,
            [this](std::uint32_t) { AcceptReady(); })) {
        throw std::runtime_error("failed to register listening socket");
    }
    if (!event_loop_.Add(
            database_wakeup_.NativeHandle(),
            EPOLLIN,
            [this](std::uint32_t) {
                database_wakeup_.Consume();
                completions_.Drain();
                CloseMarkedConnections();
            })) {
        throw std::runtime_error("failed to register database eventfd");
    }
}

GameServer::~GameServer() {
    Stop();
    std::vector<game::ConnectionId> connections;
    connections.reserve(connection_descriptors_.size());
    for (const auto& [connection_id, descriptor] : connection_descriptors_) {
        (void)descriptor;
        connections.push_back(connection_id);
    }
    for (const auto connection_id : connections) {
        CloseConnection(connection_id);
    }
    workers_.Shutdown();
    completions_.Drain();
    if (listener_) {
        event_loop_.Remove(listener_.Get());
    }
    event_loop_.Remove(database_wakeup_.NativeHandle());
}

void GameServer::Run() {
    stopped_.store(false);
    event_loop_.Run();
}

void GameServer::Stop() noexcept {
    if (!stopped_.exchange(true)) {
        event_loop_.Stop();
    }
}

std::uint16_t GameServer::BoundPort() const noexcept {
    return bound_port_;
}

bool GameServer::Send(
    game::ConnectionId connection_id,
    protocol::MessageType type,
    const google::protobuf::MessageLite& message) {
    const auto descriptor = connection_descriptors_.find(connection_id);
    if (descriptor == connection_descriptors_.end()) {
        return false;
    }
    const auto client = clients_.find(descriptor->second);
    if (client == clients_.end()) {
        return false;
    }

    try {
        if (!client->second->connection.QueueBytes(
                protocol::EncodeProtobufFrame(type, message))) {
            marked_for_close_.insert(connection_id);
            return false;
        }
    } catch (...) {
        marked_for_close_.insert(connection_id);
        return false;
    }

    if (client->second->connection.Flush() != net::IoStatus::kOpen) {
        marked_for_close_.insert(connection_id);
        return false;
    }
    UpdateInterest(*client->second);
    return true;
}

bool GameServer::IsConnected(game::ConnectionId connection_id) const {
    return connection_descriptors_.count(connection_id) != 0 &&
           marked_for_close_.count(connection_id) == 0;
}

void GameServer::Disconnect(game::ConnectionId connection_id) {
    CloseConnection(connection_id);
}

void GameServer::OpenListener() {
    listener_.Reset(socket(
        AF_INET,
        SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
        0));
    if (!listener_) {
        throw std::system_error(errno, std::generic_category(), "socket");
    }

    const int enabled = 1;
    if (setsockopt(
            listener_.Get(),
            SOL_SOCKET,
            SO_REUSEADDR,
            &enabled,
            sizeof(enabled)) != 0) {
        throw std::system_error(errno, std::generic_category(), "setsockopt");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    if (inet_pton(
            AF_INET,
            config_.bind_address.c_str(),
            &address.sin_addr) != 1) {
        throw std::invalid_argument("invalid IPv4 bind address");
    }
    if (bind(
            listener_.Get(),
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) != 0) {
        throw std::system_error(errno, std::generic_category(), "bind");
    }
    if (listen(listener_.Get(), config_.listen_backlog) != 0) {
        throw std::system_error(errno, std::generic_category(), "listen");
    }

    socklen_t address_size = sizeof(address);
    if (getsockname(
            listener_.Get(),
            reinterpret_cast<sockaddr*>(&address),
            &address_size) != 0) {
        throw std::system_error(errno, std::generic_category(), "getsockname");
    }
    bound_port_ = ntohs(address.sin_port);
}

void GameServer::AcceptReady() {
    while (true) {
        sockaddr_in peer{};
        socklen_t peer_size = sizeof(peer);
        const int descriptor = accept4(
            listener_.Get(),
            reinterpret_cast<sockaddr*>(&peer),
            &peer_size,
            SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (descriptor < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            throw std::system_error(errno, std::generic_category(), "accept4");
        }

        net::UniqueFd accepted(descriptor);
        if (clients_.size() >= config_.maximum_connections) {
            continue;
        }
        const game::ConnectionId connection_id = ++next_connection_id_;
        auto client = std::make_unique<Client>(Client{
            connection_id,
            net::TcpConnection(
                std::move(accepted),
                config_.maximum_pending_bytes_per_connection)});
        const int client_descriptor = client->connection.NativeHandle();
        clients_.emplace(client_descriptor, std::move(client));
        connection_descriptors_[connection_id] = client_descriptor;
        if (!event_loop_.Add(
                client_descriptor,
                EPOLLIN | EPOLLRDHUP,
                [this, client_descriptor](std::uint32_t events) {
                    ClientReady(client_descriptor, events);
                })) {
            CloseConnection(connection_id);
        }
    }
}

void GameServer::ClientReady(int descriptor, std::uint32_t events) {
    auto client = clients_.find(descriptor);
    if (client == clients_.end()) {
        return;
    }
    const game::ConnectionId connection_id = client->second->id;
    bool should_close = (events & (EPOLLERR | EPOLLHUP)) != 0;

    if ((events & EPOLLIN) != 0) {
        std::vector<protocol::Frame> frames;
        const auto status = client->second->connection.ReadAvailable(frames);
        if (status == net::IoStatus::kProtocolError ||
            status == net::IoStatus::kIoError) {
            frames.clear();
            should_close = true;
        } else if (status == net::IoStatus::kPeerClosed) {
            should_close = true;
        }
        for (const auto& frame : frames) {
            if (!IsConnected(connection_id)) {
                break;
            }
            game_service_.HandleFrame(connection_id, frame);
        }
    }

    if (!IsConnected(connection_id)) {
        CloseMarkedConnections();
        return;
    }
    client = clients_.find(descriptor);
    if (client != clients_.end() && (events & EPOLLOUT) != 0 &&
        client->second->connection.Flush() != net::IoStatus::kOpen) {
        should_close = true;
    }
    if ((events & EPOLLRDHUP) != 0) {
        should_close = true;
    }
    if (should_close) {
        marked_for_close_.insert(connection_id);
    } else if (client != clients_.end()) {
        UpdateInterest(*client->second);
    }
    CloseMarkedConnections();
}

void GameServer::UpdateInterest(Client& client) {
    std::uint32_t events = EPOLLIN | EPOLLRDHUP;
    if (client.connection.WantsWrite()) {
        events |= EPOLLOUT;
    }
    if (!event_loop_.Modify(client.connection.NativeHandle(), events)) {
        marked_for_close_.insert(client.id);
    }
}

void GameServer::CloseConnection(game::ConnectionId connection_id) {
    const auto descriptor = connection_descriptors_.find(connection_id);
    if (descriptor == connection_descriptors_.end()) {
        marked_for_close_.erase(connection_id);
        return;
    }
    const int native_handle = descriptor->second;
    event_loop_.Remove(native_handle);
    game_service_.OnDisconnected(connection_id);
    clients_.erase(native_handle);
    connection_descriptors_.erase(descriptor);
    marked_for_close_.erase(connection_id);
}

void GameServer::CloseMarkedConnections() {
    while (!marked_for_close_.empty()) {
        const auto connection_id = *marked_for_close_.begin();
        CloseConnection(connection_id);
    }
}

}  // namespace mmo::server
