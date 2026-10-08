#pragma once

#include <flecs.h>
#include <imgui.h>

#include <array>
#include <string>
#include <string_view>

struct s7_scheme;

namespace slopengine {

class AssetStore;

struct ThemeColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    ImVec4 vec() const { return ImVec4{r, g, b, a}; }
    ImU32 u32() const { return ImGui::ColorConvertFloat4ToU32(vec()); }
    ThemeColor withAlpha(float alpha) const { return ThemeColor{r, g, b, alpha}; }
};

ThemeColor themeMix(const ThemeColor& a, const ThemeColor& b, float t);
ThemeColor themeLighten(const ThemeColor& c, float t);
ThemeColor themeDarken(const ThemeColor& c, float t);

enum class ThemeFontSlot {
    Normal,
    Bold,
    Heading,
    Mono,
    Count,
};

bool parseThemeFontSlot(std::string_view name, ThemeFontSlot& out);
const char* themeFontSlotName(ThemeFontSlot slot);

struct ThemeFont {
    std::string path;
    float size = 0.0f;
};

struct ThemeColorRef {
    ThemeColor color;
    std::string token;
    bool set = false;
};

struct SlopUiTheme {
    std::array<ThemeFont, static_cast<std::size_t>(ThemeFontSlot::Count)> fonts;

    ThemeColor bg;
    ThemeColor surface;
    ThemeColor accent;
    ThemeColor text;
    ThemeColor textDim;
    ThemeColor danger;

    float rounding = 0.0f;
    float border = 1.0f;
    float frameBorder = 0.0f;
    ImVec2 padding{12.0f, 10.0f};
    ImVec2 framePadding{8.0f, 5.0f};
    ImVec2 spacing{8.0f, 6.0f};
    float buttonHeight = 32.0f;

    struct MenuBar {
        float height = 30.0f;
        ThemeFontSlot font = ThemeFontSlot::Normal;
        ThemeColorRef bg;
        bool icons = true;
        bool versionLabel = true;
    } menuBar;

    struct Window {
        ThemeFontSlot titleFont = ThemeFontSlot::Bold;
        bool titleCentered = true;
        ThemeColorRef titleBg;
        ThemeColorRef titleBgActive;
        bool draggable = true;
    } window;

    struct Chrome {
        std::string pauseTitle = "Paused";
        float pauseDim = 0.5f;
        float pauseWidth = 320.0f;
    } chrome;

    const ThemeFont& font(ThemeFontSlot slot) const {
        return fonts[static_cast<std::size_t>(slot)];
    }
    ThemeFont& font(ThemeFontSlot slot) { return fonts[static_cast<std::size_t>(slot)]; }

    bool paletteColor(std::string_view token, ThemeColor& out) const;
    ThemeColor resolve(const ThemeColorRef& ref, const ThemeColor& fallback) const;
};

SlopUiTheme defaultSlopUiTheme();

void overlayPackageThemeFromScheme(s7_scheme* scheme, SlopUiTheme& theme);

SlopUiTheme loadSlopUiTheme(AssetStore& assets, s7_scheme* scheme);

struct SlopUiFonts {
    std::array<ImFont*, static_cast<std::size_t>(ThemeFontSlot::Count)> fonts{};
    std::array<float, static_cast<std::size_t>(ThemeFontSlot::Count)> sizes{};

    ImFont* font(ThemeFontSlot slot) const { return fonts[static_cast<std::size_t>(slot)]; }
    float size(ThemeFontSlot slot) const { return sizes[static_cast<std::size_t>(slot)]; }
};

struct SlopUiThemeReload {
    bool pending = false;
};

void applySlopUiTheme(const SlopUiTheme& theme, AssetStore& assets, SlopUiFonts& fonts);

void installSlopUiTheme(flecs::world& world, AssetStore& assets, s7_scheme* scheme);

void pushThemeFont(const SlopUiFonts& fonts, ThemeFontSlot slot);
void popThemeFont();

ImGuiWindowFlags themedWindowFlags(const SlopUiTheme& theme);

bool beginThemedWindow(
    const SlopUiTheme& theme,
    const SlopUiFonts& fonts,
    const char* name,
    bool* open = nullptr,
    ImGuiWindowFlags extraFlags = 0);

ImVec2 themedMenuBarPadding(const SlopUiTheme& theme, const SlopUiFonts& fonts);

}
