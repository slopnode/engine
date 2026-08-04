#include "test_assert.hpp"

#include "render/dynamic_light_compositing.hpp"

#include <cmath>

namespace slopengine {

namespace {

bool nearEq(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

void runDynamicLightCompositingTests() {
    {
        const Vector3 bakedDisplay{0.048f, 0.048f, 0.048f};
        const Vector3 plasmaLinear{0.0f, 0.04f, 2.33f};
        const Vector3 composed = composeDisplayAdditiveOverlay(bakedDisplay, plasmaLinear);
        const float delta = displayLuminance(composed) - displayLuminance(bakedDisplay);
        CHECK(delta > 0.05f);
        CHECK(composed.z > bakedDisplay.z + 0.5f);
    }

    {
        const Vector3 bakedDisplay{0.07f, 0.07f, 0.07f};
        const Vector3 plasmaLinear{3.0f, 3.0f, 3.0f};
        const Vector3 composed = composeDisplayAdditiveOverlay(bakedDisplay, plasmaLinear);
        const float delta = composed.x - bakedDisplay.x;
        CHECK(delta > 0.6f);
    }

    {
        const Vector3 bakedDisplay{0.8f, 0.8f, 0.8f};
        const Vector3 plasmaLinear{3.0f, 3.0f, 3.0f};
        const Vector3 composed = composeDisplayAdditiveOverlay(bakedDisplay, plasmaLinear);
        CHECK(composed.x > bakedDisplay.x);
    }

    {
        const Vector3 bakedDisplay{0.2f, 0.3f, 0.1f};
        const Vector3 composed = composeDisplayAdditiveOverlay(bakedDisplay, {0.0f, 0.0f, 0.0f});
        CHECK(nearEq(composed.x, bakedDisplay.x));
        CHECK(nearEq(composed.y, bakedDisplay.y));
        CHECK(nearEq(composed.z, bakedDisplay.z));
    }

    {
        const Vector3 composed =
            composeDisplayAdditiveOverlay({0.2f, 0.8f, 0.1f}, {100.0f, 100.0f, 100.0f});
        CHECK(composed.x <= 1.0f);
        CHECK(composed.y <= 1.0f);
        CHECK(composed.z <= 1.0f);
    }
}

} // namespace slopengine
