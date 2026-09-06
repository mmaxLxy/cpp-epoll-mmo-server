#include "mmo/data/memory_stores.h"
#include "mmo/server/game_server.h"

#ifdef MMO_HAS_DATABASES
#include "mmo/data/mysql_player_repository.h"
#include "mmo/data/redis_session_store.h"
#endif

#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

mmo::server::GameServer* g_server = nullptr;

void HandleSignal(int) {
    if (g_server != nullptr) {
        g_server->Stop();
    }
}

#ifdef MMO_HAS_DATABASES
std::string Environment(const char* name, std::string fallback = {}) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::move(fallback) : std::string(value);
}
#endif

}  // namespace

int main(int argc, char** argv) {
    try {
        bool use_memory = false;
        mmo::server::ServerConfig config;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--memory") {
                use_memory = true;
            } else if (argument == "--port" && index + 1 < argc) {
                config.port = static_cast<std::uint16_t>(
                    std::stoul(argv[++index]));
            } else if (argument == "--host" && index + 1 < argc) {
                config.bind_address = argv[++index];
            } else {
                throw std::invalid_argument(
                    "usage: mmo_server [--memory] [--host IPv4] [--port N]");
            }
        }

        std::unique_ptr<mmo::data::PlayerRepository> players;
        std::unique_ptr<mmo::data::SessionStore> sessions;
        if (use_memory) {
            players = std::make_unique<mmo::data::MemoryPlayerRepository>();
            sessions = std::make_unique<mmo::data::MemorySessionStore>();
        } else {
#ifdef MMO_HAS_DATABASES
            mmo::data::MySqlConfig mysql;
            mysql.host = Environment("MMO_MYSQL_HOST", "127.0.0.1");
            mysql.port = static_cast<unsigned int>(
                std::stoul(Environment("MMO_MYSQL_PORT", "3306")));
            mysql.user = Environment("MMO_MYSQL_USER");
            mysql.password = Environment("MMO_MYSQL_PASSWORD");
            mysql.database = Environment("MMO_MYSQL_DATABASE");
            if (mysql.user.empty() || mysql.database.empty()) {
                throw std::invalid_argument(
                    "set MMO_MYSQL_USER and MMO_MYSQL_DATABASE, or use --memory");
            }
            auto mysql_repository =
                std::make_unique<mmo::data::MySqlPlayerRepository>(mysql);
            const auto schema = mysql_repository->EnsureSchema();
            if (!schema.Ok()) {
                throw std::runtime_error("MySQL setup failed: " + schema.error);
            }
            mmo::data::RedisConfig redis;
            redis.host = Environment("MMO_REDIS_HOST", "127.0.0.1");
            redis.port = std::stoi(Environment("MMO_REDIS_PORT", "6379"));
            players = std::move(mysql_repository);
            sessions = std::make_unique<mmo::data::RedisSessionStore>(redis);
#else
            throw std::invalid_argument(
                "this build has no database adapters; use --memory");
#endif
        }

        mmo::server::GameServer server(*players, *sessions, config);
        g_server = &server;
        std::signal(SIGINT, HandleSignal);
        std::signal(SIGTERM, HandleSignal);
        std::cout << "MMO server listening on " << config.bind_address << ':'
                  << server.BoundPort() << '\n';
        server.Run();
        g_server = nullptr;
        std::cout << "MMO server stopped\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "server error: " << error.what() << '\n';
        return 1;
    }
}
