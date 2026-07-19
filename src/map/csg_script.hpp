#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"
#include "map/bsp.hpp"
#include "map/lightmap.hpp"
#include "map/map_meta.hpp"
#include "map/prefab.hpp"

#include <raylib.h>

#include <optional>
#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

struct LoadedMap {
    Model model{};
    std::vector<Brush> brushes;
    BspTree bsp{};
    RadFile rad{};
    MapMeta meta{};
    bool hasLightmaps = false;
    Shader lightmapShader{};
    std::vector<Texture2D> lightmapAtlases;
    std::vector<Image> lightmapAtlasImages;
    int useLightmapLoc = -1;
};

struct MapCsgDocument {
    std::vector<Brush> brushes;
    std::vector<PrefabInstance> instances;
};

std::optional<MapMeta> loadMapMeta(AssetStore& assets, std::string_view mapName);

std::optional<MapCsgDocument> loadMapCsgDocument(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

std::optional<std::vector<Brush>> loadMapBrushes(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

std::optional<std::vector<Brush>> loadPrefabBrushes(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view prefabPath);

std::optional<std::vector<Brush>> expandPrefabInstance(
    s7_scheme* scheme,
    AssetStore& assets,
    const PrefabInstance& instance);

std::vector<Brush> expandPrefabInstances(
    s7_scheme* scheme,
    AssetStore& assets,
    const std::vector<PrefabInstance>& instances);

std::optional<LoadedMap> loadAndCompileMap(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

}
