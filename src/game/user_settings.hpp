#pragma once

#include "input/actions.hpp"

#include <string>

namespace slopengine {

/** Window display mode. */
enum class WindowMode {
    Windowed,
    Fullscreen,
    Borderless,
};

/** Saved graphics options applied at startup and from the settings UI. */
struct GraphicsSettings {
    int width = 1280;
    int height = 720;
    WindowMode mode = WindowMode::Windowed;
    bool vsync = true;
};

/** Key bindings for each Action. */
struct ControlsSettings {
    int keys[actionCount]{};

    ControlsSettings();
    /** Returns the default WASD / E / F bindings. */
    static ControlsSettings defaults();
};

/** Persistent user settings (graphics + controls). */
struct UserSettings {
    GraphicsSettings graphics{};
    ControlsSettings controls{};

    static UserSettings defaults();
    /** Loads from disk, or defaults if missing / invalid. */
    static UserSettings loadOrDefault();
    /** Writes settings to the user config path. */
    bool save() const;
};

/** Stable id string for @p action (for config files). */
const char* actionId(Action action);
/** Display label for @p action. */
const char* actionLabel(Action action);
/** Stable id string for @p mode. */
const char* windowModeId(WindowMode mode);
/** Display label for @p mode. */
const char* windowModeLabel(WindowMode mode);

/** Applies window size / mode flags before InitWindow. */
void prepareGraphicsInit(const GraphicsSettings& settings);
/** Applies graphics settings to an already open window. */
void applyGraphicsSettings(const GraphicsSettings& settings);

}
