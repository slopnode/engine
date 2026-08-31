#include "navigation/nav_module.hpp"

#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "map/nav_graph.hpp"
#include "map/pvs.hpp"
#include "navigation/nav_components.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/rigid_mover.hpp"
#include "render/components.hpp"
#include "ui/ui_state.hpp"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <unordered_map>

namespace slopengine {

namespace {

constexpr float kStuckDistEps = 0.02f;

// All NAV diagnostic logging is gated behind the same "show nav paths" debug
// toggle used to draw the waypoint overlay, so it's only noisy when someone
// is actually looking at this. Every line is prefixed "NAV[label]" so it can
// be grepped straight out of the console/log.
bool navDebugLogEnabled(flecs::world& world) {
    return world.has<DebugUiState>() && world.get<DebugUiState>().showNavPaths;
}

std::string navDebugLabel(flecs::entity entity) {
    const char* name = entity.name();
    if (name != nullptr && name[0] != '\0') {
        return name;
    }
    return std::to_string(static_cast<std::uint64_t>(entity.id()));
}

void logNavPhysicsTick(
    flecs::world& world,
    flecs::entity entity,
    const NavigationAgent& agent,
    Vector3 agentPos,
    int agentLeaf,
    Vector3 goalPos,
    int goalLeaf) {
    if (!world.has<PhysicsContext>()) {
        return;
    }
    PhysicsWorld* physics = world.get<PhysicsContext>().world;
    if (physics == nullptr) {
        return;
    }
    const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
    if (!physics->hasCharacter(id)) {
        return;
    }

    const JPH::RVec3 rpos = physics->characterPosition(id);
    const JPH::Vec3 rvel = physics->characterVelocity(id);
    const bool supported = physics->characterSupported(id);

    char wpInfo[128] = "none";
    if (agent.waypointIndex >= 0 && agent.waypointIndex < static_cast<int>(agent.waypoints.size())) {
        const Vector3& wp = agent.waypoints[static_cast<std::size_t>(agent.waypointIndex)];
        const float distXZ = navHorizontalDist(agentPos, wp);
        std::snprintf(wpInfo, sizeof(wpInfo), "(%.2f,%.2f,%.2f) distXZ=%.2f", wp.x, wp.y, wp.z, distXZ);
    }

    TraceLog(
        LOG_INFO,
        "NAV[%s] tick pos=(%.3f,%.3f,%.3f) physPos=(%.3f,%.3f,%.3f) leaf=%d onPath=%d "
        "vel=(%.2f,%.2f,%.2f) supported=%d wpIdx=%d/%zu wpTarget=%s stuckTimer=%.2f "
        "goal=(%.2f,%.2f,%.2f) goalLeaf=%d",
        navDebugLabel(entity).c_str(),
        agentPos.x, agentPos.y, agentPos.z,
        static_cast<float>(rpos.GetX()), static_cast<float>(rpos.GetY()), static_cast<float>(rpos.GetZ()),
        agentLeaf,
        navFindLeafInPath(agent.leafPath, agentLeaf) >= 0 ? 1 : 0,
        static_cast<float>(rvel.GetX()), static_cast<float>(rvel.GetY()), static_cast<float>(rvel.GetZ()),
        supported ? 1 : 0,
        agent.waypointIndex, agent.waypoints.size(),
        wpInfo,
        agent.stuckTimer,
        goalPos.x, goalPos.y, goalPos.z,
        goalLeaf);
}

Vector3 actorFeet(flecs::entity entity, const CharacterMotor& motor) {
    if (entity.has<Lens>()) {
        const Vector3 eye = entity.get<Lens>().camera.position;
        return {eye.x, eye.y - motor.eyeHeight, eye.z};
    }
    if (entity.has<LocalTransformation>()) {
        return entity.get<LocalTransformation>().position;
    }
    return {};
}

Vector3 resolveGoalPos(flecs::world& world, const NavigationAgent& agent) {
    if (!agent.goalEntityId.empty()) {
        flecs::entity goal = world.lookup(agent.goalEntityId.c_str());
        if (goal.is_valid() && goal.has<CharacterMotor>()) {
            return actorFeet(goal, goal.get<CharacterMotor>());
        }
        if (goal.is_valid() && goal.has<LocalTransformation>()) {
            return goal.get<LocalTransformation>().position;
        }
    }
    return agent.goalPos;
}

Vector3 navSamplePoint(Vector3 feet, const CharacterMotor& motor, bool flyer) {
    if (!flyer) {
        return feet;
    }
    return {feet.x, feet.y + characterCenterOffset(motor), feet.z};
}

Vector3 navAgentSamplePoint(flecs::entity entity, const CharacterMotor& motor, bool flyer) {
    return navSamplePoint(actorFeet(entity, motor), motor, flyer);
}

int sampleNavLeaf(const BspTree& tree, Vector3 point) {
    return pvsSampleLeaf(tree, point);
}

// Fails open (treats an unresolved door as passable) rather than permanently
// stranding a path plan on a data mismatch between the compiled portal graph
// and the spawned door entities.
bool isDoorPortalOpen(flecs::world& world, const std::string& doorBrushId) {
    flecs::entity door = world.lookup(doorBrushId.c_str());
    if (!door.is_valid() || !door.has<RigidMover>()) {
        return true;
    }
    return door.get<RigidMover>().target >= 0.5f;
}

float agentMaxClimb(const NavigationAgent& agent, const CharacterMotor& motor) {
    return agent.flyer ? std::numeric_limits<float>::infinity() : motor.stepHeight;
}

// Flyers hover under their own vertical control (see actors.md's hover-height controller) so
// floor drops and water leaves underneath them are not a routing concern -- both resolve to
// their neutral value, same treatment agentMaxClimb already gives flyers.
float agentMaxFall(const NavigationAgent& agent) {
    return agent.flyer ? std::numeric_limits<float>::infinity() : agent.maxFall;
}

float agentWaterCostMultiplier(const NavigationAgent& agent) {
    return agent.flyer ? 1.0f : agent.waterCostMultiplier;
}

float navLateralBiasForEntity(flecs::entity_t id) {
    std::uint64_t h = static_cast<std::uint64_t>(id) * 0x9E3779B97F4A7C15ULL;
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDULL;
    h ^= h >> 33;
    constexpr std::uint64_t kMask = (1ull << 24) - 1;
    const float frac = static_cast<float>(h & kMask) / static_cast<float>(kMask);
    return frac * 2.0f - 1.0f;
}

float agentLateralBias(flecs::entity entity, NavigationAgent& agent) {
    if (!agent.haveLateralBias) {
        agent.lateralBias = navLateralBiasForEntity(entity.id());
        agent.haveLateralBias = true;
    }
    return agent.lateralBias;
}

struct NavFlowFieldKey {
    int goalLeaf = -1;
    float maxClimb = 0.0f;
    float maxFall = 0.0f;
    float waterCostMultiplier = 1.0f;
    bool operator==(const NavFlowFieldKey& other) const {
        return goalLeaf == other.goalLeaf && maxClimb == other.maxClimb &&
            maxFall == other.maxFall && waterCostMultiplier == other.waterCostMultiplier;
    }
};

struct NavFlowFieldKeyHash {
    std::size_t operator()(const NavFlowFieldKey& key) const {
        std::size_t h = std::hash<int>{}(key.goalLeaf);
        const auto mix = [&h](float v) {
            h ^= std::hash<float>{}(v) + 0x9e3779b9u + (h << 6) + (h >> 2);
        };
        mix(key.maxClimb);
        mix(key.maxFall);
        mix(key.waterCostMultiplier);
        return h;
    }
};

struct NavFlowFieldEntry {
    NavFlowField field;
    float refreshTimer = 0.0f;
    std::int64_t refreshFrame = -1;
    std::int64_t lastUsedFrame = -1;
};

struct NavFlowFieldCache {
    std::unordered_map<NavFlowFieldKey, NavFlowFieldEntry, NavFlowFieldKeyHash> entries;
    std::int64_t lastSweepFrame = -1;
};

constexpr float kFlowFieldRefreshInterval = 0.25f;
constexpr std::int64_t kFlowFieldEvictFrames = 240;

const NavFlowField& getOrBuildFlowField(
    flecs::world& world,
    const MapNavigation& nav,
    int goalLeaf,
    float maxClimb,
    float maxFall,
    float waterCostMultiplier) {
    NavFlowFieldCache& cache = world.get_mut<NavFlowFieldCache>();
    const std::int64_t currentFrame = world.get_info()->frame_count_total;

    if (cache.lastSweepFrame != currentFrame) {
        for (auto it = cache.entries.begin(); it != cache.entries.end();) {
            if (currentFrame - it->second.lastUsedFrame > kFlowFieldEvictFrames) {
                it = cache.entries.erase(it);
            } else {
                ++it;
            }
        }
        cache.lastSweepFrame = currentFrame;
    }

    const auto rebuild = [&](NavFlowFieldEntry& entry) {
        entry.field = buildNavFlowField(
            nav,
            goalLeaf,
            [&world](const std::string& doorBrushId) { return isDoorPortalOpen(world, doorBrushId); },
            maxClimb,
            maxFall,
            waterCostMultiplier);
        entry.refreshTimer = kFlowFieldRefreshInterval;
        entry.refreshFrame = currentFrame;
    };

    const NavFlowFieldKey key{goalLeaf, maxClimb, maxFall, waterCostMultiplier};
    auto it = cache.entries.find(key);
    if (it == cache.entries.end()) {
        NavFlowFieldEntry entry;
        rebuild(entry);
        it = cache.entries.emplace(key, std::move(entry)).first;
    } else if (it->second.refreshFrame != currentFrame) {
        it->second.refreshTimer -= GetFrameTime();
        it->second.refreshFrame = currentFrame;
        if (it->second.refreshTimer <= 0.0f) {
            rebuild(it->second);
        }
    }

    it->second.lastUsedFrame = currentFrame;
    return it->second.field;
}

bool goalMovedEnough(const NavigationAgent& agent, Vector3 goalPos) {
    if (!agent.haveLastGoalPos) {
        return true;
    }
    return navHorizontalDist(agent.lastGoalPos, goalPos) >= agent.goalMoveThreshold;
}

void clearNavPath(NavigationAgent& agent) {
    agent.waypoints.clear();
    agent.leafPath.clear();
    agent.waypointToLeaf.clear();
    agent.waypointIndex = 0;
    agent.stuckTimer = 0.0f;
    agent.haveStuckLastPos = false;
}

void replanAgent(
    flecs::world& world,
    flecs::entity entity,
    NavigationAgent& agent,
    Vector3 agentPos,
    Vector3 goalPos,
    bool preserveProgress,
    float maxClimb,
    const char* reason) {
    const bool logNav = navDebugLogEnabled(world);
    if (!world.has<MapNavigation>() || !world.has<MapBsp>()) {
        clearNavPath(agent);
        return;
    }

    const MapNavigation& nav = world.get<MapNavigation>();
    const BspTree& tree = world.get<MapBsp>().tree;
    const int fromLeaf = sampleNavLeaf(tree, agentPos);
    const int toLeaf = sampleNavLeaf(tree, goalPos);

    if (logNav) {
        TraceLog(
            LOG_INFO,
            "NAV[%s] replan reason=%s agentPos=(%.2f,%.2f,%.2f) fromLeaf=%d goalPos=(%.2f,%.2f,%.2f) toLeaf=%d",
            navDebugLabel(entity).c_str(),
            reason,
            agentPos.x, agentPos.y, agentPos.z, fromLeaf,
            goalPos.x, goalPos.y, goalPos.z, toLeaf);
    }

    if (fromLeaf < 0 || toLeaf < 0) {
        // A momentary leaf-sample miss (e.g. an actor mid-step during a stair
        // climb) isn't proof the goal is unreachable. Keep following the last
        // known-good path rather than dropping it and forcing the script's
        // wall-blind straight-line fallback; the next scheduled replan will
        // try again once the sample recovers.
        if (logNav) {
            TraceLog(
                LOG_INFO,
                "NAV[%s] replan aborted: %s leaf sample invalid (fromLeaf=%d toLeaf=%d)%s",
                navDebugLabel(entity).c_str(),
                fromLeaf < 0 ? "agent" : "goal",
                fromLeaf, toLeaf,
                agent.leafPath.empty() ? ", no prior path -> cleared" : ", keeping last known-good path");
        }
        if (agent.leafPath.empty()) {
            clearNavPath(agent);
        }
        return;
    }
    agent.agentLeaf = fromLeaf;
    agent.goalLeaf = toLeaf;

    const std::vector<int> oldLeafPath = agent.leafPath;
    const int oldWaypointIndex = agent.waypointIndex;
    const float oldStuckTimer = agent.stuckTimer;
    const Vector3 oldStuckLastPos = agent.stuckLastPos;
    const bool oldHaveStuckLastPos = agent.haveStuckLastPos;

    const NavFlowField& flowField = getOrBuildFlowField(
        world, nav, toLeaf, maxClimb, agentMaxFall(agent), agentWaterCostMultiplier(agent));
    const std::vector<int> leafPath = flowFieldPathFrom(flowField, fromLeaf);
    if (leafPath.empty()) {
        if (logNav) {
            TraceLog(
                LOG_INFO,
                "NAV[%s] replan found no route from leaf=%d to leaf=%d%s",
                navDebugLabel(entity).c_str(),
                fromLeaf, toLeaf,
                agent.leafPath.empty() ? " -> cleared" : " -> keeping last known-good path");
        }
        if (agent.leafPath.empty()) {
            clearNavPath(agent);
        }
        return;
    }

    const std::vector<Vector3> waypoints = leafPathToWaypoints(
        nav, leafPath, goalPos, agent.flyer, agentLateralBias(entity, agent), &agentPos);
    std::vector<int> waypointToLeaf;
    buildWaypointToLeaf(leafPath, toLeaf, waypointToLeaf);
    const int resumeIndex = findResumeWaypointIndex(
        waypoints,
        waypointToLeaf,
        leafPath,
        agentPos,
        fromLeaf,
        agent.arriveRadius);

    if (logNav) {
        std::string leafPathStr;
        for (std::size_t i = 0; i < leafPath.size(); ++i) {
            if (i != 0) {
                leafPathStr += ",";
            }
            leafPathStr += std::to_string(leafPath[i]);
        }
        TraceLog(
            LOG_INFO,
            "NAV[%s] path leaves=[%s] waypoints=%zu resumeIndex=%d",
            navDebugLabel(entity).c_str(),
            leafPathStr.c_str(),
            waypoints.size(),
            resumeIndex);
        for (std::size_t i = 0; i < waypoints.size(); ++i) {
            const Vector3& wp = waypoints[i];
            if (i + 1 < leafPath.size()) {
                const int sourceLeaf = leafPath[i];
                TraceLog(
                    LOG_INFO,
                    "NAV[%s]   wp[%zu]=(%.2f,%.2f,%.2f) portal leaf %d->%d floorY=%.3f",
                    navDebugLabel(entity).c_str(),
                    i, wp.x, wp.y, wp.z,
                    sourceLeaf, leafPath[i + 1],
                    nav.leafFloorY[static_cast<std::size_t>(sourceLeaf)]);
            } else {
                TraceLog(
                    LOG_INFO,
                    "NAV[%s]   wp[%zu]=(%.2f,%.2f,%.2f) goal",
                    navDebugLabel(entity).c_str(),
                    i, wp.x, wp.y, wp.z);
            }
        }
    }

    agent.leafPath = leafPath;
    agent.waypoints = waypoints;
    agent.waypointToLeaf = std::move(waypointToLeaf);

    const int oldFromIndex = navFindLeafInPath(oldLeafPath, fromLeaf);
    const int newFromIndex = navFindLeafInPath(leafPath, fromLeaf);
    const bool routeUnchanged =
        preserveProgress &&
        navLeafPathSuffixEqual(oldLeafPath, oldFromIndex, leafPath, newFromIndex);

    if (routeUnchanged) {
        agent.waypointIndex = std::clamp(
            oldWaypointIndex - oldFromIndex + newFromIndex, 0, static_cast<int>(waypoints.size()));
        agent.stuckTimer = oldStuckTimer;
        agent.stuckLastPos = oldStuckLastPos;
        agent.haveStuckLastPos = oldHaveStuckLastPos;
    } else {
        agent.waypointIndex = resumeIndex;
        agent.stuckTimer = 0.0f;
        agent.haveStuckLastPos = false;
    }

    agent.lastGoalPos = goalPos;
    agent.haveLastGoalPos = true;
    agent.forceReplan = false;
}

void advanceWaypoints(
    flecs::world& world,
    flecs::entity entity,
    NavigationAgent& agent,
    Vector3 agentPos,
    int agentLeaf) {
    const bool logNav = navDebugLogEnabled(world);
    while (agent.waypointIndex < static_cast<int>(agent.waypoints.size()) &&
           navWaypointCompleted(
               agent.waypoints,
               agent.waypointToLeaf,
               agent.leafPath,
               agentPos,
               agentLeaf,
               agent.waypointIndex,
               agent.arriveRadius)) {
        if (logNav) {
            const Vector3& wp = agent.waypoints[static_cast<std::size_t>(agent.waypointIndex)];
            TraceLog(
                LOG_INFO,
                "NAV[%s] reached wp[%d]=(%.2f,%.2f,%.2f) agentPos=(%.2f,%.2f,%.2f) agentLeaf=%d -> advancing to %d",
                navDebugLabel(entity).c_str(),
                agent.waypointIndex, wp.x, wp.y, wp.z,
                agentPos.x, agentPos.y, agentPos.z,
                agentLeaf,
                agent.waypointIndex + 1);
        }
        ++agent.waypointIndex;
    }
}

// Stuck detection watches the agent's own displacement, not its progress toward
// the current waypoint: distance-to-waypoint can legitimately hold flat for a
// beat while the agent turns to round a corner. Answering that with a skip to
// a waypoint the agent has no line of sight to is what cut corners through
// walls; a real stuck agent instead gets a forced replan, which can only ever
// route it back onto the portal graph.
void updateStuckSkip(
    flecs::world& world,
    flecs::entity entity,
    NavigationAgent& agent,
    Vector3 agentPos,
    float dt) {
    if (agent.waypointIndex < 0 ||
        agent.waypointIndex >= static_cast<int>(agent.waypoints.size())) {
        agent.stuckTimer = 0.0f;
        agent.haveStuckLastPos = false;
        return;
    }

    if (!agent.haveStuckLastPos) {
        agent.stuckLastPos = agentPos;
        agent.haveStuckLastPos = true;
        agent.stuckTimer = 0.0f;
        return;
    }

    // Full 3D distance, not horizontal-only: climbing a stair tread can be
    // almost pure vertical motion over one check window, which a
    // horizontal-only measure would misread as no movement at all.
    const float moved = Vector3Distance(agentPos, agent.stuckLastPos);
    if (moved >= kStuckDistEps) {
        agent.stuckTimer = 0.0f;
        agent.stuckLastPos = agentPos;
        return;
    }

    agent.stuckTimer += dt;
    if (agent.stuckTimer >= agent.stuckSkipTime) {
        if (navDebugLogEnabled(world)) {
            const Vector3& wp = agent.waypoints[static_cast<std::size_t>(agent.waypointIndex)];
            TraceLog(
                LOG_INFO,
                "NAV[%s] STUCK at pos=(%.2f,%.2f,%.2f) target wp[%d]=(%.2f,%.2f,%.2f) moved<%.3f for %.2fs -> forcing replan",
                navDebugLabel(entity).c_str(),
                agentPos.x, agentPos.y, agentPos.z,
                agent.waypointIndex, wp.x, wp.y, wp.z,
                kStuckDistEps,
                agent.stuckTimer);
        }
        agent.forceReplan = true;
        agent.stuckTimer = 0.0f;
        agent.stuckLastPos = agentPos;
    }
}

// Returns the reason a replan is needed, or nullptr if none is.
const char* needsReplan(
    flecs::world& world,
    flecs::entity entity,
    const MapNavigation& nav,
    NavigationAgent& agent,
    Vector3 goalPos,
    int agentLeaf,
    int goalLeaf,
    float dt,
    float maxClimb) {
    const bool logNav = navDebugLogEnabled(world);
    if (agent.forceReplan) {
        return "forceReplan flag set";
    }
    if (agent.waypoints.empty()) {
        return "no current path";
    }
    if (agentLeaf >= 0 && agentLeaf != agent.agentLeaf) {
        // A leaf-sample flicker right at a portal boundary (thin stair-tread
        // leaves are the common case) can report a different leaf than last
        // frame without the agent having left its route. If that leaf is
        // already part of the leaf path being followed, there's nothing to
        // replan — adopt it and let navWaypointCompleted's leaf-membership
        // check do the advancing. Re-deriving a fresh A* path on every such
        // flicker is what was producing a new near-duplicate waypoint each
        // time and stalling the agent oscillating between them.
        if (navFindLeafInPath(agent.leafPath, agentLeaf) >= 0) {
            if (logNav) {
                TraceLog(
                    LOG_INFO,
                    "NAV[%s] leaf changed %d -> %d, already on planned route -> no replan",
                    navDebugLabel(entity).c_str(),
                    agent.agentLeaf, agentLeaf);
            }
            agent.agentLeaf = agentLeaf;
            return nullptr;
        }
        if (goalLeaf >= 0) {
            const NavFlowField& flowField = getOrBuildFlowField(
                world, nav, goalLeaf, maxClimb, agentMaxFall(agent), agentWaterCostMultiplier(agent));
            const std::vector<int> newPath = flowFieldPathFrom(flowField, agentLeaf);
            if (navLeafPathRouteUnchanged(agent.leafPath, agentLeaf, newPath)) {
                if (logNav) {
                    TraceLog(
                        LOG_INFO,
                        "NAV[%s] leaf changed %d -> %d, reroute matches remaining route -> no replan",
                        navDebugLabel(entity).c_str(),
                        agent.agentLeaf, agentLeaf);
                }
                agent.agentLeaf = agentLeaf;
                return nullptr;
            }
        }
        return "agent left the planned route";
    }
    if (goalMovedEnough(agent, goalPos)) {
        return "goal moved";
    }
    agent.replanTimer -= dt;
    if (agent.replanTimer <= 0.0f) {
        return "replan timer elapsed";
    }
    return nullptr;
}

} // namespace

void resetNavFlowFieldCache(flecs::world& world) {
    world.set<NavFlowFieldCache>({});
}

void replanNavigationAgent(flecs::world& world, flecs::entity entity) {
    if (!entity.is_valid() || !entity.has<NavigationAgent>() ||
        !entity.has<LocalTransformation>() || !entity.has<CharacterMotor>()) {
        return;
    }
    if (!world.has<MapNavigation>() || !world.has<MapBsp>()) {
        return;
    }
    NavigationAgent& agent = entity.get_mut<NavigationAgent>();
    if (!agent.enabled || !agent.hasGoal) {
        return;
    }
    const CharacterMotor& motor = entity.get<CharacterMotor>();
    const Vector3 agentPos = navAgentSamplePoint(entity, motor, agent.flyer);
    const Vector3 goalPos = resolveGoalPos(world, agent);
    replanAgent(
        world, entity, agent, actorFeet(entity, motor), goalPos, false, agentMaxClimb(agent, motor),
        "external nav-set-goal call");
    agent.replanTimer = agent.replanInterval;
    agent.agentLeaf = sampleNavLeaf(world.get<MapBsp>().tree, agentPos);
}

void registerNavModule(flecs::world& world) {
    world.component<NavigationAgent>();

    world.system<NavigationAgent, LocalTransformation, CharacterMotor>("NavUpdate")
        .with<Actor>()
        .kind(flecs::OnUpdate)
        .each([](flecs::entity entity, NavigationAgent& agent, LocalTransformation&,
                 CharacterMotor& motor) {
            flecs::world worldRef = entity.world();
            if (isSimulationPaused(worldRef)) {
                return;
            }
            if (!agent.enabled || !agent.hasGoal) {
                return;
            }
            if (!worldRef.has<MapNavigation>() || !worldRef.has<MapBsp>()) {
                return;
            }

            const float dt = GetFrameTime();
            if (dt <= 0.0f) {
                return;
            }

            const MapNavigation& nav = worldRef.get<MapNavigation>();
            const Vector3 agentFeetPos = actorFeet(entity, motor);
            const Vector3 agentSamplePos = navSamplePoint(agentFeetPos, motor, agent.flyer);
            const Vector3 goalPos = resolveGoalPos(worldRef, agent);
            const BspTree& tree = worldRef.get<MapBsp>().tree;
            const int agentLeaf = sampleNavLeaf(tree, agentSamplePos);
            const int goalLeaf = sampleNavLeaf(tree, goalPos);

            updateStuckSkip(worldRef, entity, agent, agentFeetPos, dt);
            advanceWaypoints(worldRef, entity, agent, agentFeetPos, agentLeaf);

            if (navDebugLogEnabled(worldRef)) {
                logNavPhysicsTick(worldRef, entity, agent, agentFeetPos, agentLeaf, goalPos, goalLeaf);
            }

            const float maxClimb = agentMaxClimb(agent, motor);
            if (const char* reason = needsReplan(
                    worldRef, entity, nav, agent, goalPos, agentLeaf, goalLeaf, dt, maxClimb)) {
                const bool preserveProgress =
                    !agent.forceReplan && !agent.waypoints.empty();
                replanAgent(
                    worldRef, entity, agent, agentFeetPos, goalPos, preserveProgress, maxClimb, reason);
                agent.replanTimer = agent.replanInterval;
                agent.agentLeaf = agentLeaf;
            }
        });
}

}
