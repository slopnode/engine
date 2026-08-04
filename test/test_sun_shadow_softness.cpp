#include "map/radiosity.hpp"
#include "test_assert.hpp"

#include <cmath>

namespace slopengine {

void runSunShadowSoftnessTests() {
    const SunShadowSoftnessParams sharp = resolveSunShadowSoftness(0.0f);
    CHECK_EQ(sharp.rayCount, 1);
    CHECK(sharp.angularSpreadRad == 0.0f);

    const SunShadowSoftnessParams fuzzy = resolveSunShadowSoftness(1.0f);
    CHECK_EQ(fuzzy.rayCount, 16);
    CHECK(std::fabs(fuzzy.angularSpreadRad - 0.07f) < 1e-6f);

    const SunShadowSoftnessParams mid = resolveSunShadowSoftness(0.5f);
    CHECK(mid.rayCount >= 8 && mid.rayCount <= 9);
    CHECK(std::fabs(mid.angularSpreadRad - 0.035f) < 1e-6f);

    const SunShadowSoftnessParams clamped = resolveSunShadowSoftness(5.0f);
    CHECK_EQ(clamped.rayCount, fuzzy.rayCount);
    CHECK(std::fabs(clamped.angularSpreadRad - fuzzy.angularSpreadRad) < 1e-6f);
}

}
