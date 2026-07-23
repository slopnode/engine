#pragma once

#include "assets/geo_loader.hpp"
#include "assets/rigged_assets.hpp"
#include "map/brush.hpp"
#include "map/lightmap.hpp"
#include "map/fac.hpp"

#include <functional>
#include <string_view>
#include <vector>

namespace slopengine {

/** UV density and texture size used when projecting brush face UVs. */
struct MaterialUvInfo {
    float pixelsPerMeter = 64.0f;
    float textureWidth = 64.0f;
    float textureHeight = 64.0f;
};

using MaterialUvResolver = std::function<MaterialUvInfo(std::string_view materialPath)>;

/** Compiled brush mesh as a GeoAsset plus vertex buffer. */
struct CsgCompileResult {
    GeoAsset asset;
    VertBuffer buffer;
};

/** Triangulates brushes into geo; embeds lightmap UV2 when @p lightmaps is set. */
CsgCompileResult compileBrushesToGeo(
    const std::vector<Brush>& brushes,
    const MaterialUvResolver& resolveMaterialUv = {},
    const RadFile* lightmaps = nullptr);

/** Triangulates VIS faces into geo; embeds lightmap UV2 when @p lightmaps is set. */
CsgCompileResult compileVisibleFacesToGeo(
    const FacFile& vis,
    const MaterialUvResolver& resolveMaterialUv = {},
    const RadFile* lightmaps = nullptr);

}
