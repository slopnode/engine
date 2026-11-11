#include "fx/tracer_motion.hpp"

#include <algorithm>
#include <cmath>
#include <raymath.h>

namespace slopengine {

Tracer makeTracer(Vector3 from, Vector3 to, const TracerStyle& style, float lead) {
    Tracer tracer{};
    tracer.style = style;
    tracer.style.speed = std::max(style.speed, 1.0f);
    tracer.style.length = std::max(style.length, 0.01f);
    tracer.from = from;
    const Vector3 delta = Vector3Subtract(to, from);
    tracer.distance = Vector3Length(delta);
    if (tracer.distance > 1.0e-5f) {
        tracer.direction = Vector3Scale(delta, 1.0f / tracer.distance);
    }
    tracer.head = std::max(lead, 0.0f);
    return tracer;
}

bool advanceTracer(Tracer& tracer, float dt) {
    tracer.head += tracer.style.speed * std::max(dt, 0.0f);
    return tracer.head - tracer.style.length < tracer.distance;
}

bool tracerSpan(const Tracer& tracer, TracerSpan& out) {
    const float tip = std::min(tracer.head, tracer.distance);
    const float tail = std::max(tracer.head - tracer.style.length, 0.0f);
    if (tip - tail <= 1.0e-4f) {
        return false;
    }
    out.tail = Vector3Add(tracer.from, Vector3Scale(tracer.direction, tail));
    out.tip = Vector3Add(tracer.from, Vector3Scale(tracer.direction, tip));
    const float start = tracer.head - tracer.style.length;
    out.tailT = std::clamp((tail - start) / tracer.style.length, 0.0f, 1.0f);
    out.tipT = std::clamp((tip - start) / tracer.style.length, 0.0f, 1.0f);
    return true;
}

float tracerWidthAt(float width, float minPixels, float distance, float fovyDeg, float screenHeight) {
    if (minPixels <= 0.0f || screenHeight <= 0.0f) {
        return width;
    }
    const float perPixel =
        2.0f * std::max(distance, 0.0f) * std::tan(fovyDeg * DEG2RAD * 0.5f) / screenHeight;
    return std::max(width, minPixels * perPixel);
}

}
