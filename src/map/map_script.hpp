#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"
#include "map/bsp.hpp"
#include "map/lightmap.hpp"
#include "map/map_meta.hpp"
#include "map/nav_graph.hpp"
#include "map/prefab.hpp"
#include "map/pvs.hpp"
#include "render/material_anim_types.hpp"

#include <raylib.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct s7_scheme;

namespace slopengine {

/** Runtime map ready for draw, collision, and lighting. */
struct LoadedMap {
    Model model{};
    std::vector<Brush> brushes;
    std::unordered_set<std::string> moverBrushIds;
    BspTree bsp{};
    PvsFile pvs{};
    RadFile rad{};
    MapNavigation nav{};
    /** Every other baked nav profile besides @c nav (which holds the smallest-radius one,
     *  or the runtime BSP-leaf fallback); see MapNavProfiles. Keyed by profile name. */
    std::unordered_map<std::string, MapNavigation> navProfiles;
    MapMeta meta{};
    bool hasLightmaps = false;
    Shader lightmapShader{};
    std::vector<Texture2D> lightmapAtlases;
    std::vector<Image> lightmapAtlasImages;
    int useLightmapLoc = -1;
    int emissionPowerLoc = -1;
    std::vector<int> transparentMeshIndices;
    std::vector<int> skyMeshIndices;
    std::vector<int> detailMeshIndices;
    std::vector<int> twoSidedMeshIndices;
    Shader skyShader{};
    MaterialAnimTargets materialAnimTargets{};
};

/** Parsed brushes.map: local brushes plus prefab instances. */
struct MapSourceDocument {
    std::vector<Brush> brushes;
    std::vector<PrefabInstance> instances;
};

/** Intermediate state carried across loadMapStage* calls. */
struct MapLoadWork {
    std::string mapName;
    /** maps/<mapName>/brushes -- the hand-authored source, resolves to brushes.map. */
    std::string virtualPath;
    /** maps/<mapName>/compiled -- base directory for all derived build output
     *  (bsp/vis/nav/rad); each stage appends its own filename under this. */
    std::string compiledPath;
    MapMeta meta{};
    std::vector<Brush> brushes;
    std::unordered_set<std::string> moverBrushIds;
    BspTree bsp{};
    PvsFile pvs{};
    RadFile rad{};
    MapNavigation nav{};
    std::unordered_map<std::string, MapNavigation> navProfiles;
};

/** Load base+mod data/map-handlers.s7 into the registry (for CSG arg clauses). */
void loadPackageMapHandlers(s7_scheme* scheme, AssetStore& assets);

/** Load engine+base+mod data/things.s7 into the thing-def registry. */
void loadPackageThings(s7_scheme* scheme, AssetStore& assets);

/** Load engine+base+mod data/nav-profiles.s7 into the nav-profile registry. */
void loadPackageNavProfiles(s7_scheme* scheme, AssetStore& assets);

/** Loads maps/<name>/map.meta. */
std::optional<MapMeta> loadMapMeta(AssetStore& assets, std::string_view mapName);

/** Loads and evaluates maps/<name>/brushes.map without expanding prefabs. */
std::optional<MapSourceDocument> loadMapSourceDocument(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

/** Loads the map source and expands prefabs into a flat brush list. */
std::optional<std::vector<Brush>> loadMapBrushes(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

/** Loads maps/<name>/compiled/csg (slopcsg's carved output) as a flat brush list. No prefab
 *  expansion -- slopcsg already flattens prefab instances before writing this file. */
std::optional<std::vector<Brush>> loadCarvedMapBrushes(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

/** Loads prefabs/<path>.map brushes (no instance transform). */
std::optional<std::vector<Brush>> loadPrefabBrushes(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view prefabPath);

/** Expands one prefab instance into world-space brushes. */
std::optional<std::vector<Brush>> expandPrefabInstance(
    s7_scheme* scheme,
    AssetStore& assets,
    const PrefabInstance& instance);

/** Expands all prefab instances into world-space brushes. */
std::vector<Brush> expandPrefabInstances(
    s7_scheme* scheme,
    AssetStore& assets,
    const std::vector<PrefabInstance>& instances);

/** Loads meta, brushes, BSP, optional rad, and builds the draw mesh. */
std::optional<LoadedMap> loadAndCompileMap(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

/** Stage 1: meta, brushes, things, BSP. */
bool loadMapStageBsp(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName,
    MapLoadWork& work);

/** Stage 2: PVS, validated against the loaded BSP. */
bool loadMapStageVis(AssetStore& assets, MapLoadWork& work);

/** Stage 3: optional radiosity bake data. */
bool loadMapStageRad(AssetStore& assets, MapLoadWork& work);

/** Optional baked navmesh graph (see tools/slopnav), independent of Vis/Rad -- only
 *  depends on the BSP loaded in stage 1. Not yet consumed by assembleMapScene(), which
 *  still builds its own MapNavigation from the BSP leaf/portal graph at runtime; this
 *  just makes the baked data available on MapLoadWork/LoadedMap ahead of that cutover.
 *  A missing bake is not an error (most maps haven't been re-baked yet). */
bool loadMapStageNav(AssetStore& assets, MapLoadWork& work);

/** Stage 4: lightmap/sky shaders, atlas textures, CSG compile, model build. */
std::optional<LoadedMap> loadMapStageTextures(AssetStore& assets, MapLoadWork&& work);

}
