#include "browser.hpp"

#include "ui/icon_ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <unordered_map>

namespace slopaudio {

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

bool isSaudioExtension(const std::filesystem::path& path) {
    return path.extension() == ".saudio";
}

bool isClipExtension(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".ogg" || ext == ".wav";
}

void collectFromDir(
    const std::filesystem::path& dir,
    const std::string& packageId,
    bool (*matches)(const std::filesystem::path&),
    std::unordered_map<std::string, std::string>& seen) {
    if (!std::filesystem::exists(dir)) {
        return;
    }
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; it != end && !ec;
         it.increment(ec)) {
        if (ec || !it->is_regular_file() || !matches(it->path())) {
            continue;
        }
        std::error_code relEc;
        std::filesystem::path relative = std::filesystem::relative(it->path(), dir, relEc);
        if (relEc) {
            continue;
        }
        relative.replace_extension();
        seen[relative.generic_string()] = packageId;
    }
}

} // namespace

void Browser::rescan(const slopengine::AssetStore& assets) {
    std::unordered_map<std::string, std::string> saudioPaths;
    std::unordered_map<std::string, std::string> clipPaths;
    for (const slopengine::Package& package : assets.packages()) {
        collectFromDir(package.root() / "audio", package.meta().id, isSaudioExtension, saudioPaths);
        collectFromDir(package.root() / "sound", package.meta().id, isClipExtension, clipPaths);
    }

    entries.clear();
    entries.reserve(saudioPaths.size() + clipPaths.size());
    for (const auto& [path, packageId] : saudioPaths) {
        entries.push_back(AudioBrowserEntry{path, packageId, true});
    }
    for (const auto& [path, packageId] : clipPaths) {
        entries.push_back(AudioBrowserEntry{path, packageId, false});
    }
    std::sort(entries.begin(), entries.end(), [](const AudioBrowserEntry& a, const AudioBrowserEntry& b) {
        if (a.virtualPath != b.virtualPath) {
            return a.virtualPath < b.virtualPath;
        }
        return a.editable && !b.editable;
    });
}

void Browser::draw(Editor& editor, slopengine::AssetStore& assets, slopengine::AudioWorld& audioWorld) {
    (void)audioWorld;
    constexpr const char* kIcons = slopengine::kDefaultIconSet;

    ImGui::TextUnformatted("Audio");
    ImGui::SetNextItemWidth(-1.0f);
    char filterBuf[128] = {};
    std::snprintf(filterBuf, sizeof(filterBuf), "%s", filter.c_str());
    if (ImGui::InputText("##audiofilter", filterBuf, sizeof(filterBuf))) {
        filter = filterBuf;
    }
    if (slopengine::buttonWithIcon(assets, kIcons, "page_add", "New", ImVec2(-1.0f, 0.0f))) {
        editor.showNewModal = true;
    }
    if (slopengine::buttonWithIcon(assets, kIcons, "arrow_refresh", "Rescan", ImVec2(-1.0f, 0.0f))) {
        rescan(assets);
    }
    ImGui::Separator();

    if (ImGui::BeginChild("##audiobrowserlist", ImVec2(0.0f, 0.0f), false)) {
        for (const AudioBrowserEntry& entry : entries) {
            if (!containsIgnoreCase(entry.virtualPath, filter) &&
                !containsIgnoreCase(entry.packageId, filter)) {
                continue;
            }
            const bool selected = editor.doc.open && editor.doc.virtualPath == entry.virtualPath &&
                ((editor.doc.sourceKind == SourceKind::Saudio) == entry.editable);
            ImGui::PushID(entry.virtualPath.c_str());
            ImGui::PushID(entry.editable ? 0 : 1);
            const char* iconId = entry.editable ? "music" : "sound";
            if (slopengine::selectableWithIcon(assets, kIcons, iconId, entry.virtualPath.c_str(), selected)) {
                if (entry.editable) {
                    editor.loadAudio(assets, entry.virtualPath);
                } else {
                    editor.loadClip(entry.virtualPath);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\npackage: %s\n%s",
                    entry.virtualPath.c_str(),
                    entry.packageId.c_str(),
                    entry.editable ? "procedural (.saudio)" : "clip (preview only)");
            }
            ImGui::PopID();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

}
