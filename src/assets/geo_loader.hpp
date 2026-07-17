#pragma once

#include "assets/rigged_assets.hpp"

#include <raylib.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace slopengine {

using MaterialResolver = std::function<Material(std::string_view path)>;

/** Parsed vertex data for a geometry asset. */
struct VertBuffer {
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::vector<Vector2> texcoords2;
    std::vector<std::uint32_t> indices;
};

/** Per-vertex skinning weights and joint indices. */
struct VertexWeights {
    unsigned char jointIndices[4] = {};
    float jointWeights[4] = {};
};

/** Parses a geometry asset from its text source into @p asset. */
bool parseGeoAsset(std::string_view source, GeoAsset& asset);

/** Loads a binary vertex buffer from @p data into @p buffer. */
bool loadVertBuffer(std::span<const std::byte> data, VertBuffer& buffer);

/** Loads a binary skinning weights buffer from @p data into @p buffer. */
bool loadWeightsBuffer(std::span<const std::byte> data, std::vector<VertexWeights>& buffer);

/** Builds a raylib model from parsed geometry, materials, and optional skinning data. */
Model buildModelFromGeo(
    const GeoAsset& asset,
    const VertBuffer& buffer,
    const MaterialResolver& resolveMaterial,
    const std::vector<VertexWeights>* weights = nullptr,
    int skeletonBoneCount = 0);

}
