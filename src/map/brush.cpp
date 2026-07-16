#include "map/brush.hpp"

#include <cmath>
#include <utility>

namespace slopengine {

namespace {

Vector3 faceNormalFromCorners(const std::array<Vector3, 4>& corners) {
    const Vector3 e0 = {
        corners[1].x - corners[0].x,
        corners[1].y - corners[0].y,
        corners[1].z - corners[0].z,
    };
    const Vector3 e1 = {
        corners[2].x - corners[0].x,
        corners[2].y - corners[0].y,
        corners[2].z - corners[0].z,
    };
    Vector3 normal = {
        e0.y * e1.z - e0.z * e1.y,
        e0.z * e1.x - e0.x * e1.z,
        e0.x * e1.y - e0.y * e1.x,
    };
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (length > 0.0001f) {
        normal.x /= length;
        normal.y /= length;
        normal.z /= length;
    }
    return normal;
}

BrushFace makeBoxFace(
    BrushBoxSide side,
    const std::string& brushId,
    Vector3 mins,
    Vector3 maxs,
    const std::string& material) {
    BrushFace face;
    face.id = brushId + "/" + brushBoxSideName(side);
    face.material = material;

    const float x0 = mins.x;
    const float x1 = maxs.x;
    const float y0 = mins.y;
    const float y1 = maxs.y;
    const float z0 = mins.z;
    const float z1 = maxs.z;

    switch (side) {
    case BrushBoxSide::Top:
        face.corners = {
            Vector3{x0, y1, z0},
            Vector3{x0, y1, z1},
            Vector3{x1, y1, z1},
            Vector3{x1, y1, z0},
        };
        break;
    case BrushBoxSide::Bottom:
        face.corners = {
            Vector3{x0, y0, z0},
            Vector3{x1, y0, z0},
            Vector3{x1, y0, z1},
            Vector3{x0, y0, z1},
        };
        break;
    case BrushBoxSide::North:
        face.corners = {
            Vector3{x0, y0, z0},
            Vector3{x0, y1, z0},
            Vector3{x1, y1, z0},
            Vector3{x1, y0, z0},
        };
        break;
    case BrushBoxSide::South:
        face.corners = {
            Vector3{x0, y0, z1},
            Vector3{x1, y0, z1},
            Vector3{x1, y1, z1},
            Vector3{x0, y1, z1},
        };
        break;
    case BrushBoxSide::East:
        face.corners = {
            Vector3{x1, y0, z1},
            Vector3{x1, y0, z0},
            Vector3{x1, y1, z0},
            Vector3{x1, y1, z1},
        };
        break;
    case BrushBoxSide::West:
        face.corners = {
            Vector3{x0, y0, z0},
            Vector3{x0, y0, z1},
            Vector3{x0, y1, z1},
            Vector3{x0, y1, z0},
        };
        break;
    }

    face.normal = faceNormalFromCorners(face.corners);
    return face;
}

} // namespace

const char* brushBoxSideName(BrushBoxSide side) {
    switch (side) {
    case BrushBoxSide::Top: return "top";
    case BrushBoxSide::Bottom: return "bottom";
    case BrushBoxSide::North: return "north";
    case BrushBoxSide::South: return "south";
    case BrushBoxSide::East: return "east";
    case BrushBoxSide::West: return "west";
    }
    return "unknown";
}

const char* brushRoleName(BrushRole role) {
    switch (role) {
    case BrushRole::Hull: return "hull";
    case BrushRole::Detail: return "detail";
    }
    return "unknown";
}

Brush makeBrushBox(
    std::string id,
    Vector3 mins,
    Vector3 maxs,
    const std::string& defaultMaterial,
    const std::vector<std::pair<BrushBoxSide, BrushFace>>& faceOverrides,
    BrushRole role) {
    Brush brush;
    brush.id = std::move(id);
    brush.role = role;
    brush.mins = mins;
    brush.maxs = maxs;

    constexpr BrushBoxSide kSides[] = {
        BrushBoxSide::Top,
        BrushBoxSide::Bottom,
        BrushBoxSide::North,
        BrushBoxSide::South,
        BrushBoxSide::East,
        BrushBoxSide::West,
    };

    for (BrushBoxSide side : kSides) {
        BrushFace face = makeBoxFace(side, brush.id, mins, maxs, defaultMaterial);
        for (const auto& [overrideSide, overrideFace] : faceOverrides) {
            if (overrideSide != side) {
                continue;
            }
            if (!overrideFace.id.empty()) {
                face.id = overrideFace.id;
            }
            if (!overrideFace.material.empty()) {
                face.material = overrideFace.material;
            }
            face.uvShiftPixels = overrideFace.uvShiftPixels;
            break;
        }
        brush.faces.push_back(std::move(face));
    }

    return brush;
}

}
