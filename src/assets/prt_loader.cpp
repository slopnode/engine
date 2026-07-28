#include "assets/prt_loader.hpp"

#include "core/sexpr.hpp"

#include <algorithm>

namespace slopengine {

float ParticleCurve::evaluate(float t) const {
    if (keys.empty()) {
        return 1.0f;
    }
    if (keys.size() == 1) {
        return keys[0];
    }
    t = std::clamp(t, 0.0f, 1.0f);
    const float scaled = t * static_cast<float>(keys.size() - 1);
    const int i0 = static_cast<int>(scaled);
    const int i1 = std::min(i0 + 1, static_cast<int>(keys.size()) - 1);
    const float frac = scaled - static_cast<float>(i0);
    return keys[static_cast<std::size_t>(i0)] * (1.0f - frac) +
        keys[static_cast<std::size_t>(i1)] * frac;
}

namespace {

bool readStringField(const Sexpr& form, std::string& out) {
    if (!form.isList() || form.list.size() != 2 || !form.list[1].isString()) {
        return false;
    }
    out = form.list[1].text;
    return true;
}

bool readBoolField(const Sexpr& form, bool& out) {
    if (!form.isList() || form.list.size() != 2) {
        return false;
    }
    if (form.list[1].isAtom("true") || form.list[1].isAtom("#t")) {
        out = true;
        return true;
    }
    if (form.list[1].isAtom("false") || form.list[1].isAtom("#f")) {
        out = false;
        return true;
    }
    if (form.list[1].isNumber()) {
        out = form.list[1].number != 0.0;
        return true;
    }
    return false;
}

bool readNumberField(const Sexpr& form, std::size_t count, float* out) {
    if (!form.isList() || form.list.size() != count + 1) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!form.list[i + 1].isNumber()) {
            return false;
        }
        out[i] = static_cast<float>(form.list[i + 1].number);
    }
    return true;
}

bool readFloatRange(const Sexpr& form, ParticleFloatRange& out) {
    if (!form.isList() || form.list.size() < 2) {
        return false;
    }
    if (form.list.size() == 2 && form.list[1].isNumber()) {
        out.min = out.max = static_cast<float>(form.list[1].number);
        return true;
    }
    if (form.list.size() == 3 && form.list[1].isNumber() && form.list[2].isNumber()) {
        out.min = static_cast<float>(form.list[1].number);
        out.max = static_cast<float>(form.list[2].number);
        if (out.min > out.max) {
            std::swap(out.min, out.max);
        }
        return true;
    }
    return false;
}

bool readColorRange(const Sexpr& form, ParticleColorRange& out) {
    if (!form.isList()) {
        return false;
    }
    if (form.list.size() == 5 &&
        form.list[1].isNumber() && form.list[2].isNumber() &&
        form.list[3].isNumber() && form.list[4].isNumber()) {
        out.rMin = out.rMax = static_cast<float>(form.list[1].number);
        out.gMin = out.gMax = static_cast<float>(form.list[2].number);
        out.bMin = out.bMax = static_cast<float>(form.list[3].number);
        out.aMin = out.aMax = static_cast<float>(form.list[4].number);
        return true;
    }
    if (form.list.size() == 9 &&
        form.list[1].isNumber() && form.list[2].isNumber() &&
        form.list[3].isNumber() && form.list[4].isNumber() &&
        form.list[5].isNumber() && form.list[6].isNumber() &&
        form.list[7].isNumber() && form.list[8].isNumber()) {
        out.rMin = static_cast<float>(form.list[1].number);
        out.gMin = static_cast<float>(form.list[2].number);
        out.bMin = static_cast<float>(form.list[3].number);
        out.aMin = static_cast<float>(form.list[4].number);
        out.rMax = static_cast<float>(form.list[5].number);
        out.gMax = static_cast<float>(form.list[6].number);
        out.bMax = static_cast<float>(form.list[7].number);
        out.aMax = static_cast<float>(form.list[8].number);
        return true;
    }
    return false;
}

bool readCurve(const Sexpr& form, ParticleCurve& out) {
    if (!form.isList() || form.list.size() < 2) {
        return false;
    }
    out.keys.clear();
    for (std::size_t i = 1; i < form.list.size(); ++i) {
        if (!form.list[i].isNumber()) {
            return false;
        }
        out.keys.push_back(static_cast<float>(form.list[i].number));
    }
    return !out.keys.empty();
}

bool parseShape(const Sexpr& form, ParticleEmitterDef& emitter) {
    if (!form.isList() || form.list.size() < 2 || form.list[1].kind != SexprKind::Atom) {
        return false;
    }
    const std::string& kind = form.list[1].text;
    if (kind == "point") {
        emitter.shape = ParticleShapeKind::Point;
        return form.list.size() == 2;
    }
    if (kind == "box") {
        float dims[3] = {};
        if (form.list.size() != 5 || !form.list[2].isNumber() || !form.list[3].isNumber() ||
            !form.list[4].isNumber()) {
            return false;
        }
        dims[0] = static_cast<float>(form.list[2].number);
        dims[1] = static_cast<float>(form.list[3].number);
        dims[2] = static_cast<float>(form.list[4].number);
        emitter.shape = ParticleShapeKind::Box;
        emitter.shapeA = dims[0];
        emitter.shapeB = dims[1];
        emitter.shapeC = dims[2];
        return true;
    }
    if (kind == "sphere") {
        if (form.list.size() != 3 || !form.list[2].isNumber()) {
            return false;
        }
        emitter.shape = ParticleShapeKind::Sphere;
        emitter.shapeA = static_cast<float>(form.list[2].number);
        return true;
    }
    if (kind == "circle") {
        if (form.list.size() != 3 || !form.list[2].isNumber()) {
            return false;
        }
        emitter.shape = ParticleShapeKind::Circle;
        emitter.shapeA = static_cast<float>(form.list[2].number);
        return true;
    }
    if (kind == "cone") {
        if (form.list.size() != 4 || !form.list[2].isNumber() || !form.list[3].isNumber()) {
            return false;
        }
        emitter.shape = ParticleShapeKind::Cone;
        emitter.shapeA = static_cast<float>(form.list[2].number);
        emitter.shapeB = static_cast<float>(form.list[3].number);
        return true;
    }
    return false;
}

bool parseBillboard(const Sexpr& form, SpriteBillboardMode& out) {
    if (!form.isList() || form.list.size() != 2 || form.list[1].kind != SexprKind::Atom) {
        return false;
    }
    const std::string& mode = form.list[1].text;
    if (mode == "face") {
        out = SpriteBillboardMode::Face;
        return true;
    }
    if (mode == "view") {
        out = SpriteBillboardMode::View;
        return true;
    }
    if (mode == "screen") {
        out = SpriteBillboardMode::Screen;
        return true;
    }
    if (mode == "fixed") {
        out = SpriteBillboardMode::Fixed;
        return true;
    }
    return false;
}

bool applyEmitterField(const Sexpr& form, ParticleEmitterDef& emitter) {
    if (!form.isList() || form.list.empty() || form.list[0].kind != SexprKind::Atom) {
        return false;
    }
    const std::string& tag = form.list[0].text;
    if (tag == "sim") {
        if (form.list.size() != 2 || form.list[1].kind != SexprKind::Atom) {
            return false;
        }
        if (form.list[1].text == "gpu") {
            emitter.sim = ParticleSimMode::Gpu;
            return true;
        }
        if (form.list[1].text == "cpu") {
            emitter.sim = ParticleSimMode::Cpu;
            return true;
        }
        return false;
    }
    if (tag == "sprite") {
        return readStringField(form, emitter.sprite);
    }
    if (tag == "clip") {
        return readStringField(form, emitter.clip);
    }
    if (tag == "billboard") {
        return parseBillboard(form, emitter.billboard);
    }
    if (tag == "blend") {
        if (form.list.size() != 2 || form.list[1].kind != SexprKind::Atom) {
            return false;
        }
        if (form.list[1].text == "alpha") {
            emitter.blend = ParticleBlendMode::Alpha;
            return true;
        }
        if (form.list[1].text == "additive") {
            emitter.blend = ParticleBlendMode::Additive;
            return true;
        }
        return false;
    }
    if (tag == "max-particles") {
        float value = 64.0f;
        if (!readNumberField(form, 1, &value) || value < 1.0f) {
            return false;
        }
        emitter.maxParticles = static_cast<int>(value);
        return true;
    }
    if (tag == "rate") {
        return readNumberField(form, 1, &emitter.rate);
    }
    if (tag == "burst") {
        float value = 0.0f;
        if (!readNumberField(form, 1, &value) || value < 0.0f) {
            return false;
        }
        emitter.burst = static_cast<int>(value);
        return true;
    }
    if (tag == "lifetime") {
        return readFloatRange(form, emitter.lifetime);
    }
    if (tag == "speed") {
        return readFloatRange(form, emitter.speed);
    }
    if (tag == "size") {
        return readFloatRange(form, emitter.size);
    }
    if (tag == "color") {
        return readColorRange(form, emitter.color);
    }
    if (tag == "gravity") {
        return readNumberField(form, 1, &emitter.gravity);
    }
    if (tag == "space") {
        if (form.list.size() != 2 || form.list[1].kind != SexprKind::Atom) {
            return false;
        }
        if (form.list[1].text == "world") {
            emitter.space = ParticleSpace::World;
            return true;
        }
        if (form.list[1].text == "local") {
            emitter.space = ParticleSpace::Local;
            return true;
        }
        return false;
    }
    if (tag == "shape") {
        return parseShape(form, emitter);
    }
    if (tag == "size-over-life") {
        return readCurve(form, emitter.sizeOverLife);
    }
    if (tag == "alpha-over-life") {
        return readCurve(form, emitter.alphaOverLife);
    }
    if (tag == "bounce") {
        return readNumberField(form, 1, &emitter.bounce);
    }
    if (tag == "max-bounces") {
        float value = 0.0f;
        if (!readNumberField(form, 1, &value) || value < 0.0f) {
            return false;
        }
        emitter.maxBounces = static_cast<int>(value);
        return true;
    }
    if (tag == "die-on-hit" || tag == "die-on-ground") {
        return readBoolField(form, emitter.dieOnHit);
    }
    if (tag == "unlit") {
        return readBoolField(form, emitter.unlit);
    }
    return false;
}

bool parseEmitter(const Sexpr& form, ParticleEmitterDef& emitter) {
    if (!form.isList() || form.list.size() < 2 || !form.list[0].isAtom("emitter") ||
        !form.list[1].isString() || form.list[1].text.empty()) {
        return false;
    }
    emitter = {};
    emitter.name = form.list[1].text;
    for (std::size_t i = 2; i < form.list.size(); ++i) {
        if (!applyEmitterField(form.list[i], emitter)) {
            return false;
        }
    }
    if (emitter.sprite.empty()) {
        return false;
    }
    if (emitter.maxParticles < 1) {
        return false;
    }
    return true;
}

bool applySystemField(const Sexpr& form, ParticleSystemAsset& asset) {
    if (!form.isList() || form.list.empty() || form.list[0].kind != SexprKind::Atom) {
        return false;
    }
    const std::string& tag = form.list[0].text;
    if (tag == "duration") {
        return readNumberField(form, 1, &asset.duration);
    }
    if (tag == "loop") {
        return readBoolField(form, asset.loop);
    }
    if (tag == "emitter") {
        ParticleEmitterDef emitter{};
        if (!parseEmitter(form, emitter)) {
            return false;
        }
        asset.emitters.push_back(std::move(emitter));
        return true;
    }
    return false;
}

} // namespace

bool parseParticleSystemAsset(std::string_view source, ParticleSystemAsset& asset) {
    asset = {};
    const SexprParseResult parsed = parseSexprs(source);
    if (!parsed.ok) {
        return false;
    }
    if (parsed.forms.size() != 1 || !parsed.forms[0].isList() || parsed.forms[0].list.empty() ||
        !parsed.forms[0].list[0].isAtom("particle-system")) {
        return false;
    }

    const Sexpr& root = parsed.forms[0];
    for (std::size_t i = 1; i < root.list.size(); ++i) {
        if (!applySystemField(root.list[i], asset)) {
            return false;
        }
    }
    return !asset.emitters.empty();
}

}
