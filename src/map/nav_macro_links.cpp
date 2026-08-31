#include "map/nav_macro_links.hpp"

#include "physics/components.hpp"
#include "physics/physics_world.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace slopengine {

namespace {

constexpr float kSweepFixedDt = 1.0f / 60.0f;
constexpr float kSweepStuckEps = 0.02f;
constexpr float kSweepStuckTimeout = 2.0f;
constexpr float kSweepArriveRadius = 0.25f;
constexpr int kSweepTicksPerLeaf = 90;
constexpr int kSweepMinTicks = 300;
constexpr int kSweepMaxTicks = 1200;

bool finiteVec3(Vector3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

std::uint64_t nextSweepCharacterId() {
    static std::uint64_t counter = 0;
    return 0xFFFF'0000'0000'0000ull + (counter++);
}

Vector3 physicsPositionToVector3(JPH::RVec3 pos) {
    return {static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()), static_cast<float>(pos.GetZ())};
}

} // namespace

std::optional<NavMacroLink> validateMacroChain(
    PhysicsWorld& physics,
    const MapNavigation& nav,
    const BspTree& tree,
    const NavMacroChainCandidate& candidate,
    const NavMovementProfile& profile) {
    const std::vector<int> path = findLeafPath(nav, candidate.entryLeaf, candidate.exitLeaf);
    if (path.size() < 2) {
        return std::nullopt;
    }

    const std::unordered_set<int> clusterSet(candidate.leaves.begin(), candidate.leaves.end());
    for (std::size_t i = 1; i + 1 < path.size(); ++i) {
        if (clusterSet.find(path[i]) == clusterSet.end()) {
            return std::nullopt;
        }
    }

    const Vector3 entryPos = nav.leafCentroids[static_cast<std::size_t>(candidate.entryLeaf)];
    const Vector3 exitPos = nav.leafCentroids[static_cast<std::size_t>(candidate.exitLeaf)];

    const std::vector<Vector3> waypoints = leafPathToWaypoints(nav, path, exitPos, false, 0.0f, &entryPos);
    if (waypoints.empty()) {
        return std::nullopt;
    }
    std::vector<int> waypointToLeaf;
    buildWaypointToLeaf(path, candidate.exitLeaf, waypointToLeaf);

    CharacterMotor motor{};
    motor.radius = profile.radius;
    motor.stepHeight = profile.stepHeight;
    motor.moveMode = CharacterMoveMode::Slide;

    const std::uint64_t id = nextSweepCharacterId();
    const float spawnY = nav.leafFloorY[static_cast<std::size_t>(candidate.entryLeaf)] + 0.05f;
    physics.createCharacter(id, entryPos.x, spawnY, entryPos.z, motor);

    const int maxTicks = std::clamp(
        static_cast<int>(candidate.leaves.size()) * kSweepTicksPerLeaf, kSweepMinTicks, kSweepMaxTicks);

    int waypointIndex = 0;
    Vector3 lastPos{entryPos.x, spawnY, entryPos.z};
    float stuckTime = 0.0f;
    bool success = false;

    for (int tick = 0; tick < maxTicks; ++tick) {
        const Vector3 pos = physicsPositionToVector3(physics.characterPosition(id));
        if (!finiteVec3(pos)) {
            break;
        }
        const int agentLeaf = pointLeaf(tree, pos);

        while (waypointIndex < static_cast<int>(waypoints.size()) &&
               navWaypointCompleted(
                   waypoints, waypointToLeaf, path, pos, agentLeaf, waypointIndex, kSweepArriveRadius)) {
            ++waypointIndex;
        }
        if (waypointIndex >= static_cast<int>(waypoints.size())) {
            success = physics.characterSupported(id) && agentLeaf == candidate.exitLeaf;
            break;
        }

        const Vector3& target = waypoints[static_cast<std::size_t>(waypointIndex)];
        Vector3 dir{target.x - pos.x, 0.0f, target.z - pos.z};
        const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        if (len > 1.0e-4f) {
            dir.x /= len;
            dir.z /= len;
        } else {
            dir = {};
        }
        motor.wishX = dir.x;
        motor.wishZ = dir.z;

        CharacterStep step{};
        step.id = id;
        step.motor = &motor;
        physics.update(kSweepFixedDt, {step});

        const float dx = pos.x - lastPos.x;
        const float dy = pos.y - lastPos.y;
        const float dz = pos.z - lastPos.z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) < kSweepStuckEps) {
            stuckTime += kSweepFixedDt;
            if (stuckTime > kSweepStuckTimeout) {
                break;
            }
        } else {
            stuckTime = 0.0f;
        }
        lastPos = pos;
    }

    physics.destroyCharacter(id);

    if (!success) {
        return std::nullopt;
    }

    float cost = 0.0f;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        if (const NavPortalLink* link = portalLinkBetween(nav, path[i], path[i + 1])) {
            cost += link->cost;
        }
    }

    NavMacroLink result;
    result.entryLeaf = candidate.entryLeaf;
    result.exitLeaf = candidate.exitLeaf;
    result.swallowedLeaves = candidate.leaves;
    result.cost = cost;
    result.innerWaypoints = waypoints;
    result.innerWaypointToLeaf = std::move(waypointToLeaf);
    return result;
}

MapNavMacroLinks buildMapNavMacroLinks(
    PhysicsWorld& physics,
    const MapNavigation& nav,
    const BspTree& tree,
    const std::vector<NavMovementProfile>& profiles) {
    MapNavMacroLinks result;
    const std::vector<NavMacroChainCandidate> candidates = findMacroChainCandidates(nav, tree);

    for (const NavMovementProfile& profile : profiles) {
        std::vector<NavMacroLink> links;
        for (const NavMacroChainCandidate& candidate : candidates) {
            if (std::optional<NavMacroLink> link = validateMacroChain(physics, nav, tree, candidate, profile)) {
                links.push_back(std::move(*link));
            }
        }
        if (!links.empty()) {
            result.byProfile.emplace_back(profile, std::move(links));
        }
    }

    return result;
}

}
