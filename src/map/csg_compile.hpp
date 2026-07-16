#pragma once

#include "assets/geo_loader.hpp"
#include "assets/rigged_assets.hpp"
#include "map/brush.hpp"

#include <functional>
#include <string_view>
#include <vector>

namespace slopengine {

struct MaterialUvInfo {
    float pixelsPerMeter = 64.0f;
    float textureWidth = 64.0f;
    float textureHeight = 64.0f;
};

using MaterialUvResolver = std::function<MaterialUvInfo(std::string_view materialPath)>;

struct CsgCompileResult {
    GeoAsset asset;
    VertBuffer buffer;
};

CsgCompileResult compileBrushesToGeo(
    const std::vector<Brush>& brushes,
    const MaterialUvResolver& resolveMaterialUv = {});

}
