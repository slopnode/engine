#pragma once

#include <filesystem>
#include <string>

namespace slopengine {

struct UiSettings {
    std::string theme = "win95";
    std::string joinAddress = "127.0.0.1:27960";
};

UiSettings loadUiSettings(const std::filesystem::path& settingsFilePath);

}
