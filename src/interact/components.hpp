#pragma once

#include "map/handler_binding.hpp"

#include <flecs.h>
#include <raylib.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

/** Marks a placed entity as usable; Interact aims at it and calls on-use.
 *  @ingroup interact_components
 */
struct Interactable {
    std::string prompt = "Interact";
    HandlerBinding onUse; /**< Scheme on-use binding. */
    HandlerBinding canUse; /**< Optional predicate; empty = allow. Used with engineToggle. */
    bool engineToggle = false; /**< When true, engine toggles RigidMover after can-use. */
    float maxDistance = 5.0f;
};

/** Polygon surface for CSG face on-use / on-touch (no mesh required).
 *  @ingroup interact_components
 */
struct FaceUseSurface {
    std::vector<Vector3> vertices;
    Vector3 normal{};
};

/** Walk/touch callback for a CSG face; fires on enter like volume on-enter.
 *  @ingroup interact_components
 */
struct FaceTouch {
    HandlerBinding onTouch;
    float depth = 0.2f;
    std::unordered_set<std::uint64_t> inside;
};

/** Current best interact aim result for the player this frame.
 *  @ingroup interact_components
 */
struct InteractionTarget {
    flecs::entity entity{};
    float distance = 0.0f;
    std::string prompt;
    HandlerBinding onUse;
    HandlerBinding canUse;
    bool engineToggle = false;
};

}
