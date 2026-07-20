#include "game/user_settings.hpp"

#include "core/user_paths.hpp"

#include <raylib.h>

#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace slopengine {

namespace {

constexpr struct {
    Action action;
    int key;
} kDefaultBindings[] = {
    {Action::MoveForward, KEY_W},
    {Action::MoveBackward, KEY_S},
    {Action::MoveLeft, KEY_A},
    {Action::MoveRight, KEY_D},
    {Action::Jump, KEY_SPACE},
    {Action::Interact, KEY_E},
    {Action::Console, KEY_GRAVE},
    {Action::MainMenu, KEY_F1},
    {Action::Flashlight, KEY_F},
};

std::string trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

std::optional<Action> actionFromId(std::string_view id) {
    for (int i = 0; i < actionCount; ++i) {
        const Action action = static_cast<Action>(i);
        if (id == actionId(action)) {
            return action;
        }
    }
    return std::nullopt;
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

}

ControlsSettings::ControlsSettings() {
    for (int i = 0; i < actionCount; ++i) {
        keys[i] = KEY_NULL;
    }
    for (const auto& binding : kDefaultBindings) {
        keys[static_cast<int>(binding.action)] = binding.key;
    }
}

ControlsSettings ControlsSettings::defaults() {
    return ControlsSettings{};
}

UserSettings UserSettings::defaults() {
    return UserSettings{};
}

UserSettings UserSettings::loadOrDefault() {
    UserSettings settings = defaults();
    const std::filesystem::path path = userSettingsPath();
    std::ifstream input(path);
    if (!input) {
        return settings;
    }

    enum class Section {
        None,
        Graphics,
        Controls,
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
        if (trimmed == "[controls]") {
            section = Section::Controls;
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = Section::None;
            continue;
        }

        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string key = trim(trimmed.substr(0, eq));
        const std::string value = trim(trimmed.substr(eq + 1));

        if (section == Section::Graphics) {
            if (key == "width") {
                parseInt(value, settings.graphics.width);
            } else if (key == "height") {
                parseInt(value, settings.graphics.height);
            } else if (key == "mode") {
                if (const auto mode = windowModeFromId(value)) {
                    settings.graphics.mode = *mode;
                }
            } else if (key == "vsync") {
                parseBool(value, settings.graphics.vsync);
            }
        } else if (section == Section::Controls) {
            if (const auto action = actionFromId(key)) {
                int keyCode = KEY_NULL;
                if (parseInt(value, keyCode)) {
                    settings.controls.keys[static_cast<int>(*action)] = keyCode;
                }
            }
        }
    }

    if (settings.graphics.width < 640) {
        settings.graphics.width = 640;
    }
    if (settings.graphics.height < 360) {
        settings.graphics.height = 360;
    }

    return settings;
}

bool UserSettings::save() const {
    const std::filesystem::path directory = userConfigDirectory();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        TraceLog(LOG_WARNING, "SETTINGS: failed to create config directory %s", directory.string().c_str());
        return false;
    }

    const std::filesystem::path path = userSettingsPath();
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        TraceLog(LOG_WARNING, "SETTINGS: failed to write %s", path.string().c_str());
        return false;
    }

    output << "[graphics]\n"
           << "width=" << graphics.width << '\n'
           << "height=" << graphics.height << '\n'
           << "mode=" << windowModeId(graphics.mode) << '\n'
           << "vsync=" << (graphics.vsync ? "1" : "0") << "\n\n"
           << "[controls]\n";

    for (int i = 0; i < actionCount; ++i) {
        const Action action = static_cast<Action>(i);
        output << actionId(action) << '=' << controls.keys[i] << '\n';
    }

    return true;
}

const char* actionId(Action action) {
    switch (action) {
    case Action::MoveForward:
        return "MoveForward";
    case Action::MoveBackward:
        return "MoveBackward";
    case Action::MoveLeft:
        return "MoveLeft";
    case Action::MoveRight:
        return "MoveRight";
    case Action::Jump:
        return "Jump";
    case Action::Pause:
        return "Pause";
    case Action::Interact:
        return "Interact";
    case Action::Console:
        return "Console";
    case Action::MainMenu:
        return "MainMenu";
    case Action::Flashlight:
        return "Flashlight";
    case Action::Count:
        break;
    }
    return "Unknown";
}

const char* actionLabel(Action action) {
    switch (action) {
    case Action::MoveForward:
        return "Move Forward";
    case Action::MoveBackward:
        return "Move Backward";
    case Action::MoveLeft:
        return "Move Left";
    case Action::MoveRight:
        return "Move Right";
    case Action::Jump:
        return "Jump";
    case Action::Pause:
        return "Pause";
    case Action::Interact:
        return "Interact";
    case Action::Console:
        return "Console";
    case Action::MainMenu:
        return "Main Menu";
    case Action::Flashlight:
        return "Flashlight";
    case Action::Count:
        break;
    }
    return "Unknown";
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
