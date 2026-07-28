#pragma once

#include "assets/sprite_loader.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

enum class ParticleSimMode {
    Gpu,
    Cpu,
};

enum class ParticleSpace {
    World,
    Local,
};

enum class ParticleShapeKind {
    Point,
    Box,
    Sphere,
    Circle,
    Cone,
};

enum class ParticleBlendMode {
    Alpha,
    Additive,
};

struct ParticleFloatRange {
    float min = 0.0f;
    float max = 0.0f;

    float sample(float t) const {
        return min + (max - min) * t;
    }

    bool isConstant() const {
        return min == max;
    }
};

struct ParticleColorRange {
    float rMin = 1.0f;
    float gMin = 1.0f;
    float bMin = 1.0f;
    float aMin = 1.0f;
    float rMax = 1.0f;
    float gMax = 1.0f;
    float bMax = 1.0f;
    float aMax = 1.0f;
};

struct ParticleCurve {
    std::vector<float> keys;

    float evaluate(float t) const;
};

struct ParticleEmitterDef {
    std::string name;
    ParticleSimMode sim = ParticleSimMode::Gpu;
    std::string sprite;
    std::string clip;
    SpriteBillboardMode billboard = SpriteBillboardMode::Face;
    int maxParticles = 64;
    float rate = 0.0f;
    int burst = 0;
    ParticleFloatRange lifetime{1.0f, 1.0f};
    ParticleFloatRange speed{1.0f, 1.0f};
    ParticleFloatRange size{0.25f, 0.25f};
    ParticleColorRange color{};
    float gravity = 0.0f;
    ParticleSpace space = ParticleSpace::World;
    ParticleShapeKind shape = ParticleShapeKind::Point;
    float shapeA = 0.0f;
    float shapeB = 0.0f;
    float shapeC = 0.0f;
    ParticleCurve sizeOverLife{{1.0f, 1.0f}};
    ParticleCurve alphaOverLife{{1.0f, 1.0f}};
    ParticleBlendMode blend = ParticleBlendMode::Alpha;
    float bounce = 0.0f;
    int maxBounces = 0;
    bool dieOnHit = false;
};

struct ParticleSystemAsset {
    float duration = 0.0f;
    bool loop = true;
    std::vector<ParticleEmitterDef> emitters;
};

bool parseParticleSystemAsset(std::string_view source, ParticleSystemAsset& asset);

}
