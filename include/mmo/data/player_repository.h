#pragma once

#include "mmo/data/data_result.h"
#include "mmo/data/player_record.h"

#include <string>

namespace mmo::data {

class PlayerRepository {
public:
    virtual ~PlayerRepository() = default;

    virtual DataResult<PlayerRecord> LoadByAccount(
        const std::string& account) = 0;
    virtual OperationResult Save(const PlayerRecord& player) = 0;
    virtual OperationResult DeleteByAccount(const std::string& account) = 0;
};

}  // namespace mmo::data
