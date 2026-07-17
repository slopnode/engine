#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace slopengine {

struct SpriteAnimClip {
    std::string name;
    float fps = 8.0f;
    bool loop = true;
    std::vector<std::string> frames;
};

struct SpriteAnimBank {
    std::vector<SpriteAnimClip> clips;
    std::unordered_map<std::string, std::size_t> clipIndexByName;
};

bool parseSpriteAnimBank(std::string_view source, SpriteAnimBank& bank);

}
