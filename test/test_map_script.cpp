#include "test_assert.hpp"

#include "assets/asset_store.hpp"
#include "game/app_config.hpp"
#include "map/csg_write.hpp"
#include "map/map_script.hpp"

#include <s7.h>

#include <cstdio>
#include <fstream>
#include <random>

namespace slopengine {

namespace {

std::filesystem::path makeTempPackageRoot() {
    std::random_device rd;
    const auto suffix = std::to_string(rd());
    const auto root =
        std::filesystem::temp_directory_path() / ("sloptest-map-script-" + suffix);
    std::filesystem::create_directories(root);
    return root;
}

void writeFile(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
}

} // namespace

void runMapScriptTests() {
    const std::filesystem::path root = makeTempPackageRoot();

    writeFile(
        root / "package.meta",
        "(package\n"
        "  (id \"sloptest-map-script\")\n"
        "  (name \"Map Script Test Package\")\n"
        "  (version \"0.1.0\")\n"
        "  (depends))\n");

    writeFile(
        root / "maps" / "testmap" / "map.meta",
        "(map\n"
        "  (id \"testmap\")\n"
        "  (name \"testmap\")\n"
        "  (author \"\")\n"
        "  (description \"\")\n"
        "  (depends))\n");

    writeFile(
        root / "maps" / "testmap" / "static.map",
        "(brush-box\n"
        "  (id \"a-floor\")\n"
        "  (mins -4 -0.25 -4)\n"
        "  (maxs 4 0 4)\n"
        "  (material \"surfaces/floor\")\n"
        ")\n");

    AppConfig config{};
    config.base_game = root;
    AssetStore assets(config);

    s7_scheme* scheme = s7_init();
    CHECK_TRUE(scheme != nullptr);
    loadPackageMapHandlers(scheme, assets);

    CHECK_TRUE(assets.hasMapSource("testmap/static"));

    auto doc = loadMapSourceDocument(scheme, assets, "testmap");
    CHECK_TRUE(doc.has_value());
    CHECK_EQ(doc->brushes.size(), 1u);
    CHECK_TRUE(doc->instances.empty());
    if (!doc->brushes.empty()) {
        CHECK_EQ(doc->brushes[0].id, std::string("a-floor"));
    }

    auto brushes = loadMapBrushes(scheme, assets, "testmap");
    CHECK_TRUE(brushes.has_value());
    CHECK_EQ(brushes->size(), 1u);
    if (!brushes->empty()) {
        CHECK_EQ((*brushes)[0].id, std::string("a-floor"));
    }

    CHECK_FALSE(assets.hasMapSource("does-not-exist/static"));
    auto missing = loadMapSourceDocument(scheme, assets, "does-not-exist");
    CHECK_FALSE(missing.has_value());

    // slopcsg pass-through invariant: writing loadMapBrushes' output with writeMapBrushes and
    // reading it back via loadCarvedMapBrushes must reproduce the same brushes exactly.
    const std::filesystem::path carvedPath = root / "maps" / "testmap" / "static.csg";
    CHECK_TRUE(writeMapBrushes(carvedPath, *brushes));

    CHECK_TRUE(assets.hasMapCarved("testmap/static"));
    auto carved = loadCarvedMapBrushes(scheme, assets, "testmap");
    CHECK_TRUE(carved.has_value());
    CHECK_EQ(carved->size(), brushes->size());
    if (!carved->empty() && !brushes->empty()) {
        const Brush& original = (*brushes)[0];
        const Brush& roundTripped = (*carved)[0];
        CHECK_EQ(roundTripped.id, original.id);
        CHECK_EQ(roundTripped.faces.size(), original.faces.size());
        CHECK_EQ(roundTripped.mins.x, original.mins.x);
        CHECK_EQ(roundTripped.mins.y, original.mins.y);
        CHECK_EQ(roundTripped.mins.z, original.mins.z);
        CHECK_EQ(roundTripped.maxs.x, original.maxs.x);
        CHECK_EQ(roundTripped.maxs.y, original.maxs.y);
        CHECK_EQ(roundTripped.maxs.z, original.maxs.z);
    }

    CHECK_FALSE(assets.hasMapCarved("does-not-exist/static"));
    auto missingCarved = loadCarvedMapBrushes(scheme, assets, "does-not-exist");
    CHECK_FALSE(missingCarved.has_value());

    s7_quit(scheme);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

}
