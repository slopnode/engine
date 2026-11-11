#include "browser.hpp"

#include "ui/icon_ui.hpp"
#include "ui/slopui_widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <unordered_map>

namespace slopmodel {

namespace {

std::string genericUtf8(const std::filesystem::path& path) {
    const std::u8string generic = path.generic_u8string();
    return std::string(generic.begin(), generic.end());
}

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

std::vector<GeoEntry> scanGeos(const slopengine::AssetStore& assets) {
    std::unordered_map<std::string, std::string> owningPackage;
    for (const slopengine::Package& package : assets.packages()) {
        const std::filesystem::path root = package.root() / "geometry";
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) {
            continue;
        }
        for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec;
             it.increment(ec)) {
            if (ec || !it->is_regular_file() || it->path().extension() != ".geo") {
                continue;
            }
            std::error_code relEc;
            std::filesystem::path relative = std::filesystem::relative(it->path(), root, relEc);
            if (relEc) {
                continue;
            }
            relative.replace_extension();
            owningPackage[genericUtf8(relative)] = package.meta().id;
        }
    }
    std::vector<GeoEntry> entries;
    entries.reserve(owningPackage.size());
    for (const auto& [path, packageId] : owningPackage) {
        entries.push_back({path, packageId});
    }
    std::sort(entries.begin(), entries.end(), [](const GeoEntry& a, const GeoEntry& b) {
        return a.virtualPath < b.virtualPath;
    });
    return entries;
}

void ModelBrowser::rescan(const slopengine::AssetStore& assets) {
    entries = scanGeos(assets);
}

void ModelBrowser::draw(Editor& editor, slopengine::AssetStore& assets) {
    constexpr const char* kIcons = slopengine::kDefaultIconSet;

    ImGui::TextUnformatted("Models");
    ImGui::SetNextItemWidth(-1.0f);
    char filterBuf[128] = {};
    std::snprintf(filterBuf, sizeof(filterBuf), "%s", filter.c_str());
    if (ImGui::InputText("##geofilter", filterBuf, sizeof(filterBuf))) {
        filter = filterBuf;
    }
    if (slopengine::buttonWithIcon(assets, kIcons, "arrow_refresh", "Rescan", ImVec2(-1.0f, 0.0f))) {
        rescan(assets);
    }
    ImGui::Separator();

    const float listHeight = editor.doc.open ? ImGui::GetContentRegionAvail().y * 0.35f : 0.0f;
    if (slopengine::slopuiBeginChild("##geolist", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
        for (const GeoEntry& entry : entries) {
            if (!containsIgnoreCase(entry.virtualPath, filter) &&
                !containsIgnoreCase(entry.packageId, filter)) {
                continue;
            }
            const bool selected = editor.doc.open && editor.doc.virtualPath == entry.virtualPath;
            const std::string label = entry.virtualPath + "##" + entry.packageId;
            if (slopengine::selectableWithIcon(assets, kIcons, "package", label.c_str(), selected)) {
                if (!selected) {
                    editor.load(assets, entry.virtualPath);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", entry.packageId.c_str());
            }
        }
    }
    ImGui::EndChild();
}

void GeoPicker::rescan(const slopengine::AssetStore& assets) {
    entries = scanGeos(assets);
}

bool GeoPicker::drawModal(slopengine::AssetStore& assets, std::string& outPath) {
    constexpr const char* kIcons = slopengine::kDefaultIconSet;
    bool picked = false;
    if (open) {
        ImGui::OpenPopup("Pick Geo");
        open = false;
    }
    if (ImGui::BeginPopupModal("Pick Geo", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(360.0f);
        char filterBuf[128] = {};
        std::snprintf(filterBuf, sizeof(filterBuf), "%s", filter.c_str());
        if (ImGui::InputTextWithHint("##pickfilter", "filter", filterBuf, sizeof(filterBuf))) {
            filter = filterBuf;
        }
        if (slopengine::slopuiBeginChild("##picklist", ImVec2(360.0f, 320.0f), ImGuiChildFlags_Borders)) {
            for (const GeoEntry& entry : entries) {
                if (!containsIgnoreCase(entry.virtualPath, filter)) {
                    continue;
                }
                const std::string label = entry.virtualPath + "##" + entry.packageId;
                if (ImGui::Selectable(label.c_str(), false)) {
                    outPath = entry.virtualPath;
                    picked = true;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndChild();
        if (slopengine::buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return picked;
}

}
