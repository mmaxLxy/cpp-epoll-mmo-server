#include "mmo/concurrency/bounded_thread_pool.h"
#include "mmo/concurrency/completion_queue.h"
#include "mmo/data/async_data_service.h"

#include "test_support.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace {

using namespace std::chrono_literals;
using mmo::concurrency::BoundedThreadPool;
using mmo::concurrency::CompletionQueue;
using mmo::data::AsyncDataService;
using mmo::data::DataResult;
using mmo::data::DataStatus;
using mmo::data::OperationResult;
using mmo::data::PlayerRecord;
using mmo::data::PlayerRepository;
using mmo::data::SessionRecord;
using mmo::data::SessionStore;

class MemoryPlayerRepository final : public PlayerRepository {
public:
    DataResult<PlayerRecord> LoadByAccount(
        const std::string& account) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto player = players_.find(account);
        if (player == players_.end()) {
            return DataResult<PlayerRecord>::NotFound();
        }
        return DataResult<PlayerRecord>::Success(player->second);
    }

    OperationResult Save(const PlayerRecord& player) override {
        if (player.player_id == 0 || player.account.empty()) {
            return OperationResult::Failure(
                DataStatus::kInvalidArgument, "invalid player");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        players_[player.account] = player;
        return OperationResult::Success();
    }

    OperationResult DeleteByAccount(const std::string& account) override {
        std::lock_guard<std::mutex> lock(mutex_);
        players_.erase(account);
        return OperationResult::Success();
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, PlayerRecord> players_;
};

class MemorySessionStore final : public SessionStore {
public:
    OperationResult Store(
        const SessionRecord& session,
        std::chrono::seconds ttl) override {
        if (session.player_id == 0 || session.token.empty() || ttl.count() <= 0) {
            return OperationResult::Failure(
                DataStatus::kInvalidArgument, "invalid session");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[session.player_id] = session;
        return OperationResult::Success();
    }

    DataResult<SessionRecord> Load(std::uint64_t player_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto session = sessions_.find(player_id);
        if (session == sessions_.end()) {
            return DataResult<SessionRecord>::NotFound();
        }
        return DataResult<SessionRecord>::Success(session->second);
    }

    OperationResult Remove(std::uint64_t player_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(player_id);
        return OperationResult::Success();
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, SessionRecord> sessions_;
};

bool WaitForCompletions(
    CompletionQueue& completions,
    std::size_t expected,
    std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (completions.Size() >= expected) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

void CallbackRunsOnlyWhenOwnerDrainsCompletionQueue() {
    MemoryPlayerRepository players;
    MemorySessionStore sessions;
    BoundedThreadPool workers(1, 8);
    CompletionQueue completions;
    AsyncDataService service(players, sessions, workers, completions);
    const std::thread::id owner_thread = std::this_thread::get_id();
    std::thread::id callback_thread;
    bool callback_called = false;

    const PlayerRecord player{
        7, "account-7", "player-7", 1.0, 2.0, 3.0, 12345};
    CHECK_TRUE(service.SavePlayer(
        player,
        [&](OperationResult result) {
            CHECK_TRUE(result.Ok());
            callback_called = true;
            callback_thread = std::this_thread::get_id();
        }));

    CHECK_TRUE(WaitForCompletions(completions, 1));
    CHECK_FALSE(callback_called);
    CHECK_EQ(completions.Drain(), 1U);
    CHECK_TRUE(callback_called);
    CHECK_EQ(callback_thread, owner_thread);
}

void PlayerSaveLoadAndDeleteRoundTrip() {
    MemoryPlayerRepository players;
    MemorySessionStore sessions;
    BoundedThreadPool workers(2, 8);
    CompletionQueue completions;
    AsyncDataService service(players, sessions, workers, completions);
    const PlayerRecord expected{
        9, "account-9", "player-9", 10.0, 20.0, 30.0, 54321};
    bool save_ok = false;
    bool load_ok = false;
    bool delete_ok = false;

    CHECK_TRUE(service.SavePlayer(
        expected, [&](OperationResult result) { save_ok = result.Ok(); }));
    CHECK_TRUE(WaitForCompletions(completions, 1));
    completions.Drain();
    CHECK_TRUE(save_ok);

    CHECK_TRUE(service.LoadPlayer(
        expected.account,
        [&](DataResult<PlayerRecord> result) {
            load_ok = result.Ok() && result.value.value() == expected;
        }));
    CHECK_TRUE(WaitForCompletions(completions, 1));
    completions.Drain();
    CHECK_TRUE(load_ok);

    CHECK_TRUE(service.DeletePlayer(
        expected.account,
        [&](OperationResult result) { delete_ok = result.Ok(); }));
    CHECK_TRUE(WaitForCompletions(completions, 1));
    completions.Drain();
    CHECK_TRUE(delete_ok);

    bool missing = false;
    CHECK_TRUE(service.LoadPlayer(
        expected.account,
        [&](DataResult<PlayerRecord> result) {
            missing = result.status == DataStatus::kNotFound;
        }));
    CHECK_TRUE(WaitForCompletions(completions, 1));
    completions.Drain();
    CHECK_TRUE(missing);
}

void SessionStoreLoadAndRemoveRoundTrip() {
    MemoryPlayerRepository players;
    MemorySessionStore sessions;
    BoundedThreadPool workers(1, 8);
    CompletionQueue completions;
    AsyncDataService service(players, sessions, workers, completions);
    const SessionRecord expected{42, "token-42"};
    bool stored = false;
    bool loaded = false;
    bool removed = false;

    CHECK_TRUE(service.StoreSession(
        expected,
        30s,
        [&](OperationResult result) { stored = result.Ok(); }));
    CHECK_TRUE(WaitForCompletions(completions, 1));
    completions.Drain();
    CHECK_TRUE(stored);

    CHECK_TRUE(service.LoadSession(
        expected.player_id,
        [&](DataResult<SessionRecord> result) {
            loaded = result.Ok() && result.value.value() == expected;
        }));
    CHECK_TRUE(WaitForCompletions(completions, 1));
    completions.Drain();
    CHECK_TRUE(loaded);

    CHECK_TRUE(service.RemoveSession(
        expected.player_id,
        [&](OperationResult result) { removed = result.Ok(); }));
    CHECK_TRUE(WaitForCompletions(completions, 1));
    completions.Drain();
    CHECK_TRUE(removed);
}

void FullQueueRejectsInsteadOfBlockingSubmitter() {
    BoundedThreadPool workers(1, 1);
    std::promise<void> worker_started;
    std::promise<void> release_worker;
    auto release = release_worker.get_future().share();

    CHECK_TRUE(workers.TrySubmit([&] {
        worker_started.set_value();
        release.wait();
    }));
    CHECK_EQ(worker_started.get_future().wait_for(2s), std::future_status::ready);
    CHECK_TRUE(workers.TrySubmit([] {}));
    CHECK_FALSE(workers.TrySubmit([] {}));
    release_worker.set_value();
    workers.Shutdown();
    CHECK_FALSE(workers.TrySubmit([] {}));
}

}  // namespace

int main() {
    return test_support::Run({
        {"callbacks run only when owner drains completions",
         CallbackRunsOnlyWhenOwnerDrainsCompletionQueue},
        {"player save/load/delete round trip",
         PlayerSaveLoadAndDeleteRoundTrip},
        {"session store/load/remove round trip",
         SessionStoreLoadAndRemoveRoundTrip},
        {"full queue rejects without blocking", FullQueueRejectsInsteadOfBlockingSubmitter},
    });
}
