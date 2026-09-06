#include "mmo/game/aoi_world.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace mmo::game {

AoiWorld::AoiWorld(AoiConfig config) : config_(config) {
    if (!(config_.min_x < config_.max_x) ||
        !(config_.min_y < config_.max_y) ||
        config_.columns == 0 || config_.rows == 0) {
        throw std::invalid_argument("invalid AOI world configuration");
    }

    cell_width_ = (config_.max_x - config_.min_x) /
                  static_cast<double>(config_.columns);
    cell_height_ = (config_.max_y - config_.min_y) /
                   static_cast<double>(config_.rows);
    grids_.resize(config_.columns * config_.rows);
}

bool AoiWorld::AddPlayer(PlayerId player_id, Position position) {
    if (Contains(player_id)) {
        return false;
    }
    const auto grid_index = GridIndexFor(position);
    if (!grid_index.has_value()) {
        return false;
    }

    players_.emplace(player_id, PlayerState{position, *grid_index});
    grids_[*grid_index].insert(player_id);
    return true;
}

bool AoiWorld::RemovePlayer(PlayerId player_id) {
    const auto player = players_.find(player_id);
    if (player == players_.end()) {
        return false;
    }

    grids_[player->second.grid_index].erase(player_id);
    players_.erase(player);
    return true;
}

std::optional<ViewDelta> AoiWorld::MovePlayer(
    PlayerId player_id,
    Position new_position) {
    const auto player = players_.find(player_id);
    const auto new_grid_index = GridIndexFor(new_position);
    if (player == players_.end() || !new_grid_index.has_value()) {
        return std::nullopt;
    }

    const auto old_visible = VisiblePlayers(player_id).value();
    const std::size_t old_grid_index = player->second.grid_index;

    if (old_grid_index != *new_grid_index) {
        grids_[old_grid_index].erase(player_id);
        grids_[*new_grid_index].insert(player_id);
    }
    player->second.position = new_position;
    player->second.grid_index = *new_grid_index;

    const auto new_visible = VisiblePlayers(player_id).value();
    ViewDelta delta;
    std::set_difference(
        new_visible.begin(), new_visible.end(),
        old_visible.begin(), old_visible.end(),
        std::inserter(delta.entered, delta.entered.end()));
    std::set_difference(
        old_visible.begin(), old_visible.end(),
        new_visible.begin(), new_visible.end(),
        std::inserter(delta.left, delta.left.end()));
    std::set_intersection(
        old_visible.begin(), old_visible.end(),
        new_visible.begin(), new_visible.end(),
        std::inserter(delta.stayed, delta.stayed.end()));
    return delta;
}

std::optional<std::set<PlayerId>> AoiWorld::VisiblePlayers(
    PlayerId player_id) const {
    const auto player = players_.find(player_id);
    if (player == players_.end()) {
        return std::nullopt;
    }

    auto visible = VisiblePlayersAt(player->second.position);
    visible.erase(player_id);
    return visible;
}

bool AoiWorld::Contains(PlayerId player_id) const noexcept {
    return players_.find(player_id) != players_.end();
}

std::size_t AoiWorld::PlayerCount() const noexcept {
    return players_.size();
}

std::optional<std::size_t> AoiWorld::GridIndexFor(
    Position position) const noexcept {
    if (position.x < config_.min_x || position.x >= config_.max_x ||
        position.y < config_.min_y || position.y >= config_.max_y) {
        return std::nullopt;
    }

    const auto column = static_cast<std::size_t>(
        (position.x - config_.min_x) / cell_width_);
    const auto row = static_cast<std::size_t>(
        (position.y - config_.min_y) / cell_height_);
    if (column >= config_.columns || row >= config_.rows) {
        return std::nullopt;
    }
    return row * config_.columns + column;
}

std::set<PlayerId> AoiWorld::VisiblePlayersAt(Position position) const {
    std::set<PlayerId> visible;
    const auto grid_index = GridIndexFor(position);
    if (!grid_index.has_value()) {
        return visible;
    }

    for (const auto neighbor : NeighborGridIndices(*grid_index)) {
        visible.insert(grids_[neighbor].begin(), grids_[neighbor].end());
    }
    return visible;
}

std::vector<std::size_t> AoiWorld::NeighborGridIndices(
    std::size_t grid_index) const {
    const std::size_t center_row = grid_index / config_.columns;
    const std::size_t center_column = grid_index % config_.columns;
    std::vector<std::size_t> indices;
    indices.reserve(9);

    for (int row_offset = -1; row_offset <= 1; ++row_offset) {
        const auto row = static_cast<long long>(center_row) + row_offset;
        if (row < 0 || row >= static_cast<long long>(config_.rows)) {
            continue;
        }
        for (int column_offset = -1; column_offset <= 1; ++column_offset) {
            const auto column =
                static_cast<long long>(center_column) + column_offset;
            if (column < 0 ||
                column >= static_cast<long long>(config_.columns)) {
                continue;
            }
            indices.push_back(
                static_cast<std::size_t>(row) * config_.columns +
                static_cast<std::size_t>(column));
        }
    }
    return indices;
}

}  // namespace mmo::game
