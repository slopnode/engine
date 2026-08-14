#include "navigation/nav_script.hpp"

#include "navigation/nav_module.hpp"
#include "navigation/nav_components.hpp"
#include "map/bsp.hpp"
#include "map/pvs.hpp"
#include "map/nav_graph.hpp"
#include "physics/components.hpp"
#include "render/components.hpp"
#include "script/proc_role.hpp"
#include "script/script_scope.hpp"
#include "script/thing_script.hpp"

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

bool readNumberArg(s7_scheme* sc, s7_pointer& args, float& out) {
    if (!s7_is_pair(args) || !s7_is_number(s7_car(args))) {
        return false;
    }
    out = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
    args = s7_cdr(args);
    return true;
}

s7_pointer g_nav_set_goal_pos(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "nav-set-goal-pos", 1, args, "id string");
    }
    const char* id = s7_string(s7_car(args));
    s7_pointer rest = s7_cdr(args);
    float x = 0.0f;
    float z = 0.0f;
    if (!readNumberArg(sc, rest, x) || !readNumberArg(sc, rest, z)) {
        return s7_wrong_type_arg_error(sc, "nav-set-goal-pos", 2, rest, "x z numbers");
    }
    flecs::entity entity = lookupNavEntity(id);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    NavigationAgent& agent = entity.get_mut<NavigationAgent>();
    agent.hasGoal = true;
    agent.goalEntityId.clear();
    agent.goalPos = {x, 0.0f, z};
    if (entity.has<LocalTransformation>()) {
        agent.goalPos.y = entity.get<LocalTransformation>().position.y;
    }
    if (!agent.haveLastGoalPos ||
        navHorizontalDist(agent.lastGoalPos, agent.goalPos) >= agent.goalMoveThreshold) {
        agent.forceReplan = true;
    }
    replanNavigationAgent(*g_navWorld, entity);
    return s7_t(sc);
}

s7_pointer g_nav_set_goal_entity(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "nav-set-goal-entity", 1, args, "id string");
    }
    const char* id = s7_string(s7_car(args));
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "nav-set-goal-entity", 2, rest, "target-id string");
    }
    const char* targetId = s7_string(s7_car(rest));
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "nav-clear-goal", 1, args, "id string");
    }
    flecs::entity entity = lookupNavEntity(s7_string(s7_car(args)));
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
    return s7_t(sc);
}

s7_pointer g_nav_waypoint(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "nav-waypoint", 1, args, "id string");
    }
    flecs::entity entity = lookupNavEntity(s7_string(s7_car(args)));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    const NavigationAgent& agent = entity.get<NavigationAgent>();
    if (agent.waypointIndex < 0 ||
        agent.waypointIndex >= static_cast<int>(agent.waypoints.size())) {
        return s7_f(sc);
    }
    const Vector3& wp = agent.waypoints[static_cast<std::size_t>(agent.waypointIndex)];
    return s7_list(
        sc,
        3,
        s7_make_real(sc, wp.x),
        s7_make_real(sc, wp.y),
        s7_make_real(sc, wp.z));
}

s7_pointer g_nav_has_path_p(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "nav-has-path?", 1, args, "id string");
    }
    flecs::entity entity = lookupNavEntity(s7_string(s7_car(args)));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    const NavigationAgent& agent = entity.get<NavigationAgent>();
    return (agent.hasGoal && agent.waypointIndex >= 0 &&
            agent.waypointIndex < static_cast<int>(agent.waypoints.size()))
               ? s7_t(sc)
               : s7_f(sc);
}

s7_pointer g_nav_path_direction(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "nav-path-direction", 1, args, "id string");
    }
    flecs::entity entity = lookupNavEntity(s7_string(s7_car(args)));
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

    if (idx >= static_cast<int>(agent.waypoints.size())) {
        const Vector3& goal = agent.waypoints.back();
        seg = {goal.x - agentPos.x, 0.0f, goal.z - agentPos.z};
    } else {
        const Vector3& wp = agent.waypoints[static_cast<std::size_t>(idx)];
        const float dist = navHorizontalDist(agentPos, wp);
        if (idx + 1 < static_cast<int>(agent.waypoints.size())) {
            const Vector3& next = agent.waypoints[static_cast<std::size_t>(idx + 1)];
            if (dist > agent.arriveRadius) {
                seg = {wp.x - agentPos.x, 0.0f, wp.z - agentPos.z};
            } else {
                seg = {next.x - wp.x, 0.0f, next.z - wp.z};
            }
        } else if (dist > agent.arriveRadius || idx == 0) {
            seg = {wp.x - agentPos.x, 0.0f, wp.z - agentPos.z};
        } else {
            const Vector3& prev = agent.waypoints[static_cast<std::size_t>(idx - 1)];
            seg = {wp.x - prev.x, 0.0f, wp.z - prev.z};
        }
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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "nav-at-goal?", 1, args, "id string");
    }
    flecs::entity entity = lookupNavEntity(s7_string(s7_car(args)));
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
    return dist <= agent.arriveRadius ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_nav_leaf_at(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_navWorld == nullptr || !g_navWorld->has<MapBsp>()) {
        return s7_f(sc);
    }
    float x = 0, y = 0, z = 0;
    if (!readNumberArg(sc, args, x) || !readNumberArg(sc, args, y) || !readNumberArg(sc, args, z)) {
        return s7_wrong_type_arg_error(sc, "nav-leaf-at", 1, args, "x y z numbers");
    }
    const int leaf = pvsSampleLeaf(g_navWorld->get<MapBsp>().tree, {x, y, z});
    if (leaf < 0) {
        return s7_f(sc);
    }
    return s7_make_integer(sc, leaf);
}

s7_pointer g_nav_leaf_bounds(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_navWorld == nullptr || !g_navWorld->has<MapBsp>()) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_integer(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "nav-leaf-bounds", 1, args, "leaf integer");
    }
    const int leaf = static_cast<int>(s7_integer(s7_car(args)));
    const BspTree& tree = g_navWorld->get<MapBsp>().tree;
    if (leaf < 0 || leaf >= static_cast<int>(tree.leaves.size())) {
        return s7_f(sc);
    }
    const BspLeaf& bspLeaf = tree.leaves[static_cast<std::size_t>(leaf)];
    return s7_list(
        sc,
        2,
        s7_list(
            sc,
            3,
            s7_make_real(sc, bspLeaf.mins.x),
            s7_make_real(sc, bspLeaf.mins.y),
            s7_make_real(sc, bspLeaf.mins.z)),
        s7_list(
            sc,
            3,
            s7_make_real(sc, bspLeaf.maxs.x),
            s7_make_real(sc, bspLeaf.maxs.y),
            s7_make_real(sc, bspLeaf.maxs.z)));
}

s7_pointer g_nav_leaf_floor(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_navWorld == nullptr || !g_navWorld->has<MapNavigation>()) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_integer(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "nav-leaf-floor", 1, args, "leaf integer");
    }
    const int leaf = static_cast<int>(s7_integer(s7_car(args)));
    const MapNavigation& nav = g_navWorld->get<MapNavigation>();
    if (leaf < 0 || leaf >= nav.leafCount) {
        return s7_f(sc);
    }
    return s7_make_real(
        sc, static_cast<double>(nav.leafFloorY[static_cast<std::size_t>(leaf)]));
}

s7_pointer g_nav_leaf_ceiling(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_navWorld == nullptr || !g_navWorld->has<MapNavigation>()) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_integer(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "nav-leaf-ceiling", 1, args, "leaf integer");
    }
    const int leaf = static_cast<int>(s7_integer(s7_car(args)));
    const MapNavigation& nav = g_navWorld->get<MapNavigation>();
    if (leaf < 0 || leaf >= nav.leafCount) {
        return s7_f(sc);
    }
    return s7_make_real(
        sc, static_cast<double>(nav.leafCeilingY[static_cast<std::size_t>(leaf)]));
}

s7_pointer g_nav_path_altitude(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "nav-path-altitude", 1, args, "id string");
    }
    flecs::entity entity = lookupNavEntity(s7_string(s7_car(args)));
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

} // namespace

void bindNavApi(flecs::world& world, s7_scheme* scheme) {
    g_navWorld = &world;
    s7_define_function(
        scheme,
        "nav-set-goal-pos",
        g_nav_set_goal_pos,
        3,
        0,
        false,
        "(nav-set-goal-pos id x z)");
    s7_define_function(
        scheme,
        "nav-set-goal-entity",
        g_nav_set_goal_entity,
        2,
        0,
        false,
        "(nav-set-goal-entity id target-id)");
    s7_define_function(
        scheme,
        "nav-clear-goal",
        g_nav_clear_goal,
        1,
        0,
        false,
        "(nav-clear-goal id)");
    s7_define_function(
        scheme,
        "nav-waypoint",
        g_nav_waypoint,
        1,
        0,
        false,
        "(nav-waypoint id)");
    s7_define_function(
        scheme,
        "nav-has-path?",
        g_nav_has_path_p,
        1,
        0,
        false,
        "(nav-has-path? id)");
    s7_define_function(
        scheme,
        "nav-path-direction",
        g_nav_path_direction,
        1,
        0,
        false,
        "(nav-path-direction id)");
    s7_define_function(
        scheme,
        "nav-path-altitude",
        g_nav_path_altitude,
        1,
        0,
        false,
        "(nav-path-altitude id)");
    s7_define_function(
        scheme,
        "nav-leaf-at",
        g_nav_leaf_at,
        3,
        0,
        false,
        "(nav-leaf-at x y z)");
    s7_define_function(
        scheme,
        "nav-leaf-bounds",
        g_nav_leaf_bounds,
        1,
        0,
        false,
        "(nav-leaf-bounds leaf)");
    s7_define_function(
        scheme,
        "nav-leaf-floor",
        g_nav_leaf_floor,
        1,
        0,
        false,
        "(nav-leaf-floor leaf)");
    s7_define_function(
        scheme,
        "nav-leaf-ceiling",
        g_nav_leaf_ceiling,
        1,
        0,
        false,
        "(nav-leaf-ceiling leaf)");
    s7_define_function(
        scheme,
        "nav-at-goal?",
        g_nav_at_goal_p,
        1,
        0,
        false,
        "(nav-at-goal? id)");
}

}
