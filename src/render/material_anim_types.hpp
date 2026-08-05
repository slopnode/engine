#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace slopengine {

/** One mesh on an entity that uses a globally synced texture animation. */
struct MaterialAnimTarget {
    int meshIndex = 0;
    std::string animPath;
};

/** Meshes on an entity driven by texture animations. */
struct MaterialAnimTargets {
    std::vector<MaterialAnimTarget> targets;
};

/** Global playback clocks keyed by texture anim path. */
struct MaterialAnimClocks {
    struct Entry {
        float time = 0.0f;
        int frameIndex = 0;
        int lastAppliedFrameIndex = -1;
    };

    std::unordered_map<std::string, Entry> byAnimPath;
};

}
