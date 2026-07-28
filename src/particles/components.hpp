#pragma once

#include "assets/prt_loader.hpp"

#include <raylib.h>

#include <cstdint>
#include <string>
#include <vector>

namespace slopengine {

struct Particle {
    Vector3 position{};
    Vector3 velocity{};
    float age = 0.0f;
    float lifetime = 1.0f;
    float size0 = 0.25f;
    Color color0 = WHITE;
    std::uint16_t bounceCount = 0;
    bool alive = false;
};

struct ParticleEmitterRuntime {
    ParticleEmitterDef def{};
    std::vector<Particle> particles;
    float emitAccum = 0.0f;
    bool burstFired = false;
    int aliveCount = 0;
};

struct ParticleSystemInstance {
    std::string path;
    bool playing = true;
    float age = 0.0f;
    std::vector<ParticleEmitterRuntime> emitters;
};

struct ParticleFollowViewMuzzle {
    std::uint64_t host = 0;
    float depth = 0.35f;
};

}
