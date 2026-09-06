#include "mmo/game/aoi_world.h"

#include "test_support.h"

#include <set>

namespace {

using mmo::game::AoiConfig;
using mmo::game::AoiWorld;
using mmo::game::PlayerId;
using mmo::game::Position;

AoiWorld MakeWorld() {
    return AoiWorld(AoiConfig{0.0, 50.0, 0.0, 50.0, 5, 5});
}

void NearbyQueryUsesNineGridAndExcludesSelf() {
    auto world = MakeWorld();
    CHECK_TRUE(world.AddPlayer(1, Position{25.0, 25.0}));
    CHECK_TRUE(world.AddPlayer(2, Position{35.0, 25.0}));
    CHECK_TRUE(world.AddPlayer(3, Position{5.0, 5.0}));

    const auto visible = world.VisiblePlayers(1);
    CHECK_TRUE(visible.has_value());
    CHECK_EQ(*visible, std::set<PlayerId>({2}));
    CHECK_TRUE(visible->find(1) == visible->end());
}

void CrossGridMoveProducesExactViewDelta() {
    auto world = MakeWorld();
    CHECK_TRUE(world.AddPlayer(1, Position{15.0, 15.0}));
    CHECK_TRUE(world.AddPlayer(2, Position{5.0, 15.0}));
    CHECK_TRUE(world.AddPlayer(3, Position{35.0, 15.0}));
    CHECK_TRUE(world.AddPlayer(4, Position{25.0, 15.0}));

    const auto delta = world.MovePlayer(1, Position{25.0, 15.0});
    CHECK_TRUE(delta.has_value());
    CHECK_EQ(delta->entered, std::set<PlayerId>({3}));
    CHECK_EQ(delta->left, std::set<PlayerId>({2}));
    CHECK_EQ(delta->stayed, std::set<PlayerId>({4}));
    CHECK_TRUE(delta->entered.find(1) == delta->entered.end());
    CHECK_TRUE(delta->left.find(1) == delta->left.end());
}

void SameGridMoveDoesNotChangeViewMembership() {
    auto world = MakeWorld();
    CHECK_TRUE(world.AddPlayer(1, Position{11.0, 11.0}));
    CHECK_TRUE(world.AddPlayer(2, Position{21.0, 11.0}));

    const auto delta = world.MovePlayer(1, Position{19.0, 19.0});
    CHECK_TRUE(delta.has_value());
    CHECK_TRUE(delta->entered.empty());
    CHECK_TRUE(delta->left.empty());
    CHECK_EQ(delta->stayed, std::set<PlayerId>({2}));
}

void BoundaryRulesAreStable() {
    auto world = MakeWorld();
    CHECK_TRUE(world.AddPlayer(1, Position{0.0, 0.0}));
    CHECK_TRUE(world.AddPlayer(2, Position{9.99, 9.99}));
    CHECK_FALSE(world.AddPlayer(3, Position{50.0, 49.0}));
    CHECK_FALSE(world.AddPlayer(4, Position{-0.01, 0.0}));

    const auto visible = world.VisiblePlayers(1);
    CHECK_TRUE(visible.has_value());
    CHECK_EQ(*visible, std::set<PlayerId>({2}));
}

void InvalidMovePreservesExistingMembership() {
    auto world = MakeWorld();
    CHECK_TRUE(world.AddPlayer(1, Position{15.0, 15.0}));
    CHECK_TRUE(world.AddPlayer(2, Position{25.0, 15.0}));

    CHECK_FALSE(world.MovePlayer(1, Position{50.0, 15.0}).has_value());
    const auto visible = world.VisiblePlayers(1);
    CHECK_TRUE(visible.has_value());
    CHECK_EQ(*visible, std::set<PlayerId>({2}));
}

void DuplicateAndRemovalOperationsAreConsistent() {
    auto world = MakeWorld();
    CHECK_TRUE(world.AddPlayer(1, Position{15.0, 15.0}));
    CHECK_FALSE(world.AddPlayer(1, Position{25.0, 15.0}));
    CHECK_EQ(world.PlayerCount(), 1U);
    CHECK_TRUE(world.RemovePlayer(1));
    CHECK_FALSE(world.RemovePlayer(1));
    CHECK_FALSE(world.Contains(1));
    CHECK_EQ(world.PlayerCount(), 0U);
}

}  // namespace

int main() {
    return test_support::Run({
        {"nearby query uses nine-grid and excludes self",
         NearbyQueryUsesNineGridAndExcludesSelf},
        {"cross-grid move produces exact view delta",
         CrossGridMoveProducesExactViewDelta},
        {"same-grid move preserves view membership",
         SameGridMoveDoesNotChangeViewMembership},
        {"map boundary rules are stable", BoundaryRulesAreStable},
        {"invalid move preserves membership", InvalidMovePreservesExistingMembership},
        {"duplicate and removal operations are consistent",
         DuplicateAndRemovalOperationsAreConsistent},
    });
}
