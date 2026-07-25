#include "map/csg_write.hpp"

#include "map/uv_math.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace slopengine {

namespace {

std::string formatFloat(float value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
    return buf;
}

bool faceHasCustomUvAxes(const BrushFace& face) {
    if (!face.uvLock) {
        return false;
    }
    Vector3 axialU{};
    Vector3 axialV{};
    axialUvAxes(face.normal, axialU, axialV);
    constexpr float kEps = 1e-4f;
    const float du = std::fabs(face.uvUAxis.x - axialU.x) + std::fabs(face.uvUAxis.y - axialU.y) +
        std::fabs(face.uvUAxis.z - axialU.z);
    const float dv = std::fabs(face.uvVAxis.x - axialV.x) + std::fabs(face.uvVAxis.y - axialV.y) +
        std::fabs(face.uvVAxis.z - axialV.z);
    return du > kEps || dv > kEps;
}

void writeUvAxesClause(std::ostringstream& out, const BrushFace& face) {
    out << "(uv-axes " << formatFloat(face.uvUAxis.x) << " " << formatFloat(face.uvUAxis.y) << " "
        << formatFloat(face.uvUAxis.z) << " " << formatFloat(face.uvVAxis.x) << " "
        << formatFloat(face.uvVAxis.y) << " " << formatFloat(face.uvVAxis.z) << ")";
}

std::string formatVec3(Vector3 v) {
    return formatFloat(v.x) + " " + formatFloat(v.y) + " " + formatFloat(v.z);
}

std::string escapeSchemeString(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char c : value) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string defaultMaterialForBrush(const Brush& brush) {
    if (brush.faces.empty()) {
        return "default/cube";
    }
    return brush.faces.front().material.empty() ? "default/cube" : brush.faces.front().material;
}

const BrushFace* findBoxFace(const Brush& brush, BrushBoxSide side) {
    const std::string suffix = std::string("/") + brushBoxSideName(side);
    for (const BrushFace& face : brush.faces) {
        if (face.id.size() >= suffix.size() &&
            face.id.compare(face.id.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return &face;
        }
    }
    return nullptr;
}

bool faceNeedsOverride(const BrushFace& face, const std::string& brushId, BrushBoxSide side, const std::string& defaultMaterial) {
    const std::string expectedId = brushId + "/" + brushBoxSideName(side);
    if (face.id != expectedId) {
        return true;
    }
    if (face.material != defaultMaterial) {
        return true;
    }
    if (face.nodraw) {
        return true;
    }
    if (face.uvLock) {
        return true;
    }
    if (faceHasCustomUvAxes(face)) {
        return true;
    }
    if (face.uvShiftPixels.x != 0.0f || face.uvShiftPixels.y != 0.0f) {
        return true;
    }
    if (face.uvScale.x != 1.0f || face.uvScale.y != 1.0f) {
        return true;
    }
    if (!face.onUse.empty()) {
        return true;
    }
    return false;
}

void writeFaceOverride(
    std::ostringstream& out,
    const std::string& brushId,
    BrushBoxSide side,
    const BrushFace& face,
    const std::string& defaultMaterial) {
    const std::string expectedId = brushId + "/" + brushBoxSideName(side);
    out << "    (" << brushBoxSideName(side);
    if (!face.id.empty() && face.id != expectedId) {
        out << "\n      (id " << escapeSchemeString(face.id) << ")";
    }
    if (!face.material.empty() && face.material != defaultMaterial) {
        out << "\n      (material " << escapeSchemeString(face.material) << ")";
    }
    if (face.uvShiftPixels.x != 0.0f || face.uvShiftPixels.y != 0.0f) {
        out << "\n      (uv-shift " << formatFloat(face.uvShiftPixels.x) << " "
            << formatFloat(face.uvShiftPixels.y) << ")";
    }
    if (face.uvScale.x != 1.0f || face.uvScale.y != 1.0f) {
        out << "\n      (uv-scale " << formatFloat(face.uvScale.x) << " "
            << formatFloat(face.uvScale.y) << ")";
    }
    if (face.nodraw) {
        out << "\n      (nodraw)";
    }
    if (face.uvLock) {
        out << "\n      (uv-lock)";
    }
    if (faceHasCustomUvAxes(face)) {
        out << "\n      ";
        writeUvAxesClause(out, face);
    }
    if (!face.onUse.empty()) {
        out << "\n      (on-use " << escapeSchemeString(face.onUse) << ")";
    }
    out << ")\n";
}

void writeBrushBox(std::ostringstream& out, const Brush& brush) {
    const std::string material = defaultMaterialForBrush(brush);
    out << "(brush-box\n";
    out << "  (id " << escapeSchemeString(brush.id) << ")\n";
    out << "  (mins " << formatVec3(brush.mins) << ")\n";
    out << "  (maxs " << formatVec3(brush.maxs) << ")\n";
    out << "  (material " << escapeSchemeString(material) << ")\n";
    if (brush.role != BrushRole::Hull) {
        out << "  (role \"" << brushRoleName(brush.role) << "\")\n";
    }
    if (brush.nocollide && !brushRoleDefaultNocollide(brush.role)) {
        out << "  (nocollide)\n";
    }

    constexpr BrushBoxSide kSides[] = {
        BrushBoxSide::Top,
        BrushBoxSide::Bottom,
        BrushBoxSide::North,
        BrushBoxSide::South,
        BrushBoxSide::East,
        BrushBoxSide::West,
    };

    bool anyOverride = false;
    for (BrushBoxSide side : kSides) {
        const BrushFace* face = findBoxFace(brush, side);
        if (face != nullptr && faceNeedsOverride(*face, brush.id, side, material)) {
            anyOverride = true;
            break;
        }
    }

    if (anyOverride) {
        out << "  (faces\n";
        for (BrushBoxSide side : kSides) {
            const BrushFace* face = findBoxFace(brush, side);
            if (face == nullptr || !faceNeedsOverride(*face, brush.id, side, material)) {
                continue;
            }
            writeFaceOverride(out, brush.id, side, *face, material);
        }
        out << "  )\n";
    }

    out << ")\n\n";
}

void writeBrushConvex(std::ostringstream& out, const Brush& brush) {
    out << "(brush-convex\n";
    out << "  (id " << escapeSchemeString(brush.id) << ")\n";
    if (brush.role != BrushRole::Hull) {
        out << "  (role \"" << brushRoleName(brush.role) << "\")\n";
    }
    if (brush.nocollide && !brushRoleDefaultNocollide(brush.role)) {
        out << "  (nocollide)\n";
    }

    const std::string material = defaultMaterialForBrush(brush);
    bool allSameMaterial = true;
    for (const BrushFace& face : brush.faces) {
        if (face.material != material) {
            allSameMaterial = false;
            break;
        }
    }
    if (allSameMaterial && !material.empty()) {
        out << "  (material " << escapeSchemeString(material) << ")\n";
    }

    out << "  (faces\n";
    for (const BrushFace& face : brush.faces) {
        out << "    (face\n";
        if (!face.id.empty()) {
            out << "      (id " << escapeSchemeString(face.id) << ")\n";
        }
        if (!allSameMaterial && !face.material.empty()) {
            out << "      (material " << escapeSchemeString(face.material) << ")\n";
        }
        if (face.uvShiftPixels.x != 0.0f || face.uvShiftPixels.y != 0.0f) {
            out << "      (uv-shift " << formatFloat(face.uvShiftPixels.x) << " "
                << formatFloat(face.uvShiftPixels.y) << ")\n";
        }
        if (face.uvScale.x != 1.0f || face.uvScale.y != 1.0f) {
            out << "      (uv-scale " << formatFloat(face.uvScale.x) << " "
                << formatFloat(face.uvScale.y) << ")\n";
        }
        if (face.nodraw) {
            out << "      (nodraw)\n";
        }
        if (face.uvLock) {
            out << "      (uv-lock)\n";
        }
        if (faceHasCustomUvAxes(face)) {
            out << "      ";
            writeUvAxesClause(out, face);
            out << "\n";
        }
        if (!face.onUse.empty()) {
            out << "      (on-use " << escapeSchemeString(face.onUse) << ")\n";
        }
        out << "      (verts";
        for (const Vector3& v : face.vertices) {
            out << " (v " << formatVec3(v) << ")";
        }
        out << "))\n";
    }
    out << "  )\n";
    out << ")\n\n";
}

void writePrefabInstance(std::ostringstream& out, const PrefabInstance& instance) {
    out << "(prefab " << escapeSchemeString(instance.path) << "\n";
    out << "  (id " << escapeSchemeString(instance.id) << ")\n";
    out << "  (at " << formatVec3(instance.at) << ")\n";
    out << "  (angles " << formatVec3(instance.angles) << ")\n";
    out << ")\n\n";
}

std::string buildBrushesText(const std::vector<Brush>& brushes) {
    std::ostringstream out;
    for (const Brush& brush : brushes) {
        if (brush.box) {
            writeBrushBox(out, brush);
        } else {
            writeBrushConvex(out, brush);
        }
    }
    return out.str();
}

std::string buildMapCsgDocumentText(
    const std::vector<Brush>& brushes,
    const std::vector<PrefabInstance>& instances) {
    std::ostringstream out;
    out << buildBrushesText(brushes);
    for (const PrefabInstance& instance : instances) {
        writePrefabInstance(out, instance);
    }
    return out.str();
}

} // namespace

std::string brushesToCsgText(const std::vector<Brush>& brushes) {
    return buildBrushesText(brushes);
}

std::string mapCsgDocumentToText(
    const std::vector<Brush>& brushes,
    const std::vector<PrefabInstance>& instances) {
    return buildMapCsgDocumentText(brushes, instances);
}

bool writeMapBrushes(const std::filesystem::path& path, const std::vector<Brush>& brushes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << brushesToCsgText(brushes);
    return static_cast<bool>(file);
}

bool writeMapCsgDocument(
    const std::filesystem::path& path,
    const std::vector<Brush>& brushes,
    const std::vector<PrefabInstance>& instances) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << mapCsgDocumentToText(brushes, instances);
    return static_cast<bool>(file);
}

}
