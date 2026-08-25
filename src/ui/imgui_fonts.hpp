#pragma once

#include "assets/asset_store.hpp"

#include "imgui.h"

#include <string_view>

namespace slopengine {

inline constexpr std::string_view kDefaultUiFontPath = "FiraSans/FiraSans-Regular";
inline constexpr std::string_view kMonoUiFontPath = "JetBrainsMono/JetBrainsMono";

ImFont* loadImGuiFont(AssetStore& assets, std::string_view path, float sizePixels = 0.0f);

/** Points ImGui's ini persistence at a per-tool file under the user config dir instead of cwd. */
void setImGuiIniPath(std::string_view toolName);

bool setupImGuiWithUiFont(
    AssetStore& assets,
    std::string_view uiFontPath,
    bool darkTheme,
    std::string_view toolName);

ImFont* setupImGuiWithUiAndMonoFont(
    AssetStore& assets,
    std::string_view uiFontPath,
    std::string_view monoFontPath,
    bool darkTheme,
    std::string_view toolName);

}
