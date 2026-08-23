#include "material_browser.hpp"

#include "assets/icon_atlas.hpp"
#include "brush_panel.hpp"
#include "texture_panel.hpp"
#include "ui/icon_ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>
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


bool isSkyMaterial(slopengine::AssetStore& assets, const std::string& path) {
    const slopengine::MaterialAsset* asset = assets.getMaterialAsset(path);
    return asset != nullptr && asset->sky;
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

std::string basenameOf(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

bool isDirectChildOfFolder(const std::string& path, const std::string& folder) {
    if (folder.empty()) {
        return path.find('/') == std::string::npos;
    }
    const std::string prefix = folder + "/";
    if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    return path.find('/', prefix.size()) == std::string::npos;
}

std::vector<std::string> immediateSubfolders(
    const std::vector<std::string>& paths,
    const std::string& folder) {
    std::set<std::string> subs;
    if (folder.empty()) {
        for (const std::string& path : paths) {
            const std::size_t slash = path.find('/');
            if (slash != std::string::npos) {
                subs.insert(path.substr(0, slash));
            }
        }
    } else {
        const std::string prefix = folder + "/";
        for (const std::string& path : paths) {
            if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            const std::string rest = path.substr(prefix.size());
            const std::size_t slash = rest.find('/');
            if (slash != std::string::npos) {
                subs.insert(rest.substr(0, slash));
            }
        }
    }
    return {subs.begin(), subs.end()};
}

bool applyPickerSelection(
    const std::string& path,
    MaterialBrowser& browser,
    MaterialBrowserResult& result) {
    if (browser.pickerCallback) {
        browser.pickerCallback(path);
    }
    result.applied = true;
    browser.pickerOpen = false;
    return true;
}

void drawViewModeToggle(
    slopengine::AssetStore& assets,
    EditorSettings& settings) {
    const float sz = ImGui::GetFrameHeight();
    auto modeButton = [&](const char* id, const char* iconId, MaterialViewMode mode, const char* tooltip) {
        ImGui::PushID(id);
        const bool active = settings.materialViewMode == mode;
        if (active) {
            ImGui::PushStyleColor(
                ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (slopengine::iconButton(assets, slopengine::kDefaultIconSet, iconId, ImVec2(sz, 0.0f))) {
            if (settings.materialViewMode != mode) {
                settings.materialViewMode = mode;
                settings.save();
            }
        }
        if (active) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        ImGui::PopID();
    };
    modeButton("application_view_icons", "application_view_icons", MaterialViewMode::Grid, "Grid view");
    ImGui::SameLine();
    modeButton("application_view_list", "application_view_list", MaterialViewMode::List, "List view");
    ImGui::SameLine();
    modeButton("folder", "folder", MaterialViewMode::Folder, "Folder view");
}

bool drawPickerListView(
    MaterialBrowser& browser,
    slopengine::AssetStore& assets,
    AssetPickerKind kind,
    const std::vector<std::string>& visible,
    MaterialBrowserResult& result) {
    constexpr float kListThumb = 20.0f;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::string& path = visible[static_cast<std::size_t>(row)];
            ImGui::PushID(path.c_str());
            if (kind == AssetPickerKind::Material) {
                const bool skyMat = isSkyMaterial(assets, path);
                const MaterialThumbLookup thumb = browser.thumbs.lookup(path);
                if (!drawThumbImage(thumb, kListThumb)) {
                    slopengine::drawIconImGui(
                        assets,
                        slopengine::kDefaultIconSet,
                        skyMat ? "image" : "palette",
                        kListThumb);
                }
                ImGui::SameLine();
            }
            if (ImGui::Selectable(path.c_str(), false)) {
                applyPickerSelection(path, browser, result);
                ImGui::PopID();
                return true;
            }
            ImGui::PopID();
        }
    }
    return false;
}

bool drawPickerGridView(
    MaterialBrowser& browser,
    slopengine::AssetStore& assets,
    AssetPickerKind kind,
    const std::vector<std::string>& visible,
    MaterialBrowserResult& result) {
    constexpr float kGridThumb = 56.0f;
    constexpr float kCellPad = 6.0f;
    const float cellW = kGridThumb + kCellPad * 2.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    int columns = static_cast<int>(avail / cellW);
    if (columns < 1) {
        columns = 1;
    }
    const int rowCount =
        static_cast<int>((visible.size() + static_cast<std::size_t>(columns) - 1) / columns);

    ImGuiListClipper clipper;
    clipper.Begin(rowCount);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int col = 0; col < columns; ++col) {
                const int index = row * columns + col;
                if (index >= static_cast<int>(visible.size())) {
                    break;
                }
                if (col > 0) {
                    ImGui::SameLine();
                }
                const std::string& path = visible[static_cast<std::size_t>(index)];
                ImGui::PushID(path.c_str());
                const bool skyMat = kind == AssetPickerKind::Material && isSkyMaterial(assets, path);
                ImGui::BeginGroup();
                bool clicked = false;
                if (kind == AssetPickerKind::Material) {
                    const MaterialThumbLookup thumb = browser.thumbs.lookup(path);
                    if (thumb.valid()) {
                        clicked = drawThumbButton("##thumb", thumb, kGridThumb);
                    } else {
                        clicked = ImGui::Button("##thumb", ImVec2(kGridThumb, kGridThumb));
                        const ImVec2 min = ImGui::GetItemRectMin();
                        const ImVec2 restore = ImGui::GetCursorScreenPos();
                        ImGui::SetCursorScreenPos(ImVec2(
                            min.x + (kGridThumb - 16.0f) * 0.5f,
                            min.y + (kGridThumb - 16.0f) * 0.5f));
                        slopengine::drawIconImGui(
                            assets,
                            slopengine::kDefaultIconSet,
                            skyMat ? "image" : "palette",
                            16.0f);
                        ImGui::SetCursorScreenPos(restore);
                    }
                } else {
                    clicked = ImGui::Button("##thumb", ImVec2(kGridThumb, kGridThumb));
                    const ImVec2 min = ImGui::GetItemRectMin();
                    const ImVec2 restore = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(ImVec2(
                        min.x + (kGridThumb - 16.0f) * 0.5f,
                        min.y + (kGridThumb - 16.0f) * 0.5f));
                    slopengine::drawIconImGui(
                        assets, slopengine::kDefaultIconSet, "image", 16.0f);
                    ImGui::SetCursorScreenPos(restore);
                }
                const std::string label = basenameOf(path);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kGridThumb + kCellPad);
                ImGui::TextUnformatted(label.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", path.c_str());
                }
                if (clicked || ImGui::IsItemClicked()) {
                    applyPickerSelection(path, browser, result);
                    ImGui::PopID();
                    return true;
                }
                ImGui::PopID();
            }
        }
    }
    return false;
}

bool drawPickerFolderView(
    MaterialBrowser& browser,
    slopengine::AssetStore& assets,
    AssetPickerKind kind,
    const std::vector<std::string>& visible,
    MaterialBrowserResult& result) {
    if (!browser.browseFolderPath.empty()) {
        if (ImGui::Selectable("../", false)) {
            const std::size_t slash = browser.browseFolderPath.find_last_of('/');
            if (slash == std::string::npos) {
                browser.browseFolderPath.clear();
            } else {
                browser.browseFolderPath = browser.browseFolderPath.substr(0, slash);
            }
        }
    }

    const std::vector<std::string> subfolders =
        immediateSubfolders(visible, browser.browseFolderPath);
    for (const std::string& sub : subfolders) {
        ImGui::PushID(sub.c_str());
        const std::string fullFolder = browser.browseFolderPath.empty()
            ? sub
            : browser.browseFolderPath + "/" + sub;
        if (slopengine::buttonWithIcon(
                assets, slopengine::kDefaultIconSet, "folder", sub.c_str())) {
            browser.browseFolderPath = fullFolder;
        }
        ImGui::PopID();
    }

    if (!subfolders.empty()) {
        ImGui::Separator();
    }

    std::vector<std::string> here;
    here.reserve(visible.size());
    for (const std::string& path : visible) {
        if (isDirectChildOfFolder(path, browser.browseFolderPath)) {
            here.push_back(path);
        }
    }

    if (here.empty() && subfolders.empty()) {
        ImGui::TextDisabled("No matching assets in this folder");
        return false;
    }

    constexpr float kListThumb = 20.0f;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(here.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::string& path = here[static_cast<std::size_t>(row)];
            const std::string label = basenameOf(path);
            ImGui::PushID(path.c_str());
            if (kind == AssetPickerKind::Material) {
                const bool skyMat = isSkyMaterial(assets, path);
                const MaterialThumbLookup thumb = browser.thumbs.lookup(path);
                if (!drawThumbImage(thumb, kListThumb)) {
                    slopengine::drawIconImGui(
                        assets,
                        slopengine::kDefaultIconSet,
                        skyMat ? "image" : "palette",
                        kListThumb);
                }
                ImGui::SameLine();
            }
            if (ImGui::Selectable(label.c_str(), false)) {
                applyPickerSelection(path, browser, result);
                ImGui::PopID();
                return true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", path.c_str());
            }
            ImGui::PopID();
        }
    }
    return false;
}

void scanAssetsInto(
    const slopengine::AssetStore& assets,
    const char* folder,
    const char* extension,
    std::vector<std::string>& out) {
    std::unordered_map<std::string, bool> seen;
    for (const slopengine::Package& package : assets.packages()) {
        const std::filesystem::path root = package.root() / folder;
        if (!std::filesystem::exists(root)) {
            continue;
        }
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec;
             it.increment(ec)) {
            if (ec || !it->is_regular_file()) {
                continue;
            }
            if (it->path().extension() != extension) {
                continue;
            }
            std::error_code relEc;
            std::filesystem::path relative = std::filesystem::relative(it->path(), root, relEc);
            if (relEc) {
                continue;
            }
            relative.replace_extension();
            seen[relative.generic_string()] = true;
        }
    }
    out.clear();
    out.reserve(seen.size());
    for (const auto& [path, _] : seen) {
        out.push_back(path);
    }
    std::sort(out.begin(), out.end());
}

void rebuildFolderPrefixes(const std::vector<std::string>& paths, std::vector<std::string>& out) {
    std::set<std::string> folders;
    for (const std::string& path : paths) {
        std::size_t pos = 0;
        while ((pos = path.find('/', pos)) != std::string::npos) {
            folders.insert(path.substr(0, pos));
            ++pos;
        }
    }
    out.clear();
    out.push_back("");
    out.insert(out.end(), folders.begin(), folders.end());
    std::sort(out.begin() + 1, out.end());
}

bool pathInFolderScope(const std::string& path, const std::string& folder) {
    if (folder.empty()) {
        return true;
    }
    if (path.size() <= folder.size()) {
        return path == folder;
    }
    return path.compare(0, folder.size(), folder) == 0 && path[folder.size()] == '/';
}

bool assetPassesFilter(
    slopengine::AssetStore& assets,
    const std::string& path,
    const std::string& filterStr,
    const std::string& folderScope,
    bool skyOnly) {
    if (!pathInFolderScope(path, folderScope)) {
        return false;
    }
    if (skyOnly && !isSkyMaterial(assets, path)) {
        return false;
    }
    return containsIgnoreCase(path, filterStr);
}

void drawMaterialPreviewBox(float size) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        pos,
        ImVec2(pos.x + size, pos.y + size),
        ImGui::GetColorU32(ImGuiCol_FrameBg));
    draw->AddRect(
        pos,
        ImVec2(pos.x + size, pos.y + size),
        ImGui::GetColorU32(ImGuiCol_Border));
}

void drawMaterialPreview(
    slopengine::AssetStore& assets,
    MaterialThumbAtlas& thumbs,
    const std::string& materialLabel,
    float size) {
    const ImVec2 boxPos = ImGui::GetCursorScreenPos();
    drawMaterialPreviewBox(size);

    if (materialLabel == "mixed" || materialLabel == "none" || materialLabel == "(empty)") {
        return;
    }

    const MaterialThumbLookup thumb = thumbs.lookup(materialLabel);
    if (thumb.valid()) {
        const float drawW = size;
        const float drawH = size;
        ImGui::GetWindowDrawList()->AddImage(
            ImTextureID(static_cast<intptr_t>(thumb.texture->id)),
            boxPos,
            ImVec2(boxPos.x + drawW, boxPos.y + drawH),
            ImVec2(thumb.u0, thumb.v0),
            ImVec2(thumb.u1, thumb.v1),
            IM_COL32_WHITE);
        return;
    }

    const slopengine::MaterialAsset* asset = assets.getMaterialAsset(materialLabel);
    if (asset == nullptr || asset->albedoTexture.empty()) {
        return;
    }
    Texture2D texture = assets.getTexture(asset->albedoTexture);
    if (texture.id == 0 || texture.width <= 0 || texture.height <= 0) {
        return;
    }

    const float texW = static_cast<float>(texture.width);
    const float texH = static_cast<float>(texture.height);
    const float scale = std::min(size / texW, size / texH);
    const float drawW = texW * scale;
    const float drawH = texH * scale;
    const ImVec2 min{
        boxPos.x + (size - drawW) * 0.5f,
        boxPos.y + (size - drawH) * 0.5f,
    };
    const ImVec2 max{min.x + drawW, min.y + drawH};

    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    ImGui::GetWindowDrawList()->AddImage(
        ImTextureID(static_cast<intptr_t>(texture.id)),
        min,
        max,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        IM_COL32_WHITE);
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
}

} // namespace

void MaterialBrowser::rescan(const slopengine::AssetStore& assets) {
    scanAssetsInto(assets, "materials", ".mat", materials);
    scanAssetsInto(assets, "textures", ".png", textures);
    rebuildFolderPrefixes(materials, folderPrefixes);
    thumbsDirty = true;
}

void MaterialBrowser::openPicker(
    AssetPickerKind kind,
    std::function<void(const std::string&)> callback,
    AssetPickerOptions options) {
    pickerKind = kind;
    pickerCallback = std::move(callback);
    pickerSkyOnly = options.skyMaterialsOnly;
    pickerAllowNone = options.allowNone;
    pickerOpen = true;
    skyMaterialsOnly = options.skyMaterialsOnly;
    folderScopeIndex = 0;
    browseFolderPath.clear();
    if (options.initialFilter != nullptr && options.initialFilter[0] != '\0') {
        std::strncpy(filter, options.initialFilter, sizeof(filter) - 1);
        filter[sizeof(filter) - 1] = '\0';
    } else {
        filter[0] = '\0';
    }
}

MaterialBrowserResult MaterialBrowser::drawPopup(
    slopengine::AssetStore& assets,
    EditorSettings& settings) {
    MaterialBrowserResult result{};
    if (!pickerOpen) {
        return result;
    }

    const char* title =
        pickerKind == AssetPickerKind::Material ? "Browse Materials" : "Browse Textures";
    ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, &pickerOpen, ImGuiWindowFlags_None)) {
        ImGui::End();
        return result;
    }

    const std::vector<std::string>& allPaths =
        pickerKind == AssetPickerKind::Material ? materials : textures;
    std::vector<std::string> popupFolders;
    rebuildFolderPrefixes(allPaths, popupFolders);

    if (slopengine::buttonWithIcon(assets, slopengine::kDefaultIconSet, "arrow_refresh", "Refresh")) {
        result.requestRescan = true;
        thumbsDirty = true;
    }
    ImGui::SameLine();
    ImGui::Text("%d assets", static_cast<int>(allPaths.size()));
    ImGui::SameLine();
    {
        const float toggleW = ImGui::GetFrameHeight() * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
        const float right = ImGui::GetContentRegionAvail().x;
        if (right > toggleW) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + right - toggleW);
        }
        drawViewModeToggle(assets, settings);
    }

    ImGui::InputTextWithHint("##pickerfilter", "Filter…", filter, sizeof(filter));
    if (pickerKind == AssetPickerKind::Material) {
        ImGui::Checkbox("Sky materials only", &skyMaterialsOnly);
    }

    const bool folderView = settings.materialViewMode == MaterialViewMode::Folder;
    if (!folderView && popupFolders.size() > 1) {
        if (folderScopeIndex < 0 || folderScopeIndex >= static_cast<int>(popupFolders.size())) {
            folderScopeIndex = 0;
        }
        const char* preview =
            popupFolders[static_cast<std::size_t>(folderScopeIndex)].empty()
            ? "(all folders)"
            : popupFolders[static_cast<std::size_t>(folderScopeIndex)].c_str();
        if (ImGui::BeginCombo("Folder##picker", preview)) {
            for (int i = 0; i < static_cast<int>(popupFolders.size()); ++i) {
                const char* label = popupFolders[static_cast<std::size_t>(i)].empty()
                    ? "(all folders)"
                    : popupFolders[static_cast<std::size_t>(i)].c_str();
                if (ImGui::Selectable(label, folderScopeIndex == i)) {
                    folderScopeIndex = i;
                }
            }
            ImGui::EndCombo();
        }
    }

    if (pickerKind == AssetPickerKind::Material && thumbsDirty) {
        thumbs.ensure(assets, materials, settings.resolvedThumbnailCachePath());
        thumbsDirty = false;
    }

    ImGui::Separator();

    const std::string filterStr = filter;
    const std::string folderScope =
        folderScopeIndex >= 0 && folderScopeIndex < static_cast<int>(popupFolders.size())
        ? popupFolders[static_cast<std::size_t>(folderScopeIndex)]
        : std::string{};
    const bool skyOnly = pickerKind == AssetPickerKind::Material && skyMaterialsOnly;

    std::vector<std::string> visible;
    visible.reserve(allPaths.size());
    for (const std::string& path : allPaths) {
        if (pickerKind == AssetPickerKind::Material) {
            if (assetPassesFilter(assets, path, filterStr, folderScope, skyOnly)) {
                visible.push_back(path);
            }
        } else if (pathInFolderScope(path, folderScope) && containsIgnoreCase(path, filterStr)) {
            visible.push_back(path);
        }
    }

    if (skyOnly) {
        const auto defaultIt = std::find(visible.begin(), visible.end(), "engine/sky");
        if (defaultIt != visible.end() && defaultIt != visible.begin()) {
            std::rotate(visible.begin(), defaultIt, defaultIt + 1);
        }
    }

    if (ImGui::BeginChild("##pickerlist", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        if (folderView && !browseFolderPath.empty()) {
            ImGui::TextDisabled("Folder: %s", browseFolderPath.c_str());
            ImGui::Separator();
        }

        if (pickerAllowNone) {
            if (ImGui::Selectable("(none)", false)) {
                if (pickerCallback) {
                    pickerCallback({});
                }
                result.applied = true;
                pickerOpen = false;
                ImGui::EndChild();
                ImGui::End();
                return result;
            }
        }

        bool picked = false;
        switch (settings.materialViewMode) {
        case MaterialViewMode::Grid:
            picked = drawPickerGridView(*this, assets, pickerKind, visible, result);
            break;
        case MaterialViewMode::Folder:
            picked = drawPickerFolderView(*this, assets, pickerKind, visible, result);
            break;
        case MaterialViewMode::List:
        default:
            picked = drawPickerListView(*this, assets, pickerKind, visible, result);
            break;
        }
        if (picked) {
            ImGui::EndChild();
            ImGui::End();
            return result;
        }
    }
    ImGui::EndChild();

    ImGui::End();
    return result;
}

MaterialBrowserResult MaterialBrowser::drawSurfaceSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    EditorSettings& settings,
    TexturePanel& texturePanel,
    float bodyHeight) {
    MaterialBrowserResult result{};
    if (!ImGui::BeginChild("##surfacesection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return result;
    }

    constexpr float kHeaderPreview = 80.0f;
    const std::string activeMaterial = editor.doc().defaultMaterial;
    const std::string selectionMaterial = selectionMaterialLabel(editor.doc());

    const auto drawMaterialHeader = [&](const char* tableId, const std::string& material, auto&& body) {
        if (!ImGui::BeginTable(
                tableId,
                2,
                ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
            return;
        }
        ImGui::TableSetupColumn("thumb", ImGuiTableColumnFlags_WidthFixed, kHeaderPreview);
        ImGui::TableSetupColumn("details", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        drawMaterialPreview(assets, thumbs, material, kHeaderPreview);
        ImGui::TableSetColumnIndex(1);
        ImGui::PushItemWidth(-1.0f);
        body();
        ImGui::PopItemWidth();
        ImGui::EndTable();
    };

    const char* scopeLabel = "brush";
    if (editor.doc().selectionMode == SelectionMode::Face) {
        scopeLabel = "face";
    } else if (editor.doc().selectionMode == SelectionMode::Entity) {
        scopeLabel = "thing";
    }
    const bool hasApplicableSelection =
        (editor.doc().selectionMode == SelectionMode::Face && !editor.doc().selectedFaces.empty()) ||
        (editor.doc().selectionMode == SelectionMode::Brush && !editor.doc().selectedBrushes.empty());
    const bool canUseSelectionAsActive = selectionMaterial != "none" &&
        selectionMaterial != "mixed" && selectionMaterial != "(empty)";

    drawMaterialHeader("##active_mat", activeMaterial, [&] {
        ImGui::TextUnformatted("Active");
        ImGui::Spacing();
        ImGui::TextUnformatted(activeMaterial.c_str());
        ImGui::Spacing();
        if (slopengine::buttonWithIcon(
                assets,
                slopengine::kDefaultIconSet,
                "folder_page",
                "Browse…",
                ImVec2(-1.0f, 0.0f))) {
            openPicker(
                AssetPickerKind::Material,
                [&editor](const std::string& path) {
                    editor.doc().defaultMaterial = path;
                    editor.markDirty();
                    editor.statusMessage = "Active material: " + path;
                });
        }
        ImGui::BeginDisabled(!hasApplicableSelection);
        if (slopengine::buttonWithIcon(
                assets,
                slopengine::kDefaultIconSet,
                "accept",
                "Apply to Selection",
                ImVec2(-1.0f, 0.0f))) {
            applyMaterialToSelection(editor, activeMaterial);
        }
        ImGui::EndDisabled();
        if (hasApplicableSelection && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Apply the active material to the selected %s(s).", scopeLabel);
        }
    });

    ImGui::Separator();

    drawMaterialHeader("##selection_mat", selectionMaterial, [&] {
        ImGui::Text("Selection (%s)", scopeLabel);
        ImGui::Spacing();
        ImGui::TextUnformatted(selectionMaterial.c_str());
        ImGui::Spacing();
        if (slopengine::buttonWithIcon(
                assets,
                slopengine::kDefaultIconSet,
                "folder_page",
                "Browse…",
                ImVec2(-1.0f, 0.0f))) {
            openPicker(
                AssetPickerKind::Material,
                [&editor](const std::string& path) {
                    applyMaterialToSelection(editor, path);
                });
        }
        if (canUseSelectionAsActive) {
            if (slopengine::buttonWithIcon(
                    assets,
                    slopengine::kDefaultIconSet,
                    "accept",
                    "Set as Active",
                    ImVec2(-1.0f, 0.0f))) {
                editor.doc().defaultMaterial = selectionMaterial;
                editor.markDirty();
                editor.statusMessage = "Active material: " + selectionMaterial;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("Set active material for new brushes from selection.");
            }
        }
    });

    if (editor.doc().selectionMode == SelectionMode::Face &&
        !editor.doc().selectedFaces.empty()) {
        ImGui::Separator();
        if (slopengine::buttonWithIcon(
                assets,
                slopengine::kDefaultIconSet,
                "wand",
                "Select Touching",
                ImVec2(-1.0f, 0.0f))) {
            editor.selectTouchingFaces();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip(
                "Grow the face selection to every touching face with the same material, "
                "across brushes.");
        }
        if (drawFaceHandlerSection(editor)) {
            result.applied = true;
        }
    }

    if (thumbsDirty) {
        thumbs.ensure(assets, materials, settings.resolvedThumbnailCachePath());
        thumbsDirty = false;
    }

    ImGui::Separator();

    const float uvHeight = std::max(0.0f, ImGui::GetContentRegionAvail().y);
    const TexturePanelResult texResult =
        texturePanel.drawUvSection(editor, assets, uvHeight, true);
    if (texResult.changed) {
        result.applied = true;
    }

    ImGui::EndChild();
    return result;
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

    if (d.selectionMode == SelectionMode::Face) {
        if (d.selectedFaces.empty()) {
            d.defaultMaterial = materialPath;
            editor.statusMessage = "Active material: " + materialPath;
            return false;
        }
        editor.prepareEdit();
        d.defaultMaterial = materialPath;
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
            editor.abortEdit();
            editor.statusMessage = "Active material: " + materialPath;
            return false;
        }
        editor.markDirty();
        editor.markRadDirty();
        editor.endEdit();
        editor.statusMessage =
            "Applied " + materialPath + " to " + std::to_string(count) + " face(s)";
        return true;
    }

    if (d.selectionMode != SelectionMode::Brush || d.selectedBrushes.empty()) {
        d.defaultMaterial = materialPath;
        editor.statusMessage = "Active material: " + materialPath;
        return false;
    }

    editor.prepareEdit();
    d.defaultMaterial = materialPath;
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
    editor.markRadDirty();
    editor.endEdit();
    editor.statusMessage =
        "Applied " + materialPath + " to " + std::to_string(count) + " brush(es)";
    return true;
}

}
