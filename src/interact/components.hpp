#pragma once

#include <flecs.h>
#include <raylib.h>
#include <string>
#include <vector>

namespace slopengine {

/** Marks a placed entity as usable; Interact aims at it and calls on-use.
 *  @ingroup interact_components
 */
struct Interactable {
    std::string prompt = "Interact";
    std::string eventName;   /**< Scheme procedure name for on-use. */
    float maxDistance = 5.0f;
};

/** Polygon surface for CSG face on-use (raycast by interact; no mesh required).
 *  @ingroup interact_components
 */
struct FaceUseSurface {
    std::vector<Vector3> vertices;
    Vector3 normal{};
};

/** Current best interact aim result for the player this frame.
 *  @ingroup interact_components
 */
struct InteractionTarget {
    flecs::entity entity{};
    float distance = 0.0f;
    std::string prompt;
    std::string eventName;
};

}
