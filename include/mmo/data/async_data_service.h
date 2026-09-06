#pragma once

#include "mmo/concurrency/bounded_thread_pool.h"
#include "mmo/concurrency/completion_queue.h"
#include "mmo/data/player_repository.h"
#include "mmo/data/session_store.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace mmo::data {

class AsyncDataService {
public:
    using PlayerCallback =
        std::function<void(DataResult<PlayerRecord>)>;
    using SessionCallback =
        std::function<void(DataResult<SessionRecord>)>;
    using OperationCallback = std::function<void(OperationResult)>;

    AsyncDataService(
        PlayerRepository& players,
        SessionStore& sessions,
        concurrency::BoundedThreadPool& workers,
        concurrency::CompletionQueue& completions);

    bool LoadPlayer(std::string account, PlayerCallback callback);
    bool SavePlayer(PlayerRecord player, OperationCallback callback);
    bool DeletePlayer(std::string account, OperationCallback callback);

    bool StoreSession(
        SessionRecord session,
        std::chrono::seconds ttl,
        OperationCallback callback);
    bool LoadSession(std::uint64_t player_id, SessionCallback callback);
    bool RemoveSession(
        std::uint64_t player_id,
        OperationCallback callback);

private:
    PlayerRepository& players_;
    SessionStore& sessions_;
    concurrency::BoundedThreadPool& workers_;
    concurrency::CompletionQueue& completions_;
};

}  // namespace mmo::data
