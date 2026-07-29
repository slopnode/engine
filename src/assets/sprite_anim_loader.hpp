#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace slopengine {

/** Declarative layered sprite spawned on hold enter.
 *  layer != 0 (host is 0); negative draws behind, positive in front.
 *  x/y are canvas-space offsets relative to the host.
 */
struct SpriteAnimOverlay {
    int layer = 1;
    std::string sprite;
    std::string clip;
    float x = 0.0f;
    float y = 0.0f;
};

struct SpriteAnimParticle {
    std::string system;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/** One timed frame inside a sprite clip. */
struct SpriteAnimFrame {
    std::string id; /**< Frame id from the paired .spr. */
    float duration = 0.0f; /**< Hold time in seconds. */
    bool tweenRotation = false;
    bool tweenScale = false;
    bool tweenTranslate = false;
    std::string sound;
    float soundVolume = 1.0f;
    std::vector<std::string> hints;
    std::vector<SpriteAnimOverlay> overlays;
    std::vector<SpriteAnimParticle> particles;

    bool hasTween() const {
        return tweenRotation || tweenScale || tweenTranslate;
    }

    bool hasSound() const {
        return !sound.empty();
    }

    bool hasHints() const {
        return !hints.empty();
    }

    bool hasOverlays() const {
        return !overlays.empty();
    }

    bool hasParticles() const {
        return !particles.empty();
    }
};

/** One named clip from a .spanim file. */
struct SpriteAnimClip {
    std::string name;
    bool loop = true;
    std::vector<SpriteAnimFrame> frames;
};

/** Parsed sprite animation bank with name lookup. */
struct SpriteAnimBank {
    std::vector<SpriteAnimClip> clips;
    std::unordered_map<std::string, std::size_t> clipIndexByName;
};

/** Parses .spanim text into @p bank. */
bool parseSpriteAnimBank(std::string_view source, SpriteAnimBank& bank);

/** Serializes @p bank to .spanim text. */
std::string serializeSpriteAnimBank(const SpriteAnimBank& bank);

}
