#pragma once

#include "core/win32.hpp"

#include <soloud.h>

namespace slopengine {

struct SteamAudioListenerPose {
    float posX = 0.0f;
    float posY = 0.0f;
    float posZ = 0.0f;
    float aheadX = 0.0f;
    float aheadY = 0.0f;
    float aheadZ = 1.0f;
    float upX = 0.0f;
    float upY = 1.0f;
    float upZ = 0.0f;
};

struct SteamAudioSourcePose {
    SoLoud::handle voice = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float minDistance = 1.0f;
    float maxDistance = 30.0f;
};

}
