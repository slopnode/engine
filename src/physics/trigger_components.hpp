#pragma once

#include "map/handler_binding.hpp"

#include <raylib.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

/** Named tags used for trigger filter matching.
 *  @ingroup physics_components
 */
struct CollisionTags {
    std::vector<std::string> tags;
};

/** Axis-aligned trigger volume with Scheme enter/exit callbacks.
 *  @ingroup physics_components
 */
struct TriggerVolume {
    Vector3 size{1.0f, 1.0f, 1.0f};
    HandlerBinding onEnter;
    HandlerBinding onExit;
    std::vector<std::string> filterTags;
    std::unordered_set<std::uint64_t> inside;
    bool once = false;
    bool fired = false;
};

}
