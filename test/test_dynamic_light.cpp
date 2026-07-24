#include "test_assert.hpp"

#include "render/dynamic_light.hpp"
#include "render/dynamic_light_shadow_math.hpp"

#include <cmath>
#include <vector>

#include <raymath.h>

namespace slopengine {

namespace {

bool nearEq(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

RankedDynamicLight makePoint(
    Vector3 position,
    float intensity,
    float range,
    bool castShadows) {
    RankedDynamicLight light{};
    light.position = position;
    light.linearRgb = {1.0f, 1.0f, 1.0f};
    light.light.kind = DynamicLightKind::Point;
    light.light.intensity = intensity;
    light.light.range = range;
    light.light.castShadows = castShadows;
    return light;
}

Matrix identityViewProj() {
    return MatrixIdentity();
}

} // namespace

void runDynamicLightTests() {
    {
        std::vector<RankedDynamicLight> candidates;
        candidates.push_back(makePoint({0.0f, 0.0f, 10.0f}, 1.0f, 8.0f, true));
        candidates.push_back(makePoint({0.0f, 0.0f, 1.0f}, 1.0f, 8.0f, true));
        const auto ranked = rankDynamicLights(candidates, {0.0f, 0.0f, 0.0f}, 8, 2);
        CHECK_EQ(static_cast<int>(ranked.size()), 2);
        CHECK(ranked[0].score > ranked[1].score);
        CHECK(nearEq(ranked[0].position.z, 1.0f));
    }

    {
        std::vector<RankedDynamicLight> candidates;
        for (int i = 0; i < 4; ++i) {
            candidates.push_back(makePoint(
                {0.0f, 0.0f, static_cast<float>(i)},
                2.0f,
                8.0f,
                true));
        }
        const auto ranked = rankDynamicLights(candidates, {0.0f, 0.0f, 0.0f}, 8, 2);
        int shadowed = 0;
        for (const RankedDynamicLight& light : ranked) {
            if (light.shadowSlot >= 0) {
                ++shadowed;
            }
        }
        CHECK_EQ(shadowed, 2);
        CHECK_EQ(ranked[0].shadowSlot, 0);
        CHECK_EQ(ranked[1].shadowSlot, 1);
        CHECK_EQ(ranked[2].shadowSlot, -1);
        CHECK_EQ(ranked[3].shadowSlot, -1);
    }

    {
        std::vector<RankedDynamicLight> candidates;
        candidates.push_back(makePoint({0.0f, 0.0f, 1.0f}, 5.0f, 8.0f, false));
        candidates.push_back(makePoint({0.0f, 0.0f, 2.0f}, 1.0f, 8.0f, true));
        const auto ranked = rankDynamicLights(candidates, {0.0f, 0.0f, 0.0f}, 8, 2);
        CHECK_EQ(ranked[0].shadowSlot, -1);
        CHECK_EQ(ranked[1].shadowSlot, 0);
    }

    {
        RankedDynamicLight light = makePoint({0.0f, 1.5f, 1.5f}, 2.5f, 2.5f, true);
        light.linearRgb = {0.25f, 0.55f, 1.0f};
        const Vector3 door{0.0f, 1.1f, 0.0f};
        const Vector3 contrib = evaluateDynamicLightAtPoint(light, door, {0.0f, 1.0f, 0.0f});
        const float dist = Vector3Distance(light.position, door);
        const float t = dist / light.light.range;
        const float atten = std::max(0.0f, 1.0f - t * t);
        const float expected = atten * atten * light.light.intensity;
        CHECK(contrib.x > 0.05f);
        CHECK(nearEq(contrib.x, light.linearRgb.x * expected));
        CHECK(nearEq(contrib.y, light.linearRgb.y * expected));
        CHECK(nearEq(contrib.z, light.linearRgb.z * expected));
    }

    {
        RankedDynamicLight light{};
        light.position = {0.0f, 0.0f, 0.0f};
        light.direction = {0.0f, 0.0f, 1.0f};
        light.linearRgb = {1.0f, 1.0f, 1.0f};
        light.light.kind = DynamicLightKind::Spot;
        light.light.intensity = 1.0f;
        light.light.range = 8.0f;
        light.light.coneAngle = 0.7f;

        const Vector3 onAxis = evaluateDynamicLightAtPoint(
            light,
            {0.0f, 0.0f, 2.0f},
            {0.0f, 1.0f, 0.0f});
        CHECK(onAxis.x > 0.0f);

        const float cosInner = std::cos(light.light.coneAngle * 0.7f);
        const float cosOuter = std::cos(light.light.coneAngle);
        CHECK(cosInner > cosOuter);

        const float midCos = 0.5f * (cosInner + cosOuter);
        const float midAngle = std::acos(midCos);
        const Vector3 midDir{std::sin(midAngle), 0.0f, std::cos(midAngle)};
        const Vector3 midPoint = Vector3Scale(midDir, 2.0f);
        const Vector3 mid = evaluateDynamicLightAtPoint(light, midPoint, {0.0f, 1.0f, 0.0f});
        CHECK(mid.x > 0.0f);
        CHECK(mid.x < onAxis.x);
    }

    {
        CHECK_EQ(cubeFaceIndexAxis({1.0f, 0.0f, 0.0f}, 0), 0);
        CHECK_EQ(cubeFaceIndexAxis({-1.0f, 0.0f, 0.0f}, 0), 1);
        CHECK_EQ(cubeFaceIndexAxis({0.0f, 1.0f, 0.0f}, 1), 2);
        CHECK_EQ(cubeFaceIndexAxis({0.0f, -1.0f, 0.0f}, 1), 3);
        CHECK_EQ(cubeFaceIndexAxis({0.0f, 0.0f, 1.0f}, 2), 4);
        CHECK_EQ(cubeFaceIndexAxis({0.0f, 0.0f, -1.0f}, 2), 5);
    }

    {
        const Vector3 dir{1.0f, 0.8f, 0.2f};
        const int primary = cubeFacePrimaryAxis(dir);
        CHECK_EQ(primary, 0);
        const int secondary = cubeFaceSecondaryAxis(dir, primary);
        CHECK_EQ(secondary, 1);
        CHECK_EQ(cubeFaceIndexAxis(dir, primary), 0);
        CHECK_EQ(cubeFaceIndexAxis(dir, secondary), 2);
    }

    {
        const float nearShort = shadowNearPlane(2.5f);
        CHECK(nearShort < 0.05f);
        CHECK(nearEq(nearShort, 0.025f));
        CHECK(nearEq(shadowNearPlane(0.1f), 0.005f));
        CHECK(nearEq(shadowNearPlane(20.0f), 0.05f));
    }

    {
        const Matrix vp = identityViewProj();
        float visibility = 1.0f;
        const bool sampled = shadowSampleFaceDecision(
            vp,
            {1.1f, 0.5f, 0.0f},
            0.1f,
            kDynamicShadowBias,
            visibility);
        CHECK(sampled);
        CHECK(nearEq(visibility, 0.0f));
    }

    {
        Matrix viewProj[kDynamicShadowFacesPerSlot]{};
        float closest[kDynamicShadowFacesPerSlot]{};
        for (int i = 0; i < kDynamicShadowFacesPerSlot; ++i) {
            viewProj[i] = MatrixIdentity();
            closest[i] = 1.0f;
        }
        closest[4] = 0.52f;
        const float visibility = shadowVisibilityPointDecision(
            viewProj,
            closest,
            {0.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 0.5f},
            kDynamicShadowBias);
        CHECK(nearEq(visibility, 0.0f));
        CHECK(shadowNearPlane(2.5f) < 0.03f);
    }

    {
        const ShadowCameraDesc upFace = pointShadowFaceCamera({0.0f, 0.0f, 0.0f}, 2);
        CHECK(Vector3Length(upFace.up) > 0.5f);
        const ShadowCameraDesc downFace = pointShadowFaceCamera({0.0f, 0.0f, 0.0f}, 3);
        CHECK(Vector3Length(downFace.up) > 0.5f);
        const ShadowCameraDesc plusZ = pointShadowFaceCamera({0.0f, 0.0f, 0.0f}, 4);
        CHECK(nearEq(plusZ.up.x, 0.0f));
        CHECK(nearEq(plusZ.up.y, -1.0f));
        CHECK(nearEq(plusZ.up.z, 0.0f));

        const ShadowCameraDesc verticalSpot =
            spotShadowCamera({0.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 0.7f);
        CHECK(nearEq(verticalSpot.up.z, 1.0f));
    }

    {
        const Vector3 lightPos{0.0f, 0.0f, 0.0f};
        const Matrix proj = MatrixPerspective(90.0f * DEG2RAD, 1.0f, 0.05f, 8.0f);
        const Vector3 samples[] = {
            {3.0f, 0.5f, 0.2f},
            {0.2f, 3.0f, 0.5f},
            {-2.0f, 0.1f, 0.1f},
            {0.5f, -2.0f, 0.2f},
            {0.3f, 0.2f, 3.0f},
            {0.1f, 0.2f, -3.0f},
        };
        for (const Vector3& sample : samples) {
            const Vector3 dir = Vector3Subtract(sample, lightPos);
            const int face = cubeFaceIndexAxis(dir, cubeFacePrimaryAxis(dir));
            const ShadowCameraDesc desc = pointShadowFaceCamera(lightPos, face);
            const Matrix view = MatrixLookAt(desc.position, desc.target, desc.up);
            const Matrix vp = MatrixMultiply(view, proj);
            float visibility = 0.0f;
            CHECK(shadowSampleFaceDecision(vp, sample, 1.0f, kDynamicShadowBias, visibility));
            CHECK(nearEq(visibility, 1.0f));
        }
    }

    {
        const BoundingBox box{{-1.0f, -1.0f, -0.06f}, {1.0f, 1.0f, 0.06f}};
        CHECK(aabbContainsPoint(box, {0.0f, 0.0f, 0.0f}));
        CHECK_FALSE(aabbContainsPoint(box, {0.0f, 0.0f, 1.0f}, 0.02f));
        CHECK_FALSE(aabbContainsPointInset(box, {0.0f, 0.0f, 0.055f}, 0.04f));
        CHECK(aabbContainsPointInset(
            BoundingBox{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
            {0.0f, 0.0f, 0.0f},
            0.04f));
    }

    {
        const BoundingBox mapBounds{{-50.0f, -1.0f, -50.0f}, {50.0f, 10.0f, 50.0f}};
        const Vector3 indoorLight{0.0f, 1.5f, 3.0f};
        CHECK(aabbContainsPointInset(mapBounds, indoorLight));
        CHECK_FALSE(shouldSkipShadowCaster(true, mapBounds, indoorLight));
        CHECK(shouldSkipShadowCaster(
            false,
            BoundingBox{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
            {0.0f, 0.0f, 0.0f}));
    }

    {
        const BoundingBox door{{-1.0f, 0.0f, -0.06f}, {1.0f, 2.2f, 0.06f}};
        const Vector3 surfaceHit{0.0f, 1.1f, 0.06f};
        CHECK_FALSE(aabbDeeplyContainsPoint(door, surfaceHit, 0.05f));
        CHECK(aabbDeeplyContainsPoint(door, {0.0f, 1.1f, 0.0f}, 0.05f));
        CHECK_FALSE(shouldSkipShadowCaster(false, door, surfaceHit));
        CHECK(shouldSkipShadowCaster(false, door, {0.0f, 1.1f, 0.0f}));
        CHECK(aabbContainsPointInset(door, {0.0f, 1.1f, 0.0f}, 0.04f));
        CHECK_FALSE(shouldSkipShadowCaster(false, door, {0.0f, 1.1f, 0.14f}));
    }
}

}
