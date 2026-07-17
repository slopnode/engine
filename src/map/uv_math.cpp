#include "map/uv_math.hpp"

#include <cmath>

namespace slopengine {

void axialUvAxes(Vector3 normal, Vector3& uAxis, Vector3& vAxis) {
    const float ax = std::fabs(normal.x);
    const float ay = std::fabs(normal.y);
    const float az = std::fabs(normal.z);

    if (ay >= ax && ay >= az) {
        uAxis = {1.0f, 0.0f, 0.0f};
        vAxis = {0.0f, 0.0f, -1.0f};
    } else if (ax >= ay && ax >= az) {
        uAxis = {0.0f, 0.0f, 1.0f};
        vAxis = {0.0f, -1.0f, 0.0f};
    } else {
        uAxis = {1.0f, 0.0f, 0.0f};
        vAxis = {0.0f, -1.0f, 0.0f};
    }
}

Vector2 worldPlanarUv(
    Vector3 position,
    Vector3 uAxis,
    Vector3 vAxis,
    Vector2 uvShiftPixels,
    const MaterialUvInfo& materialUv) {
    const float metersU = position.x * uAxis.x + position.y * uAxis.y + position.z * uAxis.z;
    const float metersV = position.x * vAxis.x + position.y * vAxis.y + position.z * vAxis.z;
    const float width = materialUv.textureWidth > 0.0f ? materialUv.textureWidth : 64.0f;
    const float height = materialUv.textureHeight > 0.0f ? materialUv.textureHeight : 64.0f;
    return Vector2{
        (metersU * materialUv.pixelsPerMeter + uvShiftPixels.x) / width,
        (metersV * materialUv.pixelsPerMeter + uvShiftPixels.y) / height,
    };
}

}
