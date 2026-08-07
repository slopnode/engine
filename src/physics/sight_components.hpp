#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

/** Line-of-sight scanner: FOV/range gated linescan against tagged targets, calling `on-sight`.
 *  @ingroup physics_components
 */
struct ActorSight {
    bool enabled = false;
    float range = 32.0f;
    float fovDegrees = 180.0f;
    float eyeLift = 0.85f;
    std::vector<std::string> seeTags;
    std::vector<std::string> ignoreTags;
    std::string filterProc;
    std::unordered_set<std::string> visible;
};

/** World singleton: round-robins ActorSight scans across frames to cap per-frame linescans.
 *  @ingroup physics_components
 */
struct SightScanState {
    std::size_t cursor = 0;
    int maxLosPerFrame = 6;
};

}
