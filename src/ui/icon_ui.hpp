#pragma once

#include "assets/asset_store.hpp"

#include "rlImGui.h"

#include <imgui.h>

#include <string_view>

namespace slopengine {

inline constexpr const char* kDefaultIconSet = "silk";

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
    bool enabled = true,
    float size = 16.0f) {
    drawIconImGui(assets, set, iconId, size);
    ImGui::SameLine();
    return ImGui::BeginMenu(label, enabled);
}

inline bool buttonWithIcon(
    AssetStore& assets,
    std::string_view set,
    std::string_view iconId,
    const char* label,
    const ImVec2& size = ImVec2(0, 0),
    float iconSize = 16.0f) {
    drawIconImGui(assets, set, iconId, iconSize);
    ImGui::SameLine();
    return ImGui::Button(label, size);
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
