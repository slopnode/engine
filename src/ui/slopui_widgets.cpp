#include "ui/slopui_widgets.hpp"

#include "assets/asset_store.hpp"
#include "ui/icon_ui.hpp"

#include <imgui_internal.h>
#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace slopengine {

namespace {

constexpr ImVec4 kTransparent{0.0f, 0.0f, 0.0f, 0.0f};

ImU32 styledColor(const ThemeColor& color) {
    return ImGui::GetColorU32(color.vec());
}

ThemePaintResolved buttonPaint(const SlopUiTheme& theme, int state, bool primary) {
    const ThemeColor base = themeRaise(theme.surface, 0.08f);
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

bool sameVec4(const ImVec4& a, const ImVec4& b) {
    return std::fabs(a.x - b.x) < 0.002f && std::fabs(a.y - b.y) < 0.002f &&
        std::fabs(a.z - b.z) < 0.002f && std::fabs(a.w - b.w) < 0.002f;
}

bool buttonColorsOverridden(const SlopUiTheme& theme) {
    return !sameVec4(ImGui::GetStyleColorVec4(ImGuiCol_Button), themeRaise(theme.surface, 0.08f).vec());
}

bool g_menuBarThemed = false;

template <typename Fn>
void fillAroundScrollbars(ImDrawList* draw, const ImVec2& pos, const ImVec2& max, Fn&& fn) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window == nullptr || !window->ScrollbarY) {
        draw->PushClipRect(pos, max, false);
        fn();
        draw->PopClipRect();
        return;
    }
    const ImRect sb = ImGui::GetWindowScrollbarRect(window, ImGuiAxis_Y);
    const ImRect regions[3] = {
        ImRect(pos.x, pos.y, sb.Min.x, max.y),
        ImRect(sb.Min.x, pos.y, max.x, sb.Min.y),
        ImRect(sb.Min.x, sb.Max.y, max.x, max.y),
    };
    for (const ImRect& r : regions) {
        if (r.Max.x <= r.Min.x || r.Max.y <= r.Min.y) {
            continue;
        }
        draw->PushClipRect(r.Min, r.Max, false);
        fn();
        draw->PopClipRect();
    }
}

void drawWindowChrome(
    const SlopUiTheme& theme,
    const SlopUiFonts& fonts,
    AssetStore* assets,
    const char* name,
    bool* open,
    bool customBg,
    ImGuiWindowFlags flags) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max{pos.x + size.x, pos.y + size.y};
    const float titleH = fonts.size(theme.window.titleFont) + style.FramePadding.y * 2.0f;
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    draw->PushClipRect(pos, max, false);

    const float frameInset = theme.bevel > 0.0f ? theme.bevel : 0.0f;
    const ThemePaintResolved bgPaint = theme.resolvePaint(theme.window.bg, theme.bg);
    const bool hasBg = (flags & ImGuiWindowFlags_NoBackground) == 0;
    if (customBg && hasBg) {
        fillAroundScrollbars(draw, pos, max, [&]() {
            if (!(theme.skin.window.set && drawThemeSkin(draw, pos, max, assets, theme.skin.window, 0))) {
                drawThemePaint(draw, pos, max, bgPaint, style.WindowRounding);
                if (theme.bevel <= 0.0f && theme.border > 0.0f) {
                    draw->AddRect(pos, max, styledColor(theme.borderColor()), style.WindowRounding, theme.border);
                }
            }
        });
    }
    if (hasBg && theme.bevel > 0.0f && !theme.skin.window.set) {
        drawThemeBevel(draw, pos, max, bgPaint.a, theme.bevel, true);
    }

    if (flags & ImGuiWindowFlags_NoTitleBar) {
        draw->PopClipRect();
        return;
    }

    const ImVec2 titleMin{pos.x + frameInset, pos.y + frameInset};
    const ImVec2 titleMax{max.x - frameInset, pos.y + titleH};
    const int titleState = focused && theme.skin.title.states > 1 ? 1 : 0;
    if (!(theme.skin.title.set && drawThemeSkin(draw, titleMin, titleMax, assets, theme.skin.title, titleState))) {
        const ThemePaintResolved titlePaint = focused
            ? theme.resolvePaint(theme.window.titleBgActive, themeMix(theme.surface, theme.accent, 0.25f))
            : theme.resolvePaint(theme.window.titleBg, themeMix(theme.bg, theme.surface, 0.5f));
        drawThemePaint(draw, titleMin, titleMax, titlePaint, style.WindowRounding, ImDrawFlags_RoundCornersTop);
        if (theme.border > 0.0f && theme.bevel <= 0.0f) {
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
    const ImVec2 p1 = paint.horizontal ? ImVec2{max.x, min.y} : ImVec2{min.x, max.y};
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(
        draw,
        vtxStart,
        vtxEnd,
        ImVec2{min.x, min.y},
        p1,
        styledColor(paint.a),
        styledColor(paint.b));
}

void drawThemeBevel(
    ImDrawList* draw,
    const ImVec2& min,
    const ImVec2& max,
    const ThemeColor& face,
    float thickness,
    bool raised) {
    const int rings = static_cast<int>(std::lround(thickness));
    if (rings <= 0 || max.x - min.x < 4.0f || max.y - min.y < 4.0f) {
        return;
    }
    for (int i = 0; i < rings; ++i) {
        const bool outer = i == 0;
        ThemeColor light = themeLighten(face, outer ? 0.9f : 0.35f);
        ThemeColor dark = themeDarken(face, outer ? 0.65f : 0.33f);
        if (!raised) {
            std::swap(light, dark);
        }
        const float inset = static_cast<float>(i);
        const ImVec2 a{min.x + inset, min.y + inset};
        const ImVec2 b{max.x - inset, max.y - inset};
        const ImU32 lightCol = styledColor(light);
        const ImU32 darkCol = styledColor(dark);
        draw->AddRectFilled(a, ImVec2{b.x - 1.0f, a.y + 1.0f}, lightCol);
        draw->AddRectFilled(a, ImVec2{a.x + 1.0f, b.y - 1.0f}, lightCol);
        draw->AddRectFilled(ImVec2{a.x, b.y - 1.0f}, b, darkCol);
        draw->AddRectFilled(ImVec2{b.x - 1.0f, a.y}, b, darkCol);
    }
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
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, theme.bevel > 0.0f ? theme.bevel : (customBg ? 0.0f : theme.border));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bgPaint.a.vec());
    ImGui::PushStyleColor(ImGuiCol_Border, theme.bevel > 0.0f ? kTransparent : ImGui::GetStyleColorVec4(ImGuiCol_Border));
    ImGui::PushStyleColor(ImGuiCol_Text, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kTransparent);
    pushThemeFont(fonts, theme.window.titleFont);
    const bool visible = ImGui::Begin(name, open, flags);
    popThemeFont();
    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar();

    if (visible) {
        drawWindowChrome(theme, fonts, assets, name, open, customBg, extraFlags);
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
    float iconSize,
    bool selected) {
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
    const int state = disabled ? 3 : (held || selected) ? 2 : hovered ? 1 : 0;

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ThemeSkin& skin = primary && theme.skin.primaryButton.set ? theme.skin.primaryButton : theme.skin.button;
    const bool sunken = held || selected;
    if (buttonColorsOverridden(theme)) {
        const ImGuiCol slot = state == 2 ? ImGuiCol_ButtonActive : state == 1 ? ImGuiCol_ButtonHovered : ImGuiCol_Button;
        const ImVec4 face = ImGui::GetStyleColorVec4(slot);
        draw->AddRectFilled(min, max, ImGui::GetColorU32(slot), style.FrameRounding);
        if (theme.bevel > 0.0f) {
            drawThemeBevel(draw, min, max, ThemeColor{face.x, face.y, face.z, face.w}, theme.bevel, !sunken);
        } else if (style.FrameBorderSize > 0.0f) {
            draw->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, style.FrameBorderSize);
        }
    } else if (!drawThemeSkin(draw, min, max, assets, skin, state)) {
        const ThemePaintResolved facePaint = buttonPaint(theme, state, primary);
        drawThemePaint(draw, min, max, facePaint, style.FrameRounding);
        if (theme.bevel > 0.0f) {
            drawThemeBevel(draw, min, max, facePaint.a, theme.bevel, !sunken);
        } else if (style.FrameBorderSize > 0.0f) {
            draw->AddRect(min, max, styledColor(theme.borderColor()), style.FrameRounding, style.FrameBorderSize);
        }
    }
    const ImVec2 pressOffset = theme.bevel > 0.0f && sunken ? ImVec2{1.0f, 1.0f} : ImVec2{0.0f, 0.0f};

    ThemeColor textColor = theme.resolve(theme.button.text, theme.text);
    if (hovered || held) {
        textColor = theme.resolve(theme.button.textHover, textColor);
    }
    if (disabled) {
        textColor = theme.textDim;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, textColor.vec());
    const ImVec2 contentMin{min.x + pressOffset.x, min.y + pressOffset.y};
    const ImVec2 contentMax{max.x + pressOffset.x, max.y + pressOffset.y};
    if (hasIcon) {
        drawIconInButton(*assets, iconSet, iconId, label, contentMin, contentMax, iconSize);
    } else {
        const ImVec2 textPos{
            contentMin.x + std::max(style.FramePadding.x, (size.x - textSize.x) * 0.5f),
            contentMin.y + (size.y - textSize.y) * 0.5f};
        draw->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), label, ImGui::FindRenderedTextEnd(label));
    }
    ImGui::PopStyleColor();
    return pressed;
}

bool beginThemedPanelEx(
    const SlopUiTheme& theme,
    AssetStore* assets,
    const char* id,
    const ImVec2& size,
    ImGuiChildFlags childFlags,
    ImGuiWindowFlags windowFlags) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_Border, kTransparent);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, theme.bevel > 0.0f ? theme.bevel : 0.0f);
    ImGuiChildFlags flags = childFlags | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (theme.bevel > 0.0f) {
        flags |= ImGuiChildFlags_Borders;
    } else {
        flags &= ~ImGuiChildFlags_Borders;
    }
    const bool open = ImGui::BeginChild(id, size, flags, windowFlags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    const ImVec2 max{pos.x + winSize.x, pos.y + winSize.y};
    fillAroundScrollbars(draw, pos, max, [&]() {
        if (!drawThemeSkin(draw, pos, max, assets, theme.skin.panel, 0)) {
            const ThemePaintResolved panelPaint = theme.resolvePaint(theme.panel.bg, theme.surface);
            drawThemePaint(draw, pos, max, panelPaint, style.ChildRounding);
            if (theme.bevel <= 0.0f && theme.border > 0.0f) {
                draw->AddRect(
                    pos, max, styledColor(theme.resolve(theme.panel.border, theme.borderColor())),
                    style.ChildRounding, theme.border);
            }
        }
    });
    if (theme.bevel > 0.0f && !theme.skin.panel.set) {
        draw->PushClipRect(pos, max, false);
        drawThemeBevel(draw, pos, max, theme.resolve(theme.panel.border, theme.surface), theme.bevel, false);
        draw->PopClipRect();
    }
    return open;
}

bool beginThemedPanel(const SlopUiTheme& theme, AssetStore* assets, const char* id, float height) {
    ImGuiChildFlags flags = 0;
    if (height == 0.0f) {
        flags |= ImGuiChildFlags_AutoResizeY;
    }
    return beginThemedPanelEx(theme, assets, id, ImVec2{0.0f, height}, flags, 0);
}

void endThemedPanel() {
    ImGui::EndChild();
}

bool slopuiBeginChild(const char* id, const ImVec2& size, ImGuiChildFlags childFlags, ImGuiWindowFlags windowFlags) {
    const SlopUiTheme* theme = activeSlopUiTheme();
    if (theme == nullptr || (childFlags & ImGuiChildFlags_Borders) == 0) {
        return ImGui::BeginChild(id, size, childFlags, windowFlags);
    }
    return beginThemedPanelEx(*theme, activeSlopUiAssets(), id, size, childFlags, windowFlags);
}

bool beginThemedMainMenuBar(const SlopUiTheme& theme, const SlopUiFonts& fonts, AssetStore* assets) {
    pushThemeFont(fonts, theme.menuBar.font);
    const ThemePaintResolved paint = theme.resolvePaint(theme.menuBar.bg, theme.surface);
    const bool custom = theme.skin.menuBar.set || paint.gradient;
    if (custom) {
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, kTransparent);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, themedMenuBarPadding(theme, fonts));
    const bool ok = ImGui::BeginMainMenuBar();
    if (custom) {
        ImGui::PopStyleColor();
    }
    if (!ok) {
        ImGui::PopStyleVar();
        popThemeFont();
        return false;
    }
    g_menuBarThemed = true;
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
    ImGui::EndMainMenuBar();
    ImGui::PopStyleVar();
    popThemeFont();
    g_menuBarThemed = false;
}

bool slopuiBegin(const char* name, bool* open, ImGuiWindowFlags flags) {
    const SlopUiTheme* theme = activeSlopUiTheme();
    const SlopUiFonts* fonts = activeSlopUiFonts();
    if (theme == nullptr || fonts == nullptr) {
        return ImGui::Begin(name, open, flags);
    }
    return beginThemedWindow(*theme, *fonts, activeSlopUiAssets(), name, open, flags);
}

bool slopuiButton(const char* label, const ImVec2& size, bool selected) {
    const SlopUiTheme* theme = activeSlopUiTheme();
    const SlopUiFonts* fonts = activeSlopUiFonts();
    if (theme == nullptr || fonts == nullptr) {
        return ImGui::Button(label, size);
    }
    return themedButton(*theme, *fonts, activeSlopUiAssets(), label, size, false, {}, {}, 16.0f, selected);
}

int slopuiActiveMainMenuBarBegin() {
    const SlopUiTheme* theme = activeSlopUiTheme();
    const SlopUiFonts* fonts = activeSlopUiFonts();
    if (theme == nullptr || fonts == nullptr) {
        return -1;
    }
    return beginThemedMainMenuBar(*theme, *fonts, activeSlopUiAssets()) ? 1 : 0;
}

bool slopuiActiveMainMenuBarEnd() {
    if (!g_menuBarThemed) {
        return false;
    }
    endThemedMainMenuBar();
    return true;
}

bool slopuiActiveMainMenuBarHeight(float& height) {
    const SlopUiTheme* theme = activeSlopUiTheme();
    const SlopUiFonts* fonts = activeSlopUiFonts();
    if (theme == nullptr || fonts == nullptr) {
        return false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, themedMenuBarPadding(*theme, *fonts));
    pushThemeFont(*fonts, theme->menuBar.font);
    height = ImGui::GetFrameHeight();
    popThemeFont();
    ImGui::PopStyleVar();
    return true;
}

bool slopuiActiveIconButton(
    AssetStore& assets,
    std::string_view set,
    std::string_view iconId,
    const char* label,
    const ImVec2& size,
    float iconSize,
    bool& pressed) {
    const SlopUiTheme* theme = activeSlopUiTheme();
    const SlopUiFonts* fonts = activeSlopUiFonts();
    if (theme == nullptr || fonts == nullptr) {
        return false;
    }
    const bool iconOnly = label == nullptr || label[0] == '\0';
    if (iconOnly) {
        ImGui::PushID(iconId.data(), iconId.data() + iconId.size());
    }
    pressed = themedButton(*theme, *fonts, &assets, iconOnly ? "" : label, size, false, set, iconId, iconSize);
    if (iconOnly) {
        ImGui::PopID();
    }
    return true;
}

}
