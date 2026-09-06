#pragma once

#include "mmo/data/session_store.h"

#include <string>

namespace mmo::data {

struct RedisConfig {
    std::string host = "127.0.0.1";
    int port = 6379;
    std::string key_prefix = "mmo:session:";
    std::chrono::milliseconds connect_timeout{1000};
};

class RedisSessionStore final : public SessionStore {
public:
    explicit RedisSessionStore(RedisConfig config);

    OperationResult Store(
        const SessionRecord& session,
        std::chrono::seconds ttl) override;
    DataResult<SessionRecord> Load(std::uint64_t player_id) override;
    OperationResult Remove(std::uint64_t player_id) override;

private:
    std::string KeyFor(std::uint64_t player_id) const;

    RedisConfig config_;
};

}  // namespace mmo::data
