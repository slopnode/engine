#include "game/user_settings.hpp"

#include "input/action_registry.hpp"
#include "input/bind_code.hpp"
#include "render/dynamic_light.hpp"

#include <raylib.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

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

std::optional<WindowMode> windowModeFromId(std::string_view id) {
    if (id == "windowed") {
        return WindowMode::Windowed;
    }
    if (id == "fullscreen") {
        return WindowMode::Fullscreen;
    }
    if (id == "borderless") {
        return WindowMode::Borderless;
    }
    return std::nullopt;
}

bool parseInt(std::string_view value, int& out) {
    if (value.empty()) {
        return false;
    }
    try {
        out = std::stoi(std::string(value));
        return true;
    } catch (...) {
        return false;
    }
}

bool parseBool(std::string_view value, bool& out) {
    if (value == "1" || value == "true" || value == "True" || value == "yes") {
        out = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "False" || value == "no") {
        out = false;
        return true;
    }
    return false;
}

void leaveExclusiveFullscreen() {
    if (IsWindowFullscreen()) {
        ToggleFullscreen();
    }
}

void leaveBorderless() {
    if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
        ToggleBorderlessWindowed();
    }
}

void applyControlBinding(ControlsSettings& controls, std::string_view actionId, std::string_view value) {
    const int index = actionRegistry().indexOf(actionId);
    if (index < 0 || index >= static_cast<int>(controls.binds.size())) {
        return;
    }
    const int bind = parseBindToken(value);
    controls.binds[static_cast<std::size_t>(index)] = bind;
}

}

ControlsSettings::ControlsSettings() {
    const ActionRegistry& registry = actionRegistry();
    binds.assign(static_cast<std::size_t>(registry.size()), KEY_NULL);
    for (int i = 0; i < registry.size(); ++i) {
        binds[static_cast<std::size_t>(i)] = registry.at(i).defaultBind;
    }
}

ControlsSettings ControlsSettings::defaults() {
    return ControlsSettings{};
}

UserSettings UserSettings::defaults() {
    return UserSettings{};
}

GraphicsSettings UserSettings::loadGraphicsOrDefault(const std::filesystem::path& settingsFilePath) {
    GraphicsSettings graphics = defaults().graphics;
    std::ifstream input(settingsFilePath);
    if (!input) {
        return graphics;
    }

    enum class Section {
        None,
        Graphics,
    };

    Section section = Section::None;
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        if (trimmed == "[graphics]") {
            section = Section::Graphics;
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = Section::None;
            continue;
        }

        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos || section != Section::Graphics) {
            continue;
        }

        const std::string key = trim(trimmed.substr(0, eq));
        const std::string value = trim(trimmed.substr(eq + 1));

        if (key == "width") {
            parseInt(value, graphics.width);
        } else if (key == "height") {
            parseInt(value, graphics.height);
        } else if (key == "mode") {
            if (const auto mode = windowModeFromId(value)) {
                graphics.mode = *mode;
            }
        } else if (key == "vsync") {
            parseBool(value, graphics.vsync);
        } else if (key == "dynamic_lights") {
            parseBool(value, graphics.dynamicLights);
        } else if (key == "dynamic_light_shadows") {
            parseBool(value, graphics.dynamicLightShadows);
        } else if (key == "max_dynamic_lights") {
            parseInt(value, graphics.maxDynamicLights);
        } else if (key == "max_shadowed_dynamic_lights") {
            parseInt(value, graphics.maxShadowedDynamicLights);
        } else if (key == "shadow_map_resolution") {
            parseInt(value, graphics.shadowMapResolution);
        }
    }

    if (graphics.width < 640) {
        graphics.width = 640;
    }
    if (graphics.height < 360) {
        graphics.height = 360;
    }
    graphics.maxDynamicLights =
        std::clamp(graphics.maxDynamicLights, 0, kMaxDynamicLights);
    graphics.maxShadowedDynamicLights = std::clamp(
        graphics.maxShadowedDynamicLights,
        0,
        std::min(graphics.maxDynamicLights, kMaxShadowedDynamicLights));
    graphics.shadowMapResolution = std::clamp(
        graphics.shadowMapResolution,
        kMinDynamicShadowMapResolution,
        kMaxDynamicShadowMapResolution);

    return graphics;
}

void UserSettings::mergeControlsFromDisk(
    ControlsSettings& controls,
    const std::filesystem::path& settingsFilePath) {
    std::ifstream input(settingsFilePath);
    if (!input) {
        return;
    }

    bool inControls = false;
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        if (trimmed == "[controls]") {
            inControls = true;
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            inControls = false;
            continue;
        }
        if (!inControls) {
            continue;
        }

        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string key = trim(trimmed.substr(0, eq));
        const std::string value = trim(trimmed.substr(eq + 1));
        applyControlBinding(controls, key, value);
    }
}

bool UserSettings::save() const {
    const std::filesystem::path directory = settingsFilePath.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        TraceLog(LOG_WARNING, "SETTINGS: failed to create config directory %s", directory.string().c_str());
        return false;
    }

    std::ofstream output(settingsFilePath, std::ios::trunc);
    if (!output) {
        TraceLog(LOG_WARNING, "SETTINGS: failed to write %s", settingsFilePath.string().c_str());
        return false;
    }

    output << "[graphics]\n"
           << "width=" << graphics.width << '\n'
           << "height=" << graphics.height << '\n'
           << "mode=" << windowModeId(graphics.mode) << '\n'
           << "vsync=" << (graphics.vsync ? "1" : "0") << '\n'
           << "dynamic_lights=" << (graphics.dynamicLights ? "1" : "0") << '\n'
           << "dynamic_light_shadows=" << (graphics.dynamicLightShadows ? "1" : "0") << '\n'
           << "max_dynamic_lights=" << graphics.maxDynamicLights << '\n'
           << "max_shadowed_dynamic_lights=" << graphics.maxShadowedDynamicLights << '\n'
           << "shadow_map_resolution=" << graphics.shadowMapResolution << "\n\n"
           << "[controls]\n";

    for (int i = 0; i < static_cast<int>(controls.binds.size()); ++i) {
        output << actionIdAt(i) << '=' << formatBindToken(controls.binds[static_cast<std::size_t>(i)]) << '\n';
    }

    return true;
}

const char* windowModeId(WindowMode mode) {
    switch (mode) {
    case WindowMode::Windowed:
        return "windowed";
    case WindowMode::Fullscreen:
        return "fullscreen";
    case WindowMode::Borderless:
        return "borderless";
    }
    return "windowed";
}

const char* windowModeLabel(WindowMode mode) {
    switch (mode) {
    case WindowMode::Windowed:
        return "Windowed";
    case WindowMode::Fullscreen:
        return "Fullscreen";
    case WindowMode::Borderless:
        return "Borderless";
    }
    return "Windowed";
}

void prepareGraphicsInit(const GraphicsSettings& settings) {
    unsigned int flags = 0;
    if (settings.vsync) {
        flags |= FLAG_VSYNC_HINT;
    }
    if (settings.mode == WindowMode::Fullscreen) {
        flags |= FLAG_FULLSCREEN_MODE;
    }
    if (settings.mode == WindowMode::Borderless) {
        flags |= FLAG_BORDERLESS_WINDOWED_MODE;
    }
    if (flags != 0) {
        SetConfigFlags(flags);
    }
}

void applyGraphicsSettings(const GraphicsSettings& settings) {
    if (!IsWindowReady()) {
        return;
    }

    if (settings.mode != WindowMode::Fullscreen) {
        leaveExclusiveFullscreen();
    }
    if (settings.mode != WindowMode::Borderless) {
        leaveBorderless();
    }

    switch (settings.mode) {
    case WindowMode::Windowed:
        SetWindowSize(settings.width, settings.height);
        break;
    case WindowMode::Fullscreen:
        SetWindowSize(settings.width, settings.height);
        if (!IsWindowFullscreen()) {
            ToggleFullscreen();
        }
        break;
    case WindowMode::Borderless:
        if (!IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
            ToggleBorderlessWindowed();
        }
        break;
    }

    if (settings.vsync) {
        SetWindowState(FLAG_VSYNC_HINT);
    } else {
        ClearWindowState(FLAG_VSYNC_HINT);
    }
}

}
