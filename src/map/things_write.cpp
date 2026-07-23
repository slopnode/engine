#include "map/things_write.hpp"

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

void writeCommonPose(std::ostringstream& out, const Thing& p) {
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
    } else if (p.yaw != 0.0f || p.kind == ThingKind::PlayerStart ||
               p.kind == ThingKind::Prop || p.kind == ThingKind::Usable ||
               p.kind == ThingKind::Actor || p.kind == ThingKind::Mover ||
               p.kind == ThingKind::Trigger || p.kind == ThingKind::SpotLight) {
        writeIndentClause(out, "(yaw " + formatFloat(p.yaw) + ")");
    }
}

void writeMoverFields(std::ostringstream& out, const Thing& p) {
    if (p.haveMoverPivot) {
        writeIndentClause(
            out,
            "(pivot " + formatFloat(p.moverPivot.x) + " " + formatFloat(p.moverPivot.y) + " " +
                formatFloat(p.moverPivot.z) + ")");
    }
    if (p.haveMoverOpenOffset) {
        writeIndentClause(
            out,
            "(open-offset " + formatFloat(p.moverOpenOffset.x) + " " +
                formatFloat(p.moverOpenOffset.y) + " " + formatFloat(p.moverOpenOffset.z) + ")");
    }
    if (p.haveMoverOpenAngle) {
        const char* tag = "open-yaw";
        if (p.moverRotAxis == 0) {
            tag = "open-pitch";
        } else if (p.moverRotAxis == 2) {
            tag = "open-roll";
        }
        writeIndentClause(out, std::string("(") + tag + " " + formatFloat(p.moverOpenAngle) + ")");
    }
    if (p.haveMoverDuration) {
        writeIndentClause(out, "(duration " + formatFloat(p.moverDuration) + ")");
    }
    if (p.haveMoverAutoClose) {
        writeIndentClause(out, "(auto-close " + formatFloat(p.moverAutoClose) + ")");
    }
    if (p.haveMoverCollideSize) {
        writeIndentClause(
            out,
            "(collide-size " + formatFloat(p.moverCollideSize.x) + " " +
                formatFloat(p.moverCollideSize.y) + " " + formatFloat(p.moverCollideSize.z) + ")");
    }
    if (p.haveMoverCollideCenter) {
        writeIndentClause(
            out,
            "(collide-center " + formatFloat(p.moverCollideCenter.x) + " " +
                formatFloat(p.moverCollideCenter.y) + " " + formatFloat(p.moverCollideCenter.z) +
                ")");
    }
    if (!p.moverBlockMode.empty() && p.moverBlockMode != "shove") {
        writeIndentClause(out, "(block-mode " + escapeSchemeString(p.moverBlockMode) + ")");
    } else if (p.kind == ThingKind::Mover) {
        writeIndentClause(out, "(block-mode \"shove\")");
    }
    if (!p.onCrush.empty()) {
        writeIndentClause(out, "(on-crush " + escapeSchemeString(p.onCrush) + ")");
    }
    if (!p.moverGroup.empty()) {
        writeIndentClause(out, "(group " + escapeSchemeString(p.moverGroup) + ")");
    }
    if (p.havePrompt || !p.onUse.empty()) {
        writeIndentClause(out, "(prompt " + escapeSchemeString(p.prompt) + ")");
        if (!p.onUse.empty()) {
            writeIndentClause(out, "(on-use " + escapeSchemeString(p.onUse) + ")");
        }
    }
}

void writeActorFields(std::ostringstream& out, const Thing& p) {
    if (p.haveMotor || p.motorRadius != 0.3f || p.motorHeight != 1.1f || p.motorSpeed != 6.0f ||
        p.motorGravity != 9.81f || p.motorStepHeight != 0.4f ||
        p.motorHull != CharacterHull::Capsule || p.motorMoveMode != CharacterMoveMode::Slide) {
        std::string clause = "(motor (radius " + formatFloat(p.motorRadius) + ") (height " +
            formatFloat(p.motorHeight) + ") (speed " + formatFloat(p.motorSpeed) + ") (gravity " +
            formatFloat(p.motorGravity) + ") (step-height " + formatFloat(p.motorStepHeight) + ")";
        if (p.motorHull == CharacterHull::Box) {
            clause += " (hull box)";
        } else {
            clause += " (hull capsule)";
        }
        if (p.motorMoveMode == CharacterMoveMode::TryMove) {
            clause += " (move try-move)";
        } else {
            clause += " (move slide)";
        }
        clause += ")";
        writeIndentClause(out, clause);
    }
    if (!p.tags.empty()) {
        std::string clause = "(tags";
        for (const std::string& tag : p.tags) {
            clause += " " + escapeSchemeString(tag);
        }
        clause += ")";
        writeIndentClause(out, clause);
    }
}

void writePresentation(std::ostringstream& out, const Thing& p) {
    if (!p.sprite.empty()) {
        writeIndentClause(out, "(sprite " + escapeSchemeString(p.sprite) + ")");
    }
    if (!p.geo.empty()) {
        writeIndentClause(out, "(geo " + escapeSchemeString(p.geo) + ")");
    }
    if (!p.sprite.empty() && p.frame != "A") {
        writeIndentClause(out, "(frame " + escapeSchemeString(p.frame) + ")");
    } else if (!p.sprite.empty() && p.kind == ThingKind::Usable) {
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

void writeLightFields(std::ostringstream& out, const Thing& p) {
    writeIndentClause(
        out,
        "(color " + formatFloat(p.color.x) + " " + formatFloat(p.color.y) + " " +
            formatFloat(p.color.z) + ")");
    writeIndentClause(out, "(intensity " + formatFloat(p.intensity) + ")");
    if (p.kind == ThingKind::PointLight || p.kind == ThingKind::SpotLight) {
        writeIndentClause(out, "(range " + formatFloat(p.range) + ")");
    }
    if (p.kind == ThingKind::SpotLight) {
        writeIndentClause(out, "(cone " + formatFloat(p.coneAngle) + ")");
    }
    if (p.kind == ThingKind::AreaLight) {
        writeIndentClause(
            out,
            "(size " + formatFloat(p.size.x) + " " + formatFloat(p.size.y) + ")");
    }
}

void writeSoundSourceFields(std::ostringstream& out, const Thing& p) {
    if (!p.audio.empty()) {
        writeIndentClause(out, "(audio " + escapeSchemeString(p.audio) + ")");
    }
    if (!p.clip.empty()) {
        writeIndentClause(out, "(clip " + escapeSchemeString(p.clip) + ")");
    }
    writeIndentClause(out, "(volume " + formatFloat(p.volume) + ")");
    writeIndentClause(out, std::string("(loop ") + (p.looping ? "#t" : "#f") + ")");
    writeIndentClause(out, std::string("(spatial ") + (p.spatial ? "#t" : "#f") + ")");
    writeIndentClause(out, "(min-distance " + formatFloat(p.minDistance) + ")");
    writeIndentClause(out, "(max-distance " + formatFloat(p.maxDistance) + ")");
}

void writeTriggerFields(std::ostringstream& out, const Thing& p) {
    if (!p.onEnter.empty()) {
        writeIndentClause(out, "(on-enter " + escapeSchemeString(p.onEnter) + ")");
    }
    if (!p.onExit.empty()) {
        writeIndentClause(out, "(on-exit " + escapeSchemeString(p.onExit) + ")");
    }
    if (p.haveTriggerSize || p.kind == ThingKind::Trigger || !p.onEnter.empty() ||
        !p.onExit.empty()) {
        writeIndentClause(
            out,
            "(trigger-size " + formatFloat(p.triggerSize.x) + " " +
                formatFloat(p.triggerSize.y) + " " + formatFloat(p.triggerSize.z) + ")");
    }
    if (!p.collideTags.empty()) {
        std::string clause = "(collide-tags";
        for (const std::string& tag : p.collideTags) {
            clause += " " + escapeSchemeString(tag);
        }
        clause += ")";
        writeIndentClause(out, clause);
    }
}

void writeThing(std::ostringstream& out, const Thing& p) {
    if (p.kind == ThingKind::Prefab) {
        out << "(prefab " << escapeSchemeString(p.prefabPath) << "\n";
        writeCommonPose(out, p);
        out << ")\n\n";
        return;
    }

    out << "(" << thingKindName(p.kind) << "\n";
    writeCommonPose(out, p);

    if (thingKindNeedsPresentation(p.kind)) {
        writePresentation(out, p);
    }
    if (p.kind == ThingKind::Usable) {
        writeIndentClause(out, "(prompt " + escapeSchemeString(p.prompt) + ")");
        if (!p.onUse.empty()) {
            writeIndentClause(out, "(on-use " + escapeSchemeString(p.onUse) + ")");
        }
    }
    if (p.kind == ThingKind::Mover) {
        writeMoverFields(out, p);
    }
    if (p.kind == ThingKind::Actor) {
        writeActorFields(out, p);
    }
    if (p.kind == ThingKind::Trigger || !p.onEnter.empty() || !p.onExit.empty() ||
        p.haveTriggerSize || !p.collideTags.empty()) {
        writeTriggerFields(out, p);
    }
    if (thingKindIsLight(p.kind)) {
        writeLightFields(out, p);
    }
    if (p.kind == ThingKind::SoundSource) {
        writeSoundSourceFields(out, p);
    }

    out << ")\n\n";
}

} // namespace

bool writeMapThings(const std::filesystem::path& path, const ThingDocument& doc) {
    std::ostringstream out;
    for (const Thing& placement : doc.things) {
        writeThing(out, placement);
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << out.str();
    return static_cast<bool>(file);
}

}
