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

std::string basenameOf(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

bool drawThumbImage(const MaterialThumbLookup& thumb, float size) {
    if (!thumb.valid()) {
        return false;
    }
    ImGui::Image(
        ImTextureID(static_cast<intptr_t>(thumb.texture->id)),
        ImVec2(size, size),
        ImVec2(thumb.u0, thumb.v0),
        ImVec2(thumb.u1, thumb.v1));
    return true;
}

bool drawThumbButton(const char* id, const MaterialThumbLookup& thumb, float size) {
    if (!thumb.valid()) {
        return false;
    }
    return ImGui::ImageButton(
        id,
        ImTextureID(static_cast<intptr_t>(thumb.texture->id)),
        ImVec2(size, size),
        ImVec2(thumb.u0, thumb.v0),
        ImVec2(thumb.u1, thumb.v1));
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
    thumbsDirty = true;
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
    EditorSettings& settings,
    float bodyHeight) {
    MaterialBrowserResult result{};
    if (!ImGui::BeginChild("##matsection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return result;
    }

    if (slopengine::buttonWithIcon(assets, slopengine::kDefaultIconSet, "arrow_refresh", "Refresh")) {
        result.requestRescan = true;
        thumbsDirty = true;
    }
    ImGui::SameLine();
    ImGui::Text("%d materials", static_cast<int>(materials.size()));
    ImGui::SameLine();
    {
        const float sz = ImGui::GetFrameHeight();
        const float toggleW = sz * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        const float right = ImGui::GetContentRegionAvail().x;
        if (right > toggleW) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + right - toggleW);
        }
        auto modeButton = [&](const char* iconId, MaterialViewMode mode) {
            ImGui::PushID(iconId);
            const bool active = settings.materialViewMode == mode;
            if (active) {
                ImGui::PushStyleColor(
                    ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##mode", ImVec2(sz, 0.0f))) {
                if (settings.materialViewMode != mode) {
                    settings.materialViewMode = mode;
                    settings.save();
                }
            }
            if (active) {
                ImGui::PopStyleColor();
            }
            const ImVec2 restore = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(
                ImVec2(pos.x + (sz - 16.0f) * 0.5f, pos.y + (sz - 16.0f) * 0.5f));
            slopengine::drawIconImGui(assets, slopengine::kDefaultIconSet, iconId, 16.0f);
            ImGui::SetCursorScreenPos(restore);
            ImGui::PopID();
        };
        modeButton("application_view_list", MaterialViewMode::List);
        ImGui::SameLine();
        modeButton("application_view_icons", MaterialViewMode::Icons);
    }

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

    if (thumbsDirty) {
        thumbs.ensure(assets, materials, settings.resolvedThumbnailCachePath());
        thumbsDirty = false;
    }

    ImGui::Separator();
    if (ImGui::BeginChild("##matlist", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        const std::string filterStr = filter;
        if (settings.materialViewMode == MaterialViewMode::List) {
            constexpr float kListThumb = 20.0f;
            for (const std::string& path : materials) {
                if (!containsIgnoreCase(path, filterStr)) {
                    continue;
                }
                ImGui::PushID(path.c_str());
                const bool isActive = path == editor.doc().defaultMaterial;
                const MaterialThumbLookup thumb = thumbs.lookup(path);
                if (!drawThumbImage(thumb, kListThumb)) {
                    slopengine::drawIconImGui(
                        assets, slopengine::kDefaultIconSet, "palette", kListThumb);
                }
                ImGui::SameLine();
                if (ImGui::Selectable(path.c_str(), isActive)) {
                    result.applied = applyMaterialToSelection(editor, path);
                }
                ImGui::PopID();
            }
        } else {
            constexpr float kGridThumb = 56.0f;
            constexpr float kCellPad = 6.0f;
            const float cellW = kGridThumb + kCellPad * 2.0f;
            const float avail = ImGui::GetContentRegionAvail().x;
            int columns = static_cast<int>(avail / cellW);
            if (columns < 1) {
                columns = 1;
            }
            int index = 0;
            for (const std::string& path : materials) {
                if (!containsIgnoreCase(path, filterStr)) {
                    continue;
                }
                if (index > 0 && (index % columns) != 0) {
                    ImGui::SameLine();
                }
                ImGui::PushID(path.c_str());
                const bool isActive = path == editor.doc().defaultMaterial;
                ImGui::BeginGroup();
                const MaterialThumbLookup thumb = thumbs.lookup(path);
                bool clicked = false;
                if (thumb.valid()) {
                    if (isActive) {
                        ImGui::PushStyleColor(
                            ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    }
                    clicked = drawThumbButton("##thumb", thumb, kGridThumb);
                    if (isActive) {
                        ImGui::PopStyleColor();
                    }
                } else {
                    clicked = ImGui::Button("##thumb", ImVec2(kGridThumb, kGridThumb));
                    const ImVec2 min = ImGui::GetItemRectMin();
                    const ImVec2 restore = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(ImVec2(
                        min.x + (kGridThumb - 16.0f) * 0.5f,
                        min.y + (kGridThumb - 16.0f) * 0.5f));
                    slopengine::drawIconImGui(
                        assets, slopengine::kDefaultIconSet, "palette", 16.0f);
                    ImGui::SetCursorScreenPos(restore);
                }
                const std::string label = basenameOf(path);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kGridThumb);
                if (isActive) {
                    ImGui::TextColored(
                        ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive), "%s", label.c_str());
                } else {
                    ImGui::TextUnformatted(label.c_str());
                }
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", path.c_str());
                }
                if (clicked || ImGui::IsItemClicked()) {
                    result.applied = applyMaterialToSelection(editor, path);
                }
                ImGui::PopID();
                ++index;
            }
        }
    }
    ImGui::EndChild();

    ImGui::EndChild();
    return result;
}

}
