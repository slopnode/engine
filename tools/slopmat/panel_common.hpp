#pragma once

#include "assets/asset_store.hpp"
#include "ui/icon_ui.hpp"

#include "imgui.h"

namespace slopmat {

constexpr const char* kIcons = slopengine::kDefaultIconSet;

inline float iconButtonWidth() {
    return 16.0f + ImGui::GetStyle().FramePadding.x * 2.0f;
}

inline void labeledField(const char* label, float labelWidth) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
}

inline bool deleteIconButton(slopengine::AssetStore& assets, const char* tooltip) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.30f, 0.30f, 1.0f));
    const bool pressed = slopengine::iconButton(assets, kIcons, "delete", ImVec2(0.0f, 0.0f));
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
}

}
