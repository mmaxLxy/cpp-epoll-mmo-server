#include "mmo/data/async_data_service.h"

#include <utility>

namespace mmo::data {

AsyncDataService::AsyncDataService(
    PlayerRepository& players,
    SessionStore& sessions,
    concurrency::BoundedThreadPool& workers,
    concurrency::CompletionQueue& completions)
    : players_(players),
      sessions_(sessions),
      workers_(workers),
      completions_(completions) {}

bool AsyncDataService::LoadPlayer(
    std::string account,
    PlayerCallback callback) {
    if (!callback) {
        return false;
    }
    return workers_.TrySubmit(
        [this, account = std::move(account), callback = std::move(callback)]() mutable {
            auto result = players_.LoadByAccount(account);
            completions_.Push(
                [callback = std::move(callback), result = std::move(result)]() mutable {
                    callback(std::move(result));
                });
        });
}

bool AsyncDataService::SavePlayer(
    PlayerRecord player,
    OperationCallback callback) {
    if (!callback) {
        return false;
    }
    return workers_.TrySubmit(
        [this, player = std::move(player), callback = std::move(callback)]() mutable {
            auto result = players_.Save(player);
            completions_.Push(
                [callback = std::move(callback), result = std::move(result)]() mutable {
                    callback(std::move(result));
                });
        });
}

bool AsyncDataService::DeletePlayer(
    std::string account,
    OperationCallback callback) {
    if (!callback) {
        return false;
    }
    return workers_.TrySubmit(
        [this, account = std::move(account), callback = std::move(callback)]() mutable {
            auto result = players_.DeleteByAccount(account);
            completions_.Push(
                [callback = std::move(callback), result = std::move(result)]() mutable {
                    callback(std::move(result));
                });
        });
}

bool AsyncDataService::StoreSession(
    SessionRecord session,
    std::chrono::seconds ttl,
    OperationCallback callback) {
    if (!callback) {
        return false;
    }
    return workers_.TrySubmit(
        [this,
         session = std::move(session),
         ttl,
         callback = std::move(callback)]() mutable {
            auto result = sessions_.Store(session, ttl);
            completions_.Push(
                [callback = std::move(callback), result = std::move(result)]() mutable {
                    callback(std::move(result));
                });
        });
}

bool AsyncDataService::LoadSession(
    std::uint64_t player_id,
    SessionCallback callback) {
    if (!callback) {
        return false;
    }
    return workers_.TrySubmit(
        [this, player_id, callback = std::move(callback)]() mutable {
            auto result = sessions_.Load(player_id);
            completions_.Push(
                [callback = std::move(callback), result = std::move(result)]() mutable {
                    callback(std::move(result));
                });
        });
}

bool AsyncDataService::RemoveSession(
    std::uint64_t player_id,
    OperationCallback callback) {
    if (!callback) {
        return false;
    }
    return workers_.TrySubmit(
        [this, player_id, callback = std::move(callback)]() mutable {
            auto result = sessions_.Remove(player_id);
            completions_.Push(
                [callback = std::move(callback), result = std::move(result)]() mutable {
                    callback(std::move(result));
                });
        });
}

}  // namespace mmo::data
