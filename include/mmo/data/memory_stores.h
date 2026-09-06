#pragma once

#include "mmo/data/player_repository.h"
#include "mmo/data/session_store.h"

#include <chrono>
#include <mutex>
#include <unordered_map>

namespace mmo::data {

class MemoryPlayerRepository final : public PlayerRepository {
public:
    DataResult<PlayerRecord> LoadByAccount(
        const std::string& account) override;
    OperationResult Save(const PlayerRecord& player) override;
    OperationResult DeleteByAccount(const std::string& account) override;

private:
    std::mutex mutex_;
    std::unordered_map<std::string, PlayerRecord> players_;
};

class MemorySessionStore final : public SessionStore {
public:
    OperationResult Store(
        const SessionRecord& session,
        std::chrono::seconds ttl) override;
    DataResult<SessionRecord> Load(std::uint64_t player_id) override;
    OperationResult Remove(std::uint64_t player_id) override;

private:
    struct Entry {
        SessionRecord session;
        std::chrono::steady_clock::time_point expires_at;
    };

    std::mutex mutex_;
    std::unordered_map<std::uint64_t, Entry> sessions_;
};

}  // namespace mmo::data
