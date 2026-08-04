#include "test_assert.hpp"

#include "render/dynamic_light_compositing.hpp"

#include <cmath>

namespace slopengine {

namespace {

bool nearEq(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

float tonemap(float linear) {
    return linear / (1.0f + std::max(linear, 0.0f));
}

} // namespace

void runDynamicLightCompositingTests() {
    {
        CHECK(nearEq(dynamicOverlayScale(0.07f), 1.0f));
        CHECK(nearEq(dynamicOverlayScale(0.69f), 1.0f));
    }

    {
        CHECK(nearEq(dynamicOverlayScale(0.98f), kDynamicOverlayMinScale));
        CHECK(nearEq(dynamicOverlayScale(1.0f), kDynamicOverlayMinScale));
    }

    {
        const float mid = dynamicOverlayScale(0.825f);
        CHECK(mid > kDynamicOverlayMinScale);
        CHECK(mid < 1.0f);
    }

    {
        const Vector3 bakedDisplay{0.07f, 0.07f, 0.07f};
        const Vector3 plasmaLinear{3.0f, 3.0f, 3.0f};
        const Vector3 composed = composeLumaAwareOverlay(bakedDisplay, plasmaLinear);
        const float delta = composed.x - bakedDisplay.x;
        CHECK(delta > 0.6f);
    }

    {
        const Vector3 bakedDisplay{0.5f, 0.5f, 0.5f};
        const Vector3 plasmaLinear{3.0f, 3.0f, 3.0f};
        const Vector3 composed = composeLumaAwareOverlay(bakedDisplay, plasmaLinear);
        const float delta = composed.x - bakedDisplay.x;
        CHECK(delta > 0.1f);
        CHECK(delta < 0.75f);
    }

    {
        const Vector3 bakedDisplay{0.986f, 0.986f, 0.986f};
        const Vector3 plasmaLinear{3.0f, 3.0f, 3.0f};
        const Vector3 composed = composeLumaAwareOverlay(bakedDisplay, plasmaLinear);
        const float oldDelta =
            tonemap(70.0f + 3.0f) - tonemap(70.0f);
        const float newDelta = composed.x - bakedDisplay.x;
        CHECK(newDelta > oldDelta * 10.0f);
        CHECK(newDelta > 0.04f);
    }

    {
        const Vector3 bakedDisplay{0.2f, 0.3f, 0.1f};
        const Vector3 composed = composeLumaAwareOverlay(bakedDisplay, {0.0f, 0.0f, 0.0f});
        CHECK(nearEq(composed.x, bakedDisplay.x));
        CHECK(nearEq(composed.y, bakedDisplay.y));
        CHECK(nearEq(composed.z, bakedDisplay.z));
    }

    {
        const Vector3 bakedDisplay{0.2f, 0.8f, 0.1f};
        const Vector3 composed = composeLumaAwareOverlay(bakedDisplay, {100.0f, 100.0f, 100.0f});
        CHECK(composed.x <= 1.0f);
        CHECK(composed.y <= 1.0f);
        CHECK(composed.z <= 1.0f);
    }
}

} // namespace slopengine
