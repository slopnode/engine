#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/area_gate_runtime.hpp"
#include "map/bsp_analyze.hpp"
#include "map/map_areas.hpp"
#include "map/pvs.hpp"

#include <flecs.h>

#include <vector>

namespace slopengine {

void runMapAreasTests() {
    {
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoor();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        const PvsFile pvs = buildPvs(tree, &analysis.exteriorEmpty);
        const MapAreas areas = buildMapAreas(tree);

        const std::int32_t north = pointLeaf(tree, {0.0f, 1.0f, -3.0f});
        const std::int32_t south = pointLeaf(tree, {0.0f, 1.0f, 3.0f});
        const std::int32_t doorLeaf = pointLeaf(tree, {0.0f, 1.0f, 0.0f});
        CHECK(north >= 0);
        CHECK(south >= 0);
        CHECK(doorLeaf >= 0);
        CHECK((tree.leaves[static_cast<std::size_t>(doorLeaf)].contents & BspContents::Door) != 0);

        const std::int32_t northArea = areas.leafArea[static_cast<std::size_t>(north)];
        const std::int32_t southArea = areas.leafArea[static_cast<std::size_t>(south)];
        CHECK(northArea >= 0);
        CHECK(southArea >= 0);
        CHECK(northArea != southArea);
        CHECK(areas.areaCount >= 2);
        CHECK_FALSE(areas.portals.empty());

        bool doorTagged = false;
        for (const MapAreaPortal& portal : areas.portals) {
            if (portal.doorBrushId == "door-1") {
                doorTagged = true;
            }
        }
        CHECK(doorTagged);

        const std::vector<std::uint8_t> allOpen(areas.portals.size(), 1);
        const std::vector<std::uint8_t> allClosed(areas.portals.size(), 0);
        const std::vector<std::uint8_t> openReach = floodOpenAreas(areas, northArea, allOpen);
        const std::vector<std::uint8_t> closedReach = floodOpenAreas(areas, northArea, allClosed);
        CHECK(openReach[static_cast<std::size_t>(southArea)] != 0);
        CHECK(closedReach[static_cast<std::size_t>(northArea)] != 0);
        CHECK(closedReach[static_cast<std::size_t>(southArea)] == 0);

        const AreaGate openGate{&areas, &openReach};
        const AreaGate closedGate{&areas, &closedReach};
        const Vector3 northEye{0.0f, 1.0f, -3.0f};
        const Vector3 southEye{0.0f, 1.0f, 3.0f};
        CHECK(pvsVisiblePoints(tree, pvs, northEye, southEye, nullptr));
        CHECK(pvsVisiblePoints(tree, pvs, northEye, southEye, &openGate));
        CHECK_FALSE(pvsVisiblePoints(tree, pvs, northEye, southEye, &closedGate));
        CHECK(pvsVisiblePoints(tree, pvs, northEye, {0.5f, 1.0f, -2.0f}, &closedGate));
        CHECK(areaGateAllows(closedGate, tree, doorLeaf));

        const BoundingBox southBox{{-0.5f, 0.0f, 2.5f}, {0.5f, 1.0f, 3.5f}};
        CHECK(pvsVisibleBox(tree, pvs, northEye, southBox, &openGate));
        CHECK_FALSE(pvsVisibleBox(tree, pvs, northEye, southBox, &closedGate));
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoorway();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapAreas areas = buildMapAreas(tree);
        const std::int32_t north = pointLeaf(tree, {0.0f, 1.0f, -3.0f});
        const std::int32_t south = pointLeaf(tree, {0.0f, 1.0f, 3.0f});
        CHECK(areas.leafArea[static_cast<std::size_t>(north)] ==
              areas.leafArea[static_cast<std::size_t>(south)]);
        CHECK(areas.portals.empty());
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoor();
        BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        PvsFile pvs = buildPvs(tree, &analysis.exteriorEmpty);
        MapAreas areas = buildMapAreas(tree);
        const Vector3 northEye{0.0f, 1.0f, -3.0f};

        flecs::world world;
        world.set<MapBsp>(MapBsp{std::move(tree)});
        world.set<MapPvs>(MapPvs{std::move(pvs)});
        world.set<MapAreas>(std::move(areas));

        bool ranWithoutCache = false;
        bool gatedWithoutCache = true;
        bool ranWithCache = false;
        bool gatedWithCache = false;
        world.system().kind(flecs::OnUpdate).run([&](flecs::iter& it) {
            while (it.next()) {
                const AreaGate gate = areaGateFromPoint(world, northEye);
                if (!world.has<MapAreaGateCache>()) {
                    ranWithoutCache = true;
                    gatedWithoutCache = gate.areas != nullptr;
                } else {
                    ranWithCache = true;
                    gatedWithCache = gate.areas != nullptr && gate.open != nullptr;
                }
            }
        });
        world.progress();
        CHECK(ranWithoutCache);
        CHECK_FALSE(gatedWithoutCache);

        world.set<MapAreaGateCache>(MapAreaGateCache{});
        world.progress();
        CHECK(ranWithCache);
        CHECK(gatedWithCache);
        const MapAreaGateCache& cache = world.get<MapAreaGateCache>();
        CHECK(cache.frame >= 0);
        CHECK(cache.portalOpen.size() == world.get<MapAreas>().portals.size());
    }
}

}
