#include "asset_picker.hpp"

#include "browser.hpp"

#include "ui/icon_ui.hpp"
#include "ui/slopui_widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace slopmat {

void AssetPicker::show(
    std::string popupTitle,
    std::vector<std::string> paths,
    std::string icon,
    DocKind kind,
    std::function<void(const std::string&)> callback) {
    title = std::move(popupTitle);
    items = std::move(paths);
    iconId = std::move(icon);
    previewKind = kind;
    onPick = std::move(callback);
    filter.clear();
    hovered.clear();
    open = true;
}

void AssetPicker::draw(slopengine::AssetStore& assets) {
    if (!open) {
        return;
    }
    constexpr const char* kIcons = slopengine::kDefaultIconSet;
    const std::string popupId = title + "##assetpicker";
    ImGui::OpenPopup(popupId.c_str());
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(popupId.c_str(), &open, ImGuiWindowFlags_None)) {
        char filterBuf[128] = {};
        std::snprintf(filterBuf, sizeof(filterBuf), "%s", filter.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        if (ImGui::InputTextWithHint("##pickfilter", "filter", filterBuf, sizeof(filterBuf))) {
            filter = filterBuf;
        }
        ImGui::Separator();

        const float listHeight = -ImGui::GetFrameHeightWithSpacing();
        const float previewWidth = 220.0f;
        const float listWidth = ImGui::GetContentRegionAvail().x - previewWidth - ImGui::GetStyle().ItemSpacing.x;
        if (slopengine::slopuiBeginChild("##picklist", ImVec2(listWidth, listHeight), ImGuiChildFlags_Borders)) {
            for (const std::string& path : items) {
                if (!containsIgnoreCase(path, filter)) {
                    continue;
                }
                ImGui::PushID(path.c_str());
                const bool picked = slopengine::selectableWithIcon(assets, kIcons, iconId.c_str(), path.c_str(), path == hovered);
                if (ImGui::IsItemHovered()) {
                    hovered = path;
                }
                if (picked) {
                    if (onPick) {
                        onPick(path);
                    }
                    open = false;
                    ImGui::CloseCurrentPopup();
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        if (slopengine::slopuiBeginChild("##pickpreview", ImVec2(previewWidth, listHeight), ImGuiChildFlags_Borders)) {
            Texture2D texture{};
            if (!hovered.empty()) {
                switch (previewKind) {
                case DocKind::Texture: texture = assets.getTexture(hovered); break;
                case DocKind::TextureGraph: texture = assets.resolveTextureGraphOutput(hovered, "albedo"); break;
                case DocKind::TextureAnim: texture = assets.resolveTextureAnimFrame(hovered, "default", 0); break;
                case DocKind::Material: break;
                }
            }
            if (texture.id != 0) {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                const float box = std::max(16.0f, std::min(avail.x, avail.y - ImGui::GetTextLineHeightWithSpacing()));
                const float scale = box / static_cast<float>(std::max(texture.width, texture.height));
                const ImVec2 size(static_cast<float>(texture.width) * scale, static_cast<float>(texture.height) * scale);
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(box, box));
                ImDrawList* draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(p0, ImVec2(p0.x + box, p0.y + box), IM_COL32(40, 40, 46, 255));
                const ImVec2 c0(p0.x + (box - size.x) * 0.5f, p0.y + (box - size.y) * 0.5f);
                draw->AddImage(static_cast<ImTextureID>(static_cast<std::uintptr_t>(texture.id)), c0, ImVec2(c0.x + size.x, c0.y + size.y));
                ImGui::TextDisabled("%d x %d", texture.width, texture.height);
            } else {
                ImGui::TextDisabled("%s", hovered.empty() ? "Hover an item to preview it" : "(no image)");
            }
        }
        ImGui::EndChild();

        if (slopengine::buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(-1.0f, 0.0f))) {
            open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}
