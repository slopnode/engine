#include "test_assert.hpp"

#include "assets/asset_store.hpp"
#include "game/app_config.hpp"
#include "game/ui_settings.hpp"
#include "ui/slopui_theme.hpp"

#include <s7.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace slopengine {

namespace {

bool near(float a, float b) {
    return std::fabs(a - b) < 0.003f;
}

bool sameColor(const ThemeColor& a, const ThemeColor& b) {
    return near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b) && near(a.a, b.a);
}

SlopUiTheme overlayFromText(const std::string& text) {
    SlopUiTheme theme = defaultSlopUiTheme();
    s7_scheme* sc = s7_init();
    s7_eval_c_string(sc, text.c_str());
    overlayPackageThemeFromScheme(sc, theme);
    s7_quit(sc);
    return theme;
}

void writeFile(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
}

void runNamedThemeTests() {
    std::random_device rd;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("sloptest-themes-" + std::to_string(rd()));
    writeFile(
        root / "package.meta",
        "(package\n"
        "  (id \"sloptest-themes\")\n"
        "  (name \"Themes\")\n"
        "  (version \"0.1.0\")\n"
        "  (depends))\n");
    writeFile(
        root / "data" / "themes" / "neon.s7",
        "(define *package-theme* '((name \"Neon\") (colors (accent 1 255 2))))\n");
    writeFile(root / "data" / "theme.s7", "(define *package-theme* '((colors (danger 9 8 7))))\n");

    AppConfig config{};
    config.game = root;
    AssetStore assets(config);
    s7_scheme* sc = s7_init();

    const std::vector<SlopUiThemeEntry> themes = listSlopUiThemes(assets, sc);
    auto named = [&](const std::string& id) {
        const auto it = std::find_if(themes.begin(), themes.end(), [&](const SlopUiThemeEntry& e) {
            return e.id == id;
        });
        return it == themes.end() ? std::string{} : it->name;
    };
    CHECK_EQ(named("win95"), std::string{"Win95"});
    CHECK_EQ(named("imgui"), std::string{"Imgui"});
    CHECK_EQ(named("tango"), std::string{"Tango"});
    CHECK_EQ(named("modern"), std::string{"Modern"});
    CHECK_EQ(named("neon"), std::string{"Neon"});

    const ThemeColor danger{9 / 255.0f, 8 / 255.0f, 7 / 255.0f, 1.0f};
    const SlopUiTheme neon = loadSlopUiTheme(assets, sc, false, "neon");
    CHECK(sameColor(neon.accent, ThemeColor{1 / 255.0f, 1.0f, 2 / 255.0f, 1.0f}));
    CHECK(sameColor(neon.danger, danger));

    const SlopUiTheme win95 = loadSlopUiTheme(assets, sc);
    CHECK(sameColor(win95.accent, ThemeColor{0.0f, 0.0f, 128 / 255.0f, 1.0f}));
    CHECK(sameColor(win95.danger, danger));
    CHECK(win95.imguiStyle.empty());

    const SlopUiTheme missing = loadSlopUiTheme(assets, sc, false, "no-such-theme");
    CHECK(sameColor(missing.accent, win95.accent));

    const SlopUiTheme imgui = loadSlopUiTheme(assets, sc, false, "imgui");
    CHECK_EQ(imgui.imguiStyle, std::string{"dark"});
    CHECK_EQ(imgui.font(ThemeFontSlot::Normal).path, std::string{"imgui:default"});

    const SlopUiTheme tango = loadSlopUiTheme(assets, sc, false, "tango");
    CHECK(tango.window.titleBgActive.gradient);
    CHECK(near(tango.rounding, 3.0f));

    const SlopUiTheme modern = loadSlopUiTheme(assets, sc, false, "modern");
    CHECK(near(modern.rounding, 6.0f));
    CHECK(near(modern.border, 0.0f));
    CHECK(near(modern.bevel, 0.0f));

    writeFile(
        root / "settings.cfg",
        "[graphics]\ntheme=modern\n\n[ui]\n  theme = tango  \n\n[controls]\ntheme=imgui\n");
    CHECK_EQ(loadUiSettings(root / "settings.cfg").theme, std::string{"tango"});
    CHECK_EQ(loadUiSettings(root / "missing.cfg").theme, std::string{kDefaultSlopUiThemeId});

    s7_quit(sc);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

} // namespace

void runSlopUiThemeTests() {
    runNamedThemeTests();
    {
        const SlopUiTheme theme = overlayFromText(
            "(define *package-theme* '((colors (accent 10 20 30))"
            " (window (title-bg (gradient accent (1 2 3) horizontal)) (title-text text-dim) (title-align left))"
            " (menu-bar (bg surface) (icons #f))"
            " (skin (button \"ui/button\" (slice 4) (states 4) (scale 2))"
            "       (title \"ui/title\" (slice 1 2 3 4)))"
            " (sounds (hover \"ui/hover\") (volume 0.5))"
            " (chrome (pause-title \"Game Paused\") (pause-dim 0.25))))");

        CHECK(sameColor(theme.accent, ThemeColor{10 / 255.0f, 20 / 255.0f, 30 / 255.0f, 1.0f}));
        CHECK(theme.window.titleBg.set);
        CHECK(theme.window.titleBg.gradient);
        CHECK_EQ(theme.window.titleBg.a.token, std::string{"accent"});
        CHECK(theme.window.titleBg.horizontal);
        CHECK(sameColor(theme.window.titleBg.b.color, ThemeColor{1 / 255.0f, 2 / 255.0f, 3 / 255.0f, 1.0f}));
        CHECK_EQ(theme.window.titleText.token, std::string{"text-dim"});
        CHECK_FALSE(theme.window.titleCentered);
        CHECK_EQ(theme.menuBar.bg.a.token, std::string{"surface"});
        CHECK_FALSE(theme.menuBar.bg.gradient);
        CHECK_FALSE(theme.menuBar.icons);
        CHECK(theme.skin.button.set);
        CHECK_EQ(theme.skin.button.texture, std::string{"ui/button"});
        CHECK(near(theme.skin.button.left, 4.0f) && near(theme.skin.button.bottom, 4.0f));
        CHECK_EQ(theme.skin.button.states, 4);
        CHECK(near(theme.skin.button.scale, 2.0f));
        CHECK(near(theme.skin.title.left, 1.0f) && near(theme.skin.title.top, 2.0f));
        CHECK(near(theme.skin.title.right, 3.0f) && near(theme.skin.title.bottom, 4.0f));
        CHECK_EQ(theme.skin.title.states, 1);
        CHECK_EQ(theme.sounds.hover, std::string{"ui/hover"});
        CHECK(near(theme.sounds.volume, 0.5f));
        CHECK_EQ(theme.chrome.pauseTitle, std::string{"Game Paused"});
        CHECK(near(theme.chrome.pauseDim, 0.25f));

        const ThemePaintResolved resolved = theme.resolvePaint(theme.window.titleBg, theme.bg);
        CHECK(resolved.gradient);
        CHECK(sameColor(resolved.a, theme.accent));

        ThemeColor border;
        CHECK(theme.paletteColor("border", border));
        CHECK(sameColor(border, theme.borderColor()));
    }

    {
        SlopUiTheme original = defaultSlopUiTheme();
        original.accent = ThemeColor{0.5f, 0.25f, 0.125f, 1.0f};
        original.rounding = 6.0f;
        original.bevel = 2.0f;
        original.window.titleBg.a.color = ThemeColor{0.1f, 0.1f, 0.4f, 1.0f};
        original.window.titleBg.a.set = true;
        original.window.titleBg.b.token = "accent";
        original.window.titleBg.b.set = true;
        original.window.titleBg.gradient = true;
        original.window.titleBg.horizontal = true;
        original.window.titleBg.set = true;
        original.padding = ImVec2{9.0f, 7.0f};
        original.menuBar.icons = false;
        original.menuBar.font = ThemeFontSlot::Bold;
        original.window.titleCentered = false;
        original.window.titleText.token = "accent";
        original.window.titleText.set = true;
        original.button.primaryBg.a.token = "accent";
        original.button.primaryBg.a.set = true;
        original.button.primaryBg.b.color = ThemeColor{0.1f, 0.2f, 0.3f, 1.0f};
        original.button.primaryBg.b.set = true;
        original.button.primaryBg.gradient = true;
        original.button.primaryBg.set = true;
        original.panel.bg.a.color = ThemeColor{0.2f, 0.2f, 0.2f, 0.5f};
        original.panel.bg.a.set = true;
        original.panel.bg.set = true;
        original.skin.panel.texture = "ui/panel";
        original.skin.panel.left = original.skin.panel.right = 3.0f;
        original.skin.panel.top = original.skin.panel.bottom = 5.0f;
        original.skin.panel.states = 2;
        original.skin.panel.scale = 1.5f;
        original.skin.panel.set = true;
        original.sounds.confirm = "ui/confirm";
        original.sounds.volume = 0.75f;
        original.chrome.pauseTitle = "Say \"Pause\"";
        original.chrome.pauseWidth = 400.0f;
        original.fonts[static_cast<std::size_t>(ThemeFontSlot::Heading)].size = 36.0f;

        const std::string text = serializeSlopUiTheme(original);
        const SlopUiTheme loaded = overlayFromText(text);

        CHECK(sameColor(loaded.accent, original.accent));
        CHECK(near(loaded.rounding, 6.0f));
        CHECK(near(loaded.bevel, 2.0f));
        CHECK(loaded.window.titleBg.gradient);
        CHECK(loaded.window.titleBg.horizontal);
        CHECK_EQ(loaded.window.titleBg.b.token, std::string{"accent"});
        CHECK(near(loaded.padding.x, 9.0f) && near(loaded.padding.y, 7.0f));
        CHECK_FALSE(loaded.menuBar.icons);
        CHECK(loaded.menuBar.font == ThemeFontSlot::Bold);
        CHECK_FALSE(loaded.window.titleCentered);
        CHECK_EQ(loaded.window.titleText.token, std::string{"accent"});
        CHECK(loaded.button.primaryBg.gradient);
        CHECK_EQ(loaded.button.primaryBg.a.token, std::string{"accent"});
        CHECK(sameColor(loaded.button.primaryBg.b.color, original.button.primaryBg.b.color));
        CHECK(loaded.panel.bg.set);
        CHECK(sameColor(loaded.panel.bg.a.color, original.panel.bg.a.color));
        CHECK_EQ(loaded.skin.panel.texture, std::string{"ui/panel"});
        CHECK(near(loaded.skin.panel.left, 3.0f) && near(loaded.skin.panel.top, 5.0f));
        CHECK_EQ(loaded.skin.panel.states, 2);
        CHECK(near(loaded.skin.panel.scale, 1.5f));
        CHECK_EQ(loaded.sounds.confirm, std::string{"ui/confirm"});
        CHECK(near(loaded.sounds.volume, 0.75f));
        CHECK_EQ(loaded.chrome.pauseTitle, original.chrome.pauseTitle);
        CHECK(near(loaded.chrome.pauseWidth, 400.0f));
        CHECK(near(loaded.fonts[static_cast<std::size_t>(ThemeFontSlot::Heading)].size, 36.0f));
        CHECK(loaded.menuBar.bg.gradient == original.menuBar.bg.gradient);
        CHECK(sameColor(loaded.window.titleBgActive.a.color, original.window.titleBgActive.a.color));
    }
}

}
