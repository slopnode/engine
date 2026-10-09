#include "ui/slopui_tool.hpp"

#include "assets/asset_store.hpp"
#include "ui/imgui_fonts.hpp"
#include "ui/slopui_theme.hpp"

#include <s7.h>

namespace slopengine {

namespace {

SlopUiTheme g_toolTheme;
SlopUiFonts g_toolFonts;

} // namespace

ImFont* installToolTheme(AssetStore& assets, std::string_view toolName) {
    setupImGuiWithUiFont(assets, kDefaultUiFontPath, true, toolName);
    s7_scheme* scheme = s7_init();
    g_toolTheme = loadSlopUiTheme(assets, scheme, true);
    if (scheme != nullptr) {
        s7_quit(scheme);
    }
    applySlopUiTheme(g_toolTheme, assets, g_toolFonts);
    setActiveSlopUi(&g_toolTheme, &g_toolFonts, &assets);
    return g_toolFonts.font(ThemeFontSlot::Mono);
}

}
