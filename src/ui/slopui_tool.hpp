#pragma once

#include <string_view>

struct ImFont;

namespace slopengine {

class AssetStore;

/** Sets up ImGui for an editor tool and installs the merged package theme with its editor overrides.
 *  Returns the theme's mono font for callers that push it explicitly. */
ImFont* installToolTheme(AssetStore& assets, std::string_view toolName, std::string_view profile = "default");

}
