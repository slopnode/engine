#include "assets/asset_store.hpp"
#include "core/log.hpp"
#include "game/app_config.hpp"
#include "map/bsp_analyze.hpp"
#include "map/bsp_io.hpp"
#include "map/map_script.hpp"
#include "map/pvs.hpp"
#include "map/pvs_io.hpp"
#include "map/pvs_audit.hpp"
#include "map/map_areas.hpp"

#include <raylib.h>
#include <s7.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char* argv[]) {
    using namespace slopengine;

    bool fast = false;
    bool badMode = false;
    int auditPairs = 0;
    bool auditViewSet = false;
    Vector3 auditViewPos{};
    std::vector<char*> args;
    for (int i = 0; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--fast") {
            fast = true;
        } else if (arg == "--audit-view" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%f,%f,%f", &auditViewPos.x, &auditViewPos.y, &auditViewPos.z) != 3) {
                badMode = true;
            }
            auditViewSet = true;
        } else if (arg == "--audit" && i + 1 < argc) {
            auditPairs = std::atoi(argv[++i]);
            if (auditPairs <= 0) {
                badMode = true;
            }
        } else if (arg == "--mode" && i + 1 < argc) {
            const std::string_view mode(argv[++i]);
            if (mode == "fast") {
                fast = true;
            } else if (mode == "full") {
                fast = false;
            } else {
                badMode = true;
            }
        } else {
            args.push_back(argv[i]);
        }
    }
    auto config = AppConfig::parse(static_cast<int>(args.size()), args.data());
    if (badMode || !config || !config->map) {
        std::cerr << "Usage: slopvis --game <path> [--mod <path>]... --map <name> [--mode full|fast] [--audit <pairs>]\n";
        return 1;
    }

    Log::init(config->verbose ? LogLevel::Debug : LogLevel::Info);
    Log::addDefaultConsoleSink();
    AssetStore assets(*config);
    s7_scheme* scheme = s7_init();
    if (scheme == nullptr) {
        std::cerr << "slopvis: failed to init scheme\n";
        return 1;
    }
    loadPackageMapHandlers(scheme, assets);

    const std::string virtualPath = *config->map + "/compiled/bsp";
    auto bspPath = assets.resolvePath(AssetKind::MapBsp, virtualPath);
    if (!bspPath) {
        std::cerr << "slopvis: missing maps/" << virtualPath << " (run slopbsp first)\n";
        s7_quit(scheme);
        return 1;
    }

    auto tree = readBspFile(*bspPath);
    if (!tree) {
        std::cerr << "slopvis: failed to read " << *bspPath << "\n";
        s7_quit(scheme);
        return 1;
    }

    if (auditViewSet) {
        const auto existingVisPath = bspPath->parent_path() / "vis";
        auto existing = readPvsFile(existingVisPath);
        if (!existing) {
            std::cerr << "slopvis: missing " << existingVisPath << " (run slopvis first)\n";
            s7_quit(scheme);
            return 1;
        }
        const ViewAuditResult view = auditView(*tree, *existing, auditViewPos);
        TraceLog(
            LOG_INFO,
            "slopvis: view audit at (%.1f %.1f %.1f) leaf=%d faces=%d visibleByMarch=%d culledWrongly=%d noLeaves=%d",
            auditViewPos.x, auditViewPos.y, auditViewPos.z,
            view.cameraLeaf,
            view.facesTested,
            view.facesVisibleByMarch,
            view.facesCulledWrongly,
            view.facesWithoutLeaves);
        for (const std::string& example : view.examples) {
            TraceLog(LOG_WARNING, "slopvis: view audit %s", example.c_str());
        }
        s7_quit(scheme);
        return 0;
    }

    if (auditPairs > 0) {
        const auto existingVisPath = bspPath->parent_path() / "vis";
        auto existing = readPvsFile(existingVisPath);
        if (!existing) {
            std::cerr << "slopvis: missing " << existingVisPath << " (run slopvis first)\n";
            s7_quit(scheme);
            return 1;
        }
        auto auditBrushes = loadMapBrushes(scheme, assets, *config->map);
        const MapHullAnalysis auditAnalysis = auditBrushes ? analyzeMapHull(*tree, *auditBrushes) : MapHullAnalysis{};
        const MapAreas areas = buildMapAreas(*tree);
        const PvsAuditResult audit = auditPvs(
            *tree, *existing, auditPairs, 12345u, auditBrushes ? &auditAnalysis.exteriorEmpty : nullptr, &areas);
        TraceLog(
            LOG_INFO,
            "slopvis: audit leaves=%d areas=%d doorPortals=%d portalPairs=%d blindPortalPairs=%d",
            static_cast<int>(tree->leaves.size()),
            areas.areaCount,
            static_cast<int>(areas.portals.size()),
            audit.portalPairs,
            audit.portalPairsBlind);
        TraceLog(
            LOG_INFO,
            "slopvis: audit pairs=%d openPairs=%d falseInvisible=%d glassPairs=%d glassInvisible=%d",
            audit.pairsTested,
            audit.openPairs,
            audit.falseInvisible,
            audit.openThroughGlass,
            audit.falseInvisibleThroughGlass);
        TraceLog(
            LOG_INFO,
            "slopvis: audit areaPairs=%d areaMismatch=%d interiorBlindLeaves=%d exteriorAbovePlayable=%d",
            audit.areaPairs,
            audit.areaMismatch,
            audit.interiorBlindLeaves,
            audit.exteriorAbovePlayable);
        for (const std::string& example : audit.blindExamples) {
            TraceLog(LOG_WARNING, "slopvis: audit %s", example.c_str());
        }
        TraceLog(LOG_INFO, "slopvis: audit falseInvisibleWithMissingPortal=%d", audit.missingPortals);
        for (const std::string& example : audit.missingPortalExamples) {
            TraceLog(LOG_WARNING, "slopvis: audit %s", example.c_str());
        }
        {
            std::vector<int> areaLeaves(static_cast<std::size_t>(areas.areaCount), 0);
            std::vector<Vector3> areaMins(static_cast<std::size_t>(areas.areaCount), Vector3{1e9f, 1e9f, 1e9f});
            std::vector<Vector3> areaMaxs(static_cast<std::size_t>(areas.areaCount), Vector3{-1e9f, -1e9f, -1e9f});
            for (std::size_t i = 0; i < areas.leafArea.size(); ++i) {
                const std::int32_t area = areas.leafArea[i];
                if (area < 0) {
                    continue;
                }
                ++areaLeaves[static_cast<std::size_t>(area)];
                const BspLeaf& leaf = tree->leaves[i];
                Vector3& mn = areaMins[static_cast<std::size_t>(area)];
                Vector3& mx = areaMaxs[static_cast<std::size_t>(area)];
                mn = {std::min(mn.x, leaf.mins.x), std::min(mn.y, leaf.mins.y), std::min(mn.z, leaf.mins.z)};
                mx = {std::max(mx.x, leaf.maxs.x), std::max(mx.y, leaf.maxs.y), std::max(mx.z, leaf.maxs.z)};
            }
            for (int area = 0; area < areas.areaCount; ++area) {
                if (areaLeaves[static_cast<std::size_t>(area)] < 20) {
                    continue;
                }
                TraceLog(
                    LOG_INFO,
                    "slopvis: area %d leaves=%d bounds (%.0f %.0f %.0f)-(%.0f %.0f %.0f)",
                    area,
                    areaLeaves[static_cast<std::size_t>(area)],
                    areaMins[static_cast<std::size_t>(area)].x, areaMins[static_cast<std::size_t>(area)].y, areaMins[static_cast<std::size_t>(area)].z,
                    areaMaxs[static_cast<std::size_t>(area)].x, areaMaxs[static_cast<std::size_t>(area)].y, areaMaxs[static_cast<std::size_t>(area)].z);
            }
            for (const MapAreaPortal& portal : areas.portals) {
                TraceLog(
                    LOG_INFO,
                    "slopvis: door portal '%s' joins area %d and area %d",
                    portal.doorBrushId.c_str(),
                    portal.areaA,
                    portal.areaB);
            }
        }
        for (const std::string& example : audit.examples) {
            TraceLog(LOG_WARNING, "slopvis: audit %s", example.c_str());
        }
        for (const std::string& example : audit.areaExamples) {
            TraceLog(LOG_WARNING, "slopvis: audit %s", example.c_str());
        }
        s7_quit(scheme);
        return 0;
    }

    auto brushes = loadMapBrushes(scheme, assets, *config->map);
    if (!brushes) {
        std::cerr << "slopvis: failed to load map brushes for '" << *config->map << "'\n";
        s7_quit(scheme);
        return 1;
    }

    const MapHullAnalysis analysis = analyzeMapHull(*tree, *brushes);
    if (!analysis.sealed) {
        TraceLog(LOG_ERROR, "slopvis: LEAK — refuse to build PVS for an unsealed hull");
        for (const std::string& step : analysis.leakPathFaceIds) {
            TraceLog(LOG_ERROR, "slopvis: leak path %s", step.c_str());
        }
        s7_quit(scheme);
        return 1;
    }

    // analysis.sealed only catches a TOTAL leak (every open leaf flooding
    // exterior). A partial leak — real interior content reachable from the
    // "exterior" flood through an unsealed opening — leaves analysis.sealed
    // true while analysis.exteriorEmpty still wrongly excludes every leaf on
    // the leaked side from ever being a PVS source (isPvsSourceLeaf in
    // pvs_build.cpp), which is exactly what breaks sprite/actor visibility
    // there. detailOutsideWarnings is already the leak signal for that case;
    // if it's non-empty, the exterior classification isn't trustworthy
    // enough to use for exclusion, so fall back to treating every open leaf
    // as a valid source rather than silently blinding a chunk of the map.
    const std::vector<std::uint8_t>* exteriorForPvs = &analysis.exteriorEmpty;
    if (!analysis.detailOutsideWarnings.empty()) {
        TraceLog(
            LOG_ERROR,
            "slopvis: %d brush(es) needing interior placement resolve into leaves "
            "flagged exterior void — this map has an unsealed opening leaking real "
            "interior space to the outside. Building PVS without exterior exclusion "
            "(no culling optimization) so visibility stays correct; seal the opening "
            "and re-run slopvis to restore normal culling.",
            static_cast<int>(analysis.detailOutsideWarnings.size()));
        for (const std::string& warning : analysis.detailOutsideWarnings) {
            TraceLog(LOG_ERROR, "slopvis:   %s", warning.c_str());
        }
        exteriorForPvs = nullptr;
    }

    const PvsFile pvs = buildPvs(*tree, exteriorForPvs, fast);
    const auto visPath = bspPath->parent_path() / "vis";
    if (!writePvsFile(visPath, pvs)) {
        std::cerr << "slopvis: failed to write " << visPath << "\n";
        s7_quit(scheme);
        return 1;
    }

    int openLeaves = 0;
    long long visiblePairs = 0;
    for (int a = 0; a < pvs.leafCount; ++a) {
        if (!leafIsOpen(tree->leaves[static_cast<std::size_t>(a)].contents)) {
            continue;
        }
        ++openLeaves;
        visiblePairs += pvsVisibleCount(pvs, a);
    }

    TraceLog(
        LOG_INFO,
        "slopvis: wrote %s (leaves=%d openLeaves=%d columns=%d uniqueRows=%d visiblePairs=%lld)",
        visPath.string().c_str(),
        pvs.leafCount,
        openLeaves,
        pvs.columnCount,
        pvs.rowCount(),
        visiblePairs);

    s7_quit(scheme);
    return 0;
}
