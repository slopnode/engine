#pragma once

#include "map/bsp.hpp"
#include "map/lightmap.hpp"
#include "map/quad_bvh.hpp"

#include <raylib.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace slopengine {

struct ProbeCell {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;

    bool operator==(const ProbeCell& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct ProbeCellHash {
    std::size_t operator()(const ProbeCell& cell) const {
        std::size_t h = std::hash<std::int32_t>{}(cell.x);
        h ^= std::hash<std::int32_t>{}(cell.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<std::int32_t>{}(cell.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

struct ProbeSH {
    Vector3 coeff[4]{};
};

struct ProbeGrid {
    float cellSize = 4.0f;
    std::unordered_map<ProbeCell, ProbeSH, ProbeCellHash> probesByCell;
};

/** Runtime baked lighting: rad charts, atlases, and surface BVH for probes. */
struct MapLighting {
    bool available = false;
    RadFile rad{};
    std::vector<LightmapFace> probeFaces{};
    std::vector<char> faceTransparentSkip{};
    QuadBvh surfaceBvh{};
    std::unordered_map<std::string, std::size_t> chartIndexByFaceId;
    std::vector<Image> atlasImages;
    Color ambient = {8, 8, 10, 255};
    ProbeGrid probeGridFine;
    ProbeGrid probeGridCoarse;

    MapLighting() = default;
    MapLighting(const MapLighting&) = delete;
    MapLighting& operator=(const MapLighting&) = delete;

    MapLighting(MapLighting&& other) noexcept
        : available(other.available)
        , rad(std::move(other.rad))
        , probeFaces(std::move(other.probeFaces))
        , faceTransparentSkip(std::move(other.faceTransparentSkip))
        , surfaceBvh(std::move(other.surfaceBvh))
        , chartIndexByFaceId(std::move(other.chartIndexByFaceId))
        , atlasImages(std::move(other.atlasImages))
        , ambient(other.ambient)
        , probeGridFine(std::move(other.probeGridFine))
        , probeGridCoarse(std::move(other.probeGridCoarse)) {
        other.available = false;
        other.atlasImages.clear();
    }

    MapLighting& operator=(MapLighting&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        unload();
        available = other.available;
        rad = std::move(other.rad);
        probeFaces = std::move(other.probeFaces);
        faceTransparentSkip = std::move(other.faceTransparentSkip);
        surfaceBvh = std::move(other.surfaceBvh);
        chartIndexByFaceId = std::move(other.chartIndexByFaceId);
        atlasImages = std::move(other.atlasImages);
        ambient = other.ambient;
        probeGridFine = std::move(other.probeGridFine);
        probeGridCoarse = std::move(other.probeGridCoarse);
        other.available = false;
        other.atlasImages.clear();
        return *this;
    }

    ~MapLighting() {
        unload();
    }

    void unload() {
        for (Image& image : atlasImages) {
            if (image.data != nullptr) {
                UnloadImage(image);
                image = {};
            }
        }
        atlasImages.clear();
        probeFaces.clear();
        faceTransparentSkip.clear();
        probeGridFine.probesByCell.clear();
        probeGridCoarse.probesByCell.clear();
        available = false;
    }
};

/** Builds MapLighting from BSP surfaces, rad data, and atlases. */
MapLighting buildMapLighting(
    const BspTree& bsp,
    RadFile rad,
    std::vector<Image> atlasImages,
    Color ambient);

/** Samples baked light along a ray (used for sprites / FP rad tint). */
std::optional<Color> sampleMapLight(
    const MapLighting& lighting,
    Vector3 origin,
    Vector3 direction,
    float maxDistance);

/** Samples the volumetric probe grid at @p point, evaluated toward @p direction. */
std::optional<Color> sampleLightProbe(
    const MapLighting& lighting,
    Vector3 point,
    Vector3 direction);

}
