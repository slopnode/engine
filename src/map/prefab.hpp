#pragma once

#include "map/brush.hpp"
#include "map/csg_compile.hpp"

#include <raylib.h>

#include <string>
#include <string_view>

namespace slopengine {

/** One (prefab …) instance in a map or nested prefab. */
struct PrefabInstance {
    std::string path; /**< Virtual path under prefabs/. */
    std::string id;
    Vector3 at{};
    Vector3 angles{}; /**< Pitch, yaw, roll in radians. */
};

/** Prefixes brush and face ids with @p instanceId. */
void remapBrushIds(Brush& brush, std::string_view instanceId);

/** Applies instance translation and pitch/yaw/roll; updates UV lock axes. */
void transformBrush(
    Brush& brush,
    Vector3 at,
    Vector3 anglesPitchYawRoll,
    const MaterialUvResolver& resolveMaterialUv = {});

}
