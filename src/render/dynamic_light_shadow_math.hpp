#pragma once

#include "render/dynamic_light.hpp"

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

struct ShadowCameraDesc {
    Vector3 position{};
    Vector3 target{};
    Vector3 up{0.0f, 1.0f, 0.0f};
    float fovy = 90.0f;
};

Vector3 cubeFaceTarget(int face);
Vector3 cubeFaceUp(int face);
int cubeFaceIndexAxis(Vector3 dir, int axis);
int cubeFacePrimaryAxis(Vector3 dir);
int cubeFaceSecondaryAxis(Vector3 dir, int primaryAxis);

ShadowCameraDesc pointShadowFaceCamera(Vector3 lightPos, int face);
ShadowCameraDesc spotShadowCamera(Vector3 lightPos, Vector3 direction, float coneAngleRadians);

float shadowNearPlane(float range);

bool aabbContainsPoint(BoundingBox box, Vector3 point, float epsilon = 0.02f);
bool aabbContainsPointInset(BoundingBox box, Vector3 point, float inset = 0.04f);
bool aabbDeeplyContainsPoint(BoundingBox box, Vector3 point, float minDepth = 0.05f);
bool shouldSkipShadowCaster(bool isMapLightmapped, BoundingBox worldBounds, Vector3 lightPos);

bool shadowSampleFaceDecision(
    const Matrix& viewProj,
    Vector3 samplePos,
    float closestDepth,
    float bias,
    float& visibilityOut);

float shadowVisibilityPointDecision(
    const Matrix viewProj[kDynamicShadowFacesPerSlot],
    const float closestDepth[kDynamicShadowFacesPerSlot],
    Vector3 lightPos,
    Vector3 samplePos,
    float bias);

float shadowVisibilitySpotDecision(
    const Matrix& viewProj,
    float closestDepth,
    Vector3 samplePos,
    float bias);

Camera3D shadowCameraFromDesc(const ShadowCameraDesc& desc);

}
