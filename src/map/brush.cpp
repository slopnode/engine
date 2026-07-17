#include "map/brush.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace slopengine {

namespace {

constexpr float kPlaneEps = 1e-4f;

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 sub3(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float length3(Vector3 v) {
    return std::sqrt(dot3(v, v));
}

Vector3 normalize3(Vector3 v) {
    const float len = length3(v);
    if (len <= 1e-8f) {
        return {};
    }
    return {v.x / len, v.y / len, v.z / len};
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
        face.vertices = {
            Vector3{x0, y1, z0},
            Vector3{x0, y1, z1},
            Vector3{x1, y1, z1},
            Vector3{x1, y1, z0},
        };
        break;
    case BrushBoxSide::Bottom:
        face.vertices = {
            Vector3{x0, y0, z0},
            Vector3{x1, y0, z0},
            Vector3{x1, y0, z1},
            Vector3{x0, y0, z1},
        };
        break;
    case BrushBoxSide::North:
        face.vertices = {
            Vector3{x0, y0, z0},
            Vector3{x0, y1, z0},
            Vector3{x1, y1, z0},
            Vector3{x1, y0, z0},
        };
        break;
    case BrushBoxSide::South:
        face.vertices = {
            Vector3{x0, y0, z1},
            Vector3{x1, y0, z1},
            Vector3{x1, y1, z1},
            Vector3{x0, y1, z1},
        };
        break;
    case BrushBoxSide::East:
        face.vertices = {
            Vector3{x1, y0, z1},
            Vector3{x1, y0, z0},
            Vector3{x1, y1, z0},
            Vector3{x1, y1, z1},
        };
        break;
    case BrushBoxSide::West:
        face.vertices = {
            Vector3{x0, y0, z0},
            Vector3{x0, y0, z1},
            Vector3{x0, y1, z1},
            Vector3{x0, y1, z0},
        };
        break;
    }

    face.normal = faceNormalFromVertices(face.vertices);
    return face;
}

} // namespace

Vector3 faceNormalFromVertices(const std::vector<Vector3>& vertices) {
    if (vertices.size() < 3) {
        return {};
    }
    Vector3 accum{};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const Vector3& a = vertices[i];
        const Vector3& b = vertices[(i + 1) % vertices.size()];
        accum.x += (a.y - b.y) * (a.z + b.z);
        accum.y += (a.z - b.z) * (a.x + b.x);
        accum.z += (a.x - b.x) * (a.y + b.y);
    }
    return normalize3(accum);
}

void recomputeBrushBounds(Brush& brush) {
    brush.mins = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    brush.maxs = {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    bool any = false;
    for (const BrushFace& face : brush.faces) {
        for (const Vector3& v : face.vertices) {
            any = true;
            brush.mins.x = std::min(brush.mins.x, v.x);
            brush.mins.y = std::min(brush.mins.y, v.y);
            brush.mins.z = std::min(brush.mins.z, v.z);
            brush.maxs.x = std::max(brush.maxs.x, v.x);
            brush.maxs.y = std::max(brush.maxs.y, v.y);
            brush.maxs.z = std::max(brush.maxs.z, v.z);
        }
    }
    if (!any) {
        brush.mins = {};
        brush.maxs = {};
    }
}

bool pointInsideBrush(Vector3 point, const Brush& brush, float epsilon) {
    for (const BrushFace& face : brush.faces) {
        if (face.vertices.empty()) {
            return false;
        }
        const float side = dot3(sub3(point, face.vertices[0]), face.normal);
        if (side > epsilon) {
            return false;
        }
    }
    return !brush.faces.empty();
}

bool pointInsideBrushInclusive(Vector3 point, const Brush& brush, float epsilon) {
    for (const BrushFace& face : brush.faces) {
        if (face.vertices.empty()) {
            return false;
        }
        const float side = dot3(sub3(point, face.vertices[0]), face.normal);
        if (side > epsilon) {
            return false;
        }
    }
    return !brush.faces.empty();
}

std::optional<BrushConvexError> validateBrushConvex(const Brush& brush) {
    if (brush.faces.size() < 4) {
        return BrushConvexError{"convex brush needs at least 4 faces"};
    }
    for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
        const BrushFace& face = brush.faces[fi];
        if (face.vertices.size() < 3) {
            return BrushConvexError{"face has fewer than 3 vertices"};
        }
        const Vector3 normal = faceNormalFromVertices(face.vertices);
        if (length3(normal) < 1e-6f) {
            return BrushConvexError{"face is degenerate"};
        }
        for (std::size_t vi = 2; vi < face.vertices.size(); ++vi) {
            const float planar = std::fabs(
                dot3(sub3(face.vertices[vi], face.vertices[0]), normal));
            if (planar > 1e-3f) {
                return BrushConvexError{"face vertices are not planar"};
            }
        }
        for (std::size_t oj = 0; oj < brush.faces.size(); ++oj) {
            if (oj == fi) {
                continue;
            }
            for (const Vector3& v : brush.faces[oj].vertices) {
                const float side = dot3(sub3(v, face.vertices[0]), face.normal);
                if (side > 1e-3f) {
                    return BrushConvexError{"brush is not convex (vertex outside face plane)"};
                }
            }
        }
    }
    return std::nullopt;
}

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
            face.nodraw = overrideFace.nodraw;
            break;
        }
        brush.faces.push_back(std::move(face));
    }

    return brush;
}

std::optional<Brush> makeBrushConvex(
    std::string id,
    std::vector<BrushFace> faces,
    BrushRole role,
    std::string& errorOut) {
    Brush brush;
    brush.id = std::move(id);
    brush.role = role;
    brush.faces = std::move(faces);
    for (std::size_t i = 0; i < brush.faces.size(); ++i) {
        BrushFace& face = brush.faces[i];
        if (face.id.empty()) {
            face.id = brush.id + "/" + std::to_string(i);
        }
        face.normal = faceNormalFromVertices(face.vertices);
    }
    recomputeBrushBounds(brush);
    if (const auto err = validateBrushConvex(brush)) {
        errorOut = err->message;
        return std::nullopt;
    }
    return brush;
}

std::vector<std::array<Vector3, 3>> triangulateFace(const std::vector<Vector3>& vertices) {
    std::vector<std::array<Vector3, 3>> tris;
    if (vertices.size() < 3) {
        return tris;
    }
    for (std::size_t i = 1; i + 1 < vertices.size(); ++i) {
        tris.push_back({vertices[0], vertices[i], vertices[i + 1]});
    }
    return tris;
}

}
