#pragma once

#include "ui/slopui_theme.hpp"

#include <imgui.h>

#include <string_view>

namespace slopengine {

class AssetStore;

void drawThemePaint(
    ImDrawList* draw,
    const ImVec2& min,
    const ImVec2& max,
    const ThemePaintResolved& paint,
    float rounding,
    ImDrawFlags flags = 0);

void drawThemeBevel(
    ImDrawList* draw,
    const ImVec2& min,
    const ImVec2& max,
    const ThemeColor& face,
    float thickness,
    bool raised);

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
    float iconSize = 16.0f,
    bool selected = false);

bool beginThemedPanel(const SlopUiTheme& theme, AssetStore* assets, const char* id, float height);
bool beginThemedPanelEx(
    const SlopUiTheme& theme,
    AssetStore* assets,
    const char* id,
    const ImVec2& size,
    ImGuiChildFlags childFlags,
    ImGuiWindowFlags windowFlags);
void endThemedPanel();

/** ImGui::BeginChild that renders bordered children as themed sunken panels when a theme is installed.
 *  Always pair with ImGui::EndChild(). */
bool slopuiBeginChild(
    const char* id,
    const ImVec2& size = ImVec2{0.0f, 0.0f},
    ImGuiChildFlags childFlags = 0,
    ImGuiWindowFlags windowFlags = 0);

bool beginThemedMainMenuBar(const SlopUiTheme& theme, const SlopUiFonts& fonts, AssetStore* assets);
void endThemedMainMenuBar();

/** ImGui::Begin through the active theme when one is installed, plain ImGui::Begin otherwise. */
bool slopuiBegin(const char* name, bool* open = nullptr, ImGuiWindowFlags flags = 0);

/** ImGui::Button through the active theme when one is installed, plain ImGui::Button otherwise. */
bool slopuiButton(const char* label, const ImVec2& size = ImVec2{0.0f, 0.0f}, bool selected = false);

}
