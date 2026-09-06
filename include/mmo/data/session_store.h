#pragma once

#include "mmo/data/data_result.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace mmo::data {

struct SessionRecord {
    std::uint64_t player_id = 0;
    std::string token;

    bool operator==(const SessionRecord& other) const noexcept {
        return player_id == other.player_id && token == other.token;
    }
};

class SessionStore {
public:
    virtual ~SessionStore() = default;

    virtual OperationResult Store(
        const SessionRecord& session,
        std::chrono::seconds ttl) = 0;
    virtual DataResult<SessionRecord> Load(std::uint64_t player_id) = 0;
    virtual OperationResult Remove(std::uint64_t player_id) = 0;
};

}  // namespace mmo::data
