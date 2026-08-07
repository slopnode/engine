#include "map/seam_stitch.hpp"
#include "test_assert.hpp"

#include <cmath>

namespace slopengine {

void runSeamStitchTests() {
    SeamStitchParams params;
    params.maxDistance = 0.1f;

    CHECK(luxelsShareSeam(
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.05f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        params));

    CHECK_FALSE(luxelsShareSeam(
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.2f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        params));

    CHECK_FALSE(luxelsShareSeam(
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.05f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        params));

    CHECK_FALSE(luxelsShareSeam(
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.05f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        params));

    SeamStitchParams disabled;
    disabled.maxDistance = 0.0f;
    CHECK_FALSE(luxelsShareSeam(
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        disabled));

    CHECK(std::fabs(seamBlendWeight(0.0f, 0.1f) - 1.0f) < 1e-6f);
    CHECK(std::fabs(seamBlendWeight(0.1f, 0.1f)) < 1e-6f);
    CHECK(std::fabs(seamBlendWeight(0.05f, 0.1f) - 0.5f) < 1e-6f);
    CHECK(std::fabs(seamBlendWeight(0.2f, 0.1f)) < 1e-6f);
    CHECK(seamBlendWeight(0.02f, 0.1f) > seamBlendWeight(0.08f, 0.1f));
    CHECK(std::fabs(seamBlendWeight(1.0f, 0.0f)) < 1e-6f);
}

} // namespace slopengine
