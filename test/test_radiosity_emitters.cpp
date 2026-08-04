#include "map/radiosity_emitters.hpp"
#include "test_assert.hpp"

#include <cmath>

namespace slopengine {

namespace {

bool near(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

bool near3(Vector3 a, Vector3 b, float eps = 1e-5f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

} // namespace

void runRadiosityEmitterTests() {
    CHECK_EQ(chooseEmitterBlockSize(50, 64, 64), 1);
    CHECK_EQ(chooseEmitterBlockSize(kTargetEmittersPerChart, 128, 128), 1);
    CHECK_EQ(chooseEmitterBlockSize(4096, 64, 64), 4);
    CHECK_EQ(chooseEmitterBlockSize(100000, 64, 64), kMaxEmitterBlockSize);
    CHECK_EQ(chooseEmitterBlockSize(4096, 2, 64), 2);

    const std::vector<EmitterMergeCandidate> candidates = {
        {{0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}},
        {{1.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}},
    };
    const std::optional<EmitterPatch> merged =
        mergeEmitterBlock(candidates, 0.25f, {0.0f, 0.0f, 1.0f}, 0.02f, 3, 7);
    CHECK(merged.has_value());
    CHECK(near(merged->area, 0.5f));
    CHECK(near3(merged->radiance, {2.0f, 2.0f, 2.0f}));
    CHECK(near(merged->position.x, 0.5f));
    CHECK(near(merged->position.y, 0.0f));
    CHECK(near(merged->position.z, 0.02f));
    CHECK_EQ(merged->faceIndex, 3);
    CHECK_EQ(merged->interiorLeaf, 7);

    CHECK(emitterPairBelowThreshold({0.01f, 0.01f, 0.01f}, 0.1f, 100.0f, 0.0025f));
    CHECK_FALSE(emitterPairBelowThreshold({10.0f, 10.0f, 10.0f}, 1.0f, 0.1f, 0.0025f));
}

}
