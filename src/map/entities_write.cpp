#include "map/entities_write.hpp"

#include <cmath>
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

void writeIndentClause(std::ostringstream& out, const std::string& clause) {
    out << "  " << clause << "\n";
}

void writeCommonPose(std::ostringstream& out, const Placement& p) {
    writeIndentClause(out, "(id " + escapeSchemeString(p.id) + ")");
    if (p.haveAt) {
        writeIndentClause(
            out,
            "(at " + formatFloat(p.at.x) + " " + formatFloat(p.at.y) + " " + formatFloat(p.at.z) +
                ")");
    }
    if (p.haveAngles) {
        writeIndentClause(
            out,
            "(angles " + formatFloat(p.angles.x) + " " + formatFloat(p.angles.y) + " " +
                formatFloat(p.angles.z) + ")");
    } else if (p.yaw != 0.0f || p.kind == PlacementKind::PlayerStart ||
               p.kind == PlacementKind::Prop || p.kind == PlacementKind::Usable ||
               p.kind == PlacementKind::SpotLight) {
        writeIndentClause(out, "(yaw " + formatFloat(p.yaw) + ")");
    }
}

void writePresentation(std::ostringstream& out, const Placement& p) {
    if (!p.sprite.empty()) {
        writeIndentClause(out, "(sprite " + escapeSchemeString(p.sprite) + ")");
    }
    if (!p.geo.empty()) {
        writeIndentClause(out, "(geo " + escapeSchemeString(p.geo) + ")");
    }
    if (!p.sprite.empty() && p.frame != "A") {
        writeIndentClause(out, "(frame " + escapeSchemeString(p.frame) + ")");
    } else if (!p.sprite.empty() && p.kind == PlacementKind::Usable) {
        writeIndentClause(out, "(frame " + escapeSchemeString(p.frame) + ")");
    }
    if (p.haveAnim) {
        std::string clause = "(anim " + escapeSchemeString(p.animClip);
        if (!p.animLoop) {
            clause += " #f";
        } else {
            clause += " #t";
        }
        clause += ")";
        writeIndentClause(out, clause);
    }
}

void writeLightFields(std::ostringstream& out, const Placement& p) {
    writeIndentClause(
        out,
        "(color " + formatFloat(p.color.x) + " " + formatFloat(p.color.y) + " " +
            formatFloat(p.color.z) + ")");
    writeIndentClause(out, "(intensity " + formatFloat(p.intensity) + ")");
    if (p.kind == PlacementKind::PointLight || p.kind == PlacementKind::SpotLight) {
        writeIndentClause(out, "(range " + formatFloat(p.range) + ")");
    }
    if (p.kind == PlacementKind::SpotLight) {
        writeIndentClause(out, "(cone " + formatFloat(p.coneAngle) + ")");
    }
    if (p.kind == PlacementKind::AreaLight) {
        writeIndentClause(
            out,
            "(size " + formatFloat(p.size.x) + " " + formatFloat(p.size.y) + ")");
    }
}

void writePlacement(std::ostringstream& out, const Placement& p) {
    if (p.kind == PlacementKind::Prefab) {
        out << "(prefab " << escapeSchemeString(p.prefabPath) << "\n";
        writeCommonPose(out, p);
        out << ")\n\n";
        return;
    }

    out << "(" << placementKindName(p.kind) << "\n";
    writeCommonPose(out, p);

    if (placementKindNeedsPresentation(p.kind)) {
        writePresentation(out, p);
    }
    if (p.kind == PlacementKind::Usable) {
        writeIndentClause(out, "(prompt " + escapeSchemeString(p.prompt) + ")");
        if (!p.onUse.empty()) {
            writeIndentClause(out, "(on-use " + escapeSchemeString(p.onUse) + ")");
        }
    }
    if (placementKindIsLight(p.kind)) {
        writeLightFields(out, p);
    }

    out << ")\n\n";
}

} // namespace

bool writeMapEntities(const std::filesystem::path& path, const PlacementDocument& doc) {
    std::ostringstream out;
    for (const Placement& placement : doc.placements) {
        writePlacement(out, placement);
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << out.str();
    return static_cast<bool>(file);
}

}
