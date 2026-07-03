#pragma once

#include "assets/rigged_assets.hpp"

#include <raylib.h>

#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace daggerlike {

using MaterialResolver = std::function<Material(std::string_view path)>;

struct VertBuffer {
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::vector<std::uint32_t> indices;
};

struct VertexWeights {
    unsigned char jointIndices[4] = {};
    float jointWeights[4] = {};
};

bool parseGeoAsset(std::string_view source, GeoAsset& asset);
bool loadVertBuffer(std::span<const std::byte> data, VertBuffer& buffer);
bool loadWeightsBuffer(std::span<const std::byte> data, std::vector<VertexWeights>& buffer);
Model buildModelFromGeo(
    const GeoAsset& asset,
    const VertBuffer& buffer,
    const MaterialResolver& resolveMaterial,
    const std::vector<VertexWeights>* weights = nullptr,
    int skeletonBoneCount = 0);

}
