#pragma once

#include <raylib.h>

namespace slopengine {

struct TracerStyle {
    float speed = 240.0f;
    float length = 1.8f;
    float width = 0.02f;
    float glowWidth = 0.09f;
    float minPixels = 1.5f;
    float tailAlpha = 0.0f;
    Color core{255, 240, 190, 255};
    Color glow{255, 140, 30, 150};
};

struct Tracer {
    TracerStyle style{};
    Vector3 from{};
    Vector3 direction{0.0f, 0.0f, 1.0f};
    float distance = 0.0f;
    float head = 0.0f;
};

struct TracerSpan {
    Vector3 tail{};
    Vector3 tip{};
    float tailT = 0.0f;
    float tipT = 1.0f;
};

Tracer makeTracer(Vector3 from, Vector3 to, const TracerStyle& style, float lead = 0.0f);

bool advanceTracer(Tracer& tracer, float dt);

bool tracerSpan(const Tracer& tracer, TracerSpan& out);

float tracerWidthAt(float width, float minPixels, float distance, float fovyDeg, float screenHeight);

}
