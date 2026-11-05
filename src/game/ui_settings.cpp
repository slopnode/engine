#include "game/ui_settings.hpp"

#include <cctype>
#include <fstream>
#include <string_view>

namespace slopengine {

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

UiSettings loadUiSettings(const std::filesystem::path& settingsFilePath) {
    UiSettings ui{};
    std::ifstream input(settingsFilePath);
    if (!input) {
        return ui;
    }

    bool inUi = false;
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            inUi = trimmed == "[ui]";
            continue;
        }
        const std::size_t eq = trimmed.find('=');
        if (!inUi || eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(trimmed.substr(0, eq));
        const std::string value = trim(trimmed.substr(eq + 1));
        if (key == "theme" && !value.empty()) {
            ui.theme = value;
        }
    }
    return ui;
}

}
