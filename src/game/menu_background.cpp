#include "game/menu_background.hpp"

#include "assets/asset_services.hpp"
#include "game/game_state.hpp"
#include "game/loading_session.hpp"
#include "map/bsp.hpp"
#include "render/hud.hpp"
#include "script/hook_registry.hpp"
#include "script/hud_script.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"

#include <raylib.h>
#include <s7.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace slopengine {

namespace {

void releaseImage(MenuBackground& bg) {
    bg.image = {};
}

TitleImageFit parseImageFit(const char* name) {
    if (name == nullptr) {
        return TitleImageFit::Stretch;
    }
    if (std::strcmp(name, "fit") == 0) {
        return TitleImageFit::Fit;
    }
    if (std::strcmp(name, "cover") == 0) {
        return TitleImageFit::Cover;
    }
    if (std::strcmp(name, "stretch") == 0) {
        return TitleImageFit::Stretch;
    }
    TraceLog(LOG_WARNING, "TITLE: unknown image fit '%s', using stretch", name);
    return TitleImageFit::Stretch;
}

void computeImageRects(
    TitleImageFit fit,
    float texW,
    float texH,
    float screenW,
    float screenH,
    Rectangle& outSrc,
    Rectangle& outDst) {
    outSrc = {0.0f, 0.0f, texW, texH};
    outDst = {0.0f, 0.0f, screenW, screenH};

    if (texW <= 0.0f || texH <= 0.0f || screenW <= 0.0f || screenH <= 0.0f) {
        return;
    }

    if (fit == TitleImageFit::Stretch) {
        return;
    }

    const float texAspect = texW / texH;
    const float screenAspect = screenW / screenH;

    if (fit == TitleImageFit::Fit) {
        if (texAspect > screenAspect) {
            outDst.width = screenW;
            outDst.height = screenW / texAspect;
            outDst.x = 0.0f;
            outDst.y = (screenH - outDst.height) * 0.5f;
        } else {
            outDst.height = screenH;
            outDst.width = screenH * texAspect;
            outDst.x = (screenW - outDst.width) * 0.5f;
            outDst.y = 0.0f;
        }
        return;
    }

    if (texAspect > screenAspect) {
        const float srcW = texH * screenAspect;
        outSrc.x = (texW - srcW) * 0.5f;
        outSrc.width = srcW;
    } else {
        const float srcH = texW / screenAspect;
        outSrc.y = (texH - srcH) * 0.5f;
        outSrc.height = srcH;
    }
}

void applyLegacyBackground(
    MenuBackground& bg,
    const char* mode,
    const char* path) {
    if (std::strcmp(mode, "picture") == 0) {
        bg.imagePath = path;
        bg.imageFit = TitleImageFit::Stretch;
        return;
    }
    if (std::strcmp(mode, "map") == 0) {
        bg.mapName = path;
        return;
    }
    TraceLog(LOG_WARNING, "TITLE: unknown legacy background mode '%s'", mode);
}

} // namespace

MenuBackground parsePackageTitleFromScheme(s7_scheme* scheme) {
    MenuBackground bg{};
    if (scheme == nullptr) {
        return bg;
    }

    const s7_pointer catalog = s7_name_to_value(scheme, "*package-title*");
    if (catalog == s7_undefined(scheme) || !s7_is_pair(catalog)) {
        return bg;
    }

    for (s7_pointer cursor = catalog; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            continue;
        }

        const char* key = s7_symbol_name(s7_car(entry));
        const s7_pointer rest = s7_cdr(entry);

        if (std::strcmp(key, "subtitle") == 0) {
            TraceLog(LOG_WARNING, "TITLE: subtitle is deprecated; draw it in (draw-title)");
            continue;
        }

        if (std::strcmp(key, "map") == 0) {
            if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
                TraceLog(LOG_WARNING, "TITLE: map entry missing path string");
                continue;
            }
            const char* path = s7_string(s7_car(rest));
            if (path == nullptr || path[0] == '\0') {
                TraceLog(LOG_WARNING, "TITLE: map path is empty");
                continue;
            }
            bg.mapName = path;
            continue;
        }

        if (std::strcmp(key, "image") == 0) {
            if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
                TraceLog(LOG_WARNING, "TITLE: image entry missing path string");
                continue;
            }
            const char* path = s7_string(s7_car(rest));
            if (path == nullptr || path[0] == '\0') {
                TraceLog(LOG_WARNING, "TITLE: image path is empty");
                continue;
            }
            bg.imagePath = path;
            bg.imageFit = TitleImageFit::Stretch;
            const s7_pointer fitCell = s7_cdr(rest);
            if (s7_is_pair(fitCell)) {
                const s7_pointer fitVal = s7_car(fitCell);
                if (s7_is_symbol(fitVal)) {
                    bg.imageFit = parseImageFit(s7_symbol_name(fitVal));
                } else if (s7_is_string(fitVal)) {
                    bg.imageFit = parseImageFit(s7_string(fitVal));
                }
            }
            continue;
        }

        if (std::strcmp(key, "canvas") == 0) {
            if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
                TraceLog(LOG_WARNING, "TITLE: canvas entry missing width");
                continue;
            }
            const int width = s7_number_to_integer(scheme, s7_car(rest));
            const s7_pointer heightCell = s7_cdr(rest);
            if (!s7_is_pair(heightCell) || !s7_is_number(s7_car(heightCell))) {
                TraceLog(LOG_WARNING, "TITLE: canvas entry missing height");
                continue;
            }
            const int height = s7_number_to_integer(scheme, s7_car(heightCell));
            bg.canvas.width = std::max(width, 1);
            bg.canvas.height = std::max(height, 1);
            continue;
        }

        if (std::strcmp(key, "background") == 0) {
            if (!s7_is_pair(rest) || !s7_is_symbol(s7_car(rest))) {
                TraceLog(LOG_WARNING, "TITLE: background entry missing mode symbol");
                continue;
            }
            const char* mode = s7_symbol_name(s7_car(rest));
            const s7_pointer pathCell = s7_cdr(rest);
            if (!s7_is_pair(pathCell) || !s7_is_string(s7_car(pathCell))) {
                TraceLog(LOG_WARNING, "TITLE: background entry missing path string");
                continue;
            }
            const char* path = s7_string(s7_car(pathCell));
            if (path == nullptr || path[0] == '\0') {
                TraceLog(LOG_WARNING, "TITLE: background path is empty");
                continue;
            }
            TraceLog(LOG_WARNING, "TITLE: (background ...) is deprecated; use (map) / (image)");
            applyLegacyBackground(bg, mode, path);
            continue;
        }

        TraceLog(LOG_WARNING, "TITLE: unknown catalog key '%s'", key);
    }

    return bg;
}

void loadMenuBackgroundConfig(flecs::world& world, AssetStore& assets, s7_scheme* scheme) {
    world.component<MenuBackground>();
    world.component<TitleCanvas>();
    MenuBackground bg{};
    const std::string baseId{assets.basePackageId()};
    if (scheme != nullptr && !baseId.empty() &&
        assets.loadDataFromPackage(scheme, baseId, "title")) {
        bg = parsePackageTitleFromScheme(scheme);
    }
    world.set<TitleCanvas>(bg.canvas);
    world.set<MenuBackground>(std::move(bg));
}

void clearMenuBackgroundImage(flecs::world& world) {
    if (!world.has<MenuBackground>()) {
        return;
    }
    MenuBackground& bg = world.get_mut<MenuBackground>();
    releaseImage(bg);
}

void markTitleMapActive(flecs::world& world, bool active) {
    if (!world.has<MenuBackground>()) {
        world.set<MenuBackground>(MenuBackground{});
    }
    world.get_mut<MenuBackground>().titleMapActive = active;
}

bool menuTitleMapActive(const flecs::world& world) {
    return world.has<MenuBackground>() && world.get<MenuBackground>().titleMapActive;
}

bool shouldDrawWorld(const flecs::world& world) {
    return isPlaying(world) || menuTitleMapActive(world) || isLoadingCrossfadeActive(world);
}

void applyMenuBackground(flecs::world& world, AssetStore& assets) {
    if (!world.has<MenuBackground>()) {
        return;
    }

    MenuBackground& bg = world.get_mut<MenuBackground>();
    world.set<TitleCanvas>(bg.canvas);

    releaseImage(bg);
    if (!bg.imagePath.empty()) {
        const Texture2D texture = assets.getTexture(bg.imagePath);
        if (texture.id == 0) {
            TraceLog(
                LOG_WARNING,
                "TITLE: failed to load image '%s'",
                bg.imagePath.c_str());
        } else {
            bg.image = texture;
        }
    }

    if (bg.mapName.empty()) {
        bg.titleMapActive = false;
        return;
    }

    if (bg.titleMapActive && world.has<CurrentMap>() &&
        world.get<CurrentMap>().id == bg.mapName) {
        return;
    }
    if (hasPendingMapLoad()) {
        return;
    }
    requestMapLoad(bg.mapName, "title");
}

void drawMenuBackgroundImage(const flecs::world& world) {
    if (!isMenu(world) || !world.has<MenuBackground>()) {
        return;
    }
    const MenuBackground& bg = world.get<MenuBackground>();
    if (bg.image.id == 0) {
        return;
    }

    const float screenW = static_cast<float>(GetRenderWidth());
    const float screenH = static_cast<float>(GetRenderHeight());
    Rectangle src{};
    Rectangle dst{};
    computeImageRects(
        bg.imageFit,
        static_cast<float>(bg.image.width),
        static_cast<float>(bg.image.height),
        screenW,
        screenH,
        src,
        dst);
    DrawTexturePro(bg.image, src, dst, {0.0f, 0.0f}, 0.0f, WHITE);
}

void drawMenuTitleCanvas(flecs::world& world) {
    if (!isMenu(world)) {
        return;
    }
    if (!world.has<AssetServices>() || world.get<AssetServices>().store == nullptr) {
        return;
    }

    TitleCanvas canvas{};
    if (world.has<TitleCanvas>()) {
        canvas = world.get<TitleCanvas>();
    } else if (world.has<MenuBackground>()) {
        canvas = world.get<MenuBackground>().canvas;
    }

    if (!world.has<HudDrawList>()) {
        world.set<HudDrawList>({});
    }
    if (!world.has<HudFontCache>()) {
        world.set<HudFontCache>({});
    }

    const float screenW = static_cast<float>(GetRenderWidth());
    const float screenH = static_cast<float>(GetRenderHeight());
    const ViewCanvasFit fit =
        makeViewCanvasFit(canvas.width, canvas.height, screenW, screenH);

    HudDrawList& hud = world.get_mut<HudDrawList>();
    hud.clear();
    setHudCanvasOverride(canvas.width, canvas.height);
    if (world.has<ScriptContext>() && world.get<ScriptContext>().scheme != nullptr) {
        callHook(world.get<ScriptContext>().scheme, "draw-title", ScriptScope::Hud);
    }
    clearHudCanvasOverride();
    flushHudDrawList(hud, *world.get_mut<AssetServices>().store, world.get_mut<HudFontCache>(), fit);
}

}
