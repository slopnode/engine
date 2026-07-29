#include "assets/sprite_anim_loader.hpp"

#include "core/sexpr.hpp"

#include <sstream>
#include <string>

namespace slopengine {

namespace {

bool parseTweenForm(const Sexpr& form, SpriteAnimFrame& out) {
    if (!form.isList() || form.list.empty() || !form.list[0].isAtom("tween")) {
        return false;
    }
    for (std::size_t i = 1; i < form.list.size(); ++i) {
        if (form.list[i].kind != SexprKind::Atom) {
            return false;
        }
        const std::string& token = form.list[i].text;
        if (token == "all") {
            out.tweenRotation = true;
            out.tweenScale = true;
            out.tweenTranslate = true;
        } else if (token == "rot") {
            out.tweenRotation = true;
        } else if (token == "scale") {
            out.tweenScale = true;
        } else if (token == "translate") {
            out.tweenTranslate = true;
        } else if (token == "offset") {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool parseSoundForm(const Sexpr& form, SpriteAnimFrame& out) {
    if (!form.isList() || form.list.size() < 2 || form.list.size() > 3 ||
        !form.list[0].isAtom("sound") || !form.list[1].isString() || form.list[1].text.empty()) {
        return false;
    }
    out.sound = form.list[1].text;
    out.soundVolume = 1.0f;
    if (form.list.size() == 3) {
        if (!form.list[2].isNumber()) {
            return false;
        }
        out.soundVolume = static_cast<float>(form.list[2].number);
    }
    return true;
}

bool parseHintForm(const Sexpr& form, SpriteAnimFrame& out) {
    if (!form.isList() || form.list.size() != 2 || !form.list[0].isAtom("hint") ||
        !form.list[1].isString() || form.list[1].text.empty()) {
        return false;
    }
    out.hints.push_back(form.list[1].text);
    return true;
}

bool parseOverlayForm(const Sexpr& form, SpriteAnimFrame& out) {
    if (!form.isList() || form.list.size() != 6 || !form.list[0].isAtom("overlay") ||
        !form.list[1].isNumber() || !form.list[2].isString() || form.list[2].text.empty() ||
        !form.list[3].isString() || form.list[3].text.empty() || !form.list[4].isNumber() ||
        !form.list[5].isNumber()) {
        return false;
    }
    const int layer = static_cast<int>(form.list[1].number);
    if (layer == 0) {
        return false;
    }
    out.overlays.push_back(SpriteAnimOverlay{
        .layer = layer,
        .sprite = form.list[2].text,
        .clip = form.list[3].text,
        .x = static_cast<float>(form.list[4].number),
        .y = static_cast<float>(form.list[5].number),
    });
    return true;
}

bool parseParticleForm(const Sexpr& form, SpriteAnimFrame& out) {
    if (!form.isList() || form.list.size() < 2 || !form.list[0].isAtom("particle") ||
        !form.list[1].isString() || form.list[1].text.empty()) {
        return false;
    }
    SpriteAnimParticle particle{};
    particle.system = form.list[1].text;
    if (form.list.size() == 2) {
        out.particles.push_back(std::move(particle));
        return true;
    }
    if (form.list.size() == 4 && form.list[2].isNumber() && form.list[3].isNumber()) {
        particle.x = static_cast<float>(form.list[2].number);
        particle.y = static_cast<float>(form.list[3].number);
        out.particles.push_back(std::move(particle));
        return true;
    }
    if (form.list.size() == 5 && form.list[2].isNumber() && form.list[3].isNumber() &&
        form.list[4].isNumber()) {
        particle.x = static_cast<float>(form.list[2].number);
        particle.y = static_cast<float>(form.list[3].number);
        particle.z = static_cast<float>(form.list[4].number);
        out.particles.push_back(std::move(particle));
        return true;
    }
    return false;
}

bool parseFrameForm(const Sexpr& form, SpriteAnimFrame& out) {
    if (!form.isList() || form.list.size() < 3 || !form.list[0].isAtom("frame") ||
        !form.list[1].isString() || form.list[1].text.empty() || !form.list[2].isNumber() ||
        form.list[2].number <= 0.0) {
        return false;
    }

    out = {};
    out.id = form.list[1].text;
    out.duration = static_cast<float>(form.list[2].number);
    for (std::size_t i = 3; i < form.list.size(); ++i) {
        const Sexpr& child = form.list[i];
        if (!child.isList() || child.list.empty() || child.list[0].kind != SexprKind::Atom) {
            return false;
        }
        const std::string& tag = child.list[0].text;
        if (tag == "tween") {
            if (!parseTweenForm(child, out)) {
                return false;
            }
        } else if (tag == "sound") {
            if (!parseSoundForm(child, out)) {
                return false;
            }
        } else if (tag == "hint") {
            if (!parseHintForm(child, out)) {
                return false;
            }
        } else if (tag == "overlay") {
            if (!parseOverlayForm(child, out)) {
                return false;
            }
        } else if (tag == "particle") {
            if (!parseParticleForm(child, out)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool parseClipForm(const Sexpr& form, SpriteAnimClip& clip, bool& legacyClipTween) {
    if (!form.isList() || form.list.size() < 2 || !form.list[0].isAtom("clip") ||
        !form.list[1].isString() || form.list[1].text.empty()) {
        return false;
    }

    clip = {};
    clip.name = form.list[1].text;
    clip.loop = true;
    legacyClipTween = false;

    for (std::size_t i = 2; i < form.list.size(); ++i) {
        const Sexpr& child = form.list[i];
        if (!child.isList() || child.list.empty() || child.list[0].kind != SexprKind::Atom) {
            return false;
        }
        const std::string& tag = child.list[0].text;
        if (tag == "loop") {
            if (child.list.size() != 2 || !child.list[1].isNumber()) {
                return false;
            }
            clip.loop = static_cast<int>(child.list[1].number) != 0;
        } else if (tag == "tween") {
            if (child.list.size() != 2 || !child.list[1].isNumber()) {
                return false;
            }
            legacyClipTween = static_cast<int>(child.list[1].number) != 0;
        } else if (tag == "frame") {
            SpriteAnimFrame frame{};
            if (!parseFrameForm(child, frame)) {
                return false;
            }
            clip.frames.push_back(std::move(frame));
        } else {
            return false;
        }
    }

    if (legacyClipTween) {
        for (SpriteAnimFrame& frame : clip.frames) {
            frame.tweenRotation = true;
            frame.tweenScale = true;
            frame.tweenTranslate = true;
        }
    }
    return !clip.frames.empty();
}

void writeTweenSuffix(std::ostringstream& out, const SpriteAnimFrame& frame) {
    if (!frame.hasTween()) {
        return;
    }
    if (frame.tweenRotation && frame.tweenScale && frame.tweenTranslate) {
        out << " (tween all)";
        return;
    }
    out << " (tween";
    if (frame.tweenRotation) {
        out << " rot";
    }
    if (frame.tweenScale) {
        out << " scale";
    }
    if (frame.tweenTranslate) {
        out << " translate";
    }
    out << ')';
}

void writeSoundSuffix(std::ostringstream& out, const SpriteAnimFrame& frame) {
    if (!frame.hasSound()) {
        return;
    }
    out << " (sound \"" << frame.sound << '"';
    if (frame.soundVolume != 1.0f) {
        out << ' ' << frame.soundVolume;
    }
    out << ')';
}

void writeHintSuffix(std::ostringstream& out, const SpriteAnimFrame& frame) {
    for (const std::string& hint : frame.hints) {
        out << " (hint \"" << hint << "\")";
    }
}

void writeOverlaySuffix(std::ostringstream& out, const SpriteAnimFrame& frame) {
    for (const SpriteAnimOverlay& overlay : frame.overlays) {
        out << " (overlay " << overlay.layer << " \"" << overlay.sprite << "\" \"" << overlay.clip
            << "\" " << overlay.x << ' ' << overlay.y << ')';
    }
}

void writeParticleSuffix(std::ostringstream& out, const SpriteAnimFrame& frame) {
    for (const SpriteAnimParticle& particle : frame.particles) {
        out << " (particle \"" << particle.system << '"';
        if (particle.x != 0.0f || particle.y != 0.0f || particle.z != 0.0f) {
            out << ' ' << particle.x << ' ' << particle.y << ' ' << particle.z;
        }
        out << ')';
    }
}

} // namespace

bool parseSpriteAnimBank(std::string_view source, SpriteAnimBank& bank) {
    bank = {};
    const SexprParseResult parsed = parseSexprs(source);
    if (!parsed.ok) {
        return false;
    }
    if (parsed.forms.size() != 1 || !parsed.forms[0].isList() || parsed.forms[0].list.empty() ||
        !parsed.forms[0].list[0].isAtom("sprite-anim")) {
        return false;
    }

    const Sexpr& root = parsed.forms[0];
    for (std::size_t i = 1; i < root.list.size(); ++i) {
        SpriteAnimClip clip{};
        bool legacyClipTween = false;
        if (!parseClipForm(root.list[i], clip, legacyClipTween)) {
            return false;
        }
        bank.clips.push_back(std::move(clip));
    }

    if (bank.clips.empty()) {
        return false;
    }

    for (std::size_t index = 0; index < bank.clips.size(); ++index) {
        const SpriteAnimClip& clip = bank.clips[index];
        if (clip.name.empty() || clip.frames.empty()) {
            return false;
        }
        for (const SpriteAnimFrame& frame : clip.frames) {
            if (frame.id.empty() || frame.duration <= 0.0f) {
                return false;
            }
        }
        bank.clipIndexByName.emplace(clip.name, index);
    }

    return true;
}

std::string serializeSpriteAnimBank(const SpriteAnimBank& bank) {
    std::ostringstream out;
    out << "(sprite-anim\n";
    for (const SpriteAnimClip& clip : bank.clips) {
        out << "  (clip \"" << clip.name << "\"\n";
        out << "    (loop " << (clip.loop ? 1 : 0) << ")\n";
        for (const SpriteAnimFrame& frame : clip.frames) {
            out << "    (frame \"" << frame.id << "\" " << frame.duration;
            writeTweenSuffix(out, frame);
            writeSoundSuffix(out, frame);
            writeHintSuffix(out, frame);
            writeOverlaySuffix(out, frame);
            writeParticleSuffix(out, frame);
            out << ")\n";
        }
        out << "  )\n";
    }
    out << ")\n";
    return out.str();
}

}
