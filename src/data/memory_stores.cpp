#include "mmo/data/memory_stores.h"

namespace mmo::data {

DataResult<PlayerRecord> MemoryPlayerRepository::LoadByAccount(
    const std::string& account) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto player = players_.find(account);
    if (player == players_.end()) {
        return DataResult<PlayerRecord>::NotFound();
    }
    return DataResult<PlayerRecord>::Success(player->second);
}

OperationResult MemoryPlayerRepository::Save(const PlayerRecord& player) {
    if (player.player_id == 0 || player.account.empty() ||
        player.nickname.empty()) {
        return OperationResult::Failure(
            DataStatus::kInvalidArgument, "invalid player record");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    players_[player.account] = player;
    return OperationResult::Success();
}

OperationResult MemoryPlayerRepository::DeleteByAccount(
    const std::string& account) {
    std::lock_guard<std::mutex> lock(mutex_);
    players_.erase(account);
    return OperationResult::Success();
}

OperationResult MemorySessionStore::Store(
    const SessionRecord& session,
    std::chrono::seconds ttl) {
    if (session.player_id == 0 || session.token.empty() || ttl.count() <= 0) {
        return OperationResult::Failure(
            DataStatus::kInvalidArgument, "invalid session");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[session.player_id] =
        Entry{session, std::chrono::steady_clock::now() + ttl};
    return OperationResult::Success();
}

DataResult<SessionRecord> MemorySessionStore::Load(std::uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto session = sessions_.find(player_id);
    if (session == sessions_.end()) {
        return DataResult<SessionRecord>::NotFound();
    }
    if (std::chrono::steady_clock::now() >= session->second.expires_at) {
        sessions_.erase(session);
        return DataResult<SessionRecord>::NotFound();
    }
    return DataResult<SessionRecord>::Success(session->second.session);
}

OperationResult MemorySessionStore::Remove(std::uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(player_id);
    return OperationResult::Success();
}

}  // namespace mmo::data
