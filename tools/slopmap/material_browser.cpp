#include "material_browser.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>
#include <unordered_map>

namespace slopmap {

namespace {

bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::string h(haystack.size(), '\0');
    std::string n(needle.size(), '\0');
    std::transform(haystack.begin(), haystack.end(), h.begin(), lower);
    std::transform(needle.begin(), needle.end(), n.begin(), lower);
    return h.find(n) != std::string::npos;
}

} // namespace

void MaterialBrowser::rescan(const slopengine::AssetStore& assets) {
    std::unordered_map<std::string, bool> seen;
    for (const slopengine::Package& package : assets.packages()) {
        const std::filesystem::path materialsRoot = package.root() / "materials";
        if (!std::filesystem::exists(materialsRoot)) {
            continue;
        }
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(materialsRoot, ec), end;
             it != end && !ec;
             it.increment(ec)) {
            if (ec || !it->is_regular_file()) {
                continue;
            }
            if (it->path().extension() != ".mat") {
                continue;
            }
            std::error_code relEc;
            std::filesystem::path relative =
                std::filesystem::relative(it->path(), materialsRoot, relEc);
            if (relEc) {
                continue;
            }
            relative.replace_extension();
            seen[relative.generic_string()] = true;
        }
    }

    materials.clear();
    materials.reserve(seen.size());
    for (const auto& [path, _] : seen) {
        materials.push_back(path);
    }
    std::sort(materials.begin(), materials.end());
}

std::string selectionMaterialLabel(const EditorDocument& doc) {
    if (doc.selection != SelectionTarget::Brush || doc.selectedBrush < 0 ||
        doc.selectedBrush >= static_cast<int>(doc.brushes.size())) {
        return "none";
    }
    const slopengine::Brush& brush = doc.brushes[static_cast<std::size_t>(doc.selectedBrush)];
    if (doc.scope == SelectionScope::Face) {
        if (doc.selectedFace < 0 || doc.selectedFace >= static_cast<int>(brush.faces.size())) {
            return "none";
        }
        const std::string& mat = brush.faces[static_cast<std::size_t>(doc.selectedFace)].material;
        return mat.empty() ? "(empty)" : mat;
    }

    if (brush.faces.empty()) {
        return "none";
    }
    const std::string& first = brush.faces.front().material;
    for (const slopengine::BrushFace& face : brush.faces) {
        if (face.material != first) {
            return "mixed";
        }
    }
    return first.empty() ? "(empty)" : first;
}

bool applyMaterialToSelection(Editor& editor, const std::string& materialPath) {
    EditorDocument& d = editor.doc();
    d.defaultMaterial = materialPath;

    if (d.selection != SelectionTarget::Brush || d.selectedBrush < 0 ||
        d.selectedBrush >= static_cast<int>(d.brushes.size())) {
        editor.statusMessage = "Active material: " + materialPath;
        return false;
    }

    slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(d.selectedBrush)];
    if (d.scope == SelectionScope::Face) {
        if (d.selectedFace < 0 || d.selectedFace >= static_cast<int>(brush.faces.size())) {
            editor.statusMessage = "Active material: " + materialPath;
            return false;
        }
        brush.faces[static_cast<std::size_t>(d.selectedFace)].material = materialPath;
        editor.markDirty();
        editor.statusMessage =
            "Applied " + materialPath + " to " +
            brush.faces[static_cast<std::size_t>(d.selectedFace)].id;
        return true;
    }

    for (slopengine::BrushFace& face : brush.faces) {
        face.material = materialPath;
    }
    editor.markDirty();
    editor.statusMessage = "Applied " + materialPath + " to " + brush.id;
    return true;
}

MaterialBrowserResult MaterialBrowser::draw(Editor& editor, float posX, float posY, float width, float height) {
    MaterialBrowserResult result{};
    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!ImGui::Begin("Materials", nullptr, flags)) {
        ImGui::End();
        return result;
    }

    if (ImGui::Button("Refresh")) {
        result.requestRescan = true;
    }
    ImGui::SameLine();
    ImGui::Text("%d materials", static_cast<int>(materials.size()));

    ImGui::InputTextWithHint("##matfilter", "Filter…", filter, sizeof(filter));
    ImGui::Text("Active: %s", editor.doc().defaultMaterial.c_str());
    ImGui::Text(
        "Selection (%s): %s",
        editor.doc().scope == SelectionScope::Face ? "face" : "brush",
        selectionMaterialLabel(editor.doc()).c_str());

    ImGui::Separator();
    if (ImGui::BeginChild("##matlist", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        const std::string filterStr = filter;
        for (const std::string& path : materials) {
            if (!containsIgnoreCase(path, filterStr)) {
                continue;
            }
            const bool isActive = path == editor.doc().defaultMaterial;
            if (ImGui::Selectable(path.c_str(), isActive)) {
                result.applied = applyMaterialToSelection(editor, path);
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();
    return result;
}

}
