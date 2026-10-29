#include "ui/slopui_theme.hpp"

#include "assets/asset_store.hpp"
#include "ui/imgui_fonts.hpp"

#include <raylib.h>

#include <algorithm>
#include <string>
#include <unordered_map>

namespace slopengine {

namespace {

std::unordered_map<std::string, ImFont*>& fontCache() {
    static std::unordered_map<std::string, ImFont*> cache;
    return cache;
}

ImFont* ensureThemeFont(AssetStore& assets, const std::string& path, float sizePixels) {
    if (path.empty()) {
        return nullptr;
    }
    auto& cache = fontCache();
    const auto it = cache.find(path);
    if (it != cache.end()) {
        return it->second;
    }
    ImFont* font = loadImGuiFont(assets, path, sizePixels);
    if (font != nullptr) {
        cache.emplace(path, font);
    }
    return font;
}

} // namespace

void applySlopUiStyle(const SlopUiTheme& theme) {
    ImGuiStyle& style = ImGui::GetStyle();

    const bool light = themeIsLight(theme.surface);
    const ThemeColor border = theme.bevel > 0.0f ? themeDarken(theme.surface, 0.55f) : theme.borderColor();
    const ThemeColor transparent{0.0f, 0.0f, 0.0f, 0.0f};
    const ThemeColor dimBase{0.0f, 0.0f, 0.0f, 1.0f};
    const ThemeColor header = themeMix(theme.surface, theme.accent, 0.30f);
    const ThemeColor headerHovered = themeMix(theme.surface, theme.accent, 0.45f);
    const ThemeColor headerActive = themeMix(theme.surface, theme.accent, 0.60f);
    const ThemeColor titleBg = theme.resolvePaint(theme.window.titleBg, themeMix(theme.bg, theme.surface, 0.5f)).a;
    const ThemeColor titleBgActive =
        theme.resolvePaint(theme.window.titleBgActive, themeMix(theme.surface, theme.accent, 0.25f)).a;
    const ThemeColor menuBarBg = theme.resolvePaint(theme.menuBar.bg, theme.surface).a;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = theme.text.vec();
    c[ImGuiCol_TextDisabled] = theme.textDim.vec();
    c[ImGuiCol_WindowBg] = theme.bg.vec();
    c[ImGuiCol_ChildBg] = transparent.vec();
    c[ImGuiCol_PopupBg] = theme.surface.vec();
    c[ImGuiCol_Border] = border.vec();
    c[ImGuiCol_BorderShadow] = transparent.vec();
    const ThemeColor frameBg = theme.bevel > 0.0f
        ? (light ? themeLighten(theme.surface, 0.9f) : themeDarken(theme.surface, 0.3f))
        : theme.surface;
    c[ImGuiCol_FrameBg] = frameBg.vec();
    c[ImGuiCol_FrameBgHovered] = themeRaise(frameBg, 0.06f).vec();
    c[ImGuiCol_FrameBgActive] = themeRaise(frameBg, 0.12f).vec();
    c[ImGuiCol_TitleBg] = titleBg.vec();
    c[ImGuiCol_TitleBgActive] = titleBgActive.vec();
    c[ImGuiCol_TitleBgCollapsed] = titleBg.vec();
    c[ImGuiCol_MenuBarBg] = menuBarBg.vec();
    c[ImGuiCol_ScrollbarBg] = (light ? themeLighten(theme.bg, 0.5f) : theme.bg).vec();
    c[ImGuiCol_ScrollbarGrab] = themeRaise(theme.surface, 0.15f).vec();
    c[ImGuiCol_ScrollbarGrabHovered] = themeRaise(theme.surface, 0.25f).vec();
    c[ImGuiCol_ScrollbarGrabActive] = themeRaise(theme.surface, 0.35f).vec();
    c[ImGuiCol_CheckMark] = theme.accent.vec();
    c[ImGuiCol_CheckboxSelectedBg] = theme.surface.vec();
    c[ImGuiCol_SliderGrab] = theme.accent.vec();
    c[ImGuiCol_SliderGrabActive] = themeLighten(theme.accent, 0.15f).vec();
    c[ImGuiCol_Button] = themeRaise(theme.surface, 0.08f).vec();
    c[ImGuiCol_ButtonHovered] = themeMix(theme.surface, theme.accent, 0.40f).vec();
    c[ImGuiCol_ButtonActive] = themeMix(theme.surface, theme.accent, 0.60f).vec();
    c[ImGuiCol_Header] = header.vec();
    c[ImGuiCol_HeaderHovered] = headerHovered.vec();
    c[ImGuiCol_HeaderActive] = headerActive.vec();
    c[ImGuiCol_Separator] = border.vec();
    c[ImGuiCol_SeparatorHovered] = theme.accent.vec();
    c[ImGuiCol_SeparatorActive] = theme.accent.vec();
    c[ImGuiCol_ResizeGrip] = transparent.vec();
    c[ImGuiCol_ResizeGripHovered] = transparent.vec();
    c[ImGuiCol_ResizeGripActive] = transparent.vec();
    c[ImGuiCol_InputTextCursor] = theme.text.vec();
    c[ImGuiCol_TabHovered] = headerHovered.vec();
    c[ImGuiCol_Tab] = themeRaise(theme.surface, 0.08f).vec();
    c[ImGuiCol_TabSelected] = header.vec();
    c[ImGuiCol_TabSelectedOverline] = theme.accent.vec();
    c[ImGuiCol_TabDimmed] = theme.surface.vec();
    c[ImGuiCol_TabDimmedSelected] = header.vec();
    c[ImGuiCol_TabDimmedSelectedOverline] = theme.accent.withAlpha(0.5f).vec();
    c[ImGuiCol_PlotLines] = theme.accent.vec();
    c[ImGuiCol_PlotLinesHovered] = themeLighten(theme.accent, 0.2f).vec();
    c[ImGuiCol_PlotHistogram] = theme.accent.vec();
    c[ImGuiCol_PlotHistogramHovered] = themeLighten(theme.accent, 0.2f).vec();
    c[ImGuiCol_TableHeaderBg] = themeRaise(theme.surface, 0.05f).vec();
    c[ImGuiCol_TableBorderStrong] = border.vec();
    c[ImGuiCol_TableBorderLight] = border.withAlpha(0.5f).vec();
    c[ImGuiCol_TableRowBg] = transparent.vec();
    c[ImGuiCol_TableRowBgAlt] = theme.text.withAlpha(0.03f).vec();
    c[ImGuiCol_TextLink] = theme.accent.vec();
    c[ImGuiCol_TextSelectedBg] = theme.accent.withAlpha(0.35f).vec();
    c[ImGuiCol_TreeLines] = border.vec();
    c[ImGuiCol_DragDropTarget] = theme.accent.vec();
    c[ImGuiCol_DragDropTargetBg] = theme.accent.withAlpha(0.2f).vec();
    c[ImGuiCol_UnsavedMarker] = theme.accent.vec();
    c[ImGuiCol_NavCursor] = theme.accent.vec();
    c[ImGuiCol_NavWindowingHighlight] = theme.accent.vec();
    c[ImGuiCol_NavWindowingDimBg] = dimBase.withAlpha(0.6f).vec();
    c[ImGuiCol_ModalWindowDimBg] = dimBase.withAlpha(theme.chrome.pauseDim).vec();

    style.WindowRounding = theme.rounding;
    style.ChildRounding = theme.rounding;
    style.PopupRounding = theme.rounding;
    style.FrameRounding = theme.rounding;
    style.GrabRounding = theme.rounding;
    style.TabRounding = theme.rounding;
    style.ScrollbarRounding = theme.rounding;
    style.WindowBorderSize = theme.border;
    style.PopupBorderSize = theme.border;
    style.ChildBorderSize = theme.border;
    style.FrameBorderSize = theme.frameBorder;
    style.WindowPadding = theme.padding;
    style.FramePadding = theme.framePadding;
    style.ItemSpacing = theme.spacing;
    style.ItemInnerSpacing = ImVec2{theme.spacing.x * 0.5f, theme.spacing.y};
    style.WindowTitleAlign = theme.window.titleCentered ? ImVec2{0.5f, 0.5f} : ImVec2{0.0f, 0.5f};
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;
    style.SeparatorTextBorderSize = std::max(theme.border, 1.0f);
}

void applySlopUiTheme(const SlopUiTheme& theme, AssetStore& assets, SlopUiFonts& fonts) {
    const float dpi = uiFontDpiScale();
    const ThemeFont& normal = theme.font(ThemeFontSlot::Normal);
    const float normalSize = normal.size > 0.0f ? normal.size : 16.0f;

    for (std::size_t i = 0; i < static_cast<std::size_t>(ThemeFontSlot::Count); ++i) {
        const ThemeFont& slot = theme.fonts[i];
        const float size = slot.size > 0.0f ? slot.size : normalSize;
        ImFont* font = ensureThemeFont(assets, slot.path, size * dpi);
        fonts.fonts[i] = font;
        fonts.sizes[i] = size * dpi;
    }

    ImFont* normalFont = fonts.font(ThemeFontSlot::Normal);
    for (std::size_t i = 0; i < static_cast<std::size_t>(ThemeFontSlot::Count); ++i) {
        if (fonts.fonts[i] == nullptr) {
            fonts.fonts[i] = normalFont;
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    if (normalFont != nullptr) {
        io.FontDefault = normalFont;
    }
    ImGui::GetStyle().FontSizeBase = fonts.size(ThemeFontSlot::Normal);
    applySlopUiStyle(theme);
}

namespace {

const SlopUiTheme* g_activeTheme = nullptr;
const SlopUiFonts* g_activeFonts = nullptr;
AssetStore* g_activeAssets = nullptr;

} // namespace

void setActiveSlopUi(const SlopUiTheme* theme, const SlopUiFonts* fonts, AssetStore* assets) {
    g_activeTheme = theme;
    g_activeFonts = fonts;
    g_activeAssets = assets;
}

const SlopUiTheme* activeSlopUiTheme() {
    return g_activeTheme;
}

const SlopUiFonts* activeSlopUiFonts() {
    return g_activeFonts;
}

AssetStore* activeSlopUiAssets() {
    return g_activeAssets;
}

void pushThemeFont(const SlopUiFonts& fonts, ThemeFontSlot slot) {
    ImGui::PushFont(fonts.font(slot), fonts.size(slot));
}

void popThemeFont() {
    ImGui::PopFont();
}

ImGuiWindowFlags themedWindowFlags(const SlopUiTheme& theme) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!theme.window.draggable) {
        flags |= ImGuiWindowFlags_NoMove;
    }
    return flags;
}

ImVec2 themedMenuBarPadding(const SlopUiTheme& theme, const SlopUiFonts& fonts) {
    const float fontSize = fonts.size(theme.menuBar.font);
    const float padY = std::max((theme.menuBar.height - fontSize) * 0.5f, 2.0f);
    return ImVec2{theme.framePadding.x, padY};
}

}
