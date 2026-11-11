#pragma once

#include <string>
#include <utility>
#include <vector>

namespace slopengine {

struct BoneScale {
    float size = 1.0f;
    float length = 1.0f;
};

using NamedBoneScales = std::vector<std::pair<std::string, BoneScale>>;

struct PoseScaleResult {
    float lift = 0.0f;
    float spread = 1.0f;
};

}
