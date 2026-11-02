#include "outliner.hpp"

#include "ui/icon_ui.hpp"
#include "ui/slopui_widgets.hpp"

#include "imgui.h"

#include <cstdint>
#include <string>
#include <vector>

namespace slopmodel {

namespace {

constexpr const char* kIcons = slopengine::kDefaultIconSet;

void visibilityToggle(const char* id, bool& visible) {
    ImGui::PushID(id);
    ImGui::Checkbox("##visible", &visible);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(visible ? "Shown in the preview" : "Hidden in the preview");
    }
    ImGui::PopID();
    ImGui::SameLine();
}

bool sectionNode(slopengine::AssetStore& assets, const char* icon, const std::string& label, const char* id) {
    slopengine::drawIconImGui(assets, kIcons, icon, 16.0f);
    ImGui::SameLine();
    return ImGui::TreeNodeEx(id, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", label.c_str());
}

void drawBone(Editor& editor, const Model& model, const std::vector<std::vector<int>>& children, int bone) {
    const bool leaf = children[static_cast<std::size_t>(bone)].empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (leaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (editor.doc.selectedBone == bone) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const bool open = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<std::intptr_t>(bone)),
        flags,
        "%s",
        model.skeleton.bones[bone].name);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        editor.doc.selectedBone = editor.doc.selectedBone == bone ? -1 : bone;
        editor.showSkeleton = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("bone %d", bone);
    }
    if (open && !leaf) {
        for (int child : children[static_cast<std::size_t>(bone)]) {
            drawBone(editor, model, children, child);
        }
        ImGui::TreePop();
    }
}

void drawSkin(Editor& editor, slopengine::AssetStore& assets) {
    visibilityToggle("skin", editor.showMesh);
    if (!sectionNode(assets, "picture", "Skin", "##skin")) {
        return;
    }
    const Document& doc = editor.doc;
    for (const slopengine::GeoPrimitive& primitive : doc.geo.primitives) {
        slopengine::drawIconImGui(assets, kIcons, "palette", 16.0f);
        ImGui::SameLine();
        ImGui::TextUnformatted(primitive.material.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "primitive %s, %d vertices, %d triangles",
                primitive.name.c_str(),
                static_cast<int>(primitive.vertexCount),
                static_cast<int>(primitive.indexCount / 3));
        }
    }
    ImGui::TreePop();
}

void drawArmature(Editor& editor, slopengine::AssetStore& assets) {
    const Model& model = editor.doc.model;
    visibilityToggle("armature", editor.showSkeleton);
    const std::string label = "Armature (" + std::to_string(model.skeleton.boneCount) + " bones)";
    if (!sectionNode(assets, "chart_organisation", label, "##armature")) {
        return;
    }
    std::vector<std::vector<int>> children(static_cast<std::size_t>(model.skeleton.boneCount));
    std::vector<int> roots;
    for (int bone = 0; bone < model.skeleton.boneCount; ++bone) {
        const int parent = model.skeleton.bones[bone].parent;
        if (parent >= 0 && parent < model.skeleton.boneCount) {
            children[static_cast<std::size_t>(parent)].push_back(bone);
        } else {
            roots.push_back(bone);
        }
    }
    for (int root : roots) {
        drawBone(editor, model, children, root);
    }
    ImGui::TreePop();
}

void drawAnimations(Editor& editor, slopengine::AssetStore& assets) {
    Document& doc = editor.doc;
    const std::string label = "Animations (" + std::to_string(doc.hasAnim ? doc.anim.clips.size() : 0) + ")";
    if (!sectionNode(assets, "film", label, "##animations")) {
        return;
    }
    if (!doc.hasAnim) {
        ImGui::TextDisabled("no bank at animations/%s.anim", doc.skeletonPath.c_str());
    }
    for (const slopengine::AnimClip& clip : doc.anim.clips) {
        const bool selected = clip.name == doc.clip;
        if (slopengine::selectableWithIcon(assets, kIcons, "control_play", clip.name.c_str(), selected)) {
            editor.playClip(clip.name, doc.loop);
            editor.scrub(assets, 0.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%.0f fps, %.2f s, %d events", clip.fps, clip.duration, static_cast<int>(clip.events.size()));
        }
    }
    ImGui::TreePop();
}

void drawAttachments(Editor& editor, slopengine::AssetStore& assets) {
    Document& doc = editor.doc;
    visibilityToggle("attachments", editor.showAttachments);
    const std::string label = "Attachments (" + std::to_string(doc.geo.attachments.size()) + ")";
    if (!sectionNode(assets, "anchor", label, "##attachments")) {
        return;
    }
    for (int i = 0; i < static_cast<int>(doc.geo.attachments.size()); ++i) {
        const slopengine::GeoAttachment& attachment = doc.geo.attachments[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        if (slopengine::selectableWithIcon(assets, kIcons, "link", attachment.name.c_str(), doc.selectedAttachment == i)) {
            doc.selectedAttachment = doc.selectedAttachment == i ? -1 : i;
            doc.selectedBone = -1;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("on %s", attachment.bone.c_str());
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

} // namespace

void drawAssetOutliner(Editor& editor, slopengine::AssetStore& assets) {
    ImGui::TextUnformatted("Asset");
    if (!editor.doc.open) {
        ImGui::TextDisabled("Open a model above");
        return;
    }
    if (slopengine::slopuiBeginChild("##outliner", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        drawSkin(editor, assets);
        if (editor.skinned()) {
            drawArmature(editor, assets);
            drawAnimations(editor, assets);
        } else {
            ImGui::TextDisabled("static geo, no armature");
        }
        drawAttachments(editor, assets);
    }
    ImGui::EndChild();
}

}
