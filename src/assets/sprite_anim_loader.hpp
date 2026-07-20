#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace slopengine {

/** One named clip from a .spanim file. */
struct SpriteAnimClip {
    std::string name;
    float fps = 8.0f;
    bool loop = true;
    std::vector<std::string> frames; /**< Frame ids from the paired .spr. */
};

/** Parsed sprite animation bank with name lookup. */
struct SpriteAnimBank {
    std::vector<SpriteAnimClip> clips;
    std::unordered_map<std::string, std::size_t> clipIndexByName;
};

/** Parses .spanim text into @p bank. */
bool parseSpriteAnimBank(std::string_view source, SpriteAnimBank& bank);

}
