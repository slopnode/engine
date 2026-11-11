#include "map/light_sample.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <filesystem>

namespace slopengine {

namespace {

bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

ProbeCube uniformCube(float value) {
    ProbeCube cube;
    for (Vector3& face : cube.face) {
        face = {value, value, value};
    }
    return cube;
}

}

void runLightProbeCubeTests() {
    MapLighting lighting;
    CHECK(!sampleLightProbeCube(lighting, {0.0f, 0.0f, 0.0f}).has_value());

    lighting.probeGridCoarse.cellSize = 2.0f;
    ProbeCube dark = uniformCube(1.0f);
    ProbeCube bright = uniformCube(3.0f);
    bright.face[2] = {5.0f, 4.0f, 3.0f};
    lighting.probeGridCoarse.probesByCell.emplace(ProbeCell{0, 0, 0}, dark);
    lighting.probeGridCoarse.probesByCell.emplace(ProbeCell{1, 0, 0}, bright);

    const auto mid = sampleLightProbeCube(lighting, {1.0f, 0.0f, 0.0f});
    CHECK(mid.has_value());
    CHECK(near(mid->face[0].x, 2.0f));
    CHECK(near(mid->face[2].x, 3.0f));
    CHECK(near(mid->face[2].y, 2.5f));
    CHECK(near(mid->face[2].z, 2.0f));

    const auto up = sampleLightProbe(lighting, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    CHECK(up.has_value());
    const Color expected = linearIrradianceToDisplayColor(3.0f, 2.5f, 2.0f);
    CHECK_EQ(static_cast<int>(up->r), static_cast<int>(expected.r));
    CHECK_EQ(static_cast<int>(up->g), static_cast<int>(expected.g));
    CHECK_EQ(static_cast<int>(up->b), static_cast<int>(expected.b));

    lighting.probeGridFine.cellSize = 1.0f;
    lighting.probeGridFine.probesByCell.emplace(ProbeCell{1, 0, 0}, uniformCube(7.0f));
    const auto preferFine = sampleLightProbeCube(lighting, {1.0f, 0.0f, 0.0f});
    CHECK(preferFine.has_value());
    CHECK(near(preferFine->face[0].x, 7.0f));

    ProbeCube sided{};
    sided.face[0] = {1.0f, 0.0f, 0.0f};
    sided.face[1] = {0.0f, 1.0f, 0.0f};
    sided.face[2] = {0.0f, 0.0f, 1.0f};
    sided.face[3] = {2.0f, 0.0f, 0.0f};
    sided.face[4] = {0.0f, 2.0f, 0.0f};
    sided.face[5] = {0.0f, 0.0f, 2.0f};
    const Vector3 posX = evaluateProbeCube(sided, {1.0f, 0.0f, 0.0f});
    CHECK(near(posX.x, 1.0f) && near(posX.y, 0.0f));
    const Vector3 negX = evaluateProbeCube(sided, {-1.0f, 0.0f, 0.0f});
    CHECK(near(negX.y, 1.0f) && near(negX.x, 0.0f));
    const Vector3 negY = evaluateProbeCube(sided, {0.0f, -1.0f, 0.0f});
    CHECK(near(negY.x, 2.0f));
    const Vector3 negZ = evaluateProbeCube(sided, {0.0f, 0.0f, -1.0f});
    CHECK(near(negZ.z, 2.0f));
    const float d = std::sqrt(0.5f);
    const Vector3 diagonal = evaluateProbeCube(sided, {d, d, 0.0f});
    CHECK(near(diagonal.x, 0.5f) && near(diagonal.z, 0.5f));
    const Vector3 uniform = evaluateProbeCube(uniformCube(2.0f), {0.48f, -0.6f, 0.64f});
    CHECK(near(uniform.x, 2.0f));

    const std::array<Color, 4> legacy{
        encodeRgbe(3.0f, 3.0f, 3.0f),
        encodeRgbe(0.0f, 0.0f, 0.0f),
        encodeRgbe(1.5f, 1.5f, 1.5f),
        encodeRgbe(0.0f, 0.0f, 0.0f),
    };
    const auto converted = probeCubeFromLegacySh(legacy);
    CHECK(near(decodeRgbe(converted[0]).x, 3.0f, 0.05f));
    CHECK(near(decodeRgbe(converted[2]).x, 4.0f, 0.05f));
    CHECK(near(decodeRgbe(converted[3]).x, 2.0f, 0.05f));

    RadFile rad;
    LightProbe probe;
    probe.cellX = 2;
    probe.cellY = -1;
    probe.cellZ = 5;
    for (std::size_t i = 0; i < kProbeCubeFaces; ++i) {
        probe.cubeRgbe[i] = encodeRgbe(static_cast<float>(i + 1), 0.5f, 0.25f);
    }
    rad.probeGridFine.cellSize = 1.5f;
    rad.probeGridFine.probes.push_back(probe);
    const auto tempDir = std::filesystem::temp_directory_path() / "sloptest_rad_cube";
    std::filesystem::create_directories(tempDir);
    const auto radPath = tempDir / "static.rad";
    CHECK(writeRadFile(radPath, rad));
    const auto loaded = readRadFile(radPath);
    CHECK(loaded.has_value());
    if (loaded) {
        CHECK_EQ(loaded->probeGridFine.probes.size(), 1u);
        CHECK(near(loaded->probeGridFine.cellSize, 1.5f));
        if (!loaded->probeGridFine.probes.empty()) {
            const LightProbe& back = loaded->probeGridFine.probes[0];
            CHECK_EQ(back.cellY, -1);
            CHECK(near(decodeRgbe(back.cubeRgbe[5]).x, 6.0f, 0.05f));
        }
    }
    std::filesystem::remove_all(tempDir);
}

}
