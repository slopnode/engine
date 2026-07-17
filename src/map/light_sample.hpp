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

struct MapLighting {
    bool available = false;
    RadFile rad{};
    QuadBvh surfaceBvh{};
    std::unordered_map<std::string, std::size_t> chartIndexByFaceId;
    std::vector<Image> atlasImages;
    Color ambient = {8, 8, 10, 255};

    MapLighting() = default;
    MapLighting(const MapLighting&) = delete;
    MapLighting& operator=(const MapLighting&) = delete;

    MapLighting(MapLighting&& other) noexcept
        : available(other.available)
        , rad(std::move(other.rad))
        , surfaceBvh(std::move(other.surfaceBvh))
        , chartIndexByFaceId(std::move(other.chartIndexByFaceId))
        , atlasImages(std::move(other.atlasImages))
        , ambient(other.ambient) {
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
        surfaceBvh = std::move(other.surfaceBvh);
        chartIndexByFaceId = std::move(other.chartIndexByFaceId);
        atlasImages = std::move(other.atlasImages);
        ambient = other.ambient;
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
        available = false;
    }
};

MapLighting buildMapLighting(
    const BspTree& bsp,
    RadFile rad,
    std::vector<Image> atlasImages,
    Color ambient);

std::optional<Color> sampleMapLight(
    const MapLighting& lighting,
    const BspTree& tree,
    Vector3 origin,
    Vector3 direction,
    float maxDistance);

}
