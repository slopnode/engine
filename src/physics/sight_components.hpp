#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

struct ActorSight {
    bool enabled = false;
    float range = 32.0f;
    float fovDegrees = 180.0f;
    float eyeLift = 0.75f;
    std::vector<std::string> seeTags;
    std::vector<std::string> ignoreTags;
    std::string filterProc;
    std::unordered_set<std::string> visible;
};

struct SightScanState {
    std::size_t cursor = 0;
    int maxLosPerFrame = 6;
};

}
