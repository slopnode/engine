#include "ui/slopui_tool.hpp"

#include "assets/asset_store.hpp"
#include "core/package.hpp"
#include "core/user_paths.hpp"
#include "game/ui_settings.hpp"
#include "ui/imgui_fonts.hpp"
#include "ui/slopui_theme.hpp"

#include <s7.h>

#include <string>
#include <vector>

namespace slopengine {

namespace {

SlopUiTheme g_toolTheme;
SlopUiFonts g_toolFonts;

std::string toolThemeId(const AssetStore& assets, std::string_view profile) {
    const std::vector<Package>& packages = assets.packages();
    if (packages.size() < 2) {
        return kDefaultSlopUiThemeId;
    }
    return loadUiSettings(profileSettingsPath(packages[0], packages[1], profile.empty() ? "default" : profile))
        .theme;
}

} // namespace

ImFont* installToolTheme(AssetStore& assets, std::string_view toolName, std::string_view profile) {
    setupImGuiWithUiFont(assets, kDefaultUiFontPath, true, toolName);
    s7_scheme* scheme = s7_init();
    g_toolTheme = loadSlopUiTheme(assets, scheme, true, toolThemeId(assets, profile));
    if (scheme != nullptr) {
        s7_quit(scheme);
    }
    applySlopUiTheme(g_toolTheme, assets, g_toolFonts);
    setActiveSlopUi(&g_toolTheme, &g_toolFonts, &assets);
    return g_toolFonts.font(ThemeFontSlot::Mono);
}

}
