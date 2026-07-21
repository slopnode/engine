#pragma once

#include <cstdint>
#include <string>

namespace slopengine {

struct AudioListener {
    bool active = true;
};

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
