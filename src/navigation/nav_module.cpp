#include "navigation/nav_module.hpp"

#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "map/nav_graph.hpp"
#include "map/pvs.hpp"
#include "navigation/nav_components.hpp"
#include "physics/components.hpp"
#include "render/components.hpp"

#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <string>

namespace slopengine {

namespace {

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

int sampleNavLeaf(const BspTree& tree, Vector3 point) {
    return pvsSampleLeaf(tree, point);
}

bool goalMovedEnough(const NavigationAgent& agent, Vector3 goalPos) {
    if (!agent.haveLastGoalPos) {
        return true;
    }
    return navHorizontalDist(agent.lastGoalPos, goalPos) >= agent.goalMoveThreshold;
}

void replanAgent(
    flecs::world& world,
    NavigationAgent& agent,
    Vector3 agentPos,
    Vector3 goalPos) {
    if (!world.has<MapNavigation>() || !world.has<MapBsp>()) {
        agent.waypoints.clear();
        agent.waypointIndex = 0;
        return;
    }

    const MapNavigation& nav = world.get<MapNavigation>();
    const BspTree& tree = world.get<MapBsp>().tree;
    const int fromLeaf = sampleNavLeaf(tree, agentPos);
    const int toLeaf = sampleNavLeaf(tree, goalPos);
    agent.agentLeaf = fromLeaf;
    agent.goalLeaf = toLeaf;

    if (fromLeaf < 0 || toLeaf < 0) {
        agent.waypoints.clear();
        agent.waypointIndex = 0;
        return;
    }

    const std::vector<int> leafPath = findLeafPath(nav, fromLeaf, toLeaf);
    if (leafPath.empty()) {
        agent.waypoints.clear();
        agent.waypointIndex = 0;
        return;
    }

    agent.waypoints = leafPathToWaypoints(nav, leafPath, goalPos);
    agent.waypointIndex = 0;
    agent.lastGoalPos = goalPos;
    agent.haveLastGoalPos = true;
    agent.forceReplan = false;
}

void advanceWaypoints(NavigationAgent& agent, Vector3 agentPos) {
    while (agent.waypointIndex < static_cast<int>(agent.waypoints.size())) {
        const Vector3& wp = agent.waypoints[static_cast<std::size_t>(agent.waypointIndex)];
        if (navHorizontalDist(agentPos, wp) > agent.arriveRadius) {
            break;
        }
        ++agent.waypointIndex;
    }
}

bool needsReplan(
    NavigationAgent& agent,
    Vector3 goalPos,
    int agentLeaf,
    float dt) {
    if (agent.forceReplan) {
        return true;
    }
    if (agent.waypoints.empty()) {
        return true;
    }
    if (agentLeaf >= 0 && agentLeaf != agent.agentLeaf) {
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
    const Vector3 agentPos = actorFeet(entity, motor);
    const Vector3 goalPos = resolveGoalPos(world, agent);
    replanAgent(world, agent, agentPos, goalPos);
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

            const Vector3 agentPos = actorFeet(entity, motor);
            const Vector3 goalPos = resolveGoalPos(worldRef, agent);
            const BspTree& tree = worldRef.get<MapBsp>().tree;
            const int agentLeaf = sampleNavLeaf(tree, agentPos);

            advanceWaypoints(agent, agentPos);

            if (needsReplan(agent, goalPos, agentLeaf, dt)) {
                replanAgent(worldRef, agent, agentPos, goalPos);
                agent.replanTimer = agent.replanInterval;
                agent.agentLeaf = agentLeaf;
            }
        });
}

}
