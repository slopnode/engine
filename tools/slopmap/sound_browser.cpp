#include "sound_browser.hpp"

#include "ui/icon_ui.hpp"
#include "ui/slopui_widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <unordered_set>

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

bool isSoundExtension(const std::filesystem::path& path) {
    return path.extension() == ".ogg";
}

bool isSaudioExtension(const std::filesystem::path& path) {
    return path.extension().string() == ".saudio";
}

// generic_string() converts through the process's narrow codepage on Windows and throws for
// a filename that codepage can't represent; generic_u8string() reads it as UTF-8 with no
// codepage involved, matching how POSIX already treats it.
std::string genericUtf8(const std::filesystem::path& path) {
    const std::u8string generic = path.generic_u8string();
    return std::string(generic.begin(), generic.end());
}

void collectFromDir(
    const std::filesystem::path& dir,
    bool (*matches)(const std::filesystem::path&),
    std::unordered_set<std::string>& seen) {
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
        seen.insert(genericUtf8(relative));
    }
}

} // namespace

void SoundBrowser::rescan(const slopengine::AssetStore& assets) {
    std::unordered_set<std::string> seenDefined;
    std::unordered_set<std::string> seenRaw;
    for (const slopengine::Package& package : assets.packages()) {
        collectFromDir(package.root() / "audio", isSaudioExtension, seenDefined);
        collectFromDir(package.root() / "sounds", isSoundExtension, seenRaw);
    }

    definedSounds.assign(seenDefined.begin(), seenDefined.end());
    std::sort(definedSounds.begin(), definedSounds.end());
    rawClips.assign(seenRaw.begin(), seenRaw.end());
    std::sort(rawClips.begin(), rawClips.end());
}

bool SoundBrowser::drawModal(slopengine::AssetStore& assets, std::string& outPath) {
    if (!open) {
        return false;
    }

    constexpr const char* kIcons = slopengine::kDefaultIconSet;
    bool picked = false;
    ImGui::OpenPopup("Pick Sound");
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Pick Sound", &open, ImGuiWindowFlags_None)) {
        ImGui::SetNextItemWidth(-1.0f);
        char filterBuf[128] = {};
        std::snprintf(filterBuf, sizeof(filterBuf), "%s", filter.c_str());
        if (ImGui::InputText("##soundfilter", filterBuf, sizeof(filterBuf))) {
            filter = filterBuf;
        }
        ImGui::Separator();
        std::vector<std::string> combined;
        const std::vector<std::string>* list = &definedSounds;
        if (mode == SoundBrowserMode::Combined) {
            combined = definedSounds;
            combined.insert(combined.end(), rawClips.begin(), rawClips.end());
            std::sort(combined.begin(), combined.end());
            combined.erase(std::unique(combined.begin(), combined.end()), combined.end());
            list = &combined;
        }
        if (slopengine::slopuiBeginChild("##soundlist", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true)) {
            for (const std::string& path : *list) {
                if (!containsIgnoreCase(path, filter)) {
                    continue;
                }
                if (slopengine::selectableWithIcon(assets, kIcons, "music", path.c_str(), false)) {
                    outPath = path;
                    picked = true;
                    open = false;
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
        }
        ImGui::EndChild();
        if (slopengine::buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(-1.0f, 0.0f))) {
            open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return picked;
}

}
