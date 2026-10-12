#pragma once

#include "editor_types.hpp"
#include "map/camera_path.hpp"

#include <raylib.h>

#include <optional>
#include <vector>

namespace slopmap {

void drawCameraPaths(
    const slopengine::GraphDocument& graphs,
    const std::vector<EntityRef>& selected,
    int activePath,
    bool showGizmos,
    const std::optional<slopengine::CameraPose>& previewPose);

bool pickKnot(
    const slopengine::GraphDocument& graphs,
    Ray ray,
    EntityRef& outRef,
    float& outDistance);

constexpr float kKnotPickRadius = 0.18f;

}
