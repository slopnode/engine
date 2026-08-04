#include "map/radiosity.hpp"
#include "test_assert.hpp"

#include <cmath>

namespace slopengine {

void runSunShadowSoftnessTests() {
    const SunShadowSoftnessParams sharp = resolveSunShadowSoftness(0.0f);
    CHECK_EQ(sharp.rayCount, 1);
    CHECK(sharp.angularSpreadRad == 0.0f);
    CHECK(sharp.leakThreshold == 0.0f);
    CHECK(sharp.sunDenoiseRangeSigma == 0.35f);
    CHECK(sharp.sunDenoiseSpatialSigma == 1.0f);
    CHECK_EQ(sharp.sunDenoiseKernelRadius, 1);

    const SunShadowSoftnessParams fuzzy = resolveSunShadowSoftness(1.0f);
    CHECK_EQ(fuzzy.rayCount, 16);
    CHECK(std::fabs(fuzzy.angularSpreadRad - 0.05f) < 1e-6f);
    CHECK(std::fabs(fuzzy.leakThreshold - 0.12f) < 1e-6f);
    CHECK(std::fabs(fuzzy.sunDenoiseRangeSigma - 1.2f) < 1e-6f);
    CHECK(std::fabs(fuzzy.sunDenoiseSpatialSigma - 2.0f) < 1e-6f);
    CHECK_EQ(fuzzy.sunDenoiseKernelRadius, 2);

    const SunShadowSoftnessParams mid = resolveSunShadowSoftness(0.5f);
    CHECK(mid.rayCount >= 8 && mid.rayCount <= 9);
    CHECK(std::fabs(mid.angularSpreadRad - 0.025f) < 1e-6f);
    CHECK(std::fabs(mid.leakThreshold - 0.06f) < 1e-6f);
    CHECK(std::fabs(mid.sunDenoiseRangeSigma - 0.775f) < 1e-6f);
    CHECK(mid.sunDenoiseSpatialSigma == 1.0f);
    CHECK_EQ(mid.sunDenoiseKernelRadius, 1);

    const SunShadowSoftnessParams clamped = resolveSunShadowSoftness(5.0f);
    CHECK_EQ(clamped.rayCount, fuzzy.rayCount);
    CHECK(std::fabs(clamped.angularSpreadRad - fuzzy.angularSpreadRad) < 1e-6f);
    CHECK(std::fabs(clamped.leakThreshold - fuzzy.leakThreshold) < 1e-6f);
}

}
