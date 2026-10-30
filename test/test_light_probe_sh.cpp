#include "map/light_sample.hpp"
#include "test_assert.hpp"

#include <cmath>

namespace slopengine {

namespace {

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

}

void runLightProbeShTests() {
    MapLighting lighting;
    CHECK(!sampleLightProbeSH(lighting, {0.0f, 0.0f, 0.0f}).has_value());

    lighting.probeGridCoarse.cellSize = 2.0f;
    ProbeSH dark;
    dark.coeff[0] = {1.0f, 1.0f, 1.0f};
    ProbeSH bright;
    bright.coeff[0] = {3.0f, 3.0f, 3.0f};
    bright.coeff[2] = {1.0f, 0.5f, 0.0f};
    lighting.probeGridCoarse.probesByCell.emplace(ProbeCell{0, 0, 0}, dark);
    lighting.probeGridCoarse.probesByCell.emplace(ProbeCell{1, 0, 0}, bright);

    const auto mid = sampleLightProbeSH(lighting, {1.0f, 0.0f, 0.0f});
    CHECK(mid.has_value());
    CHECK(near(mid->coeff[0].x, 2.0f));
    CHECK(near(mid->coeff[2].x, 0.5f));
    CHECK(near(mid->coeff[2].y, 0.25f));
    CHECK(near(mid->coeff[1].x, 0.0f));

    const auto atBright = sampleLightProbeSH(lighting, {2.0f, 0.0f, 0.0f});
    CHECK(atBright.has_value());
    CHECK(near(atBright->coeff[0].z, 3.0f));

    const auto up = sampleLightProbe(lighting, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    CHECK(up.has_value());
    const Color expected = linearIrradianceToDisplayColor(2.5f, 2.25f, 2.0f);
    CHECK_EQ(static_cast<int>(up->r), static_cast<int>(expected.r));
    CHECK_EQ(static_cast<int>(up->g), static_cast<int>(expected.g));
    CHECK_EQ(static_cast<int>(up->b), static_cast<int>(expected.b));

    lighting.probeGridFine.cellSize = 1.0f;
    ProbeSH fine;
    fine.coeff[0] = {7.0f, 7.0f, 7.0f};
    lighting.probeGridFine.probesByCell.emplace(ProbeCell{1, 0, 0}, fine);
    const auto preferFine = sampleLightProbeSH(lighting, {1.0f, 0.0f, 0.0f});
    CHECK(preferFine.has_value());
    CHECK(near(preferFine->coeff[0].x, 7.0f));
}

}
