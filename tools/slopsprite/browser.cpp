#include "browser.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <unordered_map>

namespace slopsprite {

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

void SpriteBrowser::rescan(const slopengine::AssetStore& assets) {
    std::unordered_map<std::string, std::string> owningPackage;
    for (const slopengine::Package& package : assets.packages()) {
        const std::filesystem::path spritesRoot = package.root() / "sprites";
        if (!std::filesystem::exists(spritesRoot)) {
            continue;
        }
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(spritesRoot, ec), end;
             it != end && !ec;
             it.increment(ec)) {
            if (ec || !it->is_regular_file()) {
                continue;
            }
            if (it->path().extension() != ".spr") {
                continue;
            }
            std::error_code relEc;
            std::filesystem::path relative =
                std::filesystem::relative(it->path(), spritesRoot, relEc);
            if (relEc) {
                continue;
            }
            relative.replace_extension();
            const std::string virtualPath = relative.generic_string();
            owningPackage[virtualPath] = package.meta().id;
        }
    }

    entries.clear();
    entries.reserve(owningPackage.size());
    for (const auto& [path, packageId] : owningPackage) {
        entries.push_back(SpriteBrowserEntry{path, packageId});
    }
    std::sort(entries.begin(), entries.end(), [](const SpriteBrowserEntry& a, const SpriteBrowserEntry& b) {
        return a.virtualPath < b.virtualPath;
    });
}

void SpriteBrowser::draw(Editor& editor, slopengine::AssetStore& assets) {
    ImGui::TextUnformatted("Sprites");
    ImGui::SetNextItemWidth(-1.0f);
    char filterBuf[128] = {};
    std::snprintf(filterBuf, sizeof(filterBuf), "%s", filter.c_str());
    if (ImGui::InputText("##spritefilter", filterBuf, sizeof(filterBuf))) {
        filter = filterBuf;
    }
    if (ImGui::Button("Rescan", ImVec2(-1.0f, 0.0f))) {
        rescan(assets);
    }
    ImGui::Separator();

    if (ImGui::BeginChild("##spritebrowserlist", ImVec2(0.0f, 0.0f), false)) {
        for (const SpriteBrowserEntry& entry : entries) {
            if (!containsIgnoreCase(entry.virtualPath, filter) &&
                !containsIgnoreCase(entry.packageId, filter)) {
                continue;
            }
            const bool selected = editor.doc.open && editor.doc.virtualPath == entry.virtualPath;
            ImGui::PushID(entry.virtualPath.c_str());
            if (ImGui::Selectable(entry.virtualPath.c_str(), selected)) {
                if (!editor.doc.dirty || selected) {
                    editor.loadSprite(assets, entry.virtualPath);
                } else {
                    editor.loadSprite(assets, entry.virtualPath);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\npackage: %s", entry.virtualPath.c_str(), entry.packageId.c_str());
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

}
