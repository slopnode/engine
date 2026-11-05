#include "ui/slopui_theme.hpp"

#include "assets/asset_store.hpp"
#include "core/package.hpp"
#include "ui/imgui_fonts.hpp"

#include <raylib.h>
#include <s7.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace slopengine {

namespace {

constexpr const char* kFontSlotNames[] = {"normal", "bold", "heading", "mono"};

ThemeColor rgb(int r, int g, int b) {
    return ThemeColor{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
}

ThemePaint flatPaint(int r, int g, int b) {
    ThemePaint paint;
    paint.a.color = rgb(r, g, b);
    paint.a.set = true;
    paint.set = true;
    return paint;
}

ThemePaint gradientPaint(int r0, int g0, int b0, int r1, int g1, int b1) {
    ThemePaint paint = flatPaint(r0, g0, b0);
    paint.b.color = rgb(r1, g1, b1);
    paint.b.set = true;
    paint.gradient = true;
    return paint;
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

bool readString(s7_pointer rest, std::string& out, const char* section, const char* key) {
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        TraceLog(LOG_WARNING, "THEME: %s.%s expects a string", section, key);
        return false;
    }
    out = s7_string(s7_car(rest));
    return true;
}

bool readColorCells(s7_scheme* sc, s7_pointer cells, ThemeColorRef& out) {
    if (s7_is_pair(cells) && s7_is_pair(s7_car(cells)) && s7_is_number(s7_car(s7_car(cells)))) {
        cells = s7_car(cells);
    }
    if (s7_is_pair(cells) && s7_is_symbol(s7_car(cells))) {
        out.token = s7_symbol_name(s7_car(cells));
        out.set = true;
        return true;
    }
    float channels[4] = {0.0f, 0.0f, 0.0f, 255.0f};
    int count = 0;
    for (s7_pointer cursor = cells; s7_is_pair(cursor) && count < 4; cursor = s7_cdr(cursor)) {
        if (!s7_is_number(s7_car(cursor))) {
            break;
        }
        channels[count++] = static_cast<float>(s7_number_to_real(sc, s7_car(cursor)));
    }
    if (count < 3) {
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

bool readColorRef(s7_scheme* sc, s7_pointer rest, ThemeColorRef& out, const char* section, const char* key) {
    if (!readColorCells(sc, rest, out)) {
        TraceLog(LOG_WARNING, "THEME: %s.%s expects r g b [a] or a palette token", section, key);
        return false;
    }
    return true;
}

bool readGradientStop(s7_scheme* sc, s7_pointer cell, ThemeColorRef& out) {
    if (!s7_is_pair(cell)) {
        return false;
    }
    const s7_pointer value = s7_car(cell);
    if (s7_is_symbol(value)) {
        return readColorCells(sc, cell, out);
    }
    if (s7_is_pair(value)) {
        return readColorCells(sc, value, out);
    }
    return false;
}

bool readPaint(s7_scheme* sc, s7_pointer rest, ThemePaint& out, const char* section, const char* key) {
    if (s7_is_pair(rest) && s7_is_pair(s7_car(rest)) && s7_is_symbol(s7_car(s7_car(rest))) &&
        std::strcmp(s7_symbol_name(s7_car(s7_car(rest))), "gradient") == 0) {
        const s7_pointer stops = s7_cdr(s7_car(rest));
        ThemePaint paint;
        if (!readGradientStop(sc, stops, paint.a) ||
            !s7_is_pair(stops) || !readGradientStop(sc, s7_cdr(stops), paint.b)) {
            TraceLog(LOG_WARNING, "THEME: %s.%s gradient expects two color stops", section, key);
            return false;
        }
        const s7_pointer direction = s7_is_pair(stops) && s7_is_pair(s7_cdr(stops)) ? s7_cdr(s7_cdr(stops)) : s7_nil(sc);
        if (s7_is_pair(direction) && s7_is_symbol(s7_car(direction))) {
            const char* name = s7_symbol_name(s7_car(direction));
            if (std::strcmp(name, "horizontal") == 0) {
                paint.horizontal = true;
            } else if (std::strcmp(name, "vertical") != 0) {
                TraceLog(LOG_WARNING, "THEME: %s.%s gradient direction must be vertical or horizontal", section, key);
            }
        }
        paint.gradient = true;
        paint.set = true;
        out = paint;
        return true;
    }
    ThemePaint paint;
    if (!readColorCells(sc, rest, paint.a)) {
        TraceLog(LOG_WARNING, "THEME: %s.%s expects r g b [a], a palette token, or (gradient a b)",
            section, key);
        return false;
    }
    paint.gradient = false;
    paint.set = true;
    out = paint;
    return true;
}

bool readPaletteColor(s7_scheme* sc, s7_pointer rest, ThemeColor& out, const char* key) {
    ThemeColorRef ref;
    if (!readColorCells(sc, rest, ref) || !ref.token.empty()) {
        TraceLog(LOG_WARNING, "THEME: colors.%s expects literal r g b [a]", key);
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

bool readSkin(s7_scheme* sc, s7_pointer rest, ThemeSkin& out, const char* key) {
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        TraceLog(LOG_WARNING, "THEME: skin.%s expects a texture path string", key);
        return false;
    }
    ThemeSkin skin;
    skin.texture = s7_string(s7_car(rest));
    skin.set = true;
    for (s7_pointer cursor = s7_cdr(rest); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer opt = s7_car(cursor);
        if (!s7_is_pair(opt) || !s7_is_symbol(s7_car(opt))) {
            continue;
        }
        const char* name = s7_symbol_name(s7_car(opt));
        const s7_pointer values = s7_cdr(opt);
        if (std::strcmp(name, "slice") == 0) {
            float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            int count = 0;
            for (s7_pointer c = values; s7_is_pair(c) && count < 4; c = s7_cdr(c)) {
                if (!s7_is_number(s7_car(c))) {
                    break;
                }
                v[count++] = static_cast<float>(s7_number_to_real(sc, s7_car(c)));
            }
            if (count == 1) {
                skin.left = skin.top = skin.right = skin.bottom = v[0];
            } else if (count == 4) {
                skin.left = v[0];
                skin.top = v[1];
                skin.right = v[2];
                skin.bottom = v[3];
            } else {
                TraceLog(LOG_WARNING, "THEME: skin.%s slice expects 1 or 4 numbers", key);
            }
        } else if (std::strcmp(name, "states") == 0) {
            float states = 1.0f;
            if (readReal(sc, values, states)) {
                skin.states = std::max(1, static_cast<int>(states));
            }
        } else if (std::strcmp(name, "scale") == 0) {
            float scale = 1.0f;
            if (readReal(sc, values, scale)) {
                skin.scale = std::max(scale, 0.01f);
            }
        } else {
            TraceLog(LOG_WARNING, "THEME: skin.%s unknown option '%s'", key, name);
        }
    }
    out = skin;
    return true;
}

template <typename Fn>
void forEachEntry(s7_pointer section, Fn&& fn) {
    for (s7_pointer cursor = section; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            continue;
        }
        fn(s7_symbol_name(s7_car(entry)), s7_cdr(entry));
    }
}

void parseFonts(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    forEachEntry(section, [&](const char* key, s7_pointer rest) {
        ThemeFontSlot slot;
        if (!parseThemeFontSlot(key, slot)) {
            TraceLog(LOG_WARNING, "THEME: unknown font slot '%s'", key);
            return;
        }
        if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
            TraceLog(LOG_WARNING, "THEME: fonts.%s expects a path string", key);
            return;
        }
        ThemeFont& font = theme.font(slot);
        font.path = s7_string(s7_car(rest));
        float size = 0.0f;
        if (readReal(sc, s7_cdr(rest), size)) {
            font.size = size;
        }
    });
}

void parseColors(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    forEachEntry(section, [&](const char* key, s7_pointer rest) {
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
            return;
        }
        readPaletteColor(sc, rest, *target, key);
    });
}

void parseMetrics(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    forEachEntry(section, [&](const char* key, s7_pointer rest) {
        float value = 0.0f;
        if (std::strcmp(key, "rounding") == 0) {
            if (readReal(sc, rest, value)) theme.rounding = std::max(value, 0.0f);
        } else if (std::strcmp(key, "border") == 0) {
            if (readReal(sc, rest, value)) theme.border = std::max(value, 0.0f);
        } else if (std::strcmp(key, "frame-border") == 0) {
            if (readReal(sc, rest, value)) theme.frameBorder = std::max(value, 0.0f);
        } else if (std::strcmp(key, "bevel") == 0) {
            if (readReal(sc, rest, value)) theme.bevel = std::clamp(value, 0.0f, 4.0f);
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
    });
}

void parseMenuBar(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    forEachEntry(section, [&](const char* key, s7_pointer rest) {
        float value = 0.0f;
        if (std::strcmp(key, "height") == 0) {
            if (readReal(sc, rest, value)) theme.menuBar.height = std::max(value, 0.0f);
        } else if (std::strcmp(key, "font") == 0) {
            readFontSlot(rest, theme.menuBar.font, "menu-bar", key);
        } else if (std::strcmp(key, "bg") == 0) {
            readPaint(sc, rest, theme.menuBar.bg, "menu-bar", key);
        } else if (std::strcmp(key, "icons") == 0) {
            readBool(sc, rest, theme.menuBar.icons, "menu-bar", key);
        } else if (std::strcmp(key, "version-label") == 0) {
            readBool(sc, rest, theme.menuBar.versionLabel, "menu-bar", key);
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown menu-bar key '%s'", key);
        }
    });
}

void parseWindow(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    forEachEntry(section, [&](const char* key, s7_pointer rest) {
        if (std::strcmp(key, "title-font") == 0) {
            readFontSlot(rest, theme.window.titleFont, "window", key);
        } else if (std::strcmp(key, "title-align") == 0) {
            if (!s7_is_pair(rest) || !s7_is_symbol(s7_car(rest))) {
                TraceLog(LOG_WARNING, "THEME: window.title-align expects center or left");
                return;
            }
            const char* align = s7_symbol_name(s7_car(rest));
            if (std::strcmp(align, "center") == 0) {
                theme.window.titleCentered = true;
            } else if (std::strcmp(align, "left") == 0) {
                theme.window.titleCentered = false;
            } else {
                TraceLog(LOG_WARNING, "THEME: window.title-align unknown value '%s'", align);
            }
        } else if (std::strcmp(key, "bg") == 0) {
            readPaint(sc, rest, theme.window.bg, "window", key);
        } else if (std::strcmp(key, "title-bg") == 0) {
            readPaint(sc, rest, theme.window.titleBg, "window", key);
        } else if (std::strcmp(key, "title-bg-active") == 0) {
            readPaint(sc, rest, theme.window.titleBgActive, "window", key);
        } else if (std::strcmp(key, "title-text") == 0) {
            readColorRef(sc, rest, theme.window.titleText, "window", key);
        } else if (std::strcmp(key, "draggable") == 0) {
            readBool(sc, rest, theme.window.draggable, "window", key);
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown window key '%s'", key);
        }
    });
}

void parseButton(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    forEachEntry(section, [&](const char* key, s7_pointer rest) {
        if (std::strcmp(key, "bg") == 0) {
            readPaint(sc, rest, theme.button.bg, "button", key);
        } else if (std::strcmp(key, "bg-hover") == 0) {
            readPaint(sc, rest, theme.button.bgHover, "button", key);
        } else if (std::strcmp(key, "bg-active") == 0) {
            readPaint(sc, rest, theme.button.bgActive, "button", key);
        } else if (std::strcmp(key, "bg-disabled") == 0) {
            readPaint(sc, rest, theme.button.bgDisabled, "button", key);
        } else if (std::strcmp(key, "primary-bg") == 0) {
            readPaint(sc, rest, theme.button.primaryBg, "button", key);
        } else if (std::strcmp(key, "primary-bg-hover") == 0) {
            readPaint(sc, rest, theme.button.primaryBgHover, "button", key);
        } else if (std::strcmp(key, "primary-bg-active") == 0) {
            readPaint(sc, rest, theme.button.primaryBgActive, "button", key);
        } else if (std::strcmp(key, "text") == 0) {
            readColorRef(sc, rest, theme.button.text, "button", key);
        } else if (std::strcmp(key, "text-hover") == 0) {
            readColorRef(sc, rest, theme.button.textHover, "button", key);
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown button key '%s'", key);
        }
    });
}

void parsePanel(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    forEachEntry(section, [&](const char* key, s7_pointer rest) {
        if (std::strcmp(key, "bg") == 0) {
            readPaint(sc, rest, theme.panel.bg, "panel", key);
        } else if (std::strcmp(key, "border") == 0) {
            readColorRef(sc, rest, theme.panel.border, "panel", key);
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown panel key '%s'", key);
        }
    });
}

void parseSkin(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    forEachEntry(section, [&](const char* key, s7_pointer rest) {
        ThemeSkin* target = nullptr;
        if (std::strcmp(key, "window") == 0) {
            target = &theme.skin.window;
        } else if (std::strcmp(key, "title") == 0) {
            target = &theme.skin.title;
        } else if (std::strcmp(key, "button") == 0) {
            target = &theme.skin.button;
        } else if (std::strcmp(key, "primary-button") == 0) {
            target = &theme.skin.primaryButton;
        } else if (std::strcmp(key, "panel") == 0) {
            target = &theme.skin.panel;
        } else if (std::strcmp(key, "menu-bar") == 0) {
            target = &theme.skin.menuBar;
        }
        if (target == nullptr) {
            TraceLog(LOG_WARNING, "THEME: unknown skin element '%s'", key);
            return;
        }
        if (s7_is_pair(rest) && s7_is_boolean(s7_car(rest)) && !s7_boolean(sc, s7_car(rest))) {
            *target = ThemeSkin{};
            return;
        }
        readSkin(sc, rest, *target, key);
    });
}

void parseSounds(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    forEachEntry(section, [&](const char* key, s7_pointer rest) {
        float value = 0.0f;
        if (std::strcmp(key, "hover") == 0) {
            readString(rest, theme.sounds.hover, "sounds", key);
        } else if (std::strcmp(key, "confirm") == 0) {
            readString(rest, theme.sounds.confirm, "sounds", key);
        } else if (std::strcmp(key, "back") == 0) {
            readString(rest, theme.sounds.back, "sounds", key);
        } else if (std::strcmp(key, "volume") == 0) {
            if (readReal(sc, rest, value)) theme.sounds.volume = std::clamp(value, 0.0f, 1.0f);
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown sounds key '%s'", key);
        }
    });
}

void parseChrome(s7_scheme* sc, s7_pointer section, SlopUiTheme& theme) {
    forEachEntry(section, [&](const char* key, s7_pointer rest) {
        float value = 0.0f;
        if (std::strcmp(key, "pause-title") == 0) {
            readString(rest, theme.chrome.pauseTitle, "chrome", key);
        } else if (std::strcmp(key, "pause-dim") == 0) {
            if (readReal(sc, rest, value)) theme.chrome.pauseDim = std::clamp(value, 0.0f, 1.0f);
        } else if (std::strcmp(key, "pause-width") == 0) {
            if (readReal(sc, rest, value)) theme.chrome.pauseWidth = std::max(value, 0.0f);
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown chrome key '%s'", key);
        }
    });
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

float themeLuminance(const ThemeColor& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

bool themeIsLight(const ThemeColor& c) {
    return themeLuminance(c) > 0.5f;
}

ThemeColor themeRaise(const ThemeColor& c, float t) {
    return themeIsLight(c) ? themeDarken(c, t) : themeLighten(c, t);
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
    } else if (token == "border") {
        out = borderColor();
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

ThemePaintResolved SlopUiTheme::resolvePaint(const ThemePaint& paint, const ThemeColor& fallback) const {
    ThemePaintResolved flat;
    flat.a = fallback;
    flat.b = fallback;
    flat.gradient = false;
    return resolvePaint(paint, flat);
}

ThemePaintResolved SlopUiTheme::resolvePaint(
    const ThemePaint& paint,
    const ThemePaintResolved& fallback) const {
    if (!paint.set) {
        return fallback;
    }
    ThemePaintResolved out;
    out.a = resolve(paint.a, fallback.a);
    out.gradient = paint.gradient;
    out.horizontal = paint.horizontal;
    out.b = paint.gradient ? resolve(paint.b, out.a) : out.a;
    return out;
}

ThemeColor SlopUiTheme::borderColor() const {
    return themeIsLight(surface) ? themeDarken(surface, 0.25f) : themeLighten(surface, 0.10f);
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
    theme.menuBar.bg = gradientPaint(40, 42, 50, 29, 31, 38);
    theme.window.titleBg = gradientPaint(36, 38, 46, 24, 25, 30);
    theme.window.titleBgActive = gradientPaint(78, 64, 42, 44, 40, 36);
    return theme;
}

void applyThemeSections(s7_scheme* scheme, s7_pointer catalog, SlopUiTheme& theme, bool allowEditor) {
    forEachEntry(catalog, [&](const char* key, s7_pointer rest) {
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
        } else if (std::strcmp(key, "button") == 0) {
            parseButton(scheme, rest, theme);
        } else if (std::strcmp(key, "panel") == 0) {
            parsePanel(scheme, rest, theme);
        } else if (std::strcmp(key, "skin") == 0) {
            parseSkin(scheme, rest, theme);
        } else if (std::strcmp(key, "sounds") == 0) {
            parseSounds(scheme, rest, theme);
        } else if (std::strcmp(key, "chrome") == 0) {
            parseChrome(scheme, rest, theme);
        } else if (std::strcmp(key, "name") == 0) {
        } else if (std::strcmp(key, "imgui-style") == 0) {
            if (s7_is_pair(rest) && s7_is_symbol(s7_car(rest))) {
                theme.imguiStyle = s7_symbol_name(s7_car(rest));
            } else {
                TraceLog(LOG_WARNING, "THEME: imgui-style expects dark, light or classic");
            }
        } else if (std::strcmp(key, "editor") == 0) {
            if (!allowEditor) {
                TraceLog(LOG_WARNING, "THEME: editor section cannot be nested");
            }
        } else {
            TraceLog(LOG_WARNING, "THEME: unknown section '%s'", key);
        }
    });
}

void overlayPackageThemeFromScheme(s7_scheme* scheme, SlopUiTheme& theme, bool editor) {
    if (scheme == nullptr) {
        return;
    }
    const s7_pointer catalog = s7_name_to_value(scheme, "*package-theme*");
    if (catalog == s7_undefined(scheme) || !s7_is_pair(catalog)) {
        return;
    }
    applyThemeSections(scheme, catalog, theme, true);
    if (!editor) {
        return;
    }
    forEachEntry(catalog, [&](const char* key, s7_pointer rest) {
        if (std::strcmp(key, "editor") == 0) {
            applyThemeSections(scheme, rest, theme, false);
        }
    });
}

namespace {

std::string themeNameFromScheme(s7_scheme* scheme) {
    const s7_pointer catalog = s7_name_to_value(scheme, "*package-theme*");
    std::string name;
    if (catalog == s7_undefined(scheme) || !s7_is_pair(catalog)) {
        return name;
    }
    forEachEntry(catalog, [&](const char* key, s7_pointer rest) {
        if (std::strcmp(key, "name") == 0 && s7_is_pair(rest) && s7_is_string(s7_car(rest))) {
            name = s7_string(s7_car(rest));
        }
    });
    return name;
}

bool loadNamedTheme(AssetStore& assets, s7_scheme* scheme, std::string_view themeId) {
    const std::vector<Package>& packages = assets.packages();
    const std::string path = "themes/" + std::string{themeId};
    for (auto it = packages.rbegin(); it != packages.rend(); ++it) {
        s7_define_variable(scheme, "*package-theme*", s7_f(scheme));
        if (assets.loadDataFromPackage(scheme, it->meta().id, path)) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<SlopUiThemeEntry> listSlopUiThemes(AssetStore& assets, s7_scheme* scheme) {
    std::vector<std::string> ids;
    for (const Package& package : assets.packages()) {
        const std::filesystem::path dir = package.root() / "data" / "themes";
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file() || it->path().extension() != ".s7") {
                continue;
            }
            const std::string id = it->path().stem().string();
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
                ids.push_back(id);
            }
        }
    }
    std::vector<SlopUiThemeEntry> entries;
    for (const std::string& id : ids) {
        SlopUiThemeEntry entry{id, id};
        if (scheme != nullptr && loadNamedTheme(assets, scheme, id)) {
            const std::string name = themeNameFromScheme(scheme);
            if (!name.empty()) {
                entry.name = name;
            }
        }
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const SlopUiThemeEntry& a, const SlopUiThemeEntry& b) {
        return a.name < b.name;
    });
    return entries;
}

SlopUiTheme loadSlopUiTheme(AssetStore& assets, s7_scheme* scheme, bool editor, std::string_view themeId) {
    SlopUiTheme theme = defaultSlopUiTheme();
    if (scheme == nullptr) {
        return theme;
    }
    if (!themeId.empty()) {
        if (loadNamedTheme(assets, scheme, themeId)) {
            overlayPackageThemeFromScheme(scheme, theme, editor);
            TraceLog(LOG_INFO, "THEME: applied data/themes/%.*s.s7", static_cast<int>(themeId.size()), themeId.data());
        } else if (themeId != kDefaultSlopUiThemeId) {
            TraceLog(LOG_WARNING, "THEME: theme '%.*s' not found, using '%s'",
                static_cast<int>(themeId.size()), themeId.data(), kDefaultSlopUiThemeId);
            return loadSlopUiTheme(assets, scheme, editor, kDefaultSlopUiThemeId);
        }
    }
    for (const Package& package : assets.packages()) {
        const std::string& id = package.meta().id;
        s7_define_variable(scheme, "*package-theme*", s7_f(scheme));
        if (!assets.loadDataFromPackage(scheme, id, "theme")) {
            continue;
        }
        overlayPackageThemeFromScheme(scheme, theme, editor);
        TraceLog(LOG_INFO, "THEME: applied data/theme.s7 from '%s'", id.c_str());
    }
    return theme;
}

namespace {

std::string colorLiteral(const ThemeColor& c) {
    std::ostringstream out;
    out << std::lround(c.r * 255.0f) << ' ' << std::lround(c.g * 255.0f) << ' ' << std::lround(c.b * 255.0f);
    if (c.a < 0.999f) {
        out << ' ' << std::lround(c.a * 255.0f);
    }
    return out.str();
}

std::string colorRefText(const ThemeColorRef& ref) {
    if (!ref.token.empty()) {
        return ref.token;
    }
    return colorLiteral(ref.color);
}

std::string gradientStopText(const ThemeColorRef& ref) {
    if (!ref.token.empty()) {
        return ref.token;
    }
    return "(" + colorLiteral(ref.color) + ")";
}

std::string paintText(const ThemePaint& paint) {
    if (paint.gradient) {
        return "(gradient " + gradientStopText(paint.a) + " " + gradientStopText(paint.b) +
            (paint.horizontal ? " horizontal)" : ")");
    }
    return colorRefText(paint.a);
}

std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (const char ch : text) {
        if (ch == '"' || ch == '\\') {
            out += '\\';
        }
        out += ch;
    }
    out += '"';
    return out;
}

std::string numberText(float value) {
    std::ostringstream out;
    if (std::fabs(value - std::round(value)) < 0.0005f) {
        out << static_cast<long>(std::lround(value));
    } else {
        out << value;
    }
    return out.str();
}

void emitPaint(std::ostringstream& out, const char* key, const ThemePaint& paint) {
    if (paint.set) {
        out << "      (" << key << ' ' << paintText(paint) << ")\n";
    }
}

void emitColorRef(std::ostringstream& out, const char* key, const ThemeColorRef& ref) {
    if (ref.set) {
        out << "      (" << key << ' ' << colorRefText(ref) << ")\n";
    }
}

void emitSkin(std::ostringstream& out, const char* key, const ThemeSkin& skin) {
    if (!skin.set) {
        return;
    }
    out << "      (" << key << ' ' << quoted(skin.texture) << " (slice " << numberText(skin.left) << ' '
        << numberText(skin.top) << ' ' << numberText(skin.right) << ' ' << numberText(skin.bottom) << ")";
    if (skin.states > 1) {
        out << " (states " << skin.states << ")";
    }
    if (std::fabs(skin.scale - 1.0f) > 0.0005f) {
        out << " (scale " << numberText(skin.scale) << ")";
    }
    out << ")\n";
}

} // namespace

std::string serializeSlopUiTheme(const SlopUiTheme& theme) {
    std::ostringstream out;
    out << "(define *package-theme*\n  '((fonts\n";
    for (std::size_t i = 0; i < static_cast<std::size_t>(ThemeFontSlot::Count); ++i) {
        const ThemeFont& font = theme.fonts[i];
        if (font.path.empty()) {
            continue;
        }
        out << "      (" << themeFontSlotName(static_cast<ThemeFontSlot>(i)) << ' ' << quoted(font.path);
        if (font.size > 0.0f) {
            out << ' ' << numberText(font.size);
        }
        out << ")\n";
    }
    out << "      )\n    (colors\n";
    out << "      (bg " << colorLiteral(theme.bg) << ")\n";
    out << "      (surface " << colorLiteral(theme.surface) << ")\n";
    out << "      (accent " << colorLiteral(theme.accent) << ")\n";
    out << "      (text " << colorLiteral(theme.text) << ")\n";
    out << "      (text-dim " << colorLiteral(theme.textDim) << ")\n";
    out << "      (danger " << colorLiteral(theme.danger) << ")\n";
    out << "      )\n";
    if (!theme.imguiStyle.empty()) {
        out << "    (imgui-style " << theme.imguiStyle << ")\n";
    }
    out << "    (metrics\n";
    out << "      (rounding " << numberText(theme.rounding) << ")\n";
    out << "      (border " << numberText(theme.border) << ")\n";
    out << "      (frame-border " << numberText(theme.frameBorder) << ")\n";
    out << "      (bevel " << numberText(theme.bevel) << ")\n";
    out << "      (padding " << numberText(theme.padding.x) << ' ' << numberText(theme.padding.y) << ")\n";
    out << "      (frame-padding " << numberText(theme.framePadding.x) << ' ' << numberText(theme.framePadding.y) << ")\n";
    out << "      (spacing " << numberText(theme.spacing.x) << ' ' << numberText(theme.spacing.y) << ")\n";
    out << "      (button-height " << numberText(theme.buttonHeight) << ")\n";
    out << "      )\n    (menu-bar\n";
    out << "      (height " << numberText(theme.menuBar.height) << ")\n";
    out << "      (font " << themeFontSlotName(theme.menuBar.font) << ")\n";
    emitPaint(out, "bg", theme.menuBar.bg);
    out << "      (icons " << (theme.menuBar.icons ? "#t" : "#f") << ")\n";
    out << "      (version-label " << (theme.menuBar.versionLabel ? "#t" : "#f") << ")\n";
    out << "      )\n    (window\n";
    out << "      (title-font " << themeFontSlotName(theme.window.titleFont) << ")\n";
    out << "      (title-align " << (theme.window.titleCentered ? "center" : "left") << ")\n";
    emitPaint(out, "bg", theme.window.bg);
    emitPaint(out, "title-bg", theme.window.titleBg);
    emitPaint(out, "title-bg-active", theme.window.titleBgActive);
    emitColorRef(out, "title-text", theme.window.titleText);
    out << "      (draggable " << (theme.window.draggable ? "#t" : "#f") << ")\n";
    out << "      )\n    (button\n";
    emitPaint(out, "bg", theme.button.bg);
    emitPaint(out, "bg-hover", theme.button.bgHover);
    emitPaint(out, "bg-active", theme.button.bgActive);
    emitPaint(out, "bg-disabled", theme.button.bgDisabled);
    emitPaint(out, "primary-bg", theme.button.primaryBg);
    emitPaint(out, "primary-bg-hover", theme.button.primaryBgHover);
    emitPaint(out, "primary-bg-active", theme.button.primaryBgActive);
    emitColorRef(out, "text", theme.button.text);
    emitColorRef(out, "text-hover", theme.button.textHover);
    out << "      )\n    (panel\n";
    emitPaint(out, "bg", theme.panel.bg);
    emitColorRef(out, "border", theme.panel.border);
    out << "      )\n    (skin\n";
    emitSkin(out, "window", theme.skin.window);
    emitSkin(out, "title", theme.skin.title);
    emitSkin(out, "button", theme.skin.button);
    emitSkin(out, "primary-button", theme.skin.primaryButton);
    emitSkin(out, "panel", theme.skin.panel);
    emitSkin(out, "menu-bar", theme.skin.menuBar);
    out << "      )\n    (sounds\n";
    if (!theme.sounds.hover.empty()) {
        out << "      (hover " << quoted(theme.sounds.hover) << ")\n";
    }
    if (!theme.sounds.confirm.empty()) {
        out << "      (confirm " << quoted(theme.sounds.confirm) << ")\n";
    }
    if (!theme.sounds.back.empty()) {
        out << "      (back " << quoted(theme.sounds.back) << ")\n";
    }
    out << "      (volume " << numberText(theme.sounds.volume) << ")\n";
    out << "      )\n    (chrome\n";
    out << "      (pause-title " << quoted(theme.chrome.pauseTitle) << ")\n";
    out << "      (pause-dim " << numberText(theme.chrome.pauseDim) << ")\n";
    out << "      (pause-width " << numberText(theme.chrome.pauseWidth) << ")\n";
    out << "      )))\n";
    return out.str();
}

bool exportSlopUiTheme(const SlopUiTheme& theme, const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        TraceLog(LOG_WARNING, "THEME: cannot write %s", path.string().c_str());
        return false;
    }
    file << serializeSlopUiTheme(theme);
    if (!file) {
        TraceLog(LOG_WARNING, "THEME: write failed for %s", path.string().c_str());
        return false;
    }
    TraceLog(LOG_INFO, "THEME: exported %s", path.string().c_str());
    return true;
}

}
