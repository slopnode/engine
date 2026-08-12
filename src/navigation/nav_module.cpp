#include "navigation/nav_module.hpp"

#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "map/nav_graph.hpp"
#include "map/pvs.hpp"
#include "navigation/nav_components.hpp"
#include "physics/components.hpp"
#include "physics/rigid_mover.hpp"
#include "render/components.hpp"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace slopengine {

namespace {

constexpr float kStuckDistEps = 0.02f;

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
    agent.lastWpHorizDist = -1.0f;
}

void replanAgent(
    flecs::world& world,
    NavigationAgent& agent,
    Vector3 agentPos,
    Vector3 goalPos,
    bool preserveProgress,
    float maxClimb) {
    if (!world.has<MapNavigation>() || !world.has<MapBsp>()) {
        clearNavPath(agent);
        return;
    }

    const MapNavigation& nav = world.get<MapNavigation>();
    const BspTree& tree = world.get<MapBsp>().tree;
    const int fromLeaf = sampleNavLeaf(tree, agentPos);
    const int toLeaf = sampleNavLeaf(tree, goalPos);

    if (fromLeaf < 0 || toLeaf < 0) {
        // A momentary leaf-sample miss (e.g. an actor mid-step during a stair
        // climb) isn't proof the goal is unreachable. Keep following the last
        // known-good path rather than dropping it and forcing the script's
        // wall-blind straight-line fallback; the next scheduled replan will
        // try again once the sample recovers.
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
    const float oldLastWpHorizDist = agent.lastWpHorizDist;

    const std::vector<int> leafPath = findLeafPath(
        nav,
        fromLeaf,
        toLeaf,
        [&world](const std::string& doorBrushId) { return isDoorPortalOpen(world, doorBrushId); },
        maxClimb);
    if (leafPath.empty()) {
        if (agent.leafPath.empty()) {
            clearNavPath(agent);
        }
        return;
    }

    const std::vector<Vector3> waypoints =
        leafPathToWaypoints(nav, leafPath, goalPos, agent.flyer);
    std::vector<int> waypointToLeaf;
    buildWaypointToLeaf(leafPath, toLeaf, waypointToLeaf);
    const int resumeIndex = findResumeWaypointIndex(
        waypoints,
        waypointToLeaf,
        leafPath,
        agentPos,
        fromLeaf,
        agent.arriveRadius);

    agent.leafPath = leafPath;
    agent.waypoints = waypoints;
    agent.waypointToLeaf = std::move(waypointToLeaf);

    const bool routeUnchanged =
        preserveProgress &&
        navLeafPathRouteUnchanged(oldLeafPath, fromLeaf, leafPath);

    if (preserveProgress && !oldLeafPath.empty()) {
        agent.waypointIndex = std::max(oldWaypointIndex, resumeIndex);
        if (routeUnchanged) {
            agent.stuckTimer = oldStuckTimer;
            agent.lastWpHorizDist = oldLastWpHorizDist;
        } else {
            agent.stuckTimer = 0.0f;
            agent.lastWpHorizDist = -1.0f;
        }
    } else {
        agent.waypointIndex = resumeIndex;
        agent.stuckTimer = 0.0f;
        agent.lastWpHorizDist = -1.0f;
    }

    agent.lastGoalPos = goalPos;
    agent.haveLastGoalPos = true;
    agent.forceReplan = false;
}

void advanceWaypoints(
    NavigationAgent& agent,
    Vector3 agentPos,
    int agentLeaf) {
    while (agent.waypointIndex < static_cast<int>(agent.waypoints.size()) &&
           navWaypointCompleted(
               agent.waypoints,
               agent.waypointToLeaf,
               agent.leafPath,
               agentPos,
               agentLeaf,
               agent.waypointIndex,
               agent.arriveRadius)) {
        ++agent.waypointIndex;
    }
}

void updateStuckSkip(NavigationAgent& agent, Vector3 agentPos, float dt) {
    if (agent.waypointIndex < 0 ||
        agent.waypointIndex >= static_cast<int>(agent.waypoints.size())) {
        agent.stuckTimer = 0.0f;
        agent.lastWpHorizDist = -1.0f;
        return;
    }

    const Vector3& wp = agent.waypoints[static_cast<std::size_t>(agent.waypointIndex)];
    const float dist = navHorizontalDist(agentPos, wp);
    if (agent.lastWpHorizDist >= 0.0f && dist >= agent.lastWpHorizDist - kStuckDistEps) {
        agent.stuckTimer += dt;
    } else {
        agent.stuckTimer = 0.0f;
    }
    agent.lastWpHorizDist = dist;

    const int last = static_cast<int>(agent.waypoints.size()) - 1;
    if (agent.stuckTimer >= agent.stuckSkipTime) {
        if (agent.waypointIndex < last) {
            ++agent.waypointIndex;
        } else {
            agent.forceReplan = true;
        }
        agent.stuckTimer = 0.0f;
        agent.lastWpHorizDist = -1.0f;
    }
}

bool needsReplan(
    flecs::world& world,
    const MapNavigation& nav,
    NavigationAgent& agent,
    Vector3 goalPos,
    int agentLeaf,
    int goalLeaf,
    float dt,
    float maxClimb) {
    if (agent.forceReplan) {
        return true;
    }
    if (agent.waypoints.empty()) {
        return true;
    }
    if (agentLeaf >= 0 && agentLeaf != agent.agentLeaf) {
        if (goalLeaf >= 0) {
            const std::vector<int> newPath = findLeafPath(
                nav,
                agentLeaf,
                goalLeaf,
                [&world](const std::string& doorBrushId) {
                    return isDoorPortalOpen(world, doorBrushId);
                },
                maxClimb);
            if (navLeafPathRouteUnchanged(agent.leafPath, agentLeaf, newPath)) {
                agent.agentLeaf = agentLeaf;
                return false;
            }
        }
        return true;
    }
    if (goalMovedEnough(agent, goalPos)) {
        return true;
    }
    agent.replanTimer -= dt;
    if (agent.replanTimer <= 0.0f) {
        return true;
    }
    return false;
}

} // namespace

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
    replanAgent(world, agent, actorFeet(entity, motor), goalPos, false, agentMaxClimb(agent, motor));
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

            updateStuckSkip(agent, agentFeetPos, dt);
            advanceWaypoints(agent, agentFeetPos, agentLeaf);

            const float maxClimb = agentMaxClimb(agent, motor);
            if (needsReplan(worldRef, nav, agent, goalPos, agentLeaf, goalLeaf, dt, maxClimb)) {
                const bool preserveProgress =
                    !agent.forceReplan && !agent.waypoints.empty();
                replanAgent(worldRef, agent, agentFeetPos, goalPos, preserveProgress, maxClimb);
                agent.replanTimer = agent.replanInterval;
                agent.agentLeaf = agentLeaf;
            }
        });
}

}
