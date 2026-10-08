#include "ui/slopui_widgets.hpp"

#include "assets/asset_store.hpp"
#include "audio/audio_module.hpp"
#include "ui/icon_ui.hpp"

#include <imgui_internal.h>
#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

namespace slopengine {

namespace {

constexpr ImVec4 kTransparent{0.0f, 0.0f, 0.0f, 0.0f};

ImU32 styledColor(const ThemeColor& color) {
    return ImGui::GetColorU32(color.vec());
}

ThemePaintResolved buttonPaint(const SlopUiTheme& theme, int state, bool primary) {
    const ThemeColor base = themeLighten(theme.surface, 0.08f);
    if (primary) {
        const ThemePaintResolved normal =
            theme.resolvePaint(theme.button.primaryBg, themeMix(theme.surface, theme.accent, 0.5f));
        switch (state) {
        case 1:
            return theme.resolvePaint(theme.button.primaryBgHover, themeMix(theme.surface, theme.accent, 0.7f));
        case 2:
            return theme.resolvePaint(theme.button.primaryBgActive, themeMix(theme.surface, theme.accent, 0.85f));
        case 3:
            return theme.resolvePaint(theme.button.bgDisabled, theme.surface);
        default:
            return normal;
        }
    }
    switch (state) {
    case 1:
        return theme.resolvePaint(theme.button.bgHover, themeMix(theme.surface, theme.accent, 0.4f));
    case 2:
        return theme.resolvePaint(theme.button.bgActive, themeMix(theme.surface, theme.accent, 0.6f));
    case 3:
        return theme.resolvePaint(theme.button.bgDisabled, theme.surface);
    default:
        return theme.resolvePaint(theme.button.bg, base);
    }
}

bool itemDisabled() {
    const ImGuiContext* g = ImGui::GetCurrentContext();
    return g != nullptr && (g->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
}

void drawWindowChrome(
    const SlopUiTheme& theme,
    const SlopUiFonts& fonts,
    AssetStore* assets,
    const char* name,
    bool* open,
    bool customBg) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max{pos.x + size.x, pos.y + size.y};
    const float titleH = fonts.size(theme.window.titleFont) + style.FramePadding.y * 2.0f;
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    draw->PushClipRect(pos, max, false);

    if (customBg) {
        if (!(theme.skin.window.set && drawThemeSkin(draw, pos, max, assets, theme.skin.window, 0))) {
            drawThemePaint(draw, pos, max, theme.resolvePaint(theme.window.bg, theme.bg), style.WindowRounding);
            if (theme.border > 0.0f) {
                draw->AddRect(pos, max, styledColor(theme.borderColor()), style.WindowRounding, theme.border);
            }
        }
    }

    const ImVec2 titleMax{max.x, pos.y + titleH};
    const int titleState = focused && theme.skin.title.states > 1 ? 1 : 0;
    if (!(theme.skin.title.set && drawThemeSkin(draw, pos, titleMax, assets, theme.skin.title, titleState))) {
        const ThemePaintResolved titlePaint = focused
            ? theme.resolvePaint(theme.window.titleBgActive, themeMix(theme.surface, theme.accent, 0.25f))
            : theme.resolvePaint(theme.window.titleBg, themeMix(theme.bg, theme.surface, 0.5f));
        drawThemePaint(draw, pos, titleMax, titlePaint, style.WindowRounding, ImDrawFlags_RoundCornersTop);
        if (theme.border > 0.0f) {
            draw->AddLine(
                ImVec2{pos.x, titleMax.y - 0.5f},
                ImVec2{max.x, titleMax.y - 0.5f},
                styledColor(theme.borderColor()),
                theme.border);
        }
    }

    const char* textEnd = ImGui::FindRenderedTextEnd(name);
    ImFont* titleFont = fonts.font(theme.window.titleFont);
    const float titleSize = fonts.size(theme.window.titleFont);
    pushThemeFont(fonts, theme.window.titleFont);
    const ImVec2 textSize = ImGui::CalcTextSize(name, textEnd);
    popThemeFont();
    const float buttonSz = titleSize;
    const float reservedRight = open != nullptr ? buttonSz + style.FramePadding.x + style.ItemInnerSpacing.x : 0.0f;
    float textX = pos.x + style.FramePadding.x;
    if (theme.window.titleCentered) {
        textX = pos.x + (size.x - reservedRight - textSize.x) * 0.5f;
        textX = std::max(textX, pos.x + style.FramePadding.x);
    }
    const float textY = pos.y + (titleH - textSize.y) * 0.5f;
    const ThemeColor titleText = theme.resolve(theme.window.titleText, theme.text);
    draw->PushClipRect(pos, ImVec2{max.x - reservedRight, titleMax.y}, true);
    draw->AddText(titleFont, titleSize, ImVec2{textX, textY}, styledColor(titleText), name, textEnd);
    draw->PopClipRect();

    if (open != nullptr) {
        const ImVec2 btnMin{max.x - style.FramePadding.x - buttonSz, pos.y + style.FramePadding.y};
        const ImVec2 btnMax{btnMin.x + buttonSz, btnMin.y + buttonSz};
        const bool hovered = ImGui::IsMouseHoveringRect(btnMin, btnMax) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        const ImVec2 center{(btnMin.x + btnMax.x) * 0.5f, (btnMin.y + btnMax.y) * 0.5f};
        if (hovered) {
            draw->AddCircleFilled(center, buttonSz * 0.5f, styledColor(themeMix(theme.surface, theme.accent, 0.5f)));
        }
        const float cross = buttonSz * 0.25f;
        const ImU32 crossColor = styledColor(hovered ? theme.text : titleText);
        draw->AddLine(ImVec2{center.x - cross, center.y - cross}, ImVec2{center.x + cross, center.y + cross}, crossColor, 1.5f);
        draw->AddLine(ImVec2{center.x + cross, center.y - cross}, ImVec2{center.x - cross, center.y + cross}, crossColor, 1.5f);
    }

    draw->PopClipRect();
}

} // namespace

void playUiSound(flecs::world world, UiSoundKind kind) {
    if (!world.has<SlopUiTheme>() || !world.has<AudioContext>()) {
        return;
    }
    const SlopUiTheme& theme = world.get<SlopUiTheme>();
    const std::string* path = nullptr;
    switch (kind) {
    case UiSoundKind::Hover:
        path = &theme.sounds.hover;
        break;
    case UiSoundKind::Confirm:
        path = &theme.sounds.confirm;
        break;
    case UiSoundKind::Back:
        path = &theme.sounds.back;
        break;
    }
    if (path == nullptr || path->empty()) {
        return;
    }
    const AudioContext& ctx = world.get<AudioContext>();
    if (ctx.world == nullptr || ctx.assets == nullptr || !ctx.world->ready()) {
        return;
    }
    const AudioDef* def = ctx.assets->getAudioDef(*path);
    if (def == nullptr) {
        static std::unordered_set<std::string> warned;
        if (warned.insert(*path).second) {
            TraceLog(LOG_WARNING, "THEME: ui sound def not found: %s", path->c_str());
        }
        return;
    }
    ctx.world->playAudioDef(*ctx.assets, *path, *def, theme.sounds.volume);
}

void updateUiHoverSound(flecs::world world) {
    if (!world.has<SlopUiSoundState>()) {
        world.set<SlopUiSoundState>({});
    }
    SlopUiSoundState& state = world.get_mut<SlopUiSoundState>();
    const ImGuiID hovered = ImGui::GetHoveredID();
    if (hovered == state.lastHovered) {
        return;
    }
    state.lastHovered = hovered;
    if (hovered != 0) {
        playUiSound(world, UiSoundKind::Hover);
    }
}

void drawThemePaint(
    ImDrawList* draw,
    const ImVec2& min,
    const ImVec2& max,
    const ThemePaintResolved& paint,
    float rounding,
    ImDrawFlags flags) {
    if (max.x <= min.x || max.y <= min.y) {
        return;
    }
    if (!paint.gradient) {
        draw->AddRectFilled(min, max, styledColor(paint.a), rounding, flags);
        return;
    }
    const int vtxStart = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, max, styledColor(paint.a), rounding, flags);
    const int vtxEnd = draw->VtxBuffer.Size;
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(
        draw,
        vtxStart,
        vtxEnd,
        ImVec2{min.x, min.y},
        ImVec2{min.x, max.y},
        styledColor(paint.a),
        styledColor(paint.b));
}

bool drawThemeSkin(
    ImDrawList* draw,
    const ImVec2& min,
    const ImVec2& max,
    AssetStore* assets,
    const ThemeSkin& skin,
    int state,
    ImU32 tint) {
    if (!skin.set || assets == nullptr || skin.texture.empty()) {
        return false;
    }
    const Texture2D texture = assets->getTexture(skin.texture);
    if (texture.id == 0 || texture.width <= 0 || texture.height <= 0) {
        return false;
    }
    const float texW = static_cast<float>(texture.width);
    const float texH = static_cast<float>(texture.height);
    const int states = std::max(skin.states, 1);
    const float stateH = texH / static_cast<float>(states);
    const int row = std::clamp(state, 0, states - 1);
    const float srcY = stateH * static_cast<float>(row);

    const float l = std::min(skin.left, texW * 0.5f);
    const float r = std::min(skin.right, texW * 0.5f);
    const float t = std::min(skin.top, stateH * 0.5f);
    const float b = std::min(skin.bottom, stateH * 0.5f);
    const float scale = skin.scale;
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    const float dl = std::min(l * scale, width * 0.5f);
    const float dr = std::min(r * scale, width * 0.5f);
    const float dt = std::min(t * scale, height * 0.5f);
    const float db = std::min(b * scale, height * 0.5f);

    const float xs[4] = {min.x, min.x + dl, max.x - dr, max.x};
    const float ys[4] = {min.y, min.y + dt, max.y - db, max.y};
    const float us[4] = {0.0f, l / texW, (texW - r) / texW, 1.0f};
    const float vs[4] = {srcY / texH, (srcY + t) / texH, (srcY + stateH - b) / texH, (srcY + stateH) / texH};

    const ImTextureID id = (ImTextureID)(intptr_t)texture.id;
    for (int row3 = 0; row3 < 3; ++row3) {
        for (int col = 0; col < 3; ++col) {
            const ImVec2 p0{xs[col], ys[row3]};
            const ImVec2 p1{xs[col + 1], ys[row3 + 1]};
            if (p1.x <= p0.x || p1.y <= p0.y) {
                continue;
            }
            draw->AddImage(id, p0, p1, ImVec2{us[col], vs[row3]}, ImVec2{us[col + 1], vs[row3 + 1]}, tint);
        }
    }
    return true;
}

bool beginThemedWindow(
    const SlopUiTheme& theme,
    const SlopUiFonts& fonts,
    AssetStore* assets,
    const char* name,
    bool* open,
    ImGuiWindowFlags extraFlags) {
    const ThemePaintResolved bgPaint = theme.resolvePaint(theme.window.bg, theme.bg);
    const bool customBg = theme.skin.window.set || bgPaint.gradient;

    ImGuiWindowFlags flags = themedWindowFlags(theme) | extraFlags;
    if (customBg) {
        flags |= ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    } else {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, bgPaint.a.vec());
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kTransparent);
    pushThemeFont(fonts, theme.window.titleFont);
    const bool visible = ImGui::Begin(name, open, flags);
    popThemeFont();
    ImGui::PopStyleColor(6);
    if (customBg) {
        ImGui::PopStyleVar();
    } else {
        ImGui::PopStyleColor();
    }

    if (visible) {
        drawWindowChrome(theme, fonts, assets, name, open, customBg);
    }
    return visible;
}

bool themedButton(
    const SlopUiTheme& theme,
    const SlopUiFonts& fonts,
    AssetStore* assets,
    const char* label,
    const ImVec2& sizeArg,
    bool primary,
    std::string_view iconSet,
    std::string_view iconId,
    float iconSize) {
    (void)fonts;
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool hasIcon = assets != nullptr && !iconSet.empty() && !iconId.empty();
    const ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, true);
    const bool iconOnly = hasIcon && textSize.x <= 0.0f;
    float contentW = textSize.x;
    if (hasIcon) {
        contentW = iconOnly ? iconSize : iconSize + style.ItemInnerSpacing.x + textSize.x;
    }

    ImVec2 size = sizeArg;
    if (size.x < 0.0f) {
        size.x = ImGui::GetContentRegionAvail().x;
    } else if (size.x == 0.0f) {
        size.x = contentW + style.FramePadding.x * 2.0f;
    }
    if (size.y <= 0.0f) {
        size.y = ImGui::GetFrameHeight();
    }
    size.x = std::max(size.x, 1.0f);

    ImGui::PushID(label);
    const bool pressed = ImGui::InvisibleButton("##slopui-button", size);
    ImGui::PopID();
    const bool disabled = itemDisabled();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const int state = disabled ? 3 : held ? 2 : hovered ? 1 : 0;

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ThemeSkin& skin = primary && theme.skin.primaryButton.set ? theme.skin.primaryButton : theme.skin.button;
    if (!drawThemeSkin(draw, min, max, assets, skin, state)) {
        drawThemePaint(draw, min, max, buttonPaint(theme, state, primary), style.FrameRounding);
        if (style.FrameBorderSize > 0.0f) {
            draw->AddRect(min, max, styledColor(theme.borderColor()), style.FrameRounding, style.FrameBorderSize);
        }
    }

    ThemeColor textColor = theme.resolve(theme.button.text, theme.text);
    if (hovered || held) {
        textColor = theme.resolve(theme.button.textHover, textColor);
    }
    if (disabled) {
        textColor = theme.textDim;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, textColor.vec());
    if (hasIcon) {
        drawIconInButton(*assets, iconSet, iconId, label, min, max, iconSize);
    } else {
        const ImVec2 textPos{
            min.x + std::max(style.FramePadding.x, (size.x - textSize.x) * 0.5f),
            min.y + (size.y - textSize.y) * 0.5f};
        draw->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), label, ImGui::FindRenderedTextEnd(label));
    }
    ImGui::PopStyleColor();
    return pressed;
}

bool beginThemedPanel(const SlopUiTheme& theme, AssetStore* assets, const char* id, float height) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kTransparent);
    ImGuiChildFlags flags = ImGuiChildFlags_AlwaysUseWindowPadding;
    if (height <= 0.0f) {
        flags |= ImGuiChildFlags_AutoResizeY;
    }
    const bool open = ImGui::BeginChild(id, ImVec2{0.0f, height}, flags);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max{pos.x + size.x, pos.y + size.y};
    draw->PushClipRect(pos, max, false);
    if (!drawThemeSkin(draw, pos, max, assets, theme.skin.panel, 0)) {
        drawThemePaint(draw, pos, max, theme.resolvePaint(theme.panel.bg, theme.surface), style.ChildRounding);
        if (theme.border > 0.0f) {
            draw->AddRect(
                pos, max, styledColor(theme.resolve(theme.panel.border, theme.borderColor())),
                style.ChildRounding, theme.border);
        }
    }
    draw->PopClipRect();
    return open;
}

void endThemedPanel() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

bool beginThemedMainMenuBar(const SlopUiTheme& theme, const SlopUiFonts& fonts, AssetStore* assets) {
    pushThemeFont(fonts, theme.menuBar.font);
    const ThemePaintResolved paint = theme.resolvePaint(theme.menuBar.bg, theme.surface);
    const bool custom = theme.skin.menuBar.set || paint.gradient;
    if (custom) {
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, kTransparent);
    }
    const bool ok = beginMainMenuBar(themedMenuBarPadding(theme, fonts));
    if (custom) {
        ImGui::PopStyleColor();
    }
    if (!ok) {
        popThemeFont();
        return false;
    }
    if (custom) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        const ImVec2 max{pos.x + size.x, pos.y + size.y};
        draw->PushClipRect(pos, max, false);
        if (!drawThemeSkin(draw, pos, max, assets, theme.skin.menuBar, 0)) {
            drawThemePaint(draw, pos, max, paint, 0.0f);
        }
        draw->PopClipRect();
    }
    return true;
}

void endThemedMainMenuBar() {
    endMainMenuBar();
    popThemeFont();
}

}
