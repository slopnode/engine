#include "ui/slopui_theme.hpp"

#include "assets/asset_store.hpp"
#include "core/package.hpp"
#include "ui/imgui_fonts.hpp"

#include <raylib.h>
#include <s7.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>

namespace slopengine {

namespace {

constexpr const char* kFontSlotNames[] = {"normal", "bold", "heading", "mono"};

ThemeColor rgb(int r, int g, int b) {
    return ThemeColor{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
}

bool readReal(s7_scheme* sc, s7_pointer cell, float& out) {
    if (!s7_is_pair(cell) || !s7_is_number(s7_car(cell))) {
        return false;
    }
    out = static_cast<float>(s7_number_to_real(sc, s7_car(cell)));
    return true;
}

bool readVec2(s7_scheme* sc, s7_pointer rest, ImVec2& out, const char* section, const char* key) {
    float x = 0.0f;
    float y = 0.0f;
    if (!readReal(sc, rest, x) || !readReal(sc, s7_cdr(rest), y)) {
        TraceLog(LOG_WARNING, "THEME: %s.%s expects two numbers", section, key);
        return false;
    }
    out = ImVec2{x, y};
    return true;
}

bool readBool(s7_scheme* sc, s7_pointer rest, bool& out, const char* section, const char* key) {
    if (!s7_is_pair(rest) || !s7_is_boolean(s7_car(rest))) {
        TraceLog(LOG_WARNING, "THEME: %s.%s expects a boolean", section, key);
        return false;
    }
    out = s7_boolean(sc, s7_car(rest));
    return true;
}

bool readColorRef(s7_scheme* sc, s7_pointer rest, ThemeColorRef& out, const char* section, const char* key) {
    if (s7_is_pair(rest) && s7_is_symbol(s7_car(rest))) {
        out.token = s7_symbol_name(s7_car(rest));
        out.set = true;
        return true;
    }
    float channels[4] = {0.0f, 0.0f, 0.0f, 255.0f};
    int count = 0;
    for (s7_pointer cursor = rest; s7_is_pair(cursor) && count < 4; cursor = s7_cdr(cursor)) {
        if (!s7_is_number(s7_car(cursor))) {
            break;
        }
        channels[count++] = static_cast<float>(s7_number_to_real(sc, s7_car(cursor)));
    }
    if (count < 3) {
        TraceLog(LOG_WARNING, "THEME: %s.%s expects r g b [a] or a palette token", section, key);
        return false;
    }
    out.color = ThemeColor{
        std::clamp(channels[0] / 255.0f, 0.0f, 1.0f),
        std::clamp(channels[1] / 255.0f, 0.0f, 1.0f),
        std::clamp(channels[2] / 255.0f, 0.0f, 1.0f),
        std::clamp(channels[3] / 255.0f, 0.0f, 1.0f)};
    out.token.clear();
    out.set = true;
    return true;
}

bool readPaletteColor(s7_scheme* sc, s7_pointer rest, ThemeColor& out, const char* key) {
    ThemeColorRef ref;
    if (!readColorRef(sc, rest, ref, "colors", key)) {
        return false;
    }
    if (!ref.token.empty()) {
        TraceLog(LOG_WARNING, "THEME: colors.%s must be literal r g b, not a token", key);
        return false;
    }
    out = ref.color;
    return true;
}

bool readFontSlot(s7_pointer rest, ThemeFontSlot& out, const char* section, const char* key) {
    if (!s7_is_pair(rest) || !s7_is_symbol(s7_car(rest))) {
        TraceLog(LOG_WARNING, "THEME: %s.%s expects a font slot symbol", section, key);
        return false;
    }
    if (!parseThemeFontSlot(s7_symbol_name(s7_car(rest)), out)) {
        TraceLog(LOG_WARNING, "THEME: %s.%s unknown font slot '%s'", section, key,
            s7_symbol_name(s7_car(rest)));
        return false;
    }
    return true;
}

void parseFonts(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    for (s7_pointer cursor = section; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            continue;
        }
        const char* key = s7_symbol_name(s7_car(entry));
        ThemeFontSlot slot;
        if (!parseThemeFontSlot(key, slot)) {
            TraceLog(LOG_WARNING, "THEME: unknown font slot '%s'", key);
            continue;
        }
        const s7_pointer rest = s7_cdr(entry);
        if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
            TraceLog(LOG_WARNING, "THEME: fonts.%s expects a path string", key);
            continue;
        }
        ThemeFont& font = theme.font(slot);
        font.path = s7_string(s7_car(rest));
        float size = 0.0f;
        if (readReal(sc, s7_cdr(rest), size)) {
            font.size = size;
        }
    }
}

void parseColors(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    for (s7_pointer cursor = section; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            continue;
        }
        const char* key = s7_symbol_name(s7_car(entry));
        const s7_pointer rest = s7_cdr(entry);
        ThemeColor* target = nullptr;
        if (std::strcmp(key, "bg") == 0) {
            target = &theme.bg;
        } else if (std::strcmp(key, "surface") == 0) {
            target = &theme.surface;
        } else if (std::strcmp(key, "accent") == 0) {
            target = &theme.accent;
        } else if (std::strcmp(key, "text") == 0) {
            target = &theme.text;
        } else if (std::strcmp(key, "text-dim") == 0) {
            target = &theme.textDim;
        } else if (std::strcmp(key, "danger") == 0) {
            target = &theme.danger;
        }
        if (target == nullptr) {
            TraceLog(LOG_WARNING, "THEME: unknown color token '%s'", key);
            continue;
        }
        readPaletteColor(sc, rest, *target, key);
    }
}

void parseMetrics(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    for (s7_pointer cursor = section; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            continue;
        }
        const char* key = s7_symbol_name(s7_car(entry));
        const s7_pointer rest = s7_cdr(entry);
        float value = 0.0f;
        if (std::strcmp(key, "rounding") == 0) {
            if (readReal(sc, rest, value)) theme.rounding = std::max(value, 0.0f);
        } else if (std::strcmp(key, "border") == 0) {
            if (readReal(sc, rest, value)) theme.border = std::max(value, 0.0f);
        } else if (std::strcmp(key, "frame-border") == 0) {
            if (readReal(sc, rest, value)) theme.frameBorder = std::max(value, 0.0f);
        } else if (std::strcmp(key, "padding") == 0) {
            readVec2(sc, rest, theme.padding, "metrics", key);
        } else if (std::strcmp(key, "frame-padding") == 0) {
            readVec2(sc, rest, theme.framePadding, "metrics", key);
        } else if (std::strcmp(key, "spacing") == 0) {
            readVec2(sc, rest, theme.spacing, "metrics", key);
        } else if (std::strcmp(key, "button-height") == 0) {
            if (readReal(sc, rest, value)) theme.buttonHeight = std::max(value, 0.0f);
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown metrics key '%s'", key);
        }
    }
}

void parseMenuBar(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    for (s7_pointer cursor = section; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            continue;
        }
        const char* key = s7_symbol_name(s7_car(entry));
        const s7_pointer rest = s7_cdr(entry);
        float value = 0.0f;
        if (std::strcmp(key, "height") == 0) {
            if (readReal(sc, rest, value)) theme.menuBar.height = std::max(value, 0.0f);
        } else if (std::strcmp(key, "font") == 0) {
            readFontSlot(rest, theme.menuBar.font, "menu-bar", key);
        } else if (std::strcmp(key, "bg") == 0) {
            readColorRef(sc, rest, theme.menuBar.bg, "menu-bar", key);
        } else if (std::strcmp(key, "icons") == 0) {
            readBool(sc, rest, theme.menuBar.icons, "menu-bar", key);
        } else if (std::strcmp(key, "version-label") == 0) {
            readBool(sc, rest, theme.menuBar.versionLabel, "menu-bar", key);
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown menu-bar key '%s'", key);
        }
    }
}

void parseWindow(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    for (s7_pointer cursor = section; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            continue;
        }
        const char* key = s7_symbol_name(s7_car(entry));
        const s7_pointer rest = s7_cdr(entry);
        if (std::strcmp(key, "title-font") == 0) {
            readFontSlot(rest, theme.window.titleFont, "window", key);
        } else if (std::strcmp(key, "title-align") == 0) {
            if (!s7_is_pair(rest) || !s7_is_symbol(s7_car(rest))) {
                TraceLog(LOG_WARNING, "THEME: window.title-align expects center or left");
                continue;
            }
            const char* align = s7_symbol_name(s7_car(rest));
            if (std::strcmp(align, "center") == 0) {
                theme.window.titleCentered = true;
            } else if (std::strcmp(align, "left") == 0) {
                theme.window.titleCentered = false;
            } else {
                TraceLog(LOG_WARNING, "THEME: window.title-align unknown value '%s'", align);
            }
        } else if (std::strcmp(key, "title-bg") == 0) {
            readColorRef(sc, rest, theme.window.titleBg, "window", key);
        } else if (std::strcmp(key, "title-bg-active") == 0) {
            readColorRef(sc, rest, theme.window.titleBgActive, "window", key);
        } else if (std::strcmp(key, "draggable") == 0) {
            readBool(sc, rest, theme.window.draggable, "window", key);
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown window key '%s'", key);
        }
    }
}

void parseChrome(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    for (s7_pointer cursor = section; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            continue;
        }
        const char* key = s7_symbol_name(s7_car(entry));
        const s7_pointer rest = s7_cdr(entry);
        float value = 0.0f;
        if (std::strcmp(key, "pause-title") == 0) {
            if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
                TraceLog(LOG_WARNING, "THEME: chrome.pause-title expects a string");
                continue;
            }
            theme.chrome.pauseTitle = s7_string(s7_car(rest));
        } else if (std::strcmp(key, "pause-dim") == 0) {
            if (readReal(sc, rest, value)) theme.chrome.pauseDim = std::clamp(value, 0.0f, 1.0f);
        } else if (std::strcmp(key, "pause-width") == 0) {
            if (readReal(sc, rest, value)) theme.chrome.pauseWidth = std::max(value, 0.0f);
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown chrome key '%s'", key);
        }
    }
}

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

void applyStyle(const SlopUiTheme& theme) {
    ImGuiStyle& style = ImGui::GetStyle();

    const ThemeColor border = themeLighten(theme.surface, 0.10f);
    const ThemeColor transparent{0.0f, 0.0f, 0.0f, 0.0f};
    const ThemeColor header = themeMix(theme.surface, theme.accent, 0.30f);
    const ThemeColor headerHovered = themeMix(theme.surface, theme.accent, 0.45f);
    const ThemeColor headerActive = themeMix(theme.surface, theme.accent, 0.60f);
    const ThemeColor titleBg = theme.resolve(theme.window.titleBg, themeMix(theme.bg, theme.surface, 0.5f));
    const ThemeColor titleBgActive =
        theme.resolve(theme.window.titleBgActive, themeMix(theme.surface, theme.accent, 0.25f));
    const ThemeColor menuBarBg = theme.resolve(theme.menuBar.bg, theme.surface);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = theme.text.vec();
    c[ImGuiCol_TextDisabled] = theme.textDim.vec();
    c[ImGuiCol_WindowBg] = theme.bg.vec();
    c[ImGuiCol_ChildBg] = transparent.vec();
    c[ImGuiCol_PopupBg] = theme.surface.vec();
    c[ImGuiCol_Border] = border.vec();
    c[ImGuiCol_BorderShadow] = transparent.vec();
    c[ImGuiCol_FrameBg] = theme.surface.vec();
    c[ImGuiCol_FrameBgHovered] = themeLighten(theme.surface, 0.06f).vec();
    c[ImGuiCol_FrameBgActive] = themeLighten(theme.surface, 0.12f).vec();
    c[ImGuiCol_TitleBg] = titleBg.vec();
    c[ImGuiCol_TitleBgActive] = titleBgActive.vec();
    c[ImGuiCol_TitleBgCollapsed] = titleBg.vec();
    c[ImGuiCol_MenuBarBg] = menuBarBg.vec();
    c[ImGuiCol_ScrollbarBg] = theme.bg.vec();
    c[ImGuiCol_ScrollbarGrab] = themeLighten(theme.surface, 0.15f).vec();
    c[ImGuiCol_ScrollbarGrabHovered] = themeLighten(theme.surface, 0.25f).vec();
    c[ImGuiCol_ScrollbarGrabActive] = themeLighten(theme.surface, 0.35f).vec();
    c[ImGuiCol_CheckMark] = theme.accent.vec();
    c[ImGuiCol_CheckboxSelectedBg] = theme.surface.vec();
    c[ImGuiCol_SliderGrab] = theme.accent.vec();
    c[ImGuiCol_SliderGrabActive] = themeLighten(theme.accent, 0.15f).vec();
    c[ImGuiCol_Button] = themeLighten(theme.surface, 0.08f).vec();
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
    c[ImGuiCol_Tab] = themeLighten(theme.surface, 0.08f).vec();
    c[ImGuiCol_TabSelected] = header.vec();
    c[ImGuiCol_TabSelectedOverline] = theme.accent.vec();
    c[ImGuiCol_TabDimmed] = theme.surface.vec();
    c[ImGuiCol_TabDimmedSelected] = header.vec();
    c[ImGuiCol_TabDimmedSelectedOverline] = theme.accent.withAlpha(0.5f).vec();
    c[ImGuiCol_PlotLines] = theme.accent.vec();
    c[ImGuiCol_PlotLinesHovered] = themeLighten(theme.accent, 0.2f).vec();
    c[ImGuiCol_PlotHistogram] = theme.accent.vec();
    c[ImGuiCol_PlotHistogramHovered] = themeLighten(theme.accent, 0.2f).vec();
    c[ImGuiCol_TableHeaderBg] = themeLighten(theme.surface, 0.05f).vec();
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
    c[ImGuiCol_NavWindowingDimBg] = theme.bg.withAlpha(0.6f).vec();
    c[ImGuiCol_ModalWindowDimBg] = theme.bg.withAlpha(theme.chrome.pauseDim).vec();

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

} // namespace

ThemeColor themeMix(const ThemeColor& a, const ThemeColor& b, float t) {
    return ThemeColor{
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t};
}

ThemeColor themeLighten(const ThemeColor& c, float t) {
    return themeMix(c, ThemeColor{1.0f, 1.0f, 1.0f, c.a}, t);
}

ThemeColor themeDarken(const ThemeColor& c, float t) {
    return themeMix(c, ThemeColor{0.0f, 0.0f, 0.0f, c.a}, t);
}

bool parseThemeFontSlot(std::string_view name, ThemeFontSlot& out) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(ThemeFontSlot::Count); ++i) {
        if (name == kFontSlotNames[i]) {
            out = static_cast<ThemeFontSlot>(i);
            return true;
        }
    }
    return false;
}

const char* themeFontSlotName(ThemeFontSlot slot) {
    return kFontSlotNames[static_cast<std::size_t>(slot)];
}

bool SlopUiTheme::paletteColor(std::string_view token, ThemeColor& out) const {
    if (token == "bg") {
        out = bg;
    } else if (token == "surface") {
        out = surface;
    } else if (token == "accent") {
        out = accent;
    } else if (token == "text") {
        out = text;
    } else if (token == "text-dim") {
        out = textDim;
    } else if (token == "danger") {
        out = danger;
    } else {
        return false;
    }
    return true;
}

ThemeColor SlopUiTheme::resolve(const ThemeColorRef& ref, const ThemeColor& fallback) const {
    if (!ref.set) {
        return fallback;
    }
    if (ref.token.empty()) {
        return ref.color;
    }
    ThemeColor out;
    if (paletteColor(ref.token, out)) {
        return out;
    }
    TraceLog(LOG_WARNING, "THEME: unknown palette token '%s'", ref.token.c_str());
    return fallback;
}

SlopUiTheme defaultSlopUiTheme() {
    SlopUiTheme theme;
    theme.font(ThemeFontSlot::Normal) = {std::string{kDefaultUiFontPath}, 16.0f};
    theme.font(ThemeFontSlot::Bold) = {"FiraSans/FiraSans-Bold", 16.0f};
    theme.font(ThemeFontSlot::Heading) = {"FiraSans/FiraSans-Bold", 28.0f};
    theme.font(ThemeFontSlot::Mono) = {std::string{kMonoUiFontPath}, 14.0f};
    theme.bg = rgb(17, 18, 22);
    theme.surface = rgb(29, 31, 38);
    theme.accent = rgb(214, 160, 70);
    theme.text = rgb(232, 228, 218);
    theme.textDim = rgb(146, 143, 136);
    theme.danger = rgb(204, 64, 58);
    return theme;
}

void overlayPackageThemeFromScheme(s7_scheme* scheme, SlopUiTheme& theme) {
    if (scheme == nullptr) {
        return;
    }
    const s7_pointer catalog = s7_name_to_value(scheme, "*package-theme*");
    if (catalog == s7_undefined(scheme) || !s7_is_pair(catalog)) {
        return;
    }

    for (s7_pointer cursor = catalog; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            continue;
        }
        const char* key = s7_symbol_name(s7_car(entry));
        const s7_pointer rest = s7_cdr(entry);
        if (std::strcmp(key, "fonts") == 0) {
            parseFonts(scheme, rest, theme);
        } else if (std::strcmp(key, "colors") == 0) {
            parseColors(scheme, rest, theme);
        } else if (std::strcmp(key, "metrics") == 0) {
            parseMetrics(scheme, rest, theme);
        } else if (std::strcmp(key, "menu-bar") == 0) {
            parseMenuBar(scheme, rest, theme);
        } else if (std::strcmp(key, "window") == 0) {
            parseWindow(scheme, rest, theme);
        } else if (std::strcmp(key, "chrome") == 0) {
            parseChrome(scheme, rest, theme);
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown section '%s'", key);
        }
    }
}

SlopUiTheme loadSlopUiTheme(AssetStore& assets, s7_scheme* scheme) {
    SlopUiTheme theme = defaultSlopUiTheme();
    if (scheme == nullptr) {
        return theme;
    }
    for (const Package& package : assets.packages()) {
        const std::string& id = package.meta().id;
        s7_define_variable(scheme, "*package-theme*", s7_f(scheme));
        if (!assets.loadDataFromPackage(scheme, id, "theme")) {
            continue;
        }
        overlayPackageThemeFromScheme(scheme, theme);
        TraceLog(LOG_INFO, "THEME: applied data/theme.s7 from '%s'", id.c_str());
    }
    return theme;
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
    applyStyle(theme);
}

void installSlopUiTheme(flecs::world& world, AssetStore& assets, s7_scheme* scheme) {
    world.component<SlopUiTheme>();
    world.component<SlopUiFonts>();
    world.component<SlopUiThemeReload>();
    SlopUiTheme theme = loadSlopUiTheme(assets, scheme);
    SlopUiFonts fonts{};
    applySlopUiTheme(theme, assets, fonts);
    world.set<SlopUiTheme>(std::move(theme));
    world.set<SlopUiFonts>(fonts);
    if (!world.has<SlopUiThemeReload>()) {
        world.set<SlopUiThemeReload>({});
    }
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

bool beginThemedWindow(
    const SlopUiTheme& theme,
    const SlopUiFonts& fonts,
    const char* name,
    bool* open,
    ImGuiWindowFlags extraFlags) {
    pushThemeFont(fonts, theme.window.titleFont);
    const bool visible = ImGui::Begin(name, open, themedWindowFlags(theme) | extraFlags);
    popThemeFont();
    return visible;
}

ImVec2 themedMenuBarPadding(const SlopUiTheme& theme, const SlopUiFonts& fonts) {
    const float fontSize = fonts.size(theme.menuBar.font);
    const float padY = std::max((theme.menuBar.height - fontSize) * 0.5f, 2.0f);
    return ImVec2{theme.framePadding.x, padY};
}

}
