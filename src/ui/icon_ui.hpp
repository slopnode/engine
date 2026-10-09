#pragma once

#include "assets/asset_store.hpp"
#include "assets/icon_atlas.hpp"

#include "rlImGui.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace slopengine {

inline constexpr const char* kDefaultIconSet = "silk";
inline constexpr ImVec2 kMainMenuBarFramePadding{6.0f, 6.0f};

int slopuiActiveMainMenuBarBegin();
bool slopuiActiveMainMenuBarEnd();
bool slopuiActiveMainMenuBarHeight(float& height);
bool slopuiActiveIconButton(
    AssetStore& assets,
    std::string_view set,
    std::string_view iconId,
    const char* label,
    const ImVec2& size,
    float iconSize,
    bool& pressed);

inline float mainMenuBarHeight(const ImVec2& padding) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
    const float height = ImGui::GetFrameHeight();
    ImGui::PopStyleVar();
    return height;
}

inline float mainMenuBarHeight() {
    float height = 0.0f;
    if (slopuiActiveMainMenuBarHeight(height)) {
        return height;
    }
    return mainMenuBarHeight(kMainMenuBarFramePadding);
}

inline bool drawIconImGui(
    AssetStore& assets,
    std::string_view set,
    std::string_view id,
    float size = 16.0f) {
    const IconAtlas* atlas = assets.getIconAtlas(set);
    if (atlas == nullptr || atlas->texture.id == 0) {
        return false;
    }
    const auto rect = findIconRect(*atlas, id);
    if (!rect) {
        return false;
    }
    rlImGuiImageRect(&atlas->texture, static_cast<int>(size), static_cast<int>(size), *rect);
    return true;
}

inline void drawMainMenuBarIcon(
    AssetStore& assets,
    std::string_view set,
    std::string_view id,
    float size = 16.0f) {
    const IconAtlas* atlas = assets.getIconAtlas(set);
    if (atlas == nullptr || atlas->texture.id == 0) {
        return;
    }
    const auto rect = findIconRect(*atlas, id);
    if (!rect) {
        return;
    }

    const float rowHeight = ImGui::GetFrameHeight();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float y = cursor.y + (rowHeight - size) * 0.5f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float tw = static_cast<float>(atlas->texture.width);
    const float th = static_cast<float>(atlas->texture.height);
    draw->AddImage(
        (ImTextureID)(intptr_t)atlas->texture.id,
        ImVec2(cursor.x, y),
        ImVec2(cursor.x + size, y + size),
        ImVec2(rect->x / tw, rect->y / th),
        ImVec2((rect->x + rect->width) / tw, (rect->y + rect->height) / th),
        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));

    ImGui::Dummy(ImVec2(size, rowHeight));
}

inline bool beginMainMenuBar(const ImVec2& padding) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
    return ImGui::BeginMainMenuBar();
}

inline bool beginMainMenuBar() {
    const int themed = slopuiActiveMainMenuBarBegin();
    if (themed >= 0) {
        return themed == 1;
    }
    return beginMainMenuBar(kMainMenuBarFramePadding);
}

inline void endMainMenuBar() {
    if (slopuiActiveMainMenuBarEnd()) {
        return;
    }
    ImGui::EndMainMenuBar();
    ImGui::PopStyleVar();
}

inline void drawIconInButton(
    AssetStore& assets,
    std::string_view set,
    std::string_view iconId,
    const char* label,
    const ImVec2& min,
    const ImVec2& max,
    float iconSize = 16.0f) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 textSize = label != nullptr ? ImGui::CalcTextSize(label) : ImVec2{};
    const bool iconOnly = label == nullptr || label[0] == '\0';
    const float height = max.y - min.y;
    const float contentW =
        iconOnly ? iconSize : iconSize + style.ItemInnerSpacing.x + textSize.x;
    const float x = min.x + std::max(style.FramePadding.x, (max.x - min.x - contentW) * 0.5f);
    const float yIcon = min.y + (height - iconSize) * 0.5f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const IconAtlas* atlas = assets.getIconAtlas(set);
    if (atlas != nullptr && atlas->texture.id != 0) {
        if (const auto rect = findIconRect(*atlas, iconId)) {
            const float tw = static_cast<float>(atlas->texture.width);
            const float th = static_cast<float>(atlas->texture.height);
            draw->AddImage(
                (ImTextureID)(intptr_t)atlas->texture.id,
                ImVec2(x, yIcon),
                ImVec2(x + iconSize, yIcon + iconSize),
                ImVec2(rect->x / tw, rect->y / th),
                ImVec2((rect->x + rect->width) / tw, (rect->y + rect->height) / th),
                ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
        }
    }
    if (!iconOnly) {
        const float yText = min.y + (height - textSize.y) * 0.5f;
        draw->AddText(
            ImVec2(x + iconSize + style.ItemInnerSpacing.x, yText),
            ImGui::GetColorU32(ImGuiCol_Text),
            label);
    }
}

inline bool buttonWithIcon(
    AssetStore& assets,
    std::string_view set,
    std::string_view iconId,
    const char* label,
    const ImVec2& size = ImVec2(0, 0),
    float iconSize = 16.0f) {
    const bool iconOnly = label == nullptr || label[0] == '\0';
    bool themedPressed = false;
    if (slopuiActiveIconButton(assets, set, iconId, iconOnly ? "" : label, size, iconSize, themedPressed)) {
        return themedPressed;
    }
    ImGui::PushID(iconOnly ? iconId.data() : label);
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 textSize = label != nullptr ? ImGui::CalcTextSize(label) : ImVec2{};
    const float height = size.y > 0.0f ? size.y : ImGui::GetFrameHeight();
    ImVec2 btnSize = size;
    if (btnSize.x == 0.0f) {
        btnSize.x = style.FramePadding.x * 2.0f +
            (iconOnly ? iconSize : iconSize + style.ItemInnerSpacing.x + textSize.x);
    } else if (btnSize.x < 0.0f) {
        btnSize.x = ImGui::GetContentRegionAvail().x;
    }
    if (btnSize.y <= 0.0f) {
        btnSize.y = height;
    }

    const bool pressed = ImGui::Button("##btn", btnSize);
    drawIconInButton(
        assets,
        set,
        iconId,
        label,
        ImGui::GetItemRectMin(),
        ImGui::GetItemRectMax(),
        iconSize);
    ImGui::PopID();
    return pressed;
}

inline bool iconButton(
    AssetStore& assets,
    std::string_view set,
    std::string_view iconId,
    const ImVec2& size,
    float iconSize = 16.0f) {
    return buttonWithIcon(assets, set, iconId, "", size, iconSize);
}

inline bool selectableWithIcon(
    AssetStore& assets,
    std::string_view set,
    std::string_view iconId,
    const char* label,
    bool selected,
    float size = 16.0f) {
    drawIconImGui(assets, set, iconId, size);
    ImGui::SameLine();
    return ImGui::Selectable(label, selected);
}

inline bool menuItemWithIcon(
    AssetStore& assets,
    std::string_view set,
    std::string_view iconId,
    const char* label,
    const char* shortcut = nullptr,
    bool selected = false,
    bool enabled = true,
    float size = 16.0f) {
    drawIconImGui(assets, set, iconId, size);
    ImGui::SameLine();
    return ImGui::MenuItem(label, shortcut, selected, enabled);
}

inline bool menuItemWithIcon(
    AssetStore& assets,
    std::string_view set,
    std::string_view iconId,
    const char* label,
    const char* shortcut,
    bool* pSelected,
    bool enabled = true,
    float size = 16.0f) {
    drawIconImGui(assets, set, iconId, size);
    ImGui::SameLine();
    return ImGui::MenuItem(label, shortcut, pSelected, enabled);
}

inline bool beginMenuWithIcon(
    AssetStore& assets,
    std::string_view set,
    std::string_view iconId,
    const char* label,
    bool mainMenuBar = false,
    bool enabled = true,
    float size = 16.0f) {
    if (mainMenuBar) {
        drawMainMenuBarIcon(assets, set, iconId, size);
    } else {
        drawIconImGui(assets, set, iconId, size);
    }
    ImGui::SameLine();
    return ImGui::BeginMenu(label, enabled);
}

// The widest of a group of labels, in the current font -- feed this as `labelWidth` to every
// slider in a SeparatorText section so they all get the same frame width and their labels form
// a left-aligned column, instead of each slider stretching to its own (differently-sized) label.
inline float maxLabelWidth(std::initializer_list<const char*> labels) {
    float maxWidth = 0.0f;
    for (const char* label : labels) {
        maxWidth = std::max(maxWidth, ImGui::CalcTextSize(label, nullptr, true).x);
    }
    return maxWidth;
}

// Slider's own frame width, sized so [input box][slider][label] ends exactly at the content
// region's right edge instead of the label running past it (ImGui's default item width is a
// fixed font-relative constant, not the panel's actual available width). Draw any other
// same-line content (a checkbox, a button) before calling this, not after -- this claims all
// remaining width on the line for itself.
inline float sliderWithInputFrameWidth(float reservedLabelWidth) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float trailing = reservedLabelWidth > 0.0f ? style.ItemInnerSpacing.x + reservedLabelWidth : 0.0f;
    return std::max(24.0f, ImGui::GetContentRegionAvail().x - trailing);
}

// A slider paired with an editable input box to its left, so a caller can type an exact value
// instead of dragging for it. Suppresses the slider's own value readout (which the input box
// replaces) via an empty display format. `labelWidth` defaults to this slider's own label width
// (a standalone slider fills the row); pass maxLabelWidth() of a whole group to align a column
// of sliders on the widest label in that group instead.
inline bool sliderFloatWithInput(
    const char* label,
    float* value,
    float lo,
    float hi,
    const char* format = "%.3f",
    float inputWidth = 64.0f,
    float labelWidth = -1.0f) {
    ImGui::PushID(label);
    bool changed = false;
    ImGui::SetNextItemWidth(inputWidth);
    if (ImGui::InputFloat("##input", value, 0.0f, 0.0f, format)) {
        *value = std::clamp(*value, lo, hi);
        changed = true;
    }
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    const float reserved = labelWidth >= 0.0f ? labelWidth : ImGui::CalcTextSize(label, nullptr, true).x;
    ImGui::SetNextItemWidth(sliderWithInputFrameWidth(reserved));
    if (ImGui::SliderFloat(label, value, lo, hi, "", ImGuiSliderFlags_NoInput)) {
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

inline bool sliderIntWithInput(
    const char* label,
    int* value,
    int lo,
    int hi,
    float inputWidth = 64.0f,
    float labelWidth = -1.0f) {
    ImGui::PushID(label);
    bool changed = false;
    ImGui::SetNextItemWidth(inputWidth);
    if (ImGui::InputInt("##input", value, 0, 0)) {
        *value = std::clamp(*value, lo, hi);
        changed = true;
    }
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    const float reserved = labelWidth >= 0.0f ? labelWidth : ImGui::CalcTextSize(label, nullptr, true).x;
    ImGui::SetNextItemWidth(sliderWithInputFrameWidth(reserved));
    if (ImGui::SliderInt(label, value, lo, hi, "", ImGuiSliderFlags_NoInput)) {
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

inline bool collapsingHeaderWithIcon(
    AssetStore& assets,
    std::string_view set,
    std::string_view iconId,
    const char* label,
    ImGuiTreeNodeFlags flags = 0,
    float size = 16.0f) {
    ImGui::PushID(label);
    const ImGuiTreeNodeFlags nodeFlags =
        flags | ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_AllowOverlap;
    const bool open = ImGui::TreeNodeEx("##hdr", nodeFlags);
    ImGui::SameLine(0.0f, ImGui::GetStyle().FramePadding.x);
    drawIconImGui(assets, set, iconId, size);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return open;
}

}
