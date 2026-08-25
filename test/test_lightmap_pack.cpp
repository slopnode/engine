#include "map/lightmap.hpp"
#include "test_assert.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace slopengine {

namespace {

LightmapFace makeRectFace(std::string id, float y, float xExtent, float zExtent) {
    LightmapFace face;
    face.id = std::move(id);
    face.material = "mat/a";
    face.normal = {0.0f, 1.0f, 0.0f};
    face.vertices = {
        {0.0f, y, 0.0f},
        {xExtent, y, 0.0f},
        {xExtent, y, zExtent},
        {0.0f, y, zExtent},
    };
    return face;
}

struct FootprintRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

FootprintRect footprintOf(const LightmapFaceGroup& group) {
    return {
        group.atlasX,
        group.atlasY,
        group.rotated ? group.luxelHeight : group.luxelWidth,
        group.rotated ? group.luxelWidth : group.luxelHeight,
    };
}

bool rectsOverlap(const FootprintRect& a, const FootprintRect& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

void checkNoOverlapsAndBounds(const std::vector<LightmapFaceGroup>& groups, int atlasSize) {
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const FootprintRect a = footprintOf(groups[i]);
        CHECK(a.x >= 0);
        CHECK(a.y >= 0);
        CHECK(a.x + a.w <= atlasSize);
        CHECK(a.y + a.h <= atlasSize);
        for (std::size_t j = i + 1; j < groups.size(); ++j) {
            if (groups[i].atlasIndex != groups[j].atlasIndex) {
                continue;
            }
            CHECK(!rectsOverlap(a, footprintOf(groups[j])));
        }
    }
}

const LightmapChart* findChart(const RadFile& rad, const std::string& faceId) {
    for (const LightmapChart& chart : rad.charts) {
        if (chart.faceId == faceId) {
            return &chart;
        }
    }
    return nullptr;
}

void appendU32(std::vector<std::byte>& buf, std::uint32_t value) {
    const auto* p = reinterpret_cast<const std::byte*>(&value);
    buf.insert(buf.end(), p, p + sizeof(value));
}

void appendI32(std::vector<std::byte>& buf, std::int32_t value) {
    const auto* p = reinterpret_cast<const std::byte*>(&value);
    buf.insert(buf.end(), p, p + sizeof(value));
}

void appendF32(std::vector<std::byte>& buf, float value) {
    const auto* p = reinterpret_cast<const std::byte*>(&value);
    buf.insert(buf.end(), p, p + sizeof(value));
}

void appendString(std::vector<std::byte>& buf, const std::string& value) {
    appendU32(buf, static_cast<std::uint32_t>(value.size()));
    const auto* p = reinterpret_cast<const std::byte*>(value.data());
    buf.insert(buf.end(), p, p + value.size());
}

} // namespace

void runLightmapPackTests() {
    // Backfill: two equally-sized squares each force their own atlas (a 6x6 chart cannot
    // share a 10x10 atlas with another 6x6 chart), leaving an identical leftover strip in
    // both atlas 0 and atlas 1. A third, smaller chart that fits that leftover should land
    // back in atlas 0 rather than forcing a fresh atlas 2 - proving the packer searches every
    // open atlas, not just the most recently created one.
    {
        const std::vector<LightmapFace> faces{
            makeRectFace("big1", 0.0f, 4.0f, 4.0f),
            makeRectFace("big2", 100.0f, 4.0f, 4.0f),
            makeRectFace("small", 200.0f, 1.0f, 1.0f),
        };
        const LightmapPackResult result = packLightmapCharts(faces, 1.0f, 10);
        CHECK_EQ(result.rad.atlases.size(), 2u);
        checkNoOverlapsAndBounds(result.groups, 10);

        const LightmapChart* big1 = findChart(result.rad, "big1");
        const LightmapChart* small = findChart(result.rad, "small");
        CHECK(big1 != nullptr);
        CHECK(small != nullptr);
        if (big1 != nullptr && small != nullptr) {
            CHECK_EQ(small->atlasIndex, big1->atlasIndex);
        }
    }

    // Rotation: a wide-short chart ("wide") fills most of a fresh atlas, leaving a tall
    // narrow strip. A second chart ("tall") is too wide to fit that strip normally but fits
    // once rotated 90 degrees. It should end up sharing atlas 0 with "wide", rotated.
    {
        const std::vector<LightmapFace> faces{
            makeRectFace("wide", 0.0f, 2.0f, 3.0f),
            makeRectFace("tall", 100.0f, 2.0f, 0.5f),
        };
        const LightmapPackResult result = packLightmapCharts(faces, 2.0f, 10);
        CHECK_EQ(result.rad.atlases.size(), 1u);
        checkNoOverlapsAndBounds(result.groups, 10);

        const LightmapChart* wide = findChart(result.rad, "wide");
        const LightmapChart* tall = findChart(result.rad, "tall");
        CHECK(wide != nullptr);
        CHECK(tall != nullptr);
        if (wide != nullptr && tall != nullptr) {
            CHECK_EQ(wide->atlasIndex, 0);
            CHECK_FALSE(wide->rotated);
            CHECK_EQ(wide->atlasX, 0);
            CHECK_EQ(wide->atlasY, 0);
            CHECK_EQ(tall->atlasIndex, 0);
            CHECK(tall->rotated);
            CHECK_EQ(tall->luxelWidth, 6);
            CHECK_EQ(tall->luxelHeight, 3);
            CHECK_EQ(tall->atlasX, 6);
            CHECK_EQ(tall->atlasY, 0);
        }
    }

    // Round trip: a chart packed/serialized with rotated=true must read back rotated=true.
    {
        RadFile rad;
        rad.luxelsPerMeter = 16.0f;
        LightmapAtlasInfo atlas;
        atlas.texturePath = "atlas0";
        atlas.width = 8;
        atlas.height = 8;
        atlas.encoding = LightmapEncoding::Ldr;
        rad.atlases.push_back(atlas);

        LightmapChart chart;
        chart.faceIndex = 0;
        chart.faceId = "faceRotated";
        chart.atlasIndex = 0;
        chart.luxelWidth = 6;
        chart.luxelHeight = 3;
        chart.atlasX = 1;
        chart.atlasY = 2;
        chart.u0 = 0.1f;
        chart.v0 = 0.2f;
        chart.u1 = 0.3f;
        chart.v1 = 0.4f;
        chart.groupUMin = 0.0f;
        chart.groupUMax = 1.0f;
        chart.groupVMin = 0.0f;
        chart.groupVMax = 1.0f;
        chart.rotated = true;
        rad.charts.push_back(chart);

        const auto tempDir = std::filesystem::temp_directory_path() / "sloptest_rad_rotation";
        std::filesystem::create_directories(tempDir);
        const auto radPath = tempDir / "static.rad";
        CHECK(writeRadFile(radPath, rad));
        const auto loaded = readRadFile(radPath);
        CHECK(loaded.has_value());
        if (loaded.has_value()) {
            CHECK_EQ(loaded->charts.size(), 1u);
            if (!loaded->charts.empty()) {
                CHECK(loaded->charts[0].rotated);
            }
        }
        std::filesystem::remove_all(tempDir);
    }

    // Backward compatibility: a hand-built v5 (pre-rotation-field) byte blob must still
    // parse, with rotated defaulting to false.
    {
        std::vector<std::byte> bytes;
        appendU32(bytes, kRadMagic);
        appendU32(bytes, kRadVersionProbes); // version 5, no rotated field yet
        appendF32(bytes, 16.0f); // luxelsPerMeter
        appendU32(bytes, 1); // atlas count
        appendString(bytes, "atlas0");
        appendI32(bytes, 512); // width
        appendI32(bytes, 512); // height
        appendU32(bytes, static_cast<std::uint32_t>(LightmapEncoding::Ldr));
        appendU32(bytes, 1); // chart count
        appendI32(bytes, 0); // faceIndex
        appendString(bytes, "face0");
        appendI32(bytes, 0); // atlasIndex
        appendI32(bytes, 8); // luxelWidth
        appendI32(bytes, 8); // luxelHeight
        appendI32(bytes, 0); // atlasX
        appendI32(bytes, 0); // atlasY
        appendF32(bytes, 0.0f); // u0
        appendF32(bytes, 0.0f); // v0
        appendF32(bytes, 1.0f); // u1
        appendF32(bytes, 1.0f); // v1
        appendF32(bytes, 0.0f); // groupUMin
        appendF32(bytes, 1.0f); // groupUMax
        appendF32(bytes, 0.0f); // groupVMin
        appendF32(bytes, 1.0f); // groupVMax
        appendF32(bytes, 4.0f); // probeGridCoarse.cellSize
        appendU32(bytes, 0); // probeGridCoarse.probes count
        appendF32(bytes, 4.0f); // probeGridFine.cellSize
        appendU32(bytes, 0); // probeGridFine.probes count

        const auto loaded = readRadBytes(bytes);
        CHECK(loaded.has_value());
        if (loaded.has_value()) {
            CHECK_EQ(loaded->charts.size(), 1u);
            if (!loaded->charts.empty()) {
                CHECK_FALSE(loaded->charts[0].rotated);
            }
        }
    }
}

} // namespace slopengine
