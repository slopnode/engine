#include "editor_settings.hpp"

#include "core/user_paths.hpp"

#include <cctype>
#include <fstream>
#include <system_error>

namespace slopmap {

namespace {

std::string trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

} // namespace

const char* materialViewModeId(MaterialViewMode mode) {
    switch (mode) {
    case MaterialViewMode::List:
        return "list";
    case MaterialViewMode::Icons:
        return "icons";
    }
    return "icons";
}

MaterialViewMode materialViewModeFromId(std::string_view id) {
    if (id == "list") {
        return MaterialViewMode::List;
    }
    return MaterialViewMode::Icons;
}

EditorSettings EditorSettings::loadOrDefault() {
    EditorSettings settings{};
    const std::filesystem::path path = slopengine::userSlopmapSettingsPath();
    std::ifstream in(path);
    if (!in) {
        return settings;
    }

    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = trimmed.substr(1, trimmed.size() - 2);
            continue;
        }
        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(trimmed.substr(0, eq));
        const std::string value = trim(trimmed.substr(eq + 1));
        if (section == "cache" && key == "thumbnail_path") {
            settings.thumbnailCachePath = value;
        } else if (section == "materials" && key == "view") {
            settings.materialViewMode = materialViewModeFromId(value);
        }
    }
    return settings;
}

bool EditorSettings::save() const {
    const std::filesystem::path path = slopengine::userSlopmapSettingsPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "[cache]\n";
    out << "thumbnail_path=" << thumbnailCachePath << '\n';
    out << "\n[materials]\n";
    out << "view=" << materialViewModeId(materialViewMode) << '\n';
    return static_cast<bool>(out);
}

std::filesystem::path EditorSettings::resolvedThumbnailCachePath() const {
    if (!thumbnailCachePath.empty()) {
        return std::filesystem::path(thumbnailCachePath);
    }
    return slopengine::defaultSlopmapThumbnailCacheDirectory();
}

}
