#pragma once

#include <flecs.h>
#include <string>

namespace slopengine {

/** Marks a placed entity as usable; Interact aims at it and calls on-use.
 *  @ingroup interact_components
 */
struct Interactable {
    std::string prompt = "Interact";
    std::string eventName;   /**< Scheme procedure name for on-use. */
    float maxDistance = 5.0f;
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
