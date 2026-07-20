#pragma once

#include "assets/asset_store.hpp"

#include "imgui.h"

#include <string_view>

namespace slopengine {

inline constexpr std::string_view kDefaultUiFontPath = "FiraSans/FiraSans-Regular";

ImFont* loadImGuiFont(AssetStore& assets, std::string_view path, float sizePixels = 0.0f);

bool setupImGuiWithUiFont(AssetStore& assets, std::string_view uiFontPath, bool darkTheme);

}
