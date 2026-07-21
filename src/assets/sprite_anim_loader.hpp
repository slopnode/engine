#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace slopengine {

/** One timed frame inside a sprite clip. */
struct SpriteAnimFrame {
    std::string id; /**< Frame id from the paired .spr. */
    float duration = 0.0f; /**< Hold time in seconds. */
    bool tweenRotation = false;
    bool tweenScale = false;
    bool tweenTranslate = false;
    std::string sound;
    float soundVolume = 1.0f;

    bool hasTween() const {
        return tweenRotation || tweenScale || tweenTranslate;
    }

    bool hasSound() const {
        return !sound.empty();
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
