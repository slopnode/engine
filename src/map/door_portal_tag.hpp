#pragma once

#include "map/brush.hpp"

#include <raylib.h>

#include <string>
#include <vector>

namespace slopengine {

// Shared by BSP portal tagging (bsp_build.cpp's linkDoorPortals) and navmesh edge
// tagging (nav_navmesh_build.cpp) so a doorway is recognized identically by both
// nav graph builders -- kept in one place instead of two copies that could drift.
constexpr float kDoorPortalPad = 0.25f;

/** True if @p point falls within @p doorBrush's AABB padded by kDoorPortalPad. */
inline bool pointInPaddedDoorAabb(Vector3 point, const Brush& doorBrush) {
    const Vector3 mins{
        doorBrush.mins.x - kDoorPortalPad,
        doorBrush.mins.y - kDoorPortalPad,
        doorBrush.mins.z - kDoorPortalPad,
    };
    const Vector3 maxs{
        doorBrush.maxs.x + kDoorPortalPad,
        doorBrush.maxs.y + kDoorPortalPad,
        doorBrush.maxs.z + kDoorPortalPad,
    };
    return point.x >= mins.x && point.x <= maxs.x && point.y >= mins.y && point.y <= maxs.y &&
           point.z >= mins.z && point.z <= maxs.z;
}

/** Id of the Door brush whose padded AABB contains @p point, or empty if none does. */
inline std::string doorBrushIdAtPoint(Vector3 point, const std::vector<Brush>& brushes) {
    for (const Brush& brush : brushes) {
        if (brush.role == BrushRole::Door && pointInPaddedDoorAabb(point, brush)) {
            return brush.id;
        }
    }
    return {};
}

}
