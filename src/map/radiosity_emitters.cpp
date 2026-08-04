#include "map/radiosity_emitters.hpp"

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

float emitterLuminance(Vector3 radiance) {
    return 0.2126f * radiance.x + 0.7152f * radiance.y + 0.0722f * radiance.z;
}

bool passesCastGate(Vector3 radiance) {
    return emitterLuminance(radiance) >= kMinCastLuminance;
}

bool emitterPairBelowThreshold(
    Vector3 radiance,
    float area,
    float dist2,
    float minDist2,
    float castRange) {
    if (castRange > 0.0f && dist2 > castRange * castRange) {
        return true;
    }
    const float maxForm = area / (std::max(dist2, minDist2) * kPi);
    return emitterLuminance(radiance) * maxForm < kMinEmitterContrib;
}

float emitterRangeAttenuation(float dist, float range) {
    if (range <= 0.0f) {
        return 1.0f;
    }
    const float t = dist / range;
    float atten = std::max(0.0f, 1.0f - t * t);
    return atten * atten;
}

float dist2PointToAabb(Vector3 point, Vector3 mins, Vector3 maxs) {
    const float dx = std::max({mins.x - point.x, 0.0f, point.x - maxs.x});
    const float dy = std::max({mins.y - point.y, 0.0f, point.y - maxs.y});
    const float dz = std::max({mins.z - point.z, 0.0f, point.z - maxs.z});
    return dx * dx + dy * dy + dz * dz;
}

Vector3 sampleEmissionGridBilinear(
    const EmissiveFace& face,
    std::span<const Vector3> grid,
    float u,
    float v) {
    if (face.gridWidth <= 0 || face.gridHeight <= 0) {
        return {};
    }
    const float uSpan = face.uMax - face.uMin;
    const float vSpan = face.vMax - face.vMin;
    if (uSpan < 1e-8f || vSpan < 1e-8f) {
        return {};
    }
    const float fu = clamp01((u - face.uMin) / uSpan);
    const float fv = clamp01((v - face.vMin) / vSpan);
    const float gx = fu * static_cast<float>(std::max(face.gridWidth - 1, 0));
    const float gy = fv * static_cast<float>(std::max(face.gridHeight - 1, 0));
    const int x0 = static_cast<int>(std::floor(gx));
    const int y0 = static_cast<int>(std::floor(gy));
    const int x1 = std::min(x0 + 1, face.gridWidth - 1);
    const int y1 = std::min(y0 + 1, face.gridHeight - 1);
    const float tx = gx - static_cast<float>(x0);
    const float ty = gy - static_cast<float>(y0);

    auto at = [&](int x, int y) -> Vector3 {
        const std::size_t index =
            static_cast<std::size_t>(face.gridOffset)
            + static_cast<std::size_t>(y) * static_cast<std::size_t>(face.gridWidth)
            + static_cast<std::size_t>(x);
        if (index >= grid.size()) {
            return {};
        }
        return grid[index];
    };

    const Vector3 c00 = at(x0, y0);
    const Vector3 c10 = at(x1, y0);
    const Vector3 c01 = at(x0, y1);
    const Vector3 c11 = at(x1, y1);
    const Vector3 c0 = {
        c00.x * (1.0f - tx) + c10.x * tx,
        c00.y * (1.0f - tx) + c10.y * tx,
        c00.z * (1.0f - tx) + c10.z * tx,
    };
    const Vector3 c1 = {
        c01.x * (1.0f - tx) + c11.x * tx,
        c01.y * (1.0f - tx) + c11.y * tx,
        c01.z * (1.0f - tx) + c11.z * tx,
    };
    return {
        c0.x * (1.0f - ty) + c1.x * ty,
        c0.y * (1.0f - ty) + c1.y * ty,
        c0.z * (1.0f - ty) + c1.z * ty,
    };
}

} // namespace slopengine
