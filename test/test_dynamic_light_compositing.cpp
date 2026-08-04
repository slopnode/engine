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
        const Vector3 composed = composeLinearLightingOverlay(bakedDisplay, plasmaLinear);
        CHECK(composed.z > composed.x + 0.2f);
        CHECK(composed.z > composed.y + 0.2f);
        CHECK(composed.z > bakedDisplay.z + 0.2f);
    }

    {
        const Vector3 bakedDisplay{0.07f, 0.07f, 0.07f};
        const Vector3 plasmaLinear{0.0f, 0.2f, 3.0f};
        const Vector3 composed = composeLinearLightingOverlay(bakedDisplay, plasmaLinear);
        CHECK(composed.z > composed.x);
        CHECK(composed.z > composed.y);
    }

    {
        const Vector3 bakedDisplay{0.2f, 0.3f, 0.1f};
        const Vector3 composed = composeLinearLightingOverlay(bakedDisplay, {0.0f, 0.0f, 0.0f});
        CHECK(nearEq(composed.x, bakedDisplay.x));
        CHECK(nearEq(composed.y, bakedDisplay.y));
        CHECK(nearEq(composed.z, bakedDisplay.z));
    }

    {
        const Vector3 bakedDisplay{0.1f, 0.1f, 0.1f};
        const Vector3 warmLinear{2.2f, 1.65f, 0.77f};
        const Vector3 composed = composeLinearLightingOverlay(bakedDisplay, warmLinear);
        CHECK(composed.x > composed.z);
        CHECK(composed.x > composed.y);
    }
}

} // namespace slopengine
