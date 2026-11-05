#pragma once

#include <filesystem>
#include <string>

namespace slopengine {

struct UiSettings {
    std::string theme = "win95";
};

UiSettings loadUiSettings(const std::filesystem::path& settingsFilePath);

}
