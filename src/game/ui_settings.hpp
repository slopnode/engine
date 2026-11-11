#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace slopengine {

struct UiSettings {
    std::string theme = "win95";
    std::string joinAddress = "127.0.0.1:27960";
    std::string playerName = "Player";
    std::map<std::string, std::string> profileValues;
};

UiSettings loadUiSettings(const std::filesystem::path& settingsFilePath);

}
