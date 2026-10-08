#include "script/theme_script.hpp"

#include "ui/slopui_theme.hpp"

#include <s7.h>

#include <cmath>

namespace slopengine {

namespace {

flecs::world* g_themeWorld = nullptr;

const SlopUiTheme* currentTheme() {
    if (g_themeWorld == nullptr || !g_themeWorld->has<SlopUiTheme>()) {
        return nullptr;
    }
    return &g_themeWorld->get<SlopUiTheme>();
}

bool readSlot(s7_scheme* sc, s7_pointer args, const char* fn, ThemeFontSlot& slot) {
    const s7_pointer cell = s7_car(args);
    const char* name = nullptr;
    if (s7_is_symbol(cell)) {
        name = s7_symbol_name(cell);
    } else if (s7_is_string(cell)) {
        name = s7_string(cell);
    }
    if (name == nullptr || !parseThemeFontSlot(name, slot)) {
        s7_wrong_type_arg_error(sc, fn, 1, cell, "font slot normal|bold|heading|mono");
        return false;
    }
    return true;
}

s7_pointer g_theme_color(s7_scheme* sc, s7_pointer args) {
    const s7_pointer cell = s7_car(args);
    const char* name = nullptr;
    if (s7_is_symbol(cell)) {
        name = s7_symbol_name(cell);
    } else if (s7_is_string(cell)) {
        name = s7_string(cell);
    }
    if (name == nullptr) {
        return s7_wrong_type_arg_error(sc, "theme-color", 1, cell, "palette token symbol");
    }
    const SlopUiTheme* theme = currentTheme();
    if (theme == nullptr) {
        return s7_f(sc);
    }
    ThemeColor color;
    if (!theme->paletteColor(name, color)) {
        return s7_wrong_type_arg_error(sc, "theme-color", 1, cell, "known palette token");
    }
    return s7_list(
        sc,
        4,
        s7_make_integer(sc, static_cast<s7_int>(std::lround(color.r * 255.0f))),
        s7_make_integer(sc, static_cast<s7_int>(std::lround(color.g * 255.0f))),
        s7_make_integer(sc, static_cast<s7_int>(std::lround(color.b * 255.0f))),
        s7_make_integer(sc, static_cast<s7_int>(std::lround(color.a * 255.0f))));
}

s7_pointer g_theme_font(s7_scheme* sc, s7_pointer args) {
    ThemeFontSlot slot;
    if (!readSlot(sc, args, "theme-font", slot)) {
        return s7_f(sc);
    }
    const SlopUiTheme* theme = currentTheme();
    if (theme == nullptr) {
        return s7_f(sc);
    }
    const ThemeFont& font = theme->font(slot);
    if (font.path.empty()) {
        return s7_make_string(sc, theme->font(ThemeFontSlot::Normal).path.c_str());
    }
    return s7_make_string(sc, font.path.c_str());
}

s7_pointer g_theme_font_size(s7_scheme* sc, s7_pointer args) {
    ThemeFontSlot slot;
    if (!readSlot(sc, args, "theme-font-size", slot)) {
        return s7_f(sc);
    }
    const SlopUiTheme* theme = currentTheme();
    if (theme == nullptr) {
        return s7_f(sc);
    }
    float size = theme->font(slot).size;
    if (size <= 0.0f) {
        size = theme->font(ThemeFontSlot::Normal).size;
    }
    return s7_make_real(sc, size);
}

} // namespace

void bindThemeApi(flecs::world& world, s7_scheme* scheme) {
    g_themeWorld = &world;
    if (scheme == nullptr) {
        return;
    }
    s7_define_function(scheme, "theme-color", g_theme_color, 1, 0, false, "(theme-color token)");
    s7_define_function(scheme, "theme-font", g_theme_font, 1, 0, false, "(theme-font slot)");
    s7_define_function(
        scheme, "theme-font-size", g_theme_font_size, 1, 0, false, "(theme-font-size slot)");
}

}
