#include "script/hud_script.hpp"
#include "script/script_scope.hpp"

#include "render/components.hpp"
#include "render/hud.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include <s7.h>

namespace slopengine {

namespace {

flecs::world* g_hudWorld = nullptr;
thread_local bool g_hudCanvasOverrideActive = false;
thread_local HudCanvas g_hudCanvasOverride{};

HudDrawList* hudList() {
    if (g_hudWorld == nullptr || !g_hudWorld->has<HudDrawList>()) {
        return nullptr;
    }
    return &g_hudWorld->get_mut<HudDrawList>();
}

HudCanvas hudCanvas() {
    if (g_hudCanvasOverrideActive) {
        return g_hudCanvasOverride;
    }
    HudCanvas canvas{};
    if (g_hudWorld != nullptr && g_hudWorld->has<HudCanvas>()) {
        canvas = g_hudWorld->get<HudCanvas>();
    }
    return canvas;
}

bool parseAnchorSymbol(s7_scheme*, s7_pointer value, HudAnchor& out) {
    const char* name = nullptr;
    if (s7_is_symbol(value)) {
        name = s7_symbol_name(value);
    } else if (s7_is_string(value)) {
        name = s7_string(value);
    }
    if (name == nullptr) {
        return false;
    }

    if (std::strcmp(name, "top-left") == 0) {
        out = HudAnchor::TopLeft;
        return true;
    }
    if (std::strcmp(name, "top-right") == 0) {
        out = HudAnchor::TopRight;
        return true;
    }
    if (std::strcmp(name, "bottom-left") == 0) {
        out = HudAnchor::BottomLeft;
        return true;
    }
    if (std::strcmp(name, "bottom-right") == 0) {
        out = HudAnchor::BottomRight;
        return true;
    }
    if (std::strcmp(name, "center") == 0) {
        out = HudAnchor::Center;
        return true;
    }
    if (std::strcmp(name, "bottom-center") == 0) {
        out = HudAnchor::BottomCenter;
        return true;
    }
    return false;
}

void resolveCmdOrigin(const HudDrawList& list, float offsetX, float offsetY, float& outX, float& outY) {
    const HudCanvas canvas = hudCanvas();
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    hudAnchorPoint(
        list.anchor,
        static_cast<float>(std::max(canvas.width, 1)),
        static_cast<float>(std::max(canvas.height, 1)),
        anchorX,
        anchorY);
    outX = anchorX + offsetX;
    outY = anchorY + offsetY;
}

bool readNumber(s7_scheme* sc, s7_pointer& args, float& out) {
    if (!s7_is_pair(args) || !s7_is_number(s7_car(args))) {
        return false;
    }
    out = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
    args = s7_cdr(args);
    return true;
}

bool readColor(s7_scheme* sc, s7_pointer& args, Color& out) {
    float r = 255.0f;
    float g = 255.0f;
    float b = 255.0f;
    float a = 255.0f;
    if (!readNumber(sc, args, r) || !readNumber(sc, args, g) || !readNumber(sc, args, b)) {
        return false;
    }
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        readNumber(sc, args, a);
    }
    out = Color{
        static_cast<unsigned char>(std::clamp(static_cast<int>(r), 0, 255)),
        static_cast<unsigned char>(std::clamp(static_cast<int>(g), 0, 255)),
        static_cast<unsigned char>(std::clamp(static_cast<int>(b), 0, 255)),
        static_cast<unsigned char>(std::clamp(static_cast<int>(a), 0, 255)),
    };
    return true;
}

s7_pointer g_hud_anchor(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::HudDraw)) {
        return s7_f(sc);
    }
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "hud-anchor", 1, args, "anchor symbol");
    }
    HudAnchor anchor = HudAnchor::TopLeft;
    if (!parseAnchorSymbol(sc, s7_car(args), anchor)) {
        return s7_wrong_type_arg_error(sc, "hud-anchor", 1, s7_car(args), "known anchor symbol");
    }
    list->anchor = anchor;
    return s7_t(sc);
}

s7_pointer g_hud_font(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::HudDraw)) {
        return s7_f(sc);
    }
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "hud-font", 1, args, "font-path string");
    }
    list->fontPath = s7_string(s7_car(args));
    return s7_t(sc);
}

s7_pointer g_hud_rect(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::HudDraw)) {
        return s7_f(sc);
    }
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }

    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    if (!readNumber(sc, args, x) || !readNumber(sc, args, y) || !readNumber(sc, args, w) ||
        !readNumber(sc, args, h)) {
        return s7_wrong_type_arg_error(sc, "hud-rect", 1, args, "x y w h numbers");
    }

    Color color = WHITE;
    if (s7_is_pair(args)) {
        if (!readColor(sc, args, color)) {
            return s7_wrong_type_arg_error(sc, "hud-rect", 5, args, "r g b [a]");
        }
    }

    HudCmd cmd{};
    cmd.kind = HudCmdKind::Rect;
    resolveCmdOrigin(*list, x, y, cmd.x, cmd.y);
    cmd.w = w;
    cmd.h = h;
    cmd.color = color;
    list->commands.push_back(std::move(cmd));
    return s7_t(sc);
}

s7_pointer g_hud_image(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::HudDraw)) {
        return s7_f(sc);
    }
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "hud-image", 1, args, "texture-path string");
    }
    const std::string path = s7_string(s7_car(args));
    args = s7_cdr(args);

    float x = 0.0f;
    float y = 0.0f;
    if (!readNumber(sc, args, x) || !readNumber(sc, args, y)) {
        return s7_wrong_type_arg_error(sc, "hud-image", 2, args, "x y numbers");
    }

    float w = 0.0f;
    float h = 0.0f;
    bool nativeSize = true;
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        s7_pointer peek = s7_cdr(args);
        if (s7_is_pair(peek) && s7_is_number(s7_car(peek))) {
            readNumber(sc, args, w);
            readNumber(sc, args, h);
            nativeSize = false;
        }
    }

    Color color = WHITE;
    if (s7_is_pair(args)) {
        if (!readColor(sc, args, color)) {
            return s7_wrong_type_arg_error(sc, "hud-image", 4, args, "r g b [a]");
        }
    }

    HudCmd cmd{};
    cmd.kind = HudCmdKind::Image;
    resolveCmdOrigin(*list, x, y, cmd.x, cmd.y);
    cmd.w = w;
    cmd.h = h;
    cmd.nativeImageSize = nativeSize;
    cmd.color = color;
    cmd.path = path;
    list->commands.push_back(std::move(cmd));
    return s7_t(sc);
}

s7_pointer g_hud_text(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::HudDraw)) {
        return s7_f(sc);
    }
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "hud-text", 1, args, "text string");
    }
    const std::string text = s7_string(s7_car(args));
    args = s7_cdr(args);

    float x = 0.0f;
    float y = 0.0f;
    float size = 8.0f;
    if (!readNumber(sc, args, x) || !readNumber(sc, args, y) || !readNumber(sc, args, size)) {
        return s7_wrong_type_arg_error(sc, "hud-text", 2, args, "x y size numbers");
    }

    Color color = WHITE;
    if (s7_is_pair(args)) {
        if (!readColor(sc, args, color)) {
            return s7_wrong_type_arg_error(sc, "hud-text", 5, args, "r g b [a]");
        }
    }

    HudCmd cmd{};
    cmd.kind = HudCmdKind::Text;
    resolveCmdOrigin(*list, x, y, cmd.x, cmd.y);
    cmd.size = size;
    cmd.color = color;
    cmd.text = text;
    cmd.fontPath = list->fontPath;
    list->commands.push_back(std::move(cmd));
    return s7_t(sc);
}

} // namespace

void setHudCanvasOverride(int width, int height) {
    g_hudCanvasOverrideActive = true;
    g_hudCanvasOverride.width = std::max(width, 1);
    g_hudCanvasOverride.height = std::max(height, 1);
}

void clearHudCanvasOverride() {
    g_hudCanvasOverrideActive = false;
    g_hudCanvasOverride = {};
}

void bindHudApi(flecs::world& world, s7_scheme* scheme) {
    g_hudWorld = &world;
    if (!world.has<HudDrawList>()) {
        world.set<HudDrawList>({});
    }
    if (!world.has<HudFontCache>()) {
        world.set<HudFontCache>({});
    }
    if (scheme == nullptr) {
        return;
    }

    s7_define_function(scheme, "hud-anchor", g_hud_anchor, 1, 0, false, "(hud-anchor symbol)");
    s7_define_function(scheme, "hud-font", g_hud_font, 1, 0, false, "(hud-font path)");
    s7_define_function(scheme, "hud-rect", g_hud_rect, 4, 4, false, "(hud-rect x y w h r g b [a])");
    s7_define_function(scheme, "hud-image", g_hud_image, 3, 6, false,
                       "(hud-image tex-path x y [w h] [r g b a])");
    s7_define_function(scheme, "hud-text", g_hud_text, 4, 4, false, "(hud-text str x y size [r g b a])");
}

}
