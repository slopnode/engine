#include "script/hud_script.hpp"
#include "script/s7_read.hpp"
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

bool parseAnchorName(const char* name, HudAnchor& out) {
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

unsigned char colorByte(float value) {
    return static_cast<unsigned char>(std::clamp(static_cast<int>(value), 0, 255));
}

Color readColor(ArgReader& in) {
    const float r = in.realf("r g b [a]");
    const float g = in.realf("r g b [a]");
    const float b = in.realf("r g b [a]");
    const float a = in.optRealf(255.0f);
    return Color{colorByte(r), colorByte(g), colorByte(b), colorByte(a)};
}

s7_pointer g_hud_anchor(s7_scheme* sc, s7_pointer args) {
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "hud-anchor", args);
    const s7_pointer value = in.peek();
    const char* name = in.name("known anchor symbol");
    HudAnchor anchor = HudAnchor::TopLeft;
    if (!parseAnchorName(name, anchor)) {
        return in.wrongTypeAt(1, value, "known anchor symbol");
    }
    list->anchor = anchor;
    return s7_t(sc);
}

s7_pointer g_hud_font(s7_scheme* sc, s7_pointer args) {
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "hud-font", args);
    list->fontPath = in.string("font-path string");
    return s7_t(sc);
}

s7_pointer g_hud_rect(s7_scheme* sc, s7_pointer args) {
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "hud-rect", args);
    const float x = in.realf("x y w h numbers");
    const float y = in.realf("x y w h numbers");
    const float w = in.realf("x y w h numbers");
    const float h = in.realf("x y w h numbers");
    Color color = WHITE;
    if (in.more()) {
        color = readColor(in);
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
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "hud-image", args);
    const std::string path = in.string("texture-path string");
    const float x = in.realf("x y numbers");
    const float y = in.realf("x y numbers");

    float w = 0.0f;
    float h = 0.0f;
    bool nativeSize = true;
    if (in.nextIsNumber()) {
        const s7_pointer peek = s7_cdr(in.rest());
        if (s7_is_pair(peek) && s7_is_number(s7_car(peek))) {
            w = in.realf("w h numbers");
            h = in.realf("w h numbers");
            nativeSize = false;
        }
    }

    Color color = WHITE;
    if (in.more()) {
        color = readColor(in);
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
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "hud-text", args);
    const std::string text = in.string("text string");
    const float x = in.realf("x y size numbers");
    const float y = in.realf("x y size numbers");
    const float size = in.realf("x y size numbers");
    Color color = WHITE;
    if (in.more()) {
        color = readColor(in);
    }

    HudCmd cmd{};
    cmd.kind = HudCmdKind::Text;
    resolveCmdOrigin(*list, x, y, cmd.x, cmd.y);
    cmd.size = size;
    cmd.color = color;
    cmd.text = text;
    cmd.fontPath = list->fontPath;
    cmd.align = list->textAlign;
    list->commands.push_back(std::move(cmd));
    return s7_t(sc);
}

s7_pointer g_hud_text_align(s7_scheme* sc, s7_pointer args) {
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "hud-text-align", args);
    const s7_pointer value = in.peek();
    const char* name = in.name("'left, 'center or 'right");
    if (std::strcmp(name, "left") == 0) {
        list->textAlign = HudTextAlign::Left;
    } else if (std::strcmp(name, "center") == 0) {
        list->textAlign = HudTextAlign::Center;
    } else if (std::strcmp(name, "right") == 0) {
        list->textAlign = HudTextAlign::Right;
    } else {
        return in.wrongTypeAt(1, value, "'left, 'center or 'right");
    }
    return s7_t(sc);
}

s7_pointer g_hud_draw_eye(s7_scheme* sc, s7_pointer args) {
    HudDrawList* list = hudList();
    if (list == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "hud-draw-eye", args);
    const float x = in.realf("x y w h numbers");
    const float y = in.realf("x y w h numbers");
    const float w = in.realf("x y w h numbers");
    const float h = in.realf("x y w h numbers");
    const std::string maskPath = in.optString("");

    if (g_hudWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = g_hudWorld->lookup("Player");
    if (!player.is_valid() || !player.has<ExtraEye>()) {
        return s7_f(sc);
    }
    const ExtraEye& eye = player.get<ExtraEye>();
    if (eye.target.id == 0) {
        return s7_f(sc);
    }

    HudCmd cmd{};
    cmd.kind = HudCmdKind::Texture;
    resolveCmdOrigin(*list, x, y, cmd.x, cmd.y);
    cmd.w = w;
    cmd.h = h;
    cmd.rawTexture = eye.target.texture;
    cmd.maskPath = maskPath;
    list->commands.push_back(std::move(cmd));
    return s7_t(sc);
}

constexpr SchemeBinding kHudBindings[] = {
    {"hud-anchor", g_hud_anchor, 1, 0, false, "(hud-anchor symbol)", ScriptCap::HudDraw},
    {"hud-font", g_hud_font, 1, 0, false, "(hud-font path)", ScriptCap::HudDraw},
    {"hud-rect", g_hud_rect, 4, 4, false, "(hud-rect x y w h r g b [a])", ScriptCap::HudDraw},
    {"hud-image", g_hud_image, 3, 6, false, "(hud-image tex-path x y [w h] [r g b a])", ScriptCap::HudDraw},
    {"hud-text", g_hud_text, 4, 4, false, "(hud-text str x y size [r g b a])", ScriptCap::HudDraw},
    {"hud-text-align", g_hud_text_align, 1, 0, false, "(hud-text-align symbol)", ScriptCap::HudDraw},
    {"hud-draw-eye", g_hud_draw_eye, 4, 1, false, "(hud-draw-eye x y w h [mask-path])", ScriptCap::HudDraw},
};

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
    registerBindings(scheme, kHudBindings);
}

}
