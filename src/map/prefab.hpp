#pragma once

#include "map/brush.hpp"
#include "map/csg_compile.hpp"

#include <raylib.h>

#include <string>
#include <string_view>

namespace slopengine {

struct PrefabInstance {
    std::string path;
    std::string id;
    Vector3 at{};
    Vector3 angles{};
};

void remapBrushIds(Brush& brush, std::string_view instanceId);

void transformBrush(
    Brush& brush,
    Vector3 at,
    Vector3 anglesPitchYawRoll,
    const MaterialUvResolver& resolveMaterialUv = {});

}
