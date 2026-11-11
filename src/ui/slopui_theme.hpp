#pragma once

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

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
float themeLuminance(const ThemeColor& c);
bool themeIsLight(const ThemeColor& c);
ThemeColor themeRaise(const ThemeColor& c, float t);

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

struct ThemePaint {
    ThemeColorRef a;
    ThemeColorRef b;
    bool gradient = false;
    bool horizontal = false;
    bool set = false;
};

struct ThemePaintResolved {
    ThemeColor a;
    ThemeColor b;
    bool gradient = false;
    bool horizontal = false;
};

struct ThemeSkin {
    std::string texture;
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    int states = 1;
    float scale = 1.0f;
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

    std::string imguiStyle;

    float rounding = 0.0f;
    float border = 1.0f;
    float frameBorder = 0.0f;
    float bevel = 0.0f;
    ImVec2 padding{10.0f, 8.0f};
    ImVec2 framePadding{8.0f, 4.0f};
    ImVec2 spacing{6.0f, 4.0f};
    float buttonHeight = 30.0f;

    struct MenuBar {
        float height = 28.0f;
        ThemeFontSlot font = ThemeFontSlot::Normal;
        ThemePaint bg;
        bool icons = true;
        bool versionLabel = true;
    } menuBar;

    struct Window {
        ThemeFontSlot titleFont = ThemeFontSlot::Bold;
        bool titleCentered = true;
        ThemePaint bg;
        ThemePaint titleBg;
        ThemePaint titleBgActive;
        ThemeColorRef titleText;
        bool draggable = true;
    } window;

    struct Button {
        ThemePaint bg;
        ThemePaint bgHover;
        ThemePaint bgActive;
        ThemePaint bgDisabled;
        ThemePaint primaryBg;
        ThemePaint primaryBgHover;
        ThemePaint primaryBgActive;
        ThemeColorRef text;
        ThemeColorRef textHover;
    } button;

    struct Panel {
        ThemePaint bg;
        ThemeColorRef border;
    } panel;

    struct Skins {
        ThemeSkin window;
        ThemeSkin title;
        ThemeSkin button;
        ThemeSkin primaryButton;
        ThemeSkin panel;
        ThemeSkin menuBar;
    } skin;

    struct Sounds {
        std::string hover;
        std::string confirm;
        std::string back;
        float volume = 1.0f;
    } sounds;

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
    ThemePaintResolved resolvePaint(const ThemePaint& paint, const ThemeColor& fallback) const;
    ThemePaintResolved resolvePaint(const ThemePaint& paint, const ThemePaintResolved& fallback) const;
    ThemeColor borderColor() const;
};

SlopUiTheme defaultSlopUiTheme();

void overlayPackageThemeFromScheme(s7_scheme* scheme, SlopUiTheme& theme, bool editor = false);

inline constexpr const char* kDefaultSlopUiThemeId = "win95";

struct SlopUiThemeEntry {
    std::string id;
    std::string name;
};

std::vector<SlopUiThemeEntry> listSlopUiThemes(AssetStore& assets, s7_scheme* scheme);

SlopUiTheme loadSlopUiTheme(
    AssetStore& assets,
    s7_scheme* scheme,
    bool editor = false,
    std::string_view themeId = kDefaultSlopUiThemeId);

std::string serializeSlopUiTheme(const SlopUiTheme& theme);

bool exportSlopUiTheme(const SlopUiTheme& theme, const std::filesystem::path& path);

struct SlopUiFonts {
    std::array<ImFont*, static_cast<std::size_t>(ThemeFontSlot::Count)> fonts{};
    std::array<float, static_cast<std::size_t>(ThemeFontSlot::Count)> sizes{};

    ImFont* font(ThemeFontSlot slot) const { return fonts[static_cast<std::size_t>(slot)]; }
    float size(ThemeFontSlot slot) const { return sizes[static_cast<std::size_t>(slot)]; }
};

struct SlopUiThemeReload {
    bool pending = false;
};

struct SlopUiThemeCatalog {
    std::vector<SlopUiThemeEntry> themes;
};

void applySlopUiTheme(const SlopUiTheme& theme, AssetStore& assets, SlopUiFonts& fonts);

void applySlopUiStyle(const SlopUiTheme& theme);

void setActiveSlopUi(const SlopUiTheme* theme, const SlopUiFonts* fonts, AssetStore* assets);
const SlopUiTheme* activeSlopUiTheme();
const SlopUiFonts* activeSlopUiFonts();
AssetStore* activeSlopUiAssets();

void pushThemeFont(const SlopUiFonts& fonts, ThemeFontSlot slot);
void popThemeFont();

ImGuiWindowFlags themedWindowFlags(const SlopUiTheme& theme);

ImVec2 themedMenuBarPadding(const SlopUiTheme& theme, const SlopUiFonts& fonts);

}
