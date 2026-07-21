#include "render/hud.hpp"

#include "assets/asset_store.hpp"
#include "core/vfs.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace slopengine {

namespace {

constexpr int kHudFontBaseSize = 64;

Font* ensureHudFont(HudFontCache& cache, AssetStore& assets, const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }

    const auto it = cache.fonts.find(path);
    if (it != cache.fonts.end()) {
        return &it->second;
    }

    if (!assets.hasFont(path)) {
        TraceLog(LOG_WARNING, "HUD: font not found: %s", path.c_str());
        return nullptr;
    }

    const std::vector<std::byte> bytes = assets.readBinary(path, AssetKind::Font);
    if (bytes.empty()) {
        TraceLog(LOG_WARNING, "HUD: font empty: %s", path.c_str());
        return nullptr;
    }

    Font font = LoadFontFromMemory(
        ".ttf",
        reinterpret_cast<const unsigned char*>(bytes.data()),
        static_cast<int>(bytes.size()),
        kHudFontBaseSize,
        nullptr,
        0);
    if (font.texture.id == 0) {
        TraceLog(LOG_WARNING, "HUD: LoadFontFromMemory failed: %s", path.c_str());
        return nullptr;
    }

    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    auto [inserted, ok] = cache.fonts.emplace(path, font);
    if (!ok) {
        UnloadFont(font);
        return nullptr;
    }
    return &inserted->second;
}

} // namespace

ViewCanvasFit makeViewCanvasFit(int width, int height, float screenW, float screenH) {
    ViewCanvasFit fit{};
    fit.canvasW = static_cast<float>(std::max(width, 1));
    fit.canvasH = static_cast<float>(std::max(height, 1));
    fit.scale = std::min(screenW / fit.canvasW, screenH / fit.canvasH);
    fit.offsetX = (screenW - fit.canvasW * fit.scale) * 0.5f;
    fit.offsetY = (screenH - fit.canvasH * fit.scale) * 0.5f;
    return fit;
}

void hudAnchorPoint(HudAnchor anchor, float canvasW, float canvasH, float& outX, float& outY) {
    switch (anchor) {
    case HudAnchor::TopLeft:
        outX = 0.0f;
        outY = 0.0f;
        break;
    case HudAnchor::TopRight:
        outX = canvasW;
        outY = 0.0f;
        break;
    case HudAnchor::BottomLeft:
        outX = 0.0f;
        outY = canvasH;
        break;
    case HudAnchor::BottomRight:
        outX = canvasW;
        outY = canvasH;
        break;
    case HudAnchor::Center:
        outX = canvasW * 0.5f;
        outY = canvasH * 0.5f;
        break;
    case HudAnchor::BottomCenter:
        outX = canvasW * 0.5f;
        outY = canvasH;
        break;
    }
}

void HudDrawList::clear() {
    commands.clear();
    anchor = HudAnchor::TopLeft;
    fontPath.clear();
}

void HudFontCache::unload() {
    for (auto& [path, font] : fonts) {
        (void)path;
        if (font.texture.id != 0) {
            UnloadFont(font);
        }
    }
    fonts.clear();
}

void flushHudDrawList(HudDrawList& list, AssetStore& assets, HudFontCache& fonts, const ViewCanvasFit& fit) {
    if (list.commands.empty()) {
        return;
    }

    BeginBlendMode(BLEND_ALPHA);
    for (const HudCmd& cmd : list.commands) {
        switch (cmd.kind) {
        case HudCmdKind::Rect: {
            const float screenX = fit.offsetX + cmd.x * fit.scale;
            const float screenY = fit.offsetY + cmd.y * fit.scale;
            const float screenW = cmd.w * fit.scale;
            const float screenH = cmd.h * fit.scale;
            DrawRectangle(
                static_cast<int>(std::lround(screenX)),
                static_cast<int>(std::lround(screenY)),
                static_cast<int>(std::lround(screenW)),
                static_cast<int>(std::lround(screenH)),
                cmd.color);
            break;
        }
        case HudCmdKind::Image: {
            const Texture2D texture = assets.getTexture(cmd.path);
            if (texture.id == 0) {
                break;
            }
            float destW = cmd.w;
            float destH = cmd.h;
            if (cmd.nativeImageSize || destW <= 0.0f || destH <= 0.0f) {
                destW = static_cast<float>(texture.width);
                destH = static_cast<float>(texture.height);
            }
            const Rectangle source{
                0.0f,
                0.0f,
                static_cast<float>(texture.width),
                static_cast<float>(texture.height),
            };
            const Rectangle dest{
                fit.offsetX + cmd.x * fit.scale,
                fit.offsetY + cmd.y * fit.scale,
                destW * fit.scale,
                destH * fit.scale,
            };
            SetTextureFilter(texture, TEXTURE_FILTER_POINT);
            DrawTexturePro(texture, source, dest, Vector2{0.0f, 0.0f}, 0.0f, cmd.color);
            break;
        }
        case HudCmdKind::Text: {
            Font* font = ensureHudFont(fonts, assets, cmd.fontPath);
            const Font& drawFont = font != nullptr ? *font : GetFontDefault();
            const float fontSize = cmd.size * fit.scale;
            const Vector2 pos{
                fit.offsetX + cmd.x * fit.scale,
                fit.offsetY + cmd.y * fit.scale,
            };
            DrawTextEx(drawFont, cmd.text.c_str(), pos, fontSize, 1.0f * fit.scale, cmd.color);
            break;
        }
        }
    }
    EndBlendMode();
}

}
