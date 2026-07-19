#pragma once

#include "assets/asset_store.hpp"

namespace slopengine {

/** Flecs world singleton that exposes the shared AssetStore to systems. */
struct AssetServices {
    AssetStore* store = nullptr;
};

}
