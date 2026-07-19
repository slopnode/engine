#include "map/csg_write.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace slopengine {

namespace {

std::string formatFloat(float value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
    return buf;
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
    if (face.uvShiftPixels.x != 0.0f || face.uvShiftPixels.y != 0.0f) {
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
    if (face.nodraw) {
        out << "\n      (nodraw)";
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
    if (brush.role == BrushRole::Detail) {
        out << "  (role \"detail\")\n";
    }
    if (brush.nocollide) {
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
    if (brush.role == BrushRole::Detail) {
        out << "  (role \"detail\")\n";
    }
    if (brush.nocollide) {
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
        if (face.nodraw) {
            out << "      (nodraw)\n";
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

} // namespace

std::string brushesToCsgText(const std::vector<Brush>& brushes) {
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

bool writeMapBrushes(const std::filesystem::path& path, const std::vector<Brush>& brushes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << brushesToCsgText(brushes);
    return static_cast<bool>(file);
}

}
