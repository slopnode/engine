#pragma once

#include <raylib.h>

#include <string>

namespace slopengine {

/** Motor-driven flyer: package sets velocity/gravity; engine integrates vs static hulls. */
struct MotoredBody {
    Vector3 velocity = {0.0f, 0.0f, 0.0f};
    float gravity = 0.0f;
    float radius = 0.12f;
    float lifetime = 8.0f;
    float age = 0.0f;
    std::string onImpact;
};

struct SphereCastHit {
    Vector3 point = {0.0f, 0.0f, 0.0f};
    Vector3 normal = {0.0f, 1.0f, 0.0f};
    float fraction = 1.0f;
};

}
