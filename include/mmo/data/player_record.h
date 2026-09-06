#pragma once

#include <cstdint>
#include <string>

namespace mmo::data {

struct PlayerRecord {
    std::uint64_t player_id = 0;
    std::string account;
    std::string nickname;
    double position_x = 0.0;
    double position_y = 0.0;
    double position_z = 0.0;
    std::int64_t last_login_unix = 0;

    bool operator==(const PlayerRecord& other) const noexcept {
        return player_id == other.player_id && account == other.account &&
               nickname == other.nickname &&
               position_x == other.position_x &&
               position_y == other.position_y &&
               position_z == other.position_z &&
               last_login_unix == other.last_login_unix;
    }
};

}  // namespace mmo::data
