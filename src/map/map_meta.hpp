#pragma once

#include <raylib.h>

#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

/** Optional directional sun from map.meta (bake only). */
struct MapSun {
    bool enabled = false;
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    Vector3 angles{}; /**< Pitch yaw roll; yaw-only maps to angles.y. */
};

/** Fields from maps/<name>/map.meta. */
struct MapMeta {
    std::string id;
    std::string name;
    std::string author;
    std::string description;
    std::string package; /**< Owning package id (filled from directory at load). */
    std::vector<std::string> depends; /**< Other package ids that must be mounted. */
    Vector3 ambient{0.02f, 0.02f, 0.025f}; /**< Soft fill used when baking radiosity. */
    MapSun sun;
};

/** Parses map.meta text into @p out. */
bool parseMapMeta(std::string_view source, MapMeta& out);

}
