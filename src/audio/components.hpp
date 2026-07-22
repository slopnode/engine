#pragma once

#include <cstdint>
#include <string>

namespace slopengine {

/** Listener pose for spatial audio (usually on the player/camera).
 *  @ingroup audio_components
 */
struct AudioListener {
    bool active = true;
};

/** One-shot or looping sound source, optionally spatialized.
 *  @ingroup audio_components
 */
struct AudioSource {
    std::string audio;
    std::string clip;
    float volume = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 30.0f;
    bool looping = false;
    bool spatial = true;
    bool autoplay = false;
    bool playing = false;
    std::uint32_t voice = 0;
};

}
