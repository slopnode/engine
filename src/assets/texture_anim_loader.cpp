#include "assets/texture_anim_loader.hpp"

#include "assets/asset_store.hpp"
#include "assets/material_loader.hpp"
#include "assets/rigged_assets.hpp"
#include "core/sexpr.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace slopengine {

namespace {

bool parseFrameForm(const Sexpr& form, TextureAnimFrame& out) {
    if (!form.isList() || form.list.size() != 3 || !form.list[0].isAtom("frame") ||
        !form.list[1].isString() || form.list[1].text.empty() || !form.list[2].isNumber() ||
        form.list[2].number <= 0.0) {
        return false;
    }
    out = {};
    out.texture = form.list[1].text;
    out.duration = static_cast<float>(form.list[2].number);
    return true;
}

bool parseClipForm(const Sexpr& form, TextureAnimClip& clip) {
    if (!form.isList() || form.list.size() < 2 || !form.list[0].isAtom("clip") ||
        !form.list[1].isString() || form.list[1].text.empty()) {
        return false;
    }

    clip = {};
    clip.name = form.list[1].text;
    clip.loop = true;

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
        } else if (tag == "frame") {
            TextureAnimFrame frame{};
            if (!parseFrameForm(child, frame)) {
                return false;
            }
            clip.frames.push_back(std::move(frame));
        } else {
            return false;
        }
    }

    return !clip.frames.empty();
}

} // namespace

float textureAnimClipDuration(const TextureAnimClip& clip) {
    float duration = 0.0f;
    for (const TextureAnimFrame& frame : clip.frames) {
        duration += frame.duration;
    }
    return duration;
}

int textureAnimFrameIndexAt(const TextureAnimClip& clip, float time) {
    if (clip.frames.empty()) {
        return 0;
    }

    const float duration = textureAnimClipDuration(clip);
    if (duration <= 0.0f) {
        return 0;
    }

    float localTime = time;
    if (clip.loop) {
        localTime = std::fmod(localTime, duration);
        if (localTime < 0.0f) {
            localTime += duration;
        }
    } else {
        localTime = std::clamp(localTime, 0.0f, duration - 1e-6f);
    }

    float elapsed = 0.0f;
    for (std::size_t i = 0; i < clip.frames.size(); ++i) {
        elapsed += clip.frames[i].duration;
        if (localTime < elapsed) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(clip.frames.size()) - 1;
}

std::string_view textureAnimFrameTexture(const TextureAnimClip& clip, int frameIndex) {
    if (frameIndex < 0 || static_cast<std::size_t>(frameIndex) >= clip.frames.size()) {
        return {};
    }
    return clip.frames[static_cast<std::size_t>(frameIndex)].texture;
}

bool parseTextureAnimBank(std::string_view source, TextureAnimBank& bank) {
    bank = {};
    const SexprParseResult parsed = parseSexprs(source);
    if (!parsed.ok) {
        return false;
    }
    if (parsed.forms.size() != 1 || !parsed.forms[0].isList() || parsed.forms[0].list.empty() ||
        !parsed.forms[0].list[0].isAtom("texture-anim")) {
        return false;
    }

    const Sexpr& root = parsed.forms[0];
    for (std::size_t i = 1; i < root.list.size(); ++i) {
        TextureAnimClip clip{};
        if (!parseClipForm(root.list[i], clip)) {
            return false;
        }
        bank.clips.push_back(std::move(clip));
    }

    if (bank.clips.empty()) {
        return false;
    }

    for (std::size_t index = 0; index < bank.clips.size(); ++index) {
        const TextureAnimClip& clip = bank.clips[index];
        if (clip.name.empty() || clip.frames.empty()) {
            return false;
        }
        for (const TextureAnimFrame& frame : clip.frames) {
            if (frame.texture.empty() || frame.duration <= 0.0f) {
                return false;
            }
        }
        bank.clipIndexByName.emplace(clip.name, index);
    }

    return true;
}

std::string serializeTextureAnimBank(const TextureAnimBank& bank) {
    std::ostringstream out;
    out << "(texture-anim\n";
    for (const TextureAnimClip& clip : bank.clips) {
        out << "  (clip \"" << clip.name << "\"\n";
        out << "    (loop " << (clip.loop ? 1 : 0) << ")\n";
        for (const TextureAnimFrame& frame : clip.frames) {
            out << "    (frame \"" << frame.texture << "\" " << frame.duration << ")\n";
        }
        out << "  )\n";
    }
    out << ")\n";
    return out.str();
}

void collectMaterialAnimTargets(const GeoAsset& geo, AssetStore& assets, MaterialAnimTargets& out) {
    out.targets.clear();
    out.targets.reserve(geo.primitives.size());

    for (std::size_t meshIndex = 0; meshIndex < geo.primitives.size(); ++meshIndex) {
        const GeoPrimitive& primitive = geo.primitives[meshIndex];
        const MaterialAsset* materialAsset = assets.getMaterialAsset(primitive.material);
        if (materialAsset == nullptr || materialAsset->textureAnimPath.empty()) {
            continue;
        }
        out.targets.push_back(MaterialAnimTarget{
            .meshIndex = static_cast<int>(meshIndex),
            .animPath = materialAsset->textureAnimPath,
        });
    }
}

}
