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

EmissiveFace makeTestFace() {
    EmissiveFace face;
    face.faceIndex = 0;
    face.normal = {0.0f, 0.0f, 1.0f};
    face.uAxis = {1.0f, 0.0f, 0.0f};
    face.vAxis = {0.0f, 1.0f, 0.0f};
    face.planeD = 0.0f;
    face.uMin = 0.0f;
    face.uMax = 2.0f;
    face.vMin = 0.0f;
    face.vMax = 2.0f;
    face.area = 4.0f;
    face.gridWidth = 2;
    face.gridHeight = 2;
    face.gridOffset = 0;
    face.peakRadiance = {2.0f, 2.0f, 2.0f};
    face.aabbMins = {0.0f, 0.0f, 0.0f};
    face.aabbMaxs = {2.0f, 2.0f, 0.0f};
    return face;
}

} // namespace

void runRadiosityEmitterTests() {
    CHECK(passesCastGate({1.0f, 1.0f, 1.0f}));
    CHECK_FALSE(passesCastGate({0.01f, 0.01f, 0.01f}));

    CHECK(emitterPairBelowThreshold({0.01f, 0.01f, 0.01f}, 0.1f, 100.0f, 0.0025f));
    CHECK_FALSE(emitterPairBelowThreshold({10.0f, 10.0f, 10.0f}, 1.0f, 0.1f, 0.0025f));
    CHECK(emitterPairBelowThreshold({10.0f, 10.0f, 10.0f}, 1.0f, 25.0f, 0.0025f, 4.0f));
    CHECK_FALSE(emitterPairBelowThreshold({10.0f, 10.0f, 10.0f}, 1.0f, 4.0f, 0.0025f, 4.0f));

    const float unlimitedRadius = emitterInfluenceRadius({10.0f, 10.0f, 10.0f}, 16.0f, 0.05f);
    const float cappedRadius = emitterInfluenceRadius({10.0f, 10.0f, 10.0f}, 16.0f, 0.05f, 4.0f);
    CHECK(cappedRadius < unlimitedRadius);
    CHECK(near(cappedRadius, 4.0f));

    CHECK(near(emitterRangeAttenuation(0.0f, 8.0f), 1.0f));
    CHECK(near(emitterRangeAttenuation(4.0f, 0.0f), 1.0f));
    CHECK(near(emitterRangeAttenuation(8.0f, 8.0f), 0.0f));
    CHECK(emitterRangeAttenuation(4.0f, 8.0f) > 0.0f);
    CHECK(emitterRangeAttenuation(4.0f, 8.0f) < 1.0f);

    CHECK(near(dist2PointToAabb({0.5f, 0.5f, 1.0f}, {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 0.0f}), 1.0f));

    const EmissiveFace face = makeTestFace();
    const std::vector<Vector3> grid = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
    };
    const Vector3 centerSample = sampleEmissionGridBilinear(face, grid, 1.0f, 1.0f);
    CHECK(near3(centerSample, {0.5f, 0.5f, 0.5f}));

    std::vector<EmissiveFace> faces(2);
    faces[0] = face;
    faces[1] = face;
    faces[1].faceIndex = 1;
    faces[1].aabbMins = {2000.0f, 0.0f, 0.0f};
    faces[1].aabbMaxs = {2002.0f, 2.0f, 0.0f};
    faces[1].peakRadiance = {1e-4f, 1e-4f, 1e-4f};

    const EmitterBvh bvh = buildEmitterBvh(faces, 0.05f);
    CHECK_FALSE(bvh.empty());

    std::vector<std::int32_t> nearIndices;
    forEachEmitterNear(bvh, {1.0f, 1.0f, 1.0f}, 2.0f, [&](std::int32_t index) {
        nearIndices.push_back(index);
    });
    CHECK_EQ(nearIndices.size(), 1u);
    CHECK_EQ(nearIndices[0], 0);

    nearIndices.clear();
    forEachEmitterNear(bvh, {2001.0f, 1.0f, 1.0f}, 2.0f, [&](std::int32_t index) {
        nearIndices.push_back(index);
    });
    CHECK_EQ(nearIndices.size(), 1u);
    CHECK_EQ(nearIndices[0], 1);
}

} // namespace slopengine
