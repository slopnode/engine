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

bool isProceduralExtension(const std::filesystem::path& path) {
    return path.extension() == ".sfxr";
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

const char* iconForKind(SourceKind kind) {
    switch (kind) {
        case SourceKind::Definition: return "page_link";
        case SourceKind::Procedural: return "wand";
        case SourceKind::Clip: return "sound";
    }
    return "sound";
}

const char* tooltipForKind(SourceKind kind) {
    switch (kind) {
        case SourceKind::Definition: return "definition (.saudio): source + bus/volume/filters";
        case SourceKind::Procedural: return "procedural (.sfxr): sfxr synth params";
        case SourceKind::Clip: return "clip (preview only)";
    }
    return "";
}

void drawEntryList(
    Editor& editor,
    slopengine::AssetStore& assets,
    const std::vector<AudioBrowserEntry>& entries,
    const std::string& filter,
    SourceKind tab) {
    constexpr const char* kIcons = slopengine::kDefaultIconSet;
    for (const AudioBrowserEntry& entry : entries) {
        if (entry.kind != tab) {
            continue;
        }
        if (!containsIgnoreCase(entry.virtualPath, filter) &&
            !containsIgnoreCase(entry.packageId, filter)) {
            continue;
        }
        const bool selected =
            editor.doc.open && editor.doc.virtualPath == entry.virtualPath && editor.doc.sourceKind == entry.kind;
        ImGui::PushID(entry.virtualPath.c_str());
        if (slopengine::selectableWithIcon(
                assets, kIcons, iconForKind(entry.kind), entry.virtualPath.c_str(), selected)) {
            switch (entry.kind) {
                case SourceKind::Definition: editor.loadDefinition(assets, entry.virtualPath); break;
                case SourceKind::Procedural: editor.loadProcedural(assets, entry.virtualPath); break;
                case SourceKind::Clip: editor.loadClip(entry.virtualPath); break;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s\npackage: %s\n%s", entry.virtualPath.c_str(), entry.packageId.c_str(),
                tooltipForKind(entry.kind));
        }
        ImGui::PopID();
    }
}

} // namespace

void Browser::rescan(const slopengine::AssetStore& assets) {
    std::unordered_map<std::string, std::string> defPaths;
    std::unordered_map<std::string, std::string> procPaths;
    std::unordered_map<std::string, std::string> clipPaths;
    for (const slopengine::Package& package : assets.packages()) {
        collectFromDir(package.root() / "audio", package.meta().id, isSaudioExtension, defPaths);
        collectFromDir(package.root() / "audio", package.meta().id, isProceduralExtension, procPaths);
        collectFromDir(package.root() / "sound", package.meta().id, isClipExtension, clipPaths);
    }

    entries.clear();
    entries.reserve(defPaths.size() + procPaths.size() + clipPaths.size());
    for (const auto& [path, packageId] : defPaths) {
        entries.push_back(AudioBrowserEntry{path, packageId, SourceKind::Definition});
    }
    for (const auto& [path, packageId] : procPaths) {
        entries.push_back(AudioBrowserEntry{path, packageId, SourceKind::Procedural});
    }
    for (const auto& [path, packageId] : clipPaths) {
        entries.push_back(AudioBrowserEntry{path, packageId, SourceKind::Clip});
    }
    std::sort(entries.begin(), entries.end(), [](const AudioBrowserEntry& a, const AudioBrowserEntry& b) {
        return a.virtualPath < b.virtualPath;
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
    if (activeTab != SourceKind::Clip) {
        const char* label = activeTab == SourceKind::Procedural ? "New Procedural" : "New Definition";
        if (slopengine::buttonWithIcon(assets, kIcons, "page_add", label, ImVec2(-1.0f, 0.0f))) {
            editor.newModalKind = activeTab;
            editor.showNewModal = true;
        }
    }
    if (slopengine::buttonWithIcon(assets, kIcons, "arrow_refresh", "Rescan", ImVec2(-1.0f, 0.0f))) {
        rescan(assets);
    }
    ImGui::Separator();

    if (ImGui::BeginChild("##audiobrowserlist", ImVec2(0.0f, 0.0f), false)) {
        if (ImGui::BeginTabBar("##audioBrowserTabs")) {
            if (ImGui::BeginTabItem("Definitions")) {
                activeTab = SourceKind::Definition;
                drawEntryList(editor, assets, entries, filter, SourceKind::Definition);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Procedural")) {
                activeTab = SourceKind::Procedural;
                drawEntryList(editor, assets, entries, filter, SourceKind::Procedural);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Files")) {
                activeTab = SourceKind::Clip;
                drawEntryList(editor, assets, entries, filter, SourceKind::Clip);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
}

}
