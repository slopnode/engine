#include "test_assert.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "game/app_config.hpp"
#include "map/thing_def_registry.hpp"
#include "map/things_spawn.hpp"
#include "render/components.hpp"

#include <s7.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

namespace {

Thing typedMarker(const ThingDef& def, const char* id, float x) {
    Thing placement{};
    applyThingDef(def, placement);
    placement.id = id;
    placement.at = {x, 0.0f, 2.0f};
    placement.haveAt = true;
    return placement;
}

std::vector<std::string> placedOfType(const flecs::world& world, std::string_view type) {
    std::vector<std::string> out;
    for (const Thing& thing : world.get<MapThingPlacements>().things) {
        if (type.empty() || thing.type == type) {
            out.push_back(thing.id);
        }
    }
    return out;
}

}

void runThingRespawnTests() {
    std::random_device rd;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("sloptest-respawn-" + std::to_string(rd()));
    std::filesystem::create_directories(root);
    std::ofstream(root / "package.meta") << "(package\n"
                                            "  (id \"sloptest-respawn\")\n"
                                            "  (name \"Respawn Test Package\")\n"
                                            "  (version \"0.1.0\")\n"
                                            "  (depends))\n";
    AppConfig config{};
    config.game = root;
    AssetStore assets(config);

    thingDefRegistry().clear();
    ThingDef start{};
    start.id = "dm-start";
    start.kind = ThingKind::Marker;
    CHECK(thingDefRegistry().registerDef(start));
    const ThingDef* def = thingDefRegistry().find("dm-start");
    CHECK(def != nullptr);
    if (def == nullptr) {
        return;
    }

    ThingDocument doc{};
    doc.things.push_back(typedMarker(*def, "dm-a", 1.0f));
    Thing spot{};
    spot.kind = ThingKind::Marker;
    spot.id = "spot";
    spot.haveAt = true;
    doc.things.push_back(spot);
    doc.things.push_back(typedMarker(*def, "dm-b", 3.0f));
    Thing plat{};
    plat.kind = ThingKind::Mover;
    plat.id = "plat";
    plat.brush = "lift";
    doc.things.push_back(plat);

    flecs::world world;
    world.set<AssetServices>({&assets});
    s7_scheme* sc = s7_init();
    spawnThings(world, assets, sc, doc);

    const flecs::entity first = world.lookup("dm-a");
    CHECK(first.is_valid() && first.has<ThingTypeRef>() && first.get<ThingTypeRef>().type == "dm-start");
    CHECK((placedOfType(world, "dm-start") == std::vector<std::string>{"dm-a", "dm-b"}));
    CHECK((placedOfType(world, "") == std::vector<std::string>{"dm-a", "spot", "dm-b", "plat"}));
    CHECK_FALSE(respawnMapThing(world, assets, sc, "dm-a"));

    first.destruct();
    CHECK_FALSE(world.lookup("dm-a").is_valid());
    CHECK(respawnMapThing(world, assets, sc, "dm-a"));
    const flecs::entity again = world.lookup("dm-a");
    CHECK(again.is_valid() && again.get<ThingTypeRef>().type == "dm-start");
    CHECK(again.has<LocalTransformation>() && again.get<LocalTransformation>().position.x == 1.0f);
    CHECK((placedOfType(world, "dm-start") == std::vector<std::string>{"dm-a", "dm-b"}));

    CHECK_FALSE(respawnMapThing(world, assets, sc, "plat"));
    CHECK_FALSE(respawnMapThing(world, assets, sc, "nothing"));

    ThingDocument next{};
    next.things.push_back(typedMarker(*def, "dm-c", 5.0f));
    spawnThings(world, assets, sc, next);
    CHECK((placedOfType(world, "dm-start") == std::vector<std::string>{"dm-c"}));
    world.lookup("dm-b").destruct();
    CHECK_FALSE(respawnMapThing(world, assets, sc, "dm-b"));

    s7_quit(sc);
    thingDefRegistry().clear();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

}
