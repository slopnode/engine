#pragma once

namespace slopengine {

struct FrameCounters {
    int mapMeshDraws = 0;
    int modelMeshDraws = 0;
    int transparentMeshDraws = 0;
    int spritesDrawn = 0;
    int particlesDrawn = 0;
    int shadowFaces = 0;
    int shadowMeshDraws = 0;
    int mapFrustumCulled = 0;
    int mapPvsCulled = 0;
    int modelsFrustumCulled = 0;
    int modelsPvsCulled = 0;
    int spritesFrustumCulled = 0;
    int spritesPvsCulled = 0;
    int transparentFrustumCulled = 0;
    int transparentPvsCulled = 0;
    int lightsCulled = 0;
    int lightsActive = 0;
    int pvsTests = 0;
    int bvhRays = 0;
    int physicsRays = 0;
    int probeSamples = 0;
};

inline FrameCounters& frameCounters() {
    static FrameCounters counters{};
    return counters;
}

inline void resetFrameCounters() {
    frameCounters() = FrameCounters{};
}

}
