#include "navigation/nav_script.hpp"

#include "navigation/nav_module.hpp"
#include "navigation/nav_components.hpp"
#include "map/bsp.hpp"
#include "map/pvs.hpp"
#include "map/nav_graph.hpp"
#include "physics/components.hpp"
#include "render/components.hpp"
#include "script/proc_role.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"
#include "script/thing_script.hpp"

#include <raymath.h>
#include <s7.h>

#include <cmath>
#include <string>

namespace slopengine {

namespace {

flecs::world* g_navWorld = nullptr;

flecs::entity lookupNavEntity(std::string_view id) {
    if (g_navWorld == nullptr || id.empty()) {
        return {};
    }
    flecs::entity entity = g_navWorld->lookup(std::string(id).c_str());
    if (!entity.is_valid() || !entity.has<NavigationAgent>()) {
        return {};
    }
    return entity;
}

Vector3 navActorFeet(flecs::entity entity) {
    if (!entity.has<CharacterMotor>()) {
        if (entity.has<LocalTransformation>()) {
            return entity.get<LocalTransformation>().position;
        }
        return {};
    }
    const CharacterMotor& motor = entity.get<CharacterMotor>();
    if (entity.has<Lens>()) {
        const Vector3 eye = entity.get<Lens>().camera.position;
        return {eye.x, eye.y - motor.eyeHeight, eye.z};
    }
    if (entity.has<LocalTransformation>()) {
        return entity.get<LocalTransformation>().position;
    }
    return {};
}

s7_pointer g_nav_set_goal_pos(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "nav-set-goal-pos", args);
    const char* id = in.string("id string");
    const float x = in.realf("x z numbers");
    const float z = in.realf("x z numbers");
    const bool haveY = in.nextIsNumber();
    const float y = in.optRealf(0.0f);
    flecs::entity entity = lookupNavEntity(id);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    NavigationAgent& agent = entity.get_mut<NavigationAgent>();
    agent.hasGoal = true;
    agent.goalEntityId.clear();
    agent.goalPos = {x, 0.0f, z};
    if (haveY) {
        agent.goalPos.y = y;
    } else if (entity.has<LocalTransformation>()) {
        agent.goalPos.y = entity.get<LocalTransformation>().position.y;
    }
    if (!agent.haveLastGoalPos ||
        Vector3Distance(agent.lastGoalPos, agent.goalPos) >= agent.goalMoveThreshold) {
        agent.forceReplan = true;
    }
    replanNavigationAgent(*g_navWorld, entity);
    return s7_t(sc);
}

s7_pointer g_nav_set_goal_entity(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "nav-set-goal-entity", args);
    const char* id = in.string("id string");
    const char* targetId = in.string("target-id string");
    flecs::entity entity = lookupNavEntity(id);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    NavigationAgent& agent = entity.get_mut<NavigationAgent>();
    const std::string nextTarget = targetId;
    if (agent.goalEntityId != nextTarget) {
        agent.goalEntityId = nextTarget;
        agent.forceReplan = true;
    }
    agent.hasGoal = true;
    if (agent.forceReplan) {
        replanNavigationAgent(*g_navWorld, entity);
    }
    return s7_t(sc);
}

s7_pointer g_nav_clear_goal(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "nav-clear-goal", args);
    flecs::entity entity = lookupNavEntity(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    NavigationAgent& agent = entity.get_mut<NavigationAgent>();
    agent.hasGoal = false;
    agent.goalEntityId.clear();
    agent.waypoints.clear();
    agent.leafPath.clear();
    agent.waypointToLeaf.clear();
    agent.waypointIndex = 0;
    agent.stuckTimer = 0.0f;
    agent.haveStuckLastPos = false;
    agent.haveLastGoalPos = false;
    agent.forceReplan = false;
    agent.goalReachable = true;
    return s7_t(sc);
}

s7_pointer g_nav_waypoint(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "nav-waypoint", args);
    flecs::entity entity = lookupNavEntity(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    const NavigationAgent& agent = entity.get<NavigationAgent>();
    if (agent.waypointIndex < 0 ||
        agent.waypointIndex >= static_cast<int>(agent.waypoints.size())) {
        return s7_f(sc);
    }
    return makeVec3List(sc, agent.waypoints[static_cast<std::size_t>(agent.waypointIndex)]);
}

s7_pointer g_nav_has_path_p(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "nav-has-path?", args);
    flecs::entity entity = lookupNavEntity(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    const NavigationAgent& agent = entity.get<NavigationAgent>();
    return s7_make_boolean(
        sc,
        agent.hasGoal && agent.waypointIndex >= 0 &&
            agent.waypointIndex < static_cast<int>(agent.waypoints.size()));
}

s7_pointer g_nav_path_direction(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "nav-path-direction", args);
    flecs::entity entity = lookupNavEntity(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    const NavigationAgent& agent = entity.get<NavigationAgent>();
    if (!agent.hasGoal || agent.waypoints.empty()) {
        return s7_f(sc);
    }

    const Vector3 agentPos = navActorFeet(entity);
    const int idx = agent.waypointIndex;
    Vector3 seg{};

    if (idx < 0) {
        return s7_f(sc);
    }

    // Always steers straight at the agent's actual current target (the waypoint it hasn't
    // reached yet, or -- once past the last one and the goal is still known-reachable -- the
    // live goal itself) rather than blending toward the next leg early. The funnel algorithm
    // already guarantees a taut path stays inside the walkable corridor; blending ahead of
    // arrival cut that guarantee, which was harmless while navmesh erosion left a body-radius
    // buffer at every boundary but sends the agent past the true edge now that open ledges
    // (as opposed to real walls) carry no such buffer. A stale, no-longer-reachable goal
    // (agent.goalReachable false) isn't something to home in on at all -- see NavigationAgent's
    // doc comment -- so past-the-end reports no direction instead of beelining toward it.
    if (idx >= static_cast<int>(agent.waypoints.size())) {
        if (!agent.goalReachable) {
            return s7_f(sc);
        }
        const Vector3& goal = agent.waypoints.back();
        seg = {goal.x - agentPos.x, 0.0f, goal.z - agentPos.z};
    } else {
        const Vector3& wp = agent.waypoints[static_cast<std::size_t>(idx)];
        seg = {wp.x - agentPos.x, 0.0f, wp.z - agentPos.z};
    }

    const float len = std::sqrt(seg.x * seg.x + seg.z * seg.z);
    if (len < 1.0e-4f) {
        return s7_f(sc);
    }
    return s7_list(
        sc,
        2,
        s7_make_real(sc, seg.x / len),
        s7_make_real(sc, seg.z / len));
}

s7_pointer g_nav_at_goal_p(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "nav-at-goal?", args);
    flecs::entity entity = lookupNavEntity(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    const NavigationAgent& agent = entity.get<NavigationAgent>();
    if (!agent.hasGoal || agent.waypoints.empty()) {
        return s7_f(sc);
    }
    if (agent.waypointIndex < static_cast<int>(agent.waypoints.size()) - 1) {
        return s7_f(sc);
    }
    const Vector3 pos = navActorFeet(entity);
    const Vector3& goal = agent.waypoints.back();
    const float dx = pos.x - goal.x;
    const float dz = pos.z - goal.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    return s7_make_boolean(sc, dist <= agent.arriveRadius);
}

s7_pointer g_nav_leaf_at(s7_scheme* sc, s7_pointer args) {
    if (g_navWorld == nullptr || !g_navWorld->has<MapBsp>()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "nav-leaf-at", args);
    const Vector3 point = in.vec3("x y z numbers");
    const int leaf = pvsSampleLeaf(g_navWorld->get<MapBsp>().tree, point);
    if (leaf < 0) {
        return s7_f(sc);
    }
    return s7_make_integer(sc, leaf);
}

s7_pointer g_nav_leaf_bounds(s7_scheme* sc, s7_pointer args) {
    if (g_navWorld == nullptr || !g_navWorld->has<MapBsp>()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "nav-leaf-bounds", args);
    const int leaf = static_cast<int>(in.exactInteger("leaf integer"));
    const BspTree& tree = g_navWorld->get<MapBsp>().tree;
    if (leaf < 0 || leaf >= static_cast<int>(tree.leaves.size())) {
        return s7_f(sc);
    }
    const BspLeaf& bspLeaf = tree.leaves[static_cast<std::size_t>(leaf)];
    return s7_list(sc, 2, makeVec3List(sc, bspLeaf.mins), makeVec3List(sc, bspLeaf.maxs));
}

s7_pointer g_nav_leaf_floor(s7_scheme* sc, s7_pointer args) {
    if (g_navWorld == nullptr || !g_navWorld->has<MapNavigation>()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "nav-leaf-floor", args);
    const int leaf = static_cast<int>(in.exactInteger("leaf integer"));
    const MapNavigation& nav = g_navWorld->get<MapNavigation>();
    if (leaf < 0 || leaf >= nav.leafCount) {
        return s7_f(sc);
    }
    return s7_make_real(
        sc, static_cast<double>(nav.leafFloorY[static_cast<std::size_t>(leaf)]));
}

s7_pointer g_nav_leaf_ceiling(s7_scheme* sc, s7_pointer args) {
    if (g_navWorld == nullptr || !g_navWorld->has<MapNavigation>()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "nav-leaf-ceiling", args);
    const int leaf = static_cast<int>(in.exactInteger("leaf integer"));
    const MapNavigation& nav = g_navWorld->get<MapNavigation>();
    if (leaf < 0 || leaf >= nav.leafCount) {
        return s7_f(sc);
    }
    return s7_make_real(
        sc, static_cast<double>(nav.leafCeilingY[static_cast<std::size_t>(leaf)]));
}

s7_pointer g_nav_path_altitude(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "nav-path-altitude", args);
    flecs::entity entity = lookupNavEntity(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    const NavigationAgent& agent = entity.get<NavigationAgent>();
    if (!agent.hasGoal || agent.waypoints.empty()) {
        return s7_f(sc);
    }

    const Vector3 agentPos = navActorFeet(entity);
    const int idx = agent.waypointIndex;
    if (idx < 0) {
        return s7_f(sc);
    }

    if (idx >= static_cast<int>(agent.waypoints.size())) {
        return s7_make_real(sc, static_cast<double>(agent.waypoints.back().y));
    }

    const Vector3& wp = agent.waypoints[static_cast<std::size_t>(idx)];
    const float dist = navHorizontalDist(agentPos, wp);
    if (idx + 1 < static_cast<int>(agent.waypoints.size())) {
        if (dist > agent.arriveRadius) {
            return s7_make_real(sc, static_cast<double>(wp.y));
        }
        return s7_make_real(
            sc,
            static_cast<double>(agent.waypoints[static_cast<std::size_t>(idx + 1)].y));
    }
    if (dist > agent.arriveRadius || idx == 0) {
        return s7_make_real(sc, static_cast<double>(wp.y));
    }
    if (idx > 0) {
        return s7_make_real(sc, static_cast<double>(wp.y));
    }
    return s7_make_real(sc, static_cast<double>(wp.y));
}

constexpr SchemeBinding kNavBindings[] = {
    {"nav-set-goal-pos", g_nav_set_goal_pos, 3, 1, false, "(nav-set-goal-pos id x z [y])", ScriptCap::WorldMutate},
    {"nav-set-goal-entity", g_nav_set_goal_entity, 2, 0, false, "(nav-set-goal-entity id target-id)", ScriptCap::WorldMutate},
    {"nav-clear-goal", g_nav_clear_goal, 1, 0, false, "(nav-clear-goal id)", ScriptCap::WorldMutate},
    {"nav-waypoint", g_nav_waypoint, 1, 0, false, "(nav-waypoint id)", ScriptCap::ReadWorld},
    {"nav-has-path?", g_nav_has_path_p, 1, 0, false, "(nav-has-path? id)", ScriptCap::ReadWorld},
    {"nav-path-direction", g_nav_path_direction, 1, 0, false, "(nav-path-direction id)", ScriptCap::ReadWorld},
    {"nav-path-altitude", g_nav_path_altitude, 1, 0, false, "(nav-path-altitude id)", ScriptCap::ReadWorld},
    {"nav-leaf-at", g_nav_leaf_at, 3, 0, false, "(nav-leaf-at x y z)", ScriptCap::ReadWorld},
    {"nav-leaf-bounds", g_nav_leaf_bounds, 1, 0, false, "(nav-leaf-bounds leaf)", ScriptCap::ReadWorld},
    {"nav-leaf-floor", g_nav_leaf_floor, 1, 0, false, "(nav-leaf-floor leaf)", ScriptCap::ReadWorld},
    {"nav-leaf-ceiling", g_nav_leaf_ceiling, 1, 0, false, "(nav-leaf-ceiling leaf)", ScriptCap::ReadWorld},
    {"nav-at-goal?", g_nav_at_goal_p, 1, 0, false, "(nav-at-goal? id)", ScriptCap::ReadWorld},
};

} // namespace

void bindNavApi(flecs::world& world, s7_scheme* scheme) {
    g_navWorld = &world;
    registerBindings(scheme, kNavBindings);
}

}
