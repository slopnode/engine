#pragma once

#include "map/nav_graph.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace slopengine {

struct PhysicsWorld;

struct NavMovementProfile {
    float radius = 0.3f;
    float stepHeight = 0.5f;

    bool operator==(const NavMovementProfile& other) const {
        return radius == other.radius && stepHeight == other.stepHeight;
    }
};

struct NavMacroLink {
    int entryLeaf = -1;
    int exitLeaf = -1;
    std::vector<int> swallowedLeaves;
    float cost = 0.0f;
    std::vector<Vector3> innerWaypoints;
    std::vector<int> innerWaypointToLeaf;
};

struct MapNavMacroLinks {
    std::vector<std::pair<NavMovementProfile, std::vector<NavMacroLink>>> byProfile;
};

/** Sweep-tests whether @p profile can walk @p candidate end to end by literally driving a
 *  throwaway character through it via @p physics's real Slide-mode stepping (the same path
 *  every ground-moving monster ships with), following the same funnel waypoints/arrival
 *  logic a real NavigationAgent would use. Returns nullopt on failure (stuck, timed out, or
 *  a shorter route already exists elsewhere in @p nav bypassing this candidate). Must only
 *  be called while @p physics has no other characters that could interfere (see
 *  buildMapNavMacroLinks's call site in assembleMapScene for the intended safe window). */
std::optional<NavMacroLink> validateMacroChain(
    PhysicsWorld& physics,
    const MapNavigation& nav,
    const BspTree& tree,
    const NavMacroChainCandidate& candidate,
    const NavMovementProfile& profile);

/** Runs findMacroChainCandidates then validateMacroChain for every candidate against every
 *  profile in @p profiles, collecting the successes. */
MapNavMacroLinks buildMapNavMacroLinks(
    PhysicsWorld& physics,
    const MapNavigation& nav,
    const BspTree& tree,
    const std::vector<NavMovementProfile>& profiles);

}
