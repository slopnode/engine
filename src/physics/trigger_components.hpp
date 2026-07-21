#pragma once

#include <raylib.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

struct CollisionTags {
    std::vector<std::string> tags;
};

struct TriggerVolume {
    Vector3 size{1.0f, 1.0f, 1.0f};
    std::string onEnter;
    std::string onExit;
    std::vector<std::string> filterTags;
    std::unordered_set<std::uint64_t> inside;
};

}
