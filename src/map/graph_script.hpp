#pragma once

#include "assets/asset_store.hpp"
#include "map/graph.hpp"

#include <optional>
#include <string_view>

struct s7_scheme;

namespace slopengine {

/** Loads maps/<name>/graphs.s7 into a graph document. Missing file → empty document. */
std::optional<GraphDocument> loadMapGraphs(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

}
