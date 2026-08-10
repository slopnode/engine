#include "render/dynamic_light_shadow_math.hpp"

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

Vector4 transformPoint(const Matrix& m, Vector3 p) {
    return {
        m.m0 * p.x + m.m4 * p.y + m.m8 * p.z + m.m12,
        m.m1 * p.x + m.m5 * p.y + m.m9 * p.z + m.m13,
        m.m2 * p.x + m.m6 * p.y + m.m10 * p.z + m.m14,
        m.m3 * p.x + m.m7 * p.y + m.m11 * p.z + m.m15,
    };
}

} // namespace

Vector3 cubeFaceTarget(int face) {
    switch (face) {
    case 0:
        return {1.0f, 0.0f, 0.0f};
    case 1:
        return {-1.0f, 0.0f, 0.0f};
    case 2:
        return {0.0f, 1.0f, 0.0f};
    case 3:
        return {0.0f, -1.0f, 0.0f};
    case 4:
        return {0.0f, 0.0f, 1.0f};
    default:
        return {0.0f, 0.0f, -1.0f};
    }
}

Vector3 cubeFaceUp(int face) {
    switch (face) {
    case 2:
        return {0.0f, 0.0f, 1.0f};
    case 3:
        return {0.0f, 0.0f, -1.0f};
    default:
        return {0.0f, -1.0f, 0.0f};
    }
}

int cubeFaceIndexAxis(Vector3 dir, int axis) {
    if (axis == 0) {
        return dir.x > 0.0f ? 0 : 1;
    }
    if (axis == 1) {
        return dir.y > 0.0f ? 2 : 3;
    }
    return dir.z > 0.0f ? 4 : 5;
}

int cubeFacePrimaryAxis(Vector3 dir) {
    const Vector3 a{std::fabs(dir.x), std::fabs(dir.y), std::fabs(dir.z)};
    if (a.y >= a.x && a.y >= a.z) {
        return 1;
    }
    if (a.z >= a.x && a.z >= a.y) {
        return 2;
    }
    return 0;
}

int cubeFaceSecondaryAxis(Vector3 dir, int primaryAxis) {
    const Vector3 a{std::fabs(dir.x), std::fabs(dir.y), std::fabs(dir.z)};
    if (primaryAxis == 0) {
        return (a.y >= a.z) ? 1 : 2;
    }
    if (primaryAxis == 1) {
        return (a.x >= a.z) ? 0 : 2;
    }
    return (a.x >= a.y) ? 0 : 1;
}

ShadowCameraDesc pointShadowFaceCamera(Vector3 lightPos, int face) {
    ShadowCameraDesc desc{};
    desc.position = lightPos;
    desc.target = Vector3Add(lightPos, cubeFaceTarget(face));
    desc.up = cubeFaceUp(face);
    desc.fovy = 90.0f;
    return desc;
}

ShadowCameraDesc spotShadowCamera(Vector3 lightPos, Vector3 direction, float coneAngleRadians) {
    ShadowCameraDesc desc{};
    desc.position = lightPos;
    const Vector3 forward = Vector3Normalize(direction);
    desc.target = Vector3Add(lightPos, forward);
    const float align = std::fabs(Vector3DotProduct(forward, {0.0f, 1.0f, 0.0f}));
    desc.up = align > 0.95f ? Vector3{0.0f, 0.0f, 1.0f} : Vector3{0.0f, 1.0f, 0.0f};
    const float coneDeg = coneAngleRadians * RAD2DEG;
    desc.fovy = std::clamp(coneDeg * 2.0f * 1.15f, 10.0f, 170.0f);
    return desc;
}

float shadowNearPlane(float range) {
    return std::clamp(range * 0.01f, 0.005f, 0.05f);
}

bool aabbContainsPoint(BoundingBox box, Vector3 point, float epsilon) {
    return point.x >= box.min.x - epsilon && point.x <= box.max.x + epsilon &&
        point.y >= box.min.y - epsilon && point.y <= box.max.y + epsilon &&
        point.z >= box.min.z - epsilon && point.z <= box.max.z + epsilon;
}

bool aabbContainsPointInset(BoundingBox box, Vector3 point, float inset) {
    const float insetX = std::min(inset, std::max(0.0f, (box.max.x - box.min.x) * 0.5f - 1e-4f));
    const float insetY = std::min(inset, std::max(0.0f, (box.max.y - box.min.y) * 0.5f - 1e-4f));
    const float insetZ = std::min(inset, std::max(0.0f, (box.max.z - box.min.z) * 0.5f - 1e-4f));
    return point.x >= box.min.x + insetX && point.x <= box.max.x - insetX &&
        point.y >= box.min.y + insetY && point.y <= box.max.y - insetY &&
        point.z >= box.min.z + insetZ && point.z <= box.max.z - insetZ;
}

bool aabbDeeplyContainsPoint(BoundingBox box, Vector3 point, float minDepth) {
    const float dx = std::min(point.x - box.min.x, box.max.x - point.x);
    const float dy = std::min(point.y - box.min.y, box.max.y - point.y);
    const float dz = std::min(point.z - box.min.z, box.max.z - point.z);
    return dx >= minDepth && dy >= minDepth && dz >= minDepth;
}

bool shouldSkipShadowCaster(bool isMapLightmapped, BoundingBox worldBounds, Vector3 lightPos) {
    if (isMapLightmapped) {
        return false;
    }
    return aabbDeeplyContainsPoint(worldBounds, lightPos);
}

bool shadowSampleFaceDecision(
    const Matrix& viewProj,
    Vector3 samplePos,
    float closestDepth,
    float bias,
    float& visibilityOut) {
    visibilityOut = 1.0f;
    const Vector4 clip = transformPoint(viewProj, samplePos);
    if (std::fabs(clip.w) < 1e-6f) {
        return false;
    }
    const Vector3 ndc{
        clip.x / clip.w,
        clip.y / clip.w,
        clip.z / clip.w,
    };
    if (ndc.z < -1.0f || ndc.z > 1.0f || ndc.x < -1.0f || ndc.x > 1.0f ||
        ndc.y < -1.0f || ndc.y > 1.0f) {
        return false;
    }
    const float current = ndc.z * 0.5f + 0.5f;
    visibilityOut = (current - bias > closestDepth) ? 0.0f : 1.0f;
    return true;
}

float shadowVisibilityPointDecision(
    const Matrix viewProj[kDynamicShadowFacesPerSlot],
    const float closestDepth[kDynamicShadowFacesPerSlot],
    Vector3 lightPos,
    Vector3 samplePos,
    float bias) {
    const Vector3 dir = Vector3Subtract(samplePos, lightPos);
    const int primary = cubeFacePrimaryAxis(dir);
    const int primaryFace = cubeFaceIndexAxis(dir, primary);
    float visibility = 1.0f;
    if (shadowSampleFaceDecision(
            viewProj[primaryFace],
            samplePos,
            closestDepth[primaryFace],
            bias,
            visibility)) {
        return visibility;
    }

    const int secondary = cubeFaceSecondaryAxis(dir, primary);
    const int secondaryFace = cubeFaceIndexAxis(dir, secondary);
    if (shadowSampleFaceDecision(
            viewProj[secondaryFace],
            samplePos,
            closestDepth[secondaryFace],
            bias,
            visibility)) {
        return visibility;
    }
    return 1.0f;
}

float shadowVisibilitySpotDecision(
    const Matrix& viewProj,
    float closestDepth,
    Vector3 samplePos,
    float bias) {
    float visibility = 1.0f;
    if (shadowSampleFaceDecision(viewProj, samplePos, closestDepth, bias, visibility)) {
        return visibility;
    }
    return 1.0f;
}

Camera3D shadowCameraFromDesc(const ShadowCameraDesc& desc) {
    Camera3D camera{};
    camera.position = desc.position;
    camera.target = desc.target;
    camera.up = desc.up;
    camera.fovy = desc.fovy;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

}
