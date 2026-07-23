#include "material_browser.hpp"

#include "ui/icon_ui.hpp"

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
    if (doc.selectionMode == SelectionMode::Face) {
        if (doc.selectedFaces.empty()) {
            return "none";
        }
        std::string first;
        bool haveFirst = false;
        for (const FaceRef& ref : doc.selectedFaces) {
            if (!ref.valid() || ref.brush >= static_cast<int>(doc.brushes.size())) {
                continue;
            }
            const slopengine::Brush& brush = doc.brushes[static_cast<std::size_t>(ref.brush)];
            if (ref.face >= static_cast<int>(brush.faces.size())) {
                continue;
            }
            const std::string& mat = brush.faces[static_cast<std::size_t>(ref.face)].material;
            if (!haveFirst) {
                first = mat;
                haveFirst = true;
            } else if (mat != first) {
                return "mixed";
            }
        }
        if (!haveFirst) {
            return "none";
        }
        return first.empty() ? "(empty)" : first;
    }

    if (doc.selectionMode != SelectionMode::Brush || doc.selectedBrushes.empty()) {
        return "none";
    }
    std::string first;
    bool haveFirst = false;
    for (int index : doc.selectedBrushes) {
        if (index < 0 || index >= static_cast<int>(doc.brushes.size())) {
            continue;
        }
        const slopengine::Brush& brush = doc.brushes[static_cast<std::size_t>(index)];
        for (const slopengine::BrushFace& face : brush.faces) {
            if (!haveFirst) {
                first = face.material;
                haveFirst = true;
            } else if (face.material != first) {
                return "mixed";
            }
        }
    }
    if (!haveFirst) {
        return "none";
    }
    return first.empty() ? "(empty)" : first;
}

bool applyMaterialToSelection(Editor& editor, const std::string& materialPath) {
    EditorDocument& d = editor.doc();
    d.defaultMaterial = materialPath;

    if (d.selectionMode == SelectionMode::Face) {
        if (d.selectedFaces.empty()) {
            editor.statusMessage = "Active material: " + materialPath;
            return false;
        }
        int count = 0;
        for (const FaceRef& ref : d.selectedFaces) {
            if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
            if (ref.face >= static_cast<int>(brush.faces.size())) {
                continue;
            }
            brush.faces[static_cast<std::size_t>(ref.face)].material = materialPath;
            ++count;
        }
        if (count == 0) {
            editor.statusMessage = "Active material: " + materialPath;
            return false;
        }
        editor.markDirty();
        editor.markFacDirty();
        editor.statusMessage =
            "Applied " + materialPath + " to " + std::to_string(count) + " face(s)";
        return true;
    }

    if (d.selectionMode != SelectionMode::Brush || d.selectedBrushes.empty()) {
        editor.statusMessage = "Active material: " + materialPath;
        return false;
    }

    int count = 0;
    for (int index : d.selectedBrushes) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(index)];
        for (slopengine::BrushFace& face : brush.faces) {
            face.material = materialPath;
        }
        ++count;
    }
    editor.markDirty();
    editor.markFacDirty();
    editor.statusMessage =
        "Applied " + materialPath + " to " + std::to_string(count) + " brush(es)";
    return true;
}

MaterialBrowserResult MaterialBrowser::drawSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    float bodyHeight) {
    MaterialBrowserResult result{};
    if (!ImGui::BeginChild("##matsection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return result;
    }

    if (slopengine::buttonWithIcon(assets, slopengine::kDefaultIconSet, "arrow_refresh", "Refresh")) {
        result.requestRescan = true;
    }
    ImGui::SameLine();
    ImGui::Text("%d materials", static_cast<int>(materials.size()));

    ImGui::InputTextWithHint("##matfilter", "Filter…", filter, sizeof(filter));
    ImGui::Text("Active: %s", editor.doc().defaultMaterial.c_str());
    const char* scopeLabel = "brush";
    if (editor.doc().selectionMode == SelectionMode::Face) {
        scopeLabel = "face";
    } else if (editor.doc().selectionMode == SelectionMode::Entity) {
        scopeLabel = "entity";
    }
    ImGui::Text(
        "Selection (%s): %s",
        scopeLabel,
        selectionMaterialLabel(editor.doc()).c_str());

    ImGui::Separator();
    if (ImGui::BeginChild("##matlist", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        const std::string filterStr = filter;
        for (const std::string& path : materials) {
            if (!containsIgnoreCase(path, filterStr)) {
                continue;
            }
            ImGui::PushID(path.c_str());
            const bool isActive = path == editor.doc().defaultMaterial;
            if (slopengine::selectableWithIcon(assets, slopengine::kDefaultIconSet, "palette", path.c_str(), isActive)) {
                result.applied = applyMaterialToSelection(editor, path);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::EndChild();
    return result;
}

}
