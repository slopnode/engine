#include "browser.hpp"

#include "drag_drop.hpp"
#include "panel_common.hpp"

#include "ui/icon_ui.hpp"
#include "ui/slopui_widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <system_error>
#include <unordered_map>

namespace slopmat {

namespace {

void collectFromDir(
    const std::filesystem::path& dir,
    const std::string& packageId,
    const char* extension,
    std::unordered_map<std::string, std::string>& seen) {
    if (!std::filesystem::exists(dir)) {
        return;
    }
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
        if (ec || !it->is_regular_file() || it->path().extension() != extension) {
            continue;
        }
        std::error_code relEc;
        std::filesystem::path relative = std::filesystem::relative(it->path(), dir, relEc);
        if (relEc) {
            continue;
        }
        relative.replace_extension();
        const std::u8string generic = relative.generic_u8string();
        seen[std::string(generic.begin(), generic.end())] = packageId;
    }
}

const char* iconForKind(DocKind kind) {
    switch (kind) {
    case DocKind::Material: return "palette";
    case DocKind::TextureGraph: return "layers";
    case DocKind::TextureAnim: return "film";
    case DocKind::Texture: return "picture";
    }
    return "picture";
}

DragPayload payloadForKind(DocKind kind) {
    switch (kind) {
    case DocKind::Material: return DragPayload::Material;
    case DocKind::TextureGraph: return DragPayload::TextureGraph;
    case DocKind::TextureAnim: return DragPayload::TextureAnim;
    case DocKind::Texture: return DragPayload::Texture;
    }
    return DragPayload::Texture;
}

const char* tooltipForKind(DocKind kind) {
    switch (kind) {
    case DocKind::Material: return "material (.mat)";
    case DocKind::TextureGraph: return "texture graph (.texgraph): composited / procedural texture";
    case DocKind::TextureAnim: return "texture animation (.texanim): timed frames";
    case DocKind::Texture: return "texture (.png), view-only";
    }
    return "";
}

void drawEntryRow(Editor& editor, slopengine::AssetStore& assets, ThumbCache& thumbs, const BrowserEntry& entry, const char* label) {
    const float thumbSize = static_cast<float>(ThumbCache::kSize) * 0.5f;
    const bool selected = editor.doc.open && editor.doc.virtualPath == entry.virtualPath && editor.doc.kind == entry.kind;
    ImGui::PushID(entry.virtualPath.c_str());
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(thumbSize, thumbSize));
    const ImVec2 cellMin = ImGui::GetItemRectMin();
    const Texture2D thumb = thumbs.get(assets, entry.kind, entry.virtualPath);
    if (thumb.id != 0) {
        const float scale = thumbSize / static_cast<float>(std::max(thumb.width, thumb.height));
        const ImVec2 size(static_cast<float>(thumb.width) * scale, static_cast<float>(thumb.height) * scale);
        const ImVec2 p0(cellMin.x + (thumbSize - size.x) * 0.5f, cellMin.y + (thumbSize - size.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddImage(
            static_cast<ImTextureID>(static_cast<std::uintptr_t>(thumb.id)), p0, ImVec2(p0.x + size.x, p0.y + size.y));
    } else if (const std::optional<Rectangle> rect = assets.getIconRect(kIcons, iconForKind(entry.kind))) {
        const slopengine::IconAtlas* atlas = assets.getIconAtlas(kIcons);
        if (atlas != nullptr && atlas->texture.id != 0) {
            const float aw = static_cast<float>(atlas->texture.width);
            const float ah = static_cast<float>(atlas->texture.height);
            const ImVec2 p0(cellMin.x + (thumbSize - 16.0f) * 0.5f, cellMin.y + (thumbSize - 16.0f) * 0.5f);
            ImGui::GetWindowDrawList()->AddImage(
                static_cast<ImTextureID>(static_cast<std::uintptr_t>(atlas->texture.id)),
                p0,
                ImVec2(p0.x + 16.0f, p0.y + 16.0f),
                ImVec2(rect->x / aw, rect->y / ah),
                ImVec2((rect->x + rect->width) / aw, (rect->y + rect->height) / ah));
        }
    }
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    const bool clicked = ImGui::Selectable(label, selected, ImGuiSelectableFlags_None, ImVec2(0.0f, thumbSize));
    ImGui::PopStyleVar();
    beginDragSource(payloadForKind(entry.kind), entry.virtualPath);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\npackage: %s\n%s\n(drag onto a path field)", entry.virtualPath.c_str(), entry.packageId.c_str(), tooltipForKind(entry.kind));
    }
    ImGui::EndGroup();
    if (clicked) {
        switch (entry.kind) {
        case DocKind::Material: editor.loadMaterial(assets, entry.virtualPath); break;
        case DocKind::TextureGraph: editor.loadTextureGraph(assets, entry.virtualPath); break;
        case DocKind::TextureAnim: editor.loadTextureAnim(assets, entry.virtualPath); break;
        case DocKind::Texture: editor.loadTexture(assets, entry.virtualPath); break;
        }
    }
    ImGui::PopID();
}

void drawTreeNode(Editor& editor, slopengine::AssetStore& assets, ThumbCache& thumbs, const BrowserTreeNode& node, bool forceOpen, bool flat) {
    for (const BrowserTreeNode& folder : node.folders) {
        if (flat) {
            drawTreeNode(editor, assets, thumbs, folder, forceOpen, true);
            continue;
        }
        if (forceOpen) {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        ImGui::PushID(folder.name.c_str());
        const bool open = ImGui::TreeNodeEx(folder.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        ImGui::PopID();
        if (open) {
            drawTreeNode(editor, assets, thumbs, folder, forceOpen, false);
            ImGui::TreePop();
        }
    }
    for (const BrowserEntry* leaf : node.leaves) {
        const char* label = leaf->virtualPath.c_str();
        if (!flat) {
            const std::size_t slash = leaf->virtualPath.rfind('/');
            label = slash == std::string::npos ? label : label + slash + 1;
        }
        drawEntryRow(editor, assets, thumbs, *leaf, label);
    }
}

BrowserTreeNode& folderChild(BrowserTreeNode& node, const std::string& name) {
    for (BrowserTreeNode& child : node.folders) {
        if (child.name == name) {
            return child;
        }
    }
    node.folders.push_back(BrowserTreeNode{name, {}, {}});
    return node.folders.back();
}

void sortTree(BrowserTreeNode& node) {
    std::sort(node.folders.begin(), node.folders.end(), [](const BrowserTreeNode& a, const BrowserTreeNode& b) { return a.name < b.name; });
    std::sort(node.leaves.begin(), node.leaves.end(), [](const BrowserEntry* a, const BrowserEntry* b) { return a->virtualPath < b->virtualPath; });
    for (BrowserTreeNode& child : node.folders) {
        sortTree(child);
    }
}

} // namespace

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

void Browser::rescan(const slopengine::AssetStore& assets) {
    std::unordered_map<std::string, std::string> materials;
    std::unordered_map<std::string, std::string> graphs;
    std::unordered_map<std::string, std::string> textures;
    std::unordered_map<std::string, std::string> anims;
    packageOrder.clear();
    for (const slopengine::Package& package : assets.packages()) {
        packageOrder.push_back(package.meta().id);
        collectFromDir(package.root() / "materials", package.meta().id, ".mat", materials);
        collectFromDir(package.root() / "textures", package.meta().id, ".texgraph", graphs);
        collectFromDir(package.root() / "textures", package.meta().id, ".png", textures);
        collectFromDir(package.root() / "textures", package.meta().id, ".texanim", anims);
    }

    entries.clear();
    entries.reserve(materials.size() + graphs.size() + textures.size() + anims.size());
    for (const auto& [path, packageId] : materials) {
        entries.push_back({path, packageId, DocKind::Material});
    }
    for (const auto& [path, packageId] : graphs) {
        entries.push_back({path, packageId, DocKind::TextureGraph});
    }
    for (const auto& [path, packageId] : textures) {
        entries.push_back({path, packageId, DocKind::Texture});
    }
    for (const auto& [path, packageId] : anims) {
        entries.push_back({path, packageId, DocKind::TextureAnim});
    }
    std::sort(entries.begin(), entries.end(), [](const BrowserEntry& a, const BrowserEntry& b) {
        return a.virtualPath < b.virtualPath;
    });
    treeKey_.clear();
}

void Browser::rebuildTree() {
    packages_.clear();
    for (const std::string& packageId : packageOrder) {
        packages_.push_back(BrowserTreeNode{packageId, {}, {}});
    }
    for (const BrowserEntry& entry : entries) {
        if (entry.kind != activeTab) {
            continue;
        }
        if (!containsIgnoreCase(entry.virtualPath, filter) && !containsIgnoreCase(entry.packageId, filter)) {
            continue;
        }
        BrowserTreeNode* package = nullptr;
        for (BrowserTreeNode& candidate : packages_) {
            if (candidate.name == entry.packageId) {
                package = &candidate;
            }
        }
        if (package == nullptr) {
            packages_.push_back(BrowserTreeNode{entry.packageId, {}, {}});
            package = &packages_.back();
        }
        BrowserTreeNode* node = package;
        if (treeView) {
            std::size_t pos = 0;
            while (true) {
                const std::size_t slash = entry.virtualPath.find('/', pos);
                if (slash == std::string::npos) {
                    break;
                }
                node = &folderChild(*node, entry.virtualPath.substr(pos, slash - pos));
                pos = slash + 1;
            }
        }
        node->leaves.push_back(&entry);
    }
    for (BrowserTreeNode& package : packages_) {
        sortTree(package);
    }
}

std::vector<std::string> Browser::pathsOfKind(DocKind kind) const {
    std::vector<std::string> out;
    for (const BrowserEntry& entry : entries) {
        if (entry.kind == kind) {
            out.push_back(entry.virtualPath);
        }
    }
    return out;
}

void Browser::draw(Editor& editor, slopengine::AssetStore& assets, ThumbCache& thumbs) {
    const float buttonW = iconButtonWidth();
    char filterBuf[128] = {};
    std::snprintf(filterBuf, sizeof(filterBuf), "%s", filter.c_str());
    ImGui::SetNextItemWidth(-(buttonW + ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::InputTextWithHint("##filter", "filter", filterBuf, sizeof(filterBuf))) {
        filter = filterBuf;
    }
    ImGui::SameLine();
    if (slopengine::iconButton(assets, kIcons, "arrow_refresh", ImVec2(0.0f, 0.0f))) {
        rescan(assets);
        thumbs.clear();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Rescan mounted packages");
    }

    if (slopengine::slopuiBeginTabBar("##browserTabs")) {
        if (slopengine::slopuiBeginTabItem("Materials")) {
            activeTab = DocKind::Material;
            ImGui::EndTabItem();
        }
        if (slopengine::slopuiBeginTabItem("Graphs")) {
            activeTab = DocKind::TextureGraph;
            ImGui::EndTabItem();
        }
        if (slopengine::slopuiBeginTabItem("Animations")) {
            activeTab = DocKind::TextureAnim;
            ImGui::EndTabItem();
        }
        if (slopengine::slopuiBeginTabItem("Textures")) {
            activeTab = DocKind::Texture;
            ImGui::EndTabItem();
        }
        slopengine::slopuiEndTabBar();
    }

    if (ImGui::RadioButton("Tree", treeView)) {
        treeView = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("List", !treeView)) {
        treeView = false;
    }

    const std::string key = filter + "|" + std::to_string(static_cast<int>(activeTab)) + "|" + (treeView ? "t" : "l");
    if (key != treeKey_) {
        rebuildTree();
        treeKey_ = key;
    }

    if (slopengine::slopuiBeginChild("##browserlist", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        const bool forceOpen = !filter.empty();
        for (const BrowserTreeNode& package : packages_) {
            if (package.folders.empty() && package.leaves.empty()) {
                continue;
            }
            ImGui::PushID(package.name.c_str());
            ImGui::SetNextItemOpen(true, forceOpen ? ImGuiCond_Always : ImGuiCond_Once);
            const bool open = ImGui::TreeNodeEx(package.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed);
            if (open) {
                drawTreeNode(editor, assets, thumbs, package, forceOpen, !treeView);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

}
