#pragma once

#include "core/win32.hpp"

#include <soloud.h>

#include <vector>

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

// SDK-agnostic snapshot of one active spatial voice's simulated acoustics, for debug
// visualization. Mirrors SteamAudioSlotRenderParams but stays free of IPL/phonon types so
// callers outside the audio module (e.g. render_debug.cpp) don't need the Steam Audio SDK.
struct SteamAudioDebugSourceInfo {
    SoLoud::handle voice = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float occlusion = 1.0f;
    float transmission[3] = {1.0f, 1.0f, 1.0f};
    float reverbTimes[3] = {0.0f, 0.0f, 0.0f};
    bool reflectionsActive = false;
};

struct SteamAudioDebugTriangle {
    float v0[3] = {0.0f, 0.0f, 0.0f};
    float v1[3] = {0.0f, 0.0f, 0.0f};
    float v2[3] = {0.0f, 0.0f, 0.0f};
};

// Wireframe-able snapshot of the baked Steam Audio occluder mesh (already excludes
// movable/door brushes), for spotting exclusion mistakes in the live scene.
struct SteamAudioDebugScene {
    std::vector<SteamAudioDebugTriangle> triangles;
};

}
