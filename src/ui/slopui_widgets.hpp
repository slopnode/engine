#pragma once

#include "ui/slopui_theme.hpp"

#include <flecs.h>
#include <imgui.h>

#include <string_view>

namespace slopengine {

class AssetStore;

enum class UiSoundKind {
    Hover,
    Confirm,
    Back,
};

struct SlopUiSoundState {
    ImGuiID lastHovered = 0;
};

void playUiSound(flecs::world world, UiSoundKind kind);

void updateUiHoverSound(flecs::world world);

void drawThemePaint(
    ImDrawList* draw,
    const ImVec2& min,
    const ImVec2& max,
    const ThemePaintResolved& paint,
    float rounding,
    ImDrawFlags flags = 0);

bool drawThemeSkin(
    ImDrawList* draw,
    const ImVec2& min,
    const ImVec2& max,
    AssetStore* assets,
    const ThemeSkin& skin,
    int state,
    ImU32 tint = IM_COL32_WHITE);

bool beginThemedWindow(
    const SlopUiTheme& theme,
    const SlopUiFonts& fonts,
    AssetStore* assets,
    const char* name,
    bool* open = nullptr,
    ImGuiWindowFlags extraFlags = 0);

bool themedButton(
    const SlopUiTheme& theme,
    const SlopUiFonts& fonts,
    AssetStore* assets,
    const char* label,
    const ImVec2& size = ImVec2{0.0f, 0.0f},
    bool primary = false,
    std::string_view iconSet = {},
    std::string_view iconId = {},
    float iconSize = 16.0f);

bool beginThemedPanel(const SlopUiTheme& theme, AssetStore* assets, const char* id, float height);
void endThemedPanel();

bool beginThemedMainMenuBar(const SlopUiTheme& theme, const SlopUiFonts& fonts, AssetStore* assets);
void endThemedMainMenuBar();

}
