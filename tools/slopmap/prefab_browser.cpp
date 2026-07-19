#include "prefab_browser.hpp"

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

void PrefabBrowser::rescan(const slopengine::AssetStore& assets) {
    std::unordered_map<std::string, bool> seen;
    for (const slopengine::Package& package : assets.packages()) {
        const std::filesystem::path prefabsRoot = package.root() / "prefabs";
        if (!std::filesystem::exists(prefabsRoot)) {
            continue;
        }
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(prefabsRoot, ec), end;
             it != end && !ec;
             it.increment(ec)) {
            if (ec || !it->is_regular_file()) {
                continue;
            }
            if (it->path().extension() != ".csg") {
                continue;
            }
            std::error_code relEc;
            std::filesystem::path relative =
                std::filesystem::relative(it->path(), prefabsRoot, relEc);
            if (relEc) {
                continue;
            }
            relative.replace_extension();
            seen[relative.generic_string()] = true;
        }
    }

    prefabs.clear();
    prefabs.reserve(seen.size());
    for (const auto& [path, _] : seen) {
        prefabs.push_back(path);
    }
    std::sort(prefabs.begin(), prefabs.end());
}

PrefabBrowserResult PrefabBrowser::draw(Editor& editor, float posX, float posY, float width, float height) {
    PrefabBrowserResult result{};
    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!ImGui::Begin("Prefabs", nullptr, flags)) {
        ImGui::End();
        return result;
    }

    if (ImGui::Button("Refresh")) {
        result.requestRescan = true;
    }
    ImGui::SameLine();
    ImGui::Text("%d prefabs", static_cast<int>(prefabs.size()));

    ImGui::InputTextWithHint("##prefilter", "Filter…", filter, sizeof(filter));
    ImGui::Text(
        "Place: %s",
        editor.placePrefabPath.empty() ? "(none)" : editor.placePrefabPath.c_str());

    ImGui::Separator();
    if (ImGui::BeginChild("##prelist", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        const std::string filterStr = filter;
        for (const std::string& path : prefabs) {
            if (!containsIgnoreCase(path, filterStr)) {
                continue;
            }
            const bool isActive = path == editor.placePrefabPath;
            if (ImGui::Selectable(path.c_str(), isActive)) {
                editor.placePrefabPath = path;
                result.selected = true;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                editor.placePrefabPath = path;
                result.openRequested = true;
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();
    return result;
}

}
