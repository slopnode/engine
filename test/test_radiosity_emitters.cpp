#include "map/emitter_bvh.hpp"
#include "map/radiosity_emitters.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <vector>

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
    CHECK(passesCastGate({1.0f, 1.0f, 1.0f}));
    CHECK_FALSE(passesCastGate({0.01f, 0.01f, 0.01f}));

    const std::vector<EmitterMergeCandidate> uniform = {
        {{0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}},
        {{1.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}},
    };
    const std::vector<EmitterMergeCandidate> mixed = {
        {{0.0f, 0.0f, 0.0f}, {0.1f, 0.1f, 0.1f}},
        {{1.0f, 0.0f, 0.0f}, {5.0f, 5.0f, 5.0f}},
    };
    CHECK(blockIsUniform(uniform));
    CHECK_FALSE(blockIsUniform(mixed));

    CHECK_FALSE(shouldMergeChart(1000));
    CHECK(shouldMergeChart(kMaxEmittersBeforeMerge + 1));
    CHECK_FALSE(shouldMergeChart(kMaxEmittersBeforeGpuMerge, kMaxEmittersBeforeGpuMerge));
    CHECK(shouldMergeChart(kMaxEmittersBeforeGpuMerge + 1, kMaxEmittersBeforeGpuMerge));
    CHECK_EQ(emitterMergeThreshold(false), kMaxEmittersBeforeMerge);
    CHECK_EQ(emitterMergeThreshold(true), kMaxEmittersBeforeGpuMerge);

    const std::optional<EmitterPatch> merged =
        mergeEmitterBlock(uniform, 0.25f, {0.0f, 0.0f, 1.0f}, 0.02f, 3, 7);
    CHECK(merged.has_value());
    CHECK(near(merged->area, 0.5f));
    CHECK(near3(merged->radiance, {2.0f, 2.0f, 2.0f}));
    CHECK_EQ(merged->faceIndex, 3);

    CHECK(emitterPairBelowThreshold({0.01f, 0.01f, 0.01f}, 0.1f, 100.0f, 0.0025f));
    CHECK_FALSE(emitterPairBelowThreshold({10.0f, 10.0f, 10.0f}, 1.0f, 0.1f, 0.0025f));

    std::vector<EmitterPatch> emitters(2);
    emitters[0].position = {0.0f, 0.0f, 0.0f};
    emitters[0].normal = {0.0f, 0.0f, 1.0f};
    emitters[0].radiance = {1e-4f, 1e-4f, 1e-4f};
    emitters[0].area = 0.25f;
    emitters[0].faceIndex = 0;
    emitters[1].position = {100.0f, 0.0f, 0.0f};
    emitters[1].normal = {0.0f, 0.0f, 1.0f};
    emitters[1].radiance = {1e-4f, 1e-4f, 1e-4f};
    emitters[1].area = 0.25f;
    emitters[1].faceIndex = 1;

    const EmitterBvh bvh = buildEmitterBvh(emitters, 0.05f);
    CHECK_FALSE(bvh.empty());

    std::vector<std::int32_t> nearIndices;
    forEachEmitterNear(bvh, {0.0f, 0.0f, 0.0f}, 0.15f, [&](std::int32_t index) {
        nearIndices.push_back(index);
    });
    CHECK_EQ(nearIndices.size(), 1u);
    CHECK_EQ(nearIndices[0], 0);

    nearIndices.clear();
    forEachEmitterNear(bvh, {100.0f, 0.0f, 0.0f}, 0.15f, [&](std::int32_t index) {
        nearIndices.push_back(index);
    });
    CHECK_EQ(nearIndices.size(), 1u);
    CHECK_EQ(nearIndices[0], 1);
}

}
