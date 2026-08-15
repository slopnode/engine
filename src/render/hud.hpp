#pragma once

#include "render/components.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace slopengine {

class AssetStore;

struct ViewCanvasFit {
    float canvasW = 320.0f;
    float canvasH = 200.0f;
    float scale = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

ViewCanvasFit makeViewCanvasFit(int width, int height, float screenW, float screenH);

enum class HudAnchor {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Center,
    BottomCenter,
};

void hudAnchorPoint(HudAnchor anchor, float canvasW, float canvasH, float& outX, float& outY);

enum class HudCmdKind {
    Rect,
    Image,
    Texture,
    Text,
};

struct HudCmd {
    HudCmdKind kind = HudCmdKind::Rect;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float size = 0.0f;
    Color color = WHITE;
    bool nativeImageSize = false;
    std::string path;
    std::string text;
    std::string fontPath;
    Texture2D rawTexture{};
    std::string maskPath;
};

/** Deferred HUD draw commands for the current frame.
 *  @ingroup render_components
 */
struct HudDrawList {
    HudAnchor anchor = HudAnchor::TopLeft;
    std::string fontPath;
    std::vector<HudCmd> commands;

    void clear();
};

/** Cached raylib fonts keyed by package path.
 *  @ingroup render_components
 */
struct HudFontCache {
    std::unordered_map<std::string, Font> fonts;

    HudFontCache() = default;
    HudFontCache(const HudFontCache&) = delete;
    HudFontCache& operator=(const HudFontCache&) = delete;

    HudFontCache(HudFontCache&& other) noexcept : fonts(std::move(other.fonts)) {
        other.fonts.clear();
    }

    HudFontCache& operator=(HudFontCache&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        unload();
        fonts = std::move(other.fonts);
        other.fonts.clear();
        return *this;
    }

    ~HudFontCache() {
        unload();
    }

    void unload();
};

void flushHudDrawList(HudDrawList& list, AssetStore& assets, HudFontCache& fonts, const ViewCanvasFit& fit);

}
