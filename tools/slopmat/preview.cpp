#include "preview.hpp"

#include "ui/icon_ui.hpp"
#include "ui/slopui_widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace slopmat {

namespace {

void drawCheckerboard(ImDrawList* draw, ImVec2 p0, ImVec2 p1) {
    constexpr float kCell = 24.0f;
    const ImU32 dark = IM_COL32(46, 46, 52, 255);
    const ImU32 light = IM_COL32(62, 62, 70, 255);
    draw->AddRectFilled(p0, p1, dark);
    int row = 0;
    for (float y = p0.y; y < p1.y; y += kCell, ++row) {
        int col = 0;
        for (float x = p0.x; x < p1.x; x += kCell, ++col) {
            if (((row + col) & 1) == 0) {
                continue;
            }
            draw->AddRectFilled(
                ImVec2(x, y), ImVec2(std::min(x + kCell, p1.x), std::min(y + kCell, p1.y)), light);
        }
    }
}

} // namespace

void drawPreviewPanel(PreviewView& view, Editor& editor, slopengine::AssetStore& assets) {
    constexpr const char* kIcons = slopengine::kDefaultIconSet;
    EditorDocument& doc = editor.doc;

    if (!doc.open) {
        ImGui::TextDisabled("Pick a material, texture graph or texture from the browser.");
        return;
    }

    if (doc.previews.empty()) {
        view.slot = 0;
    } else {
        view.slot = std::clamp(view.slot, 0, static_cast<int>(doc.previews.size()) - 1);
    }

    const bool animated = doc.kind == DocKind::TextureAnim && !doc.anim.clips.empty() && !doc.previews.empty();
    if (animated) {
        const slopengine::TextureAnimClip& clip = doc.anim.clips[static_cast<std::size_t>(std::clamp(doc.animClip, 0, static_cast<int>(doc.anim.clips.size()) - 1))];
        if (view.playing) {
            view.playTime += GetFrameTime();
            view.slot = std::clamp(slopengine::textureAnimFrameIndexAt(clip, view.playTime), 0, static_cast<int>(doc.previews.size()) - 1);
        }
        if (slopengine::buttonWithIcon(assets, kIcons, view.playing ? "control_pause" : "control_play", view.playing ? "Pause" : "Play")) {
            view.playing = !view.playing;
        }
        ImGui::SameLine();
    }

    ImGui::SetNextItemWidth(140.0f);
    const char* current = doc.previews.empty() ? "(none)" : doc.previews[static_cast<std::size_t>(view.slot)].name.c_str();
    if (ImGui::BeginCombo("##slot", current)) {
        for (std::size_t i = 0; i < doc.previews.size(); ++i) {
            const bool selected = static_cast<int>(i) == view.slot;
            if (ImGui::Selectable(doc.previews[i].name.c_str(), selected)) {
                view.slot = static_cast<int>(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("##zoom", &view.zoom, 0.25f, 16.0f, "zoom %.2fx", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    ImGui::Checkbox("Tile", &view.tile);
    ImGui::SameLine();
    ImGui::Checkbox("3D", &view.world);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Floor and wall at the material's texel density; drag to orbit, wheel to zoom");
    }
    if (view.world) {
        ImGui::SameLine();
        if (doc.kind == DocKind::Material) {
            ImGui::TextDisabled("%.0f px/m", doc.material.pixelsPerMeter);
        } else {
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("##texel", &view.texelSize, 1.0f, 1.0f, 4096.0f, "%.0f px/m");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &view.worldPreview.showGrid);
    }
    ImGui::SameLine();
    if (slopengine::buttonWithIcon(assets, kIcons, "arrow_refresh", "Rebuild")) {
        doc.previewDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Re-evaluate the preview from the current document");
    }

    const Texture2D texture = doc.previews.empty() ? Texture2D{} : doc.previews[static_cast<std::size_t>(view.slot)].texture;
    ImGui::SameLine();
    if (texture.id != 0) {
        ImGui::TextDisabled("%s  %dx%d", doc.virtualPath.c_str(), texture.width, texture.height);
    } else {
        ImGui::TextDisabled("%s  (no image)", doc.virtualPath.c_str());
    }
    if (!doc.problem.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", doc.problem.c_str());
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x <= 8.0f || avail.y <= 8.0f) {
        return;
    }
    if (view.world) {
        const float ppm = doc.kind == DocKind::Material ? doc.material.pixelsPerMeter : view.texelSize;
        view.worldPreview.draw(texture, ppm, avail);
        return;
    }
    if (slopengine::slopuiBeginChild("##previewcanvas", avail, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + ImGui::GetContentRegionAvail().x, p0.y + ImGui::GetContentRegionAvail().y);
        draw->PushClipRect(p0, p1, true);
        drawCheckerboard(draw, p0, p1);
        if (texture.id != 0) {
            const ImTextureID id = static_cast<ImTextureID>(static_cast<std::uintptr_t>(texture.id));
            const float w = static_cast<float>(texture.width) * view.zoom;
            const float h = static_cast<float>(texture.height) * view.zoom;
            if (view.tile) {
                const float regionW = p1.x - p0.x;
                const float regionH = p1.y - p0.y;
                draw->AddImage(id, p0, p1, ImVec2(0.0f, 0.0f), ImVec2(regionW / w, regionH / h));
            } else {
                const ImVec2 c0(std::floor(p0.x + ((p1.x - p0.x) - w) * 0.5f), std::floor(p0.y + ((p1.y - p0.y) - h) * 0.5f));
                draw->AddImage(id, c0, ImVec2(c0.x + w, c0.y + h));
                draw->AddRect(c0, ImVec2(c0.x + w, c0.y + h), IM_COL32(255, 255, 255, 40));
            }
        }
        draw->PopClipRect();
        if (ImGui::IsWindowHovered()) {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                view.zoom = std::clamp(view.zoom * (wheel > 0.0f ? 1.25f : 0.8f), 0.25f, 16.0f);
            }
        }
    }
    ImGui::EndChild();
}

}
