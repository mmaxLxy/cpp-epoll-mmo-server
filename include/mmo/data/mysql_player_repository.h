#pragma once

#include "mmo/data/player_repository.h"

#include <cstdint>
#include <string>

namespace mmo::data {

struct MySqlConfig {
    std::string host = "127.0.0.1";
    unsigned int port = 3306;
    std::string user;
    std::string password;
    std::string database;
    unsigned int connect_timeout_seconds = 3;
};

class MySqlPlayerRepository final : public PlayerRepository {
public:
    explicit MySqlPlayerRepository(MySqlConfig config);

    OperationResult EnsureSchema();
    DataResult<PlayerRecord> LoadByAccount(
        const std::string& account) override;
    OperationResult Save(const PlayerRecord& player) override;
    OperationResult DeleteByAccount(const std::string& account) override;

private:
    MySqlConfig config_;
};

}  // namespace mmo::data
