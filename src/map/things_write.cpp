#include "map/things_write.hpp"

#include "map/handler_binding.hpp"
#include "map/thing_def_registry.hpp"

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

bool tagsEqual(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

std::string formatSightClause(const Thing& p) {
    std::string clause = "(sight (range " + formatFloat(p.sightRange) + ") (fov " +
        formatFloat(p.sightFovDegrees) + ") (eye-lift " + formatFloat(p.sightEyeLift) + ")";
    if (!p.sightSeeTags.empty()) {
        clause += " (see-tags";
        for (const std::string& tag : p.sightSeeTags) {
            clause += " " + escapeSchemeString(tag);
        }
        clause += ")";
    }
    if (!p.sightIgnoreTags.empty()) {
        clause += " (ignore-tags";
        for (const std::string& tag : p.sightIgnoreTags) {
            clause += " " + escapeSchemeString(tag);
        }
        clause += ")";
    }
    if (!p.sightFilterProc.empty()) {
        clause += " (filter " + escapeSchemeString(p.sightFilterProc) + ")";
    }
    clause += p.sightEnabled ? " (enabled #t))" : " (enabled #f))";
    return clause;
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
               p.kind == ThingKind::Pickup || p.kind == ThingKind::Actor ||
               p.kind == ThingKind::Mover || p.kind == ThingKind::Trigger ||
               p.kind == ThingKind::SpotLight || p.kind == ThingKind::Sun ||
               p.kind == ThingKind::DynamicSpotLight || p.kind == ThingKind::Marker ||
               p.kind == ThingKind::Particle) {
        writeIndentClause(out, "(yaw " + formatFloat(p.yaw) + ")");
    }
    if (!p.haveAngles && p.havePitch) {
        writeIndentClause(out, "(pitch " + formatFloat(p.pitch) + ")");
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
    if (!p.moverPush.empty() && p.moverPush != "full") {
        writeIndentClause(out, "(push " + escapeSchemeString(p.moverPush) + ")");
    }
    if (p.haveMoverSlide && !p.moverSlide) {
        writeIndentClause(out, "(carry #f)");
    }
    if (!p.onCrush.empty()) {
        writeIndentClause(out, "(on-crush " + escapeSchemeString(p.onCrush) + ")");
    }
    if (!p.moverGroup.empty()) {
        writeIndentClause(out, "(group " + escapeSchemeString(p.moverGroup) + ")");
    }
    if (!p.moverOpenSound.empty()) {
        writeIndentClause(out, "(open-sound " + escapeSchemeString(p.moverOpenSound) + ")");
    }
    if (!p.moverCloseSound.empty()) {
        writeIndentClause(out, "(close-sound " + escapeSchemeString(p.moverCloseSound) + ")");
    }
    if (p.haveMoverSoundVolume) {
        writeIndentClause(out, "(sound-volume " + formatFloat(p.moverSoundVolume) + ")");
    }
    if (p.havePrompt || !p.onUse.empty()) {
        writeIndentClause(out, "(prompt " + escapeSchemeString(p.prompt) + ")");
        if (!p.onUse.empty()) {
            writeIndentClause(out, formatHandlerBindingClause("on-use", p.onUse));
        }
    }
}

void writeActorFields(std::ostringstream& out, const Thing& p) {
    if (p.haveMotor || p.motorRadius != 0.3f || p.motorHeight != 1.1f || p.motorSpeed != 6.0f ||
        p.motorGravity != 9.81f || p.motorStepHeight != 0.4f ||
        p.motorHull != CharacterHull::Capsule || p.motorMoveMode != CharacterMoveMode::Slide ||
        !p.motorNavProfile.empty()) {
        std::string clause = "(motor (radius " + formatFloat(p.motorRadius) + ") (height " +
            formatFloat(p.motorHeight) + ") (speed " + formatFloat(p.motorSpeed) + ") (gravity " +
            formatFloat(p.motorGravity) + ") (step-height " + formatFloat(p.motorStepHeight) + ")";
        if (p.motorHull == CharacterHull::Box) {
            clause += " (hull box)";
        } else if (p.motorHull == CharacterHull::Sphere) {
            clause += " (hull sphere)";
        } else {
            clause += " (hull capsule)";
        }
        if (p.motorMoveMode == CharacterMoveMode::TryMove) {
            clause += " (move try-move)";
        } else if (p.motorMoveMode == CharacterMoveMode::Fly) {
            clause += " (move fly)";
        } else {
            clause += " (move slide)";
        }
        if (p.motorVerticalSpeed != 3.0f) {
            clause += " (vertical-speed " + formatFloat(p.motorVerticalSpeed) + ")";
        }
        if (p.motorHoverHeight != 0.0f) {
            clause += " (hover-height " + formatFloat(p.motorHoverHeight) + ")";
        }
        if (std::isfinite(p.motorMaxFall)) {
            clause += " (max-fall " + formatFloat(p.motorMaxFall) + ")";
        }
        if (p.motorWaterAversion != 1.0f) {
            clause += " (water-aversion " + formatFloat(p.motorWaterAversion) + ")";
        }
        if (!p.motorNavProfile.empty()) {
            clause += " (nav-profile " + escapeSchemeString(p.motorNavProfile) + ")";
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
    if (p.haveSight) {
        writeIndentClause(out, formatSightClause(p));
    }
}

void writePresentation(std::ostringstream& out, const Thing& p) {
    if (!p.sprite.empty()) {
        writeIndentClause(out, "(sprite " + escapeSchemeString(p.sprite) + ")");
    }
    if (!p.geo.empty()) {
        writeIndentClause(out, "(geo " + escapeSchemeString(p.geo) + ")");
    }
    if (!p.brush.empty()) {
        writeIndentClause(out, "(brush " + escapeSchemeString(p.brush) + ")");
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
    if (p.kind == ThingKind::PointLight || p.kind == ThingKind::SpotLight ||
        p.kind == ThingKind::DynamicPointLight || p.kind == ThingKind::DynamicSpotLight) {
        writeIndentClause(out, "(range " + formatFloat(p.range) + ")");
    }
    if (p.kind == ThingKind::SpotLight || p.kind == ThingKind::DynamicSpotLight) {
        writeIndentClause(out, "(cone " + formatFloat(p.coneAngle) + ")");
    }
    if (p.kind == ThingKind::AreaLight) {
        writeIndentClause(
            out,
            "(size " + formatFloat(p.size.x) + " " + formatFloat(p.size.y) + ")");
    }
    if (p.kind == ThingKind::DynamicPointLight || p.kind == ThingKind::DynamicSpotLight) {
        writeIndentClause(out, p.dynamicCastShadows ? "(cast-shadows #t)" : "(cast-shadows #f)");
    }
}

void writeSkyboxFields(std::ostringstream& out, const Thing& p) {
    if (!p.skyMaterial.empty()) {
        writeIndentClause(out, "(material " + escapeSchemeString(p.skyMaterial) + ")");
    }
    if (!p.haveSkyboxMode) {
        return;
    }
    switch (p.skyboxMode) {
    case SkyboxMode::Solid:
        writeIndentClause(
            out,
            "(color " + formatFloat(p.color.x) + " " + formatFloat(p.color.y) + " " +
                formatFloat(p.color.z) + ")");
        break;
    case SkyboxMode::Cube:
        writeIndentClause(out, "(cube");
        out << "\n";
        writeIndentClause(out, "(px " + escapeSchemeString(p.skyCubePx) + ")");
        writeIndentClause(out, "(nx " + escapeSchemeString(p.skyCubeNx) + ")");
        writeIndentClause(out, "(py " + escapeSchemeString(p.skyCubePy) + ")");
        writeIndentClause(out, "(ny " + escapeSchemeString(p.skyCubeNy) + ")");
        writeIndentClause(out, "(pz " + escapeSchemeString(p.skyCubePz) + ")");
        writeIndentClause(out, "(nz " + escapeSchemeString(p.skyCubeNz) + ")");
        writeIndentClause(out, ")");
        break;
    case SkyboxMode::Cylinder:
        writeIndentClause(out, "(cylinder " + escapeSchemeString(p.skyCylinderTexture) + ")");
        writeIndentClause(out, "(cylinder-offset " + formatFloat(p.skyCylinderOffset) + ")");
        writeIndentClause(out, "(cylinder-scale " + formatFloat(p.skyCylinderScale) + ")");
        writeIndentClause(out, "(cylinder-repeat " + std::to_string(p.skyCylinderRepeat) + ")");
        break;
    case SkyboxMode::Gradient:
        writeIndentClause(out, "(gradient");
        out << "\n";
        for (int i = 0; i < p.skyGradientStopCount; ++i) {
            const SkyGradientStop& stop = p.skyGradientStops[static_cast<std::size_t>(i)];
            writeIndentClause(
                out,
                "(stop " + formatFloat(stop.position) + " " + formatFloat(stop.color.x) + " " +
                    formatFloat(stop.color.y) + " " + formatFloat(stop.color.z) + ")");
        }
        writeIndentClause(out, ")");
        break;
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
        writeIndentClause(out, formatHandlerBindingClause("on-enter", p.onEnter));
    }
    if (!p.onExit.empty()) {
        writeIndentClause(out, formatHandlerBindingClause("on-exit", p.onExit));
    }
    if (p.haveTriggerSize || p.kind == ThingKind::Trigger || !p.onEnter.empty() ||
        !p.onExit.empty()) {
        writeIndentClause(
            out,
            "(trigger-size " + formatFloat(p.triggerSize.x) + " " +
                formatFloat(p.triggerSize.y) + " " + formatFloat(p.triggerSize.z) + ")");
    }
    if (p.triggerOnce) {
        writeIndentClause(out, "(once #t)");
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

bool nearEq(float a, float b) {
    return std::fabs(a - b) <= 0.0001f;
}

bool sightDiffers(const Thing& p, const Thing& baseline) {
    return p.haveSight != baseline.haveSight || p.sightEnabled != baseline.sightEnabled ||
        !nearEq(p.sightRange, baseline.sightRange) ||
        !nearEq(p.sightFovDegrees, baseline.sightFovDegrees) ||
        !nearEq(p.sightEyeLift, baseline.sightEyeLift) ||
        !tagsEqual(p.sightSeeTags, baseline.sightSeeTags) ||
        !tagsEqual(p.sightIgnoreTags, baseline.sightIgnoreTags) ||
        p.sightFilterProc != baseline.sightFilterProc;
}

void writeTypedThing(std::ostringstream& out, const Thing& p, const ThingDef& def) {
    out << "(thing\n";
    writeIndentClause(out, "(type " + escapeSchemeString(p.type) + ")");
    writeCommonPose(out, p);

    Thing baseline{};
    applyThingDef(def, baseline);

    if (p.sprite != baseline.sprite && !p.sprite.empty()) {
        writeIndentClause(out, "(sprite " + escapeSchemeString(p.sprite) + ")");
    }
    if (p.geo != baseline.geo && !p.geo.empty()) {
        writeIndentClause(out, "(geo " + escapeSchemeString(p.geo) + ")");
    }
    if (p.frame != baseline.frame) {
        writeIndentClause(out, "(frame " + escapeSchemeString(p.frame) + ")");
    }
    if (p.haveAnim != baseline.haveAnim || p.animClip != baseline.animClip ||
        p.animLoop != baseline.animLoop) {
        if (p.haveAnim) {
            std::string clause = "(anim " + escapeSchemeString(p.animClip);
            clause += p.animLoop ? " #t)" : " #f)";
            writeIndentClause(out, clause);
        }
    }
    if (p.kind == ThingKind::Actor) {
        const bool motorDiffers = p.haveMotor != baseline.haveMotor ||
            !nearEq(p.motorRadius, baseline.motorRadius) ||
            !nearEq(p.motorHeight, baseline.motorHeight) ||
            !nearEq(p.motorSpeed, baseline.motorSpeed) ||
            !nearEq(p.motorGravity, baseline.motorGravity) ||
            !nearEq(p.motorStepHeight, baseline.motorStepHeight) ||
            p.motorHull != baseline.motorHull || p.motorMoveMode != baseline.motorMoveMode;
        if (motorDiffers) {
            std::string clause = "(motor (radius " + formatFloat(p.motorRadius) + ") (height " +
                formatFloat(p.motorHeight) + ") (speed " + formatFloat(p.motorSpeed) +
                ") (gravity " + formatFloat(p.motorGravity) + ") (step-height " +
                formatFloat(p.motorStepHeight) + ")";
            clause += (p.motorHull == CharacterHull::Box)     ? " (hull box)"
                    : (p.motorHull == CharacterHull::Sphere) ? " (hull sphere)"
                                                             : " (hull capsule)";
            clause +=
                (p.motorMoveMode == CharacterMoveMode::TryMove) ? " (move try-move)"
                                                                : " (move slide)";
            clause += ")";
            writeIndentClause(out, clause);
        }
        if (!tagsEqual(p.tags, baseline.tags) && !p.tags.empty()) {
            std::string clause = "(tags";
            for (const std::string& tag : p.tags) {
                clause += " " + escapeSchemeString(tag);
            }
            clause += ")";
            writeIndentClause(out, clause);
        }
        if (sightDiffers(p, baseline) && p.haveSight) {
            writeIndentClause(out, formatSightClause(p));
        }
    }

    if (p.kind == ThingKind::Pickup) {
        if (!(p.onEnter == baseline.onEnter) && !p.onEnter.empty()) {
            writeIndentClause(out, formatHandlerBindingClause("on-enter", p.onEnter));
        }
        if (!(p.onUse == baseline.onUse) && !p.onUse.empty()) {
            writeIndentClause(out, formatHandlerBindingClause("on-use", p.onUse));
        }
        const bool triggerDiffers = p.haveTriggerSize != baseline.haveTriggerSize ||
            !nearEq(p.triggerSize.x, baseline.triggerSize.x) ||
            !nearEq(p.triggerSize.y, baseline.triggerSize.y) ||
            !nearEq(p.triggerSize.z, baseline.triggerSize.z);
        if (triggerDiffers && p.haveTriggerSize) {
            writeIndentClause(
                out,
                "(trigger-size " + formatFloat(p.triggerSize.x) + " " +
                    formatFloat(p.triggerSize.y) + " " + formatFloat(p.triggerSize.z) + ")");
        }
    }

    out << ")\n\n";
}

void writeThing(std::ostringstream& out, const Thing& p) {
    if (p.kind == ThingKind::Prefab) {
        out << "(prefab " << escapeSchemeString(p.prefabPath) << "\n";
        writeCommonPose(out, p);
        out << ")\n\n";
        return;
    }

    if (!p.type.empty()) {
        const ThingDef* def = thingDefRegistry().find(p.type);
        if (def != nullptr) {
            writeTypedThing(out, p, *def);
            return;
        }
    }

    out << "(" << thingKindName(p.kind) << "\n";
    writeCommonPose(out, p);

    if (thingKindNeedsPresentation(p.kind)) {
        writePresentation(out, p);
    }
    if (p.kind == ThingKind::Usable) {
        writeIndentClause(out, "(prompt " + escapeSchemeString(p.prompt) + ")");
        if (!p.onUse.empty()) {
            writeIndentClause(out, formatHandlerBindingClause("on-use", p.onUse));
        }
    }
    if (p.kind == ThingKind::Pickup && !p.onUse.empty()) {
        writeIndentClause(out, formatHandlerBindingClause("on-use", p.onUse));
    }
    if (p.kind == ThingKind::Mover) {
        writeMoverFields(out, p);
    }
    if (p.kind == ThingKind::Actor) {
        writeActorFields(out, p);
    }
    if (p.kind == ThingKind::Trigger || !p.onEnter.empty() || !p.onExit.empty() ||
        p.haveTriggerSize || p.triggerOnce || !p.collideTags.empty()) {
        writeTriggerFields(out, p);
    }
    if (thingKindIsLight(p.kind)) {
        writeLightFields(out, p);
    }
    if (p.kind == ThingKind::Skybox) {
        writeSkyboxFields(out, p);
    }
    if (p.kind == ThingKind::SoundSource) {
        writeSoundSourceFields(out, p);
    }
    if (p.kind == ThingKind::Particle) {
        writeIndentClause(out, "(system " + escapeSchemeString(p.particleSystem) + ")");
        if (p.haveParticlePlay) {
            writeIndentClause(out, p.particlePlay ? "(play #t)" : "(play #f)");
        }
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
