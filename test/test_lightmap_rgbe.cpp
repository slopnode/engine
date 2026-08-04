#include "map/lightmap.hpp"
#include "test_assert.hpp"

#include <raylib.h>

#include <cmath>
#include <filesystem>
#include <span>
#include <vector>

namespace slopengine {

namespace {

bool near(float a, float b, float eps = 0.5f) {
    return std::fabs(a - b) <= eps;
}

bool near3(Vector3 a, Vector3 b, float eps = 0.5f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

RadFile makeSampleRad(LightmapEncoding encoding) {
    RadFile rad;
    rad.luxelsPerMeter = 16.0f;
    LightmapAtlasInfo atlas;
    atlas.texturePath = "atlas0";
    atlas.width = 4;
    atlas.height = 4;
    atlas.encoding = encoding;
    rad.atlases.push_back(atlas);
    LightmapChart chart;
    chart.faceIndex = 0;
    chart.faceId = "face0";
    chart.atlasIndex = 0;
    chart.luxelWidth = 2;
    chart.luxelHeight = 2;
    rad.charts.push_back(chart);
    return rad;
}

} // namespace

void runLightmapRgbeTests() {
    const Color black = encodeRgbe(0.0f, 0.0f, 0.0f);
    CHECK_EQ(black.r, 0u);
    CHECK_EQ(black.g, 0u);
    CHECK_EQ(black.b, 0u);
    CHECK_EQ(black.a, 0u);

    const Color encoded = encodeRgbe(200.0f, 50.0f, 50.0f);
    const Vector3 decoded = decodeRgbe(encoded);
    CHECK(near3(decoded, {200.0f, 50.0f, 50.0f}));

    const Color low = encodeRgbe(0.01f, 0.02f, 0.03f);
    const Vector3 lowDecoded = decodeRgbe(low);
    CHECK(near(lowDecoded.x, 0.01f, 0.002f));
    CHECK(near(lowDecoded.y, 0.02f, 0.002f));
    CHECK(near(lowDecoded.z, 0.03f, 0.002f));

    const Color sunRed = encodeRgbe(200.0f, 0.0f, 0.0f);
    const Vector3 sunDecoded = decodeRgbe(sunRed);
    CHECK(near3(sunDecoded, {200.0f, 0.0f, 0.0f}));
    const Color sunDisplay = linearIrradianceToDisplayColor(sunDecoded.x, sunDecoded.y, sunDecoded.z);
    CHECK(sunDisplay.r > sunDisplay.g + 20);
    CHECK(sunDisplay.r > sunDisplay.b + 20);

    const Color emptyPixel{0, 0, 0, 0};
    const Vector3 emptyDecoded = decodeRgbe(emptyPixel);
    CHECK(near3(emptyDecoded, {0.0f, 0.0f, 0.0f}));

    Color corruptedAlpha = encodeRgbe(200.0f, 50.0f, 50.0f);
    corruptedAlpha.a = 255;
    const Vector3 corruptedDecoded = decodeRgbe(corruptedAlpha);
    const Vector3 correctDecoded = decodeRgbe(encodeRgbe(200.0f, 50.0f, 50.0f));
    CHECK(corruptedDecoded.x > correctDecoded.x * 100.0f);

    const auto tempDir = std::filesystem::temp_directory_path() / "sloptest_rad_v3";
    std::filesystem::create_directories(tempDir);

    {
        Image image = GenImageColor(1, 1, sunRed);
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        const auto pngPath = tempDir / "atlas0.png";
        CHECK(ExportImage(image, pngPath.string().c_str()));
        UnloadImage(image);
        Image loaded = LoadImage(pngPath.string().c_str());
        CHECK(loaded.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        const Color reloaded = GetImageColor(loaded, 0, 0);
        CHECK_EQ(reloaded.r, sunRed.r);
        CHECK_EQ(reloaded.g, sunRed.g);
        CHECK_EQ(reloaded.b, sunRed.b);
        CHECK_EQ(reloaded.a, sunRed.a);
        UnloadImage(loaded);
    }

    CHECK(primaryLightmapEncoding(makeSampleRad(LightmapEncoding::Ldr)) == LightmapEncoding::Ldr);
    CHECK(primaryLightmapEncoding(makeSampleRad(LightmapEncoding::Rgbe)) == LightmapEncoding::Rgbe);

    const auto radPath = tempDir / "static.rad";

    const RadFile rgbeRad = makeSampleRad(LightmapEncoding::Rgbe);
    CHECK(writeRadFile(radPath, rgbeRad));
    const auto loadedV3 = readRadFile(radPath);
    CHECK(loadedV3.has_value());
    CHECK_EQ(loadedV3->atlases.size(), 1u);
    CHECK(loadedV3->atlases[0].encoding == LightmapEncoding::Rgbe);

    std::vector<std::byte> legacyBytes{
        std::byte{0x52}, std::byte{0x41}, std::byte{0x44}, std::byte{0x31},
        std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x41},
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x06}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{'a'}, std::byte{'t'}, std::byte{'l'}, std::byte{'a'}, std::byte{'s'}, std::byte{'0'},
        std::byte{0x04}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x04}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    const auto loadedV2 = readRadBytes(legacyBytes);
    CHECK(loadedV2.has_value());
    CHECK(near(loadedV2->luxelsPerMeter, 16.0f));
    CHECK_EQ(loadedV2->atlases.size(), 1u);
    CHECK(loadedV2->atlases[0].encoding == LightmapEncoding::Ldr);
    CHECK_EQ(loadedV2->atlases[0].texturePath, "atlas0");
    CHECK_EQ(loadedV2->atlases[0].width, 4);
    CHECK_EQ(loadedV2->atlases[0].height, 4);
    CHECK(loadedV2->charts.empty());

    std::filesystem::remove_all(tempDir);
}

} // namespace
