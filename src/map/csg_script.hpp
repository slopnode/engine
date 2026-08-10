#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"
#include "map/bsp.hpp"
#include "map/lightmap.hpp"
#include "map/map_meta.hpp"
#include "map/prefab.hpp"
#include "map/fac.hpp"
#include "map/pvs.hpp"
#include "render/material_anim_types.hpp"

#include <raylib.h>

#include <optional>
#include <string>
#include <string_view>
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
    FacFile fac{};
    FacFile doorFac{}; /**< Movable-brush faces snapshotted before erasure from @c fac. */
    PvsFile pvs{};
    RadFile rad{};
    MapMeta meta{};
    bool hasLightmaps = false;
    Shader lightmapShader{};
    std::vector<Texture2D> lightmapAtlases;
    std::vector<Image> lightmapAtlasImages;
    int useLightmapLoc = -1;
    std::vector<int> transparentMeshIndices;
    std::vector<int> skyMeshIndices;
    std::vector<int> detailMeshIndices;
    Shader skyShader{};
    MaterialAnimTargets materialAnimTargets{};
};

/** Parsed static.csg: local brushes plus prefab instances. */
struct MapCsgDocument {
    std::vector<Brush> brushes;
    std::vector<PrefabInstance> instances;
};

/** Load base+mod data/map-handlers.s7 into the registry (for CSG arg clauses). */
void loadPackageMapHandlers(s7_scheme* scheme, AssetStore& assets);

/** Load engine+base+mod data/things.s7 into the thing-def registry. */
void loadPackageThings(s7_scheme* scheme, AssetStore& assets);

/** Loads maps/<name>/map.meta. */
std::optional<MapMeta> loadMapMeta(AssetStore& assets, std::string_view mapName);

/** Loads and evaluates maps/<name>/static.csg without expanding prefabs. */
std::optional<MapCsgDocument> loadMapCsgDocument(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

/** Loads CSG and expands prefabs into a flat brush list. */
std::optional<std::vector<Brush>> loadMapBrushes(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

/** Loads prefabs/<path>.csg brushes (no instance transform). */
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

}
