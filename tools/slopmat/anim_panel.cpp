#include "anim_panel.hpp"

#include "drag_drop.hpp"
#include "panel_common.hpp"

#include "ui/icon_ui.hpp"
#include "ui/slopui_widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace slopmat {

namespace {

constexpr float kLabelWidth = 90.0f;

void labeledField(const char* label) {
    slopmat::labeledField(label, kLabelWidth);
}

bool deleteButton(slopengine::AssetStore& assets, const char* tooltip) {
    return deleteIconButton(assets, tooltip);
}

void rebuildIndex(slopengine::TextureAnimBank& bank) {
    bank.clipIndexByName.clear();
    for (std::size_t i = 0; i < bank.clips.size(); ++i) {
        bank.clipIndexByName.emplace(bank.clips[i].name, i);
    }
}

void drawFrame(Editor& editor, slopengine::AssetStore& assets, AssetPicker& picker, const Browser& browser, slopengine::TextureAnimClip& clip, std::size_t index, bool& removed, int& moveDir) {
    slopengine::TextureAnimFrame& frame = clip.frames[index];
    ImGui::PushID(static_cast<int>(index));
    const float buttonW = iconButtonWidth();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (buttonW + spacing) * 4.0f - 70.0f - spacing);
    char buf[256] = {};
    std::snprintf(buf, sizeof(buf), "%s", frame.texture.c_str());
    if (ImGui::InputTextWithHint("##frametex", "textures/<path>", buf, sizeof(buf))) {
        frame.texture = buf;
        editor.markDirty();
    }
    if (acceptDroppedPath(DragPayload::Texture, frame.texture)) {
        editor.markDirty();
    }
    ImGui::SameLine();
    if (slopengine::iconButton(assets, kIcons, "picture", ImVec2(0.0f, 0.0f))) {
        slopengine::TextureAnimFrame* target = &frame;
        picker.show("Pick Frame Texture", browser.pathsOfKind(DocKind::Texture), "picture", DocKind::Texture, [&editor, target](const std::string& path) {
            target->texture = path;
            editor.markDirty();
        });
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::DragFloat("##duration", &frame.duration, 0.01f, 0.01f, 10.0f, "%.2fs")) {
        editor.markDirty();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(index == 0);
    if (slopengine::iconButton(assets, kIcons, "arrow_up", ImVec2(0.0f, 0.0f))) {
        moveDir = -1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 >= clip.frames.size());
    if (slopengine::iconButton(assets, kIcons, "arrow_down", ImVec2(0.0f, 0.0f))) {
        moveDir = 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (deleteButton(assets, "Remove frame")) {
        removed = true;
    }
    ImGui::PopID();
}

} // namespace

void drawAnimInspector(Editor& editor, slopengine::AssetStore& assets, AssetPicker& picker, const Browser& browser) {
    slopengine::TextureAnimBank& bank = editor.doc.anim;

    ImGui::TextDisabled("%s", editor.doc.virtualPath.c_str());
    if (!editor.doc.problem.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", editor.doc.problem.c_str());
    }
    ImGui::TextWrapped("Materials play the clip named \"default\". Frames resolve under textures/.");

    bool clipRemoved = false;
    std::size_t clipRemoveIndex = 0;
    for (std::size_t ci = 0; ci < bank.clips.size(); ++ci) {
        slopengine::TextureAnimClip& clip = bank.clips[ci];
        ImGui::PushID(static_cast<int>(ci));
        char header[160];
        std::snprintf(header, sizeof(header), "%s  (%d frames)###clip", clip.name.c_str(), static_cast<int>(clip.frames.size()));
        const bool open = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - iconButtonWidth());
        if (deleteButton(assets, "Remove clip")) {
            clipRemoved = true;
            clipRemoveIndex = ci;
        }
        if (open) {
            labeledField("Name");
            char nameBuf[64] = {};
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", clip.name.c_str());
            ImGui::InputText("##clipname", nameBuf, sizeof(nameBuf));
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const std::string wanted = nameBuf;
                if (!wanted.empty() && wanted != clip.name) {
                    clip.name = wanted;
                    rebuildIndex(bank);
                    editor.markDirty();
                }
            }
            labeledField("Loop");
            if (ImGui::Checkbox("##loop", &clip.loop)) {
                editor.markDirty();
            }
            if (ImGui::RadioButton("Preview this clip", editor.doc.animClip == static_cast<int>(ci))) {
                editor.doc.animClip = static_cast<int>(ci);
                editor.doc.previewDirty = true;
            }

            bool removed = false;
            std::size_t removeIndex = 0;
            std::size_t moveIndex = 0;
            int moveDir = 0;
            for (std::size_t fi = 0; fi < clip.frames.size(); ++fi) {
                bool frameRemoved = false;
                int dir = 0;
                drawFrame(editor, assets, picker, browser, clip, fi, frameRemoved, dir);
                if (frameRemoved) {
                    removed = true;
                    removeIndex = fi;
                }
                if (dir != 0) {
                    moveIndex = fi;
                    moveDir = dir;
                }
            }
            if (removed) {
                clip.frames.erase(clip.frames.begin() + static_cast<std::ptrdiff_t>(removeIndex));
                editor.markDirty();
            } else if (moveDir != 0) {
                std::swap(clip.frames[moveIndex], clip.frames[moveDir < 0 ? moveIndex - 1 : moveIndex + 1]);
                editor.markDirty();
            }
            if (slopengine::buttonWithIcon(assets, kIcons, "add", "Add frame")) {
                slopengine::TextureAnimFrame frame{};
                if (!clip.frames.empty()) {
                    frame = clip.frames.back();
                } else {
                    frame.duration = 0.15f;
                }
                clip.frames.push_back(frame);
                editor.markDirty();
            }
        }
        ImGui::PopID();
    }
    if (clipRemoved) {
        bank.clips.erase(bank.clips.begin() + static_cast<std::ptrdiff_t>(clipRemoveIndex));
        rebuildIndex(bank);
        editor.doc.animClip = 0;
        editor.markDirty();
    }
    if (slopengine::buttonWithIcon(assets, kIcons, "add", "Add clip")) {
        slopengine::TextureAnimClip clip{};
        clip.name = bank.clips.empty() ? "default" : "clip-" + std::to_string(bank.clips.size() + 1);
        clip.loop = true;
        bank.clips.push_back(clip);
        rebuildIndex(bank);
        editor.markDirty();
    }
}

}
