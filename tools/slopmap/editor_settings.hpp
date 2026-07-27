#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace slopmap {

enum class MaterialViewMode {
    List,
    Icons,
};

struct EditorSettings {
    std::string thumbnailCachePath;
    MaterialViewMode materialViewMode = MaterialViewMode::Icons;

    static EditorSettings loadOrDefault();
    bool save() const;
    std::filesystem::path resolvedThumbnailCachePath() const;
};

const char* materialViewModeId(MaterialViewMode mode);
MaterialViewMode materialViewModeFromId(std::string_view id);

}
