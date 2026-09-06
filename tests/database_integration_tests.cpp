#include "mmo/concurrency/bounded_thread_pool.h"
#include "mmo/concurrency/completion_queue.h"
#include "mmo/data/async_data_service.h"
#include "mmo/data/mysql_player_repository.h"
#include "mmo/data/redis_session_store.h"

#include "test_support.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;
using mmo::concurrency::BoundedThreadPool;
using mmo::concurrency::CompletionQueue;
using mmo::data::AsyncDataService;
using mmo::data::DataStatus;
using mmo::data::MySqlConfig;
using mmo::data::MySqlPlayerRepository;
using mmo::data::PlayerRecord;
using mmo::data::RedisConfig;
using mmo::data::RedisSessionStore;
using mmo::data::SessionRecord;

std::string Environment(const char* name, std::string fallback = {}) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::move(fallback) : std::string(value);
}

int EnvironmentPort(const char* name, int fallback) {
    const std::string value = Environment(name);
    return value.empty() ? fallback : std::stoi(value);
}

MySqlConfig TestMySqlConfig() {
    MySqlConfig config;
    config.host = Environment("MMO_MYSQL_HOST", "127.0.0.1");
    config.port = static_cast<unsigned int>(
        EnvironmentPort("MMO_MYSQL_PORT", 3306));
    config.user = Environment("MMO_MYSQL_USER");
    config.password = Environment("MMO_MYSQL_PASSWORD");
    config.database = Environment("MMO_MYSQL_DATABASE");
    return config;
}

RedisConfig TestRedisConfig() {
    RedisConfig config;
    config.host = Environment("MMO_REDIS_HOST", "127.0.0.1");
    config.port = EnvironmentPort("MMO_REDIS_PORT", 6379);
    config.key_prefix = "mmo:integration-test:session:";
    return config;
}

bool WaitForCompletion(CompletionQueue& completions) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (completions.Size() > 0) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

void MySqlCrudRoundTrip() {
    MySqlPlayerRepository players(TestMySqlConfig());
    CHECK_TRUE(players.EnsureSchema().Ok());

    const std::string account = "integration-account-900001";
    CHECK_TRUE(players.DeleteByAccount(account).Ok());

    PlayerRecord expected{
        900001, account, "integration-player", 10.5, 20.25, -3.0, 1770000000};
    CHECK_TRUE(players.Save(expected).Ok());

    auto loaded = players.LoadByAccount(account);
    CHECK_TRUE(loaded.Ok());
    CHECK_EQ(loaded.value.value(), expected);

    expected.nickname = "updated-player";
    expected.position_x = 99.5;
    CHECK_TRUE(players.Save(expected).Ok());
    loaded = players.LoadByAccount(account);
    CHECK_TRUE(loaded.Ok());
    CHECK_EQ(loaded.value.value(), expected);

    CHECK_TRUE(players.DeleteByAccount(account).Ok());
    CHECK_EQ(players.LoadByAccount(account).status, DataStatus::kNotFound);
}

void RedisRoundTripAndExpiry() {
    RedisSessionStore sessions(TestRedisConfig());
    const SessionRecord expected{900001, "integration-token"};
    CHECK_TRUE(sessions.Remove(expected.player_id).Ok());
    CHECK_TRUE(sessions.Store(expected, 1s).Ok());

    const auto loaded = sessions.Load(expected.player_id);
    CHECK_TRUE(loaded.Ok());
    CHECK_EQ(loaded.value.value(), expected);

    std::this_thread::sleep_for(1200ms);
    CHECK_EQ(sessions.Load(expected.player_id).status, DataStatus::kNotFound);
}

void RealQueriesCompleteOnOwnerThread() {
    MySqlPlayerRepository players(TestMySqlConfig());
    RedisSessionStore sessions(TestRedisConfig());
    CHECK_TRUE(players.EnsureSchema().Ok());

    BoundedThreadPool workers(2, 8);
    CompletionQueue completions;
    AsyncDataService service(players, sessions, workers, completions);
    const std::thread::id owner_thread = std::this_thread::get_id();
    std::thread::id callback_thread;
    bool callback_ok = false;
    const PlayerRecord player{
        900002,
        "integration-account-900002",
        "async-player",
        1.0,
        2.0,
        3.0,
        1770000001};

    CHECK_TRUE(players.DeleteByAccount(player.account).Ok());
    CHECK_TRUE(service.SavePlayer(
        player,
        [&](mmo::data::OperationResult result) {
            callback_thread = std::this_thread::get_id();
            callback_ok = result.Ok();
        }));

    CHECK_TRUE(WaitForCompletion(completions));
    CHECK_FALSE(callback_ok);
    CHECK_EQ(completions.Drain(), 1U);
    CHECK_TRUE(callback_ok);
    CHECK_EQ(callback_thread, owner_thread);
    CHECK_TRUE(players.DeleteByAccount(player.account).Ok());
}

}  // namespace

int main() {
    if (Environment("MMO_MYSQL_USER").empty() ||
        Environment("MMO_MYSQL_DATABASE").empty()) {
        std::cout << "[SKIP] set MMO_MYSQL_USER and MMO_MYSQL_DATABASE to run "
                     "database integration tests\n";
        return 0;
    }

    return test_support::Run({
        {"MySQL player CRUD round trip", MySqlCrudRoundTrip},
        {"Redis session round trip and TTL expiry", RedisRoundTripAndExpiry},
        {"real queries complete on owner thread", RealQueriesCompleteOnOwnerThread},
    });
}
