#pragma once

#include "assets/asset_store.hpp"
#include "assets/icon_atlas.hpp"

#include "rlImGui.h"

#include <imgui.h>

#include <cmath>
#include <string_view>

namespace slopengine {

inline constexpr const char* kDefaultIconSet = "silk";
inline constexpr ImVec2 kMainMenuBarFramePadding{6.0f, 6.0f};

inline float mainMenuBarHeight() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, kMainMenuBarFramePadding);
    const float height = ImGui::GetFrameHeight();
    ImGui::PopStyleVar();
    return height;
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
        ImGui::GetColorU32(ImGuiCol_Text));

    ImGui::Dummy(ImVec2(size, rowHeight));
}

inline bool beginMainMenuBar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, kMainMenuBarFramePadding);
    return ImGui::BeginMainMenuBar();
}

inline void endMainMenuBar() {
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
                ImGui::GetColorU32(ImGuiCol_Text));
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
    ImGui::PushID(label != nullptr ? label : iconId.data());
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 textSize = label != nullptr ? ImGui::CalcTextSize(label) : ImVec2{};
    const bool iconOnly = label == nullptr || label[0] == '\0';
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
