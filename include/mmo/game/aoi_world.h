#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mmo::game {

using PlayerId = std::uint64_t;

struct Position {
    double x = 0.0;
    double y = 0.0;
};

struct AoiConfig {
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;
    std::size_t columns = 0;
    std::size_t rows = 0;
};

struct ViewDelta {
    std::set<PlayerId> entered;
    std::set<PlayerId> left;
    std::set<PlayerId> stayed;
};

// Grid AOI using the player's current cell plus its eight neighboring cells.
// The world owns player positions; returned sets never include the queried player.
class AoiWorld {
public:
    explicit AoiWorld(AoiConfig config);

    bool AddPlayer(PlayerId player_id, Position position);
    bool RemovePlayer(PlayerId player_id);
    std::optional<ViewDelta> MovePlayer(
        PlayerId player_id,
        Position new_position);

    std::optional<std::set<PlayerId>> VisiblePlayers(
        PlayerId player_id) const;

    bool Contains(PlayerId player_id) const noexcept;
    std::size_t PlayerCount() const noexcept;

private:
    struct PlayerState {
        Position position;
        std::size_t grid_index = 0;
    };

    std::optional<std::size_t> GridIndexFor(Position position) const noexcept;
    std::set<PlayerId> VisiblePlayersAt(Position position) const;
    std::vector<std::size_t> NeighborGridIndices(
        std::size_t grid_index) const;

    AoiConfig config_;
    double cell_width_ = 0.0;
    double cell_height_ = 0.0;
    std::vector<std::unordered_set<PlayerId>> grids_;
    std::unordered_map<PlayerId, PlayerState> players_;
};

}  // namespace mmo::game
