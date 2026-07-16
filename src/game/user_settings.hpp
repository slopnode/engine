#pragma once

#include "input/actions.hpp"

#include <string>

namespace slopengine {

enum class WindowMode {
    Windowed,
    Fullscreen,
    Borderless,
};

struct GraphicsSettings {
    int width = 1280;
    int height = 720;
    WindowMode mode = WindowMode::Windowed;
    bool vsync = true;
};

struct ControlsSettings {
    int keys[actionCount]{};

    ControlsSettings();
    static ControlsSettings defaults();
};

struct UserSettings {
    GraphicsSettings graphics{};
    ControlsSettings controls{};

    static UserSettings defaults();
    static UserSettings loadOrDefault();
    bool save() const;
};

const char* actionId(Action action);
const char* actionLabel(Action action);
const char* windowModeId(WindowMode mode);
const char* windowModeLabel(WindowMode mode);

void prepareGraphicsInit(const GraphicsSettings& settings);
void applyGraphicsSettings(const GraphicsSettings& settings);

}
