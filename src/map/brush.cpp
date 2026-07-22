#include "map/brush.hpp"

#include "map/uv_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

constexpr float kPlaneEps = 1e-4f;

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 sub3(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 cross3(Vector3 a, Vector3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
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
    case BrushRole::Hint: return "hint";
    case BrushRole::Trigger: return "trigger";
    case BrushRole::Water: return "water";
    case BrushRole::Window: return "window";
    }
    return "unknown";
}

bool parseBrushRoleName(std::string_view name, BrushRole& out) {
    if (name == "hull") {
        out = BrushRole::Hull;
        return true;
    }
    if (name == "detail") {
        out = BrushRole::Detail;
        return true;
    }
    if (name == "hint") {
        out = BrushRole::Hint;
        return true;
    }
    if (name == "trigger") {
        out = BrushRole::Trigger;
        return true;
    }
    if (name == "water") {
        out = BrushRole::Water;
        return true;
    }
    if (name == "window") {
        out = BrushRole::Window;
        return true;
    }
    return false;
}

bool brushRoleContributesSplits(BrushRole role) {
    switch (role) {
    case BrushRole::Hull:
    case BrushRole::Window:
    case BrushRole::Water:
    case BrushRole::Hint:
        return true;
    case BrushRole::Detail:
    case BrushRole::Trigger:
        return false;
    }
    return false;
}

bool brushRoleSeals(BrushRole role) {
    return role == BrushRole::Hull || role == BrushRole::Window;
}

bool brushRoleEmitsVisFaces(BrushRole role) {
    switch (role) {
    case BrushRole::Hull:
    case BrushRole::Detail:
    case BrushRole::Water:
    case BrushRole::Window:
        return true;
    case BrushRole::Hint:
    case BrushRole::Trigger:
        return false;
    }
    return false;
}

bool brushRoleDefaultNocollide(BrushRole role) {
    switch (role) {
    case BrushRole::Hint:
    case BrushRole::Trigger:
    case BrushRole::Water:
        return true;
    case BrushRole::Hull:
    case BrushRole::Detail:
    case BrushRole::Window:
        return false;
    }
    return false;
}

bool brushRoleNeedsInteriorPlacement(BrushRole role) {
    switch (role) {
    case BrushRole::Detail:
    case BrushRole::Hint:
    case BrushRole::Trigger:
    case BrushRole::Water:
        return true;
    case BrushRole::Hull:
    case BrushRole::Window:
        return false;
    }
    return false;
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
    brush.box = true;

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
            face.uvLock = overrideFace.uvLock;
            face.uvUAxis = overrideFace.uvUAxis;
            face.uvVAxis = overrideFace.uvVAxis;
            break;
        }
        ensureFaceUvAxes(face);
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
        ensureFaceUvAxes(face);
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
    if (vertices.size() == 3) {
        tris.push_back({vertices[0], vertices[1], vertices[2]});
        return tris;
    }

    const Vector3 normal = faceNormalFromVertices(vertices);
    if (length3(normal) < 1e-8f) {
        for (std::size_t i = 1; i + 1 < vertices.size(); ++i) {
            tris.push_back({vertices[0], vertices[i], vertices[i + 1]});
        }
        return tris;
    }

    std::vector<std::size_t> indices(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        indices[i] = i;
    }

    auto signedArea = [&](std::size_t i0, std::size_t i1, std::size_t i2) {
        const Vector3 a = sub3(vertices[i1], vertices[i0]);
        const Vector3 b = sub3(vertices[i2], vertices[i0]);
        return dot3(cross3(a, b), normal);
    };

    auto pointInTriangle = [&](std::size_t p, std::size_t i0, std::size_t i1, std::size_t i2) {
        const float a = signedArea(i0, i1, p);
        const float b = signedArea(i1, i2, p);
        const float c = signedArea(i2, i0, p);
        return (a >= -kPlaneEps && b >= -kPlaneEps && c >= -kPlaneEps)
            || (a <= kPlaneEps && b <= kPlaneEps && c <= kPlaneEps);
    };

    auto isEar = [&](std::size_t earIndex) {
        const std::size_t n = indices.size();
        const std::size_t iPrev = indices[(earIndex + n - 1) % n];
        const std::size_t iCurr = indices[earIndex];
        const std::size_t iNext = indices[(earIndex + 1) % n];
        if (signedArea(iPrev, iCurr, iNext) <= kPlaneEps) {
            return false;
        }
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t idx = indices[k];
            if (idx == iPrev || idx == iCurr || idx == iNext) {
                continue;
            }
            if (pointInTriangle(idx, iPrev, iCurr, iNext)) {
                return false;
            }
        }
        return true;
    };

    int guard = 0;
    while (indices.size() > 3 && guard < 10000) {
        ++guard;
        bool clipped = false;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            if (!isEar(i)) {
                continue;
            }
            const std::size_t n = indices.size();
            const std::size_t iPrev = indices[(i + n - 1) % n];
            const std::size_t iCurr = indices[i];
            const std::size_t iNext = indices[(i + 1) % n];
            tris.push_back({vertices[iPrev], vertices[iCurr], vertices[iNext]});
            indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) {
            break;
        }
    }

    if (indices.size() == 3) {
        tris.push_back({vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]});
    } else if (indices.size() > 3) {
        for (std::size_t i = 1; i + 1 < indices.size(); ++i) {
            tris.push_back({vertices[indices[0]], vertices[indices[i]], vertices[indices[i + 1]]});
        }
    }
    return tris;
}

std::optional<Brush> makeBrushCylinder(
    std::string id,
    Vector3 mins,
    Vector3 maxs,
    int sides,
    const std::string& material,
    BrushRole role,
    std::string& errorOut) {
    if (sides < 3) {
        errorOut = "cylinder needs at least 3 sides";
        return std::nullopt;
    }
    if (maxs.x <= mins.x || maxs.y <= mins.y || maxs.z <= mins.z) {
        errorOut = "cylinder AABB is empty";
        return std::nullopt;
    }

    const float cx = 0.5f * (mins.x + maxs.x);
    const float cz = 0.5f * (mins.z + maxs.z);
    const float rx = 0.5f * (maxs.x - mins.x);
    const float rz = 0.5f * (maxs.z - mins.z);
    const float y0 = mins.y;
    const float y1 = maxs.y;
    constexpr float kPi = 3.14159265358979323846f;

    std::vector<Vector3> ringBottom;
    std::vector<Vector3> ringTop;
    ringBottom.reserve(static_cast<std::size_t>(sides));
    ringTop.reserve(static_cast<std::size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        const float angle = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(sides);
        const float x = cx + rx * std::cos(angle);
        const float z = cz + rz * std::sin(angle);
        ringBottom.push_back({x, y0, z});
        ringTop.push_back({x, y1, z});
    }

    std::vector<BrushFace> faces;
    faces.reserve(static_cast<std::size_t>(sides) + 2);

    BrushFace bottom;
    bottom.id = id + "/bottom";
    bottom.material = material;
    bottom.vertices = ringBottom;
    std::reverse(bottom.vertices.begin(), bottom.vertices.end());
    faces.push_back(std::move(bottom));

    BrushFace top;
    top.id = id + "/top";
    top.material = material;
    top.vertices = ringTop;
    faces.push_back(std::move(top));

    for (int i = 0; i < sides; ++i) {
        const int next = (i + 1) % sides;
        BrushFace side;
        side.id = id + "/side-" + std::to_string(i);
        side.material = material;
        side.vertices = {
            ringBottom[static_cast<std::size_t>(i)],
            ringBottom[static_cast<std::size_t>(next)],
            ringTop[static_cast<std::size_t>(next)],
            ringTop[static_cast<std::size_t>(i)],
        };
        faces.push_back(std::move(side));
    }

    return makeBrushConvex(std::move(id), std::move(faces), role, errorOut);
}

std::vector<Brush> makeBrushStairs(
    const std::string& idPrefix,
    Vector3 mins,
    Vector3 maxs,
    int steps,
    const std::string& material,
    BrushRole role) {
    std::vector<Brush> out;
    if (steps < 1 || maxs.x <= mins.x || maxs.y <= mins.y || maxs.z <= mins.z) {
        return out;
    }

    const float sizeX = maxs.x - mins.x;
    const float sizeZ = maxs.z - mins.z;
    const bool runAlongX = sizeX >= sizeZ;
    const float stepH = (maxs.y - mins.y) / static_cast<float>(steps);
    const float stepRun =
        (runAlongX ? sizeX : sizeZ) / static_cast<float>(steps);

    out.reserve(static_cast<std::size_t>(steps));
    for (int i = 0; i < steps; ++i) {
        const float t0 = static_cast<float>(i);
        const float t1 = static_cast<float>(i + 1);
        Vector3 stepMins = mins;
        Vector3 stepMaxs = maxs;
        stepMins.y = mins.y;
        stepMaxs.y = mins.y + stepH * t1;
        if (runAlongX) {
            stepMins.x = mins.x + stepRun * t0;
            stepMaxs.x = mins.x + stepRun * t1;
        } else {
            stepMins.z = mins.z + stepRun * t0;
            stepMaxs.z = mins.z + stepRun * t1;
        }
        if (stepMins.x >= stepMaxs.x || stepMins.y >= stepMaxs.y || stepMins.z >= stepMaxs.z) {
            continue;
        }
        out.push_back(makeBrushBox(
            idPrefix + "-" + std::to_string(i),
            stepMins,
            stepMaxs,
            material,
            {},
            role));
    }
    return out;
}

std::vector<Brush> hollowBrushBox(
    const Brush& source,
    float thickness,
    const std::function<std::string()>& allocateId) {
    std::vector<Brush> out;
    if (!source.box || thickness <= 0.0f) {
        return out;
    }
    const Vector3& mins = source.mins;
    const Vector3& maxs = source.maxs;
    const float sx = maxs.x - mins.x;
    const float sy = maxs.y - mins.y;
    const float sz = maxs.z - mins.z;
    if (thickness * 2.0f >= sx || thickness * 2.0f >= sy || thickness * 2.0f >= sz) {
        return out;
    }

    const std::string material =
        source.faces.empty() ? std::string{} : source.faces.front().material;

    auto addSlab = [&](Vector3 slabMins, Vector3 slabMaxs) {
        Brush brush = makeBrushBox(allocateId(), slabMins, slabMaxs, material, {}, source.role);
        brush.nocollide = source.nocollide;
        out.push_back(std::move(brush));
    };

    addSlab(mins, {maxs.x, mins.y + thickness, maxs.z});
    addSlab({mins.x, maxs.y - thickness, mins.z}, maxs);
    addSlab(
        {mins.x, mins.y + thickness, mins.z},
        {maxs.x, maxs.y - thickness, mins.z + thickness});
    addSlab(
        {mins.x, mins.y + thickness, maxs.z - thickness},
        {maxs.x, maxs.y - thickness, maxs.z});
    addSlab(
        {mins.x, mins.y + thickness, mins.z + thickness},
        {mins.x + thickness, maxs.y - thickness, maxs.z - thickness});
    addSlab(
        {maxs.x - thickness, mins.y + thickness, mins.z + thickness},
        {maxs.x, maxs.y - thickness, maxs.z - thickness});
    return out;
}

namespace {

struct FaceAxes {
    Vector3 origin{};
    Vector3 uAxis{};
    Vector3 vAxis{};
    Vector3 normal{};
    float uExtent = 0.0f;
    float vExtent = 0.0f;
    float depthExtent = 0.0f;
};

FaceAxes axesForBoxSide(BrushBoxSide side, Vector3 mins, Vector3 maxs) {
    FaceAxes axes;
    switch (side) {
    case BrushBoxSide::Top:
        axes.origin = {mins.x, maxs.y, mins.z};
        axes.uAxis = {1.0f, 0.0f, 0.0f};
        axes.vAxis = {0.0f, 0.0f, 1.0f};
        axes.normal = {0.0f, 1.0f, 0.0f};
        axes.uExtent = maxs.x - mins.x;
        axes.vExtent = maxs.z - mins.z;
        axes.depthExtent = maxs.y - mins.y;
        break;
    case BrushBoxSide::Bottom:
        axes.origin = {mins.x, mins.y, maxs.z};
        axes.uAxis = {1.0f, 0.0f, 0.0f};
        axes.vAxis = {0.0f, 0.0f, -1.0f};
        axes.normal = {0.0f, -1.0f, 0.0f};
        axes.uExtent = maxs.x - mins.x;
        axes.vExtent = maxs.z - mins.z;
        axes.depthExtent = maxs.y - mins.y;
        break;
    case BrushBoxSide::North:
        axes.origin = {maxs.x, mins.y, mins.z};
        axes.uAxis = {-1.0f, 0.0f, 0.0f};
        axes.vAxis = {0.0f, 1.0f, 0.0f};
        axes.normal = {0.0f, 0.0f, -1.0f};
        axes.uExtent = maxs.x - mins.x;
        axes.vExtent = maxs.y - mins.y;
        axes.depthExtent = maxs.z - mins.z;
        break;
    case BrushBoxSide::South:
        axes.origin = {mins.x, mins.y, maxs.z};
        axes.uAxis = {1.0f, 0.0f, 0.0f};
        axes.vAxis = {0.0f, 1.0f, 0.0f};
        axes.normal = {0.0f, 0.0f, 1.0f};
        axes.uExtent = maxs.x - mins.x;
        axes.vExtent = maxs.y - mins.y;
        axes.depthExtent = maxs.z - mins.z;
        break;
    case BrushBoxSide::East:
        axes.origin = {maxs.x, mins.y, maxs.z};
        axes.uAxis = {0.0f, 0.0f, -1.0f};
        axes.vAxis = {0.0f, 1.0f, 0.0f};
        axes.normal = {1.0f, 0.0f, 0.0f};
        axes.uExtent = maxs.z - mins.z;
        axes.vExtent = maxs.y - mins.y;
        axes.depthExtent = maxs.x - mins.x;
        break;
    case BrushBoxSide::West:
        axes.origin = {mins.x, mins.y, mins.z};
        axes.uAxis = {0.0f, 0.0f, 1.0f};
        axes.vAxis = {0.0f, 1.0f, 0.0f};
        axes.normal = {-1.0f, 0.0f, 0.0f};
        axes.uExtent = maxs.z - mins.z;
        axes.vExtent = maxs.y - mins.y;
        axes.depthExtent = maxs.x - mins.x;
        break;
    }
    return axes;
}

void addPunchSlab(
    std::vector<Brush>& out,
    const Brush& source,
    const std::string& material,
    const std::function<std::string()>& allocateId,
    Vector3 mins,
    Vector3 maxs) {
    constexpr float kEps = 1e-5f;
    if (maxs.x - mins.x <= kEps || maxs.y - mins.y <= kEps || maxs.z - mins.z <= kEps) {
        return;
    }
    Brush brush = makeBrushBox(allocateId(), mins, maxs, material, {}, source.role);
    brush.nocollide = source.nocollide;
    out.push_back(std::move(brush));
}

} // namespace

std::vector<Brush> punchOutBrushBox(
    const Brush& source,
    BrushBoxSide faceSide,
    float u0,
    float u1,
    float v0,
    float v1,
    float depth,
    const std::function<std::string()>& allocateId) {
    std::vector<Brush> out;
    if (!source.box || depth <= 0.0f) {
        return out;
    }

    const float ou0 = std::min(u0, u1);
    const float ou1 = std::max(u0, u1);
    const float ov0 = std::min(v0, v1);
    const float ov1 = std::max(v0, v1);
    if (ou1 - ou0 <= 1e-5f || ov1 - ov0 <= 1e-5f) {
        return out;
    }

    const FaceAxes axes = axesForBoxSide(faceSide, source.mins, source.maxs);
    const float depthClamped = std::min(depth, axes.depthExtent);
    if (depthClamped <= 1e-5f) {
        return out;
    }

    const float cu0 = std::clamp(ou0, 0.0f, axes.uExtent);
    const float cu1 = std::clamp(ou1, 0.0f, axes.uExtent);
    const float cv0 = std::clamp(ov0, 0.0f, axes.vExtent);
    const float cv1 = std::clamp(ov1, 0.0f, axes.vExtent);
    if (cu1 - cu0 <= 1e-5f || cv1 - cv0 <= 1e-5f) {
        return out;
    }

    const std::string material =
        source.faces.empty() ? std::string{} : source.faces.front().material;

    // Build the cut volume in world AABB by sampling the opening prism corners.
    auto worldPoint = [&](float u, float v, float d) -> Vector3 {
        return {
            axes.origin.x + axes.uAxis.x * u + axes.vAxis.x * v - axes.normal.x * d,
            axes.origin.y + axes.uAxis.y * u + axes.vAxis.y * v - axes.normal.y * d,
            axes.origin.z + axes.uAxis.z * u + axes.vAxis.z * v - axes.normal.z * d,
        };
    };

    Vector3 holeMins{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vector3 holeMaxs{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    for (float u : {cu0, cu1}) {
        for (float v : {cv0, cv1}) {
            for (float d : {0.0f, depthClamped}) {
                const Vector3 p = worldPoint(u, v, d);
                holeMins.x = std::min(holeMins.x, p.x);
                holeMins.y = std::min(holeMins.y, p.y);
                holeMins.z = std::min(holeMins.z, p.z);
                holeMaxs.x = std::max(holeMaxs.x, p.x);
                holeMaxs.y = std::max(holeMaxs.y, p.y);
                holeMaxs.z = std::max(holeMaxs.z, p.z);
            }
        }
    }

    holeMins.x = std::max(holeMins.x, source.mins.x);
    holeMins.y = std::max(holeMins.y, source.mins.y);
    holeMins.z = std::max(holeMins.z, source.mins.z);
    holeMaxs.x = std::min(holeMaxs.x, source.maxs.x);
    holeMaxs.y = std::min(holeMaxs.y, source.maxs.y);
    holeMaxs.z = std::min(holeMaxs.z, source.maxs.z);

    const Vector3& b0 = source.mins;
    const Vector3& b1 = source.maxs;

    // Split into up to 6 slabs: leftover behind the cut, then 4 surround pieces in the cut slab.
    const bool cutX = std::fabs(axes.normal.x) > 0.9f;
    const bool cutY = std::fabs(axes.normal.y) > 0.9f;
    const bool cutZ = std::fabs(axes.normal.z) > 0.9f;

    if (cutX) {
        if (axes.normal.x > 0.0f) {
            addPunchSlab(out, source, material, allocateId, b0, {holeMins.x, b1.y, b1.z});
            if (holeMaxs.x < b1.x - 1e-5f) {
                addPunchSlab(out, source, material, allocateId, {holeMaxs.x, b0.y, b0.z}, b1);
            }
        } else {
            addPunchSlab(out, source, material, allocateId, {holeMaxs.x, b0.y, b0.z}, b1);
            if (holeMins.x > b0.x + 1e-5f) {
                addPunchSlab(out, source, material, allocateId, b0, {holeMins.x, b1.y, b1.z});
            }
        }
        const float x0 = holeMins.x;
        const float x1 = holeMaxs.x;
        addPunchSlab(out, source, material, allocateId, {x0, b0.y, b0.z}, {x1, holeMins.y, b1.z});
        addPunchSlab(out, source, material, allocateId, {x0, holeMaxs.y, b0.z}, {x1, b1.y, b1.z});
        addPunchSlab(
            out, source, material, allocateId, {x0, holeMins.y, b0.z}, {x1, holeMaxs.y, holeMins.z});
        addPunchSlab(
            out, source, material, allocateId, {x0, holeMins.y, holeMaxs.z}, {x1, holeMaxs.y, b1.z});
    } else if (cutY) {
        if (axes.normal.y > 0.0f) {
            addPunchSlab(out, source, material, allocateId, b0, {b1.x, holeMins.y, b1.z});
            if (holeMaxs.y < b1.y - 1e-5f) {
                addPunchSlab(out, source, material, allocateId, {b0.x, holeMaxs.y, b0.z}, b1);
            }
        } else {
            addPunchSlab(out, source, material, allocateId, {b0.x, holeMaxs.y, b0.z}, b1);
            if (holeMins.y > b0.y + 1e-5f) {
                addPunchSlab(out, source, material, allocateId, b0, {b1.x, holeMins.y, b1.z});
            }
        }
        const float y0 = holeMins.y;
        const float y1 = holeMaxs.y;
        addPunchSlab(out, source, material, allocateId, {b0.x, y0, b0.z}, {holeMins.x, y1, b1.z});
        addPunchSlab(out, source, material, allocateId, {holeMaxs.x, y0, b0.z}, {b1.x, y1, b1.z});
        addPunchSlab(
            out, source, material, allocateId, {holeMins.x, y0, b0.z}, {holeMaxs.x, y1, holeMins.z});
        addPunchSlab(
            out, source, material, allocateId, {holeMins.x, y0, holeMaxs.z}, {holeMaxs.x, y1, b1.z});
    } else if (cutZ) {
        if (axes.normal.z > 0.0f) {
            addPunchSlab(out, source, material, allocateId, b0, {b1.x, b1.y, holeMins.z});
            if (holeMaxs.z < b1.z - 1e-5f) {
                addPunchSlab(out, source, material, allocateId, {b0.x, b0.y, holeMaxs.z}, b1);
            }
        } else {
            addPunchSlab(out, source, material, allocateId, {b0.x, b0.y, holeMaxs.z}, b1);
            if (holeMins.z > b0.z + 1e-5f) {
                addPunchSlab(out, source, material, allocateId, b0, {b1.x, b1.y, holeMins.z});
            }
        }
        const float z0 = holeMins.z;
        const float z1 = holeMaxs.z;
        addPunchSlab(out, source, material, allocateId, {b0.x, b0.y, z0}, {holeMins.x, b1.y, z1});
        addPunchSlab(out, source, material, allocateId, {holeMaxs.x, b0.y, z0}, {b1.x, b1.y, z1});
        addPunchSlab(
            out, source, material, allocateId, {holeMins.x, b0.y, z0}, {holeMaxs.x, holeMins.y, z1});
        addPunchSlab(
            out, source, material, allocateId, {holeMins.x, holeMaxs.y, z0}, {holeMaxs.x, b1.y, z1});
    }

    return out;
}

BrushBoxSide brushBoxSideFromNormal(Vector3 normal) {
    if (std::fabs(normal.y) >= std::fabs(normal.x) && std::fabs(normal.y) >= std::fabs(normal.z)) {
        return normal.y >= 0.0f ? BrushBoxSide::Top : BrushBoxSide::Bottom;
    }
    if (std::fabs(normal.z) >= std::fabs(normal.x)) {
        return normal.z >= 0.0f ? BrushBoxSide::South : BrushBoxSide::North;
    }
    return normal.x >= 0.0f ? BrushBoxSide::East : BrushBoxSide::West;
}

}
