#pragma once

#include "input/actions.hpp"

#include <string>
#include <vector>

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
    bool dynamicLights = true;
    bool dynamicLightShadows = true;
};

/** Key / mouse bindings for each registered action. */
struct ControlsSettings {
    std::vector<int> binds;

    ControlsSettings();
    /** Returns defaults from the current ActionRegistry. */
    static ControlsSettings defaults();
};

/** Persistent user settings (graphics + controls). */
struct UserSettings {
    GraphicsSettings graphics{};
    ControlsSettings controls{};

    static UserSettings defaults();
    /** Loads graphics from disk (or defaults). Controls stay default-sized until actions are registered. */
    static GraphicsSettings loadGraphicsOrDefault();
    /** Merges [controls] from disk onto @p controls by action id. */
    static void mergeControlsFromDisk(ControlsSettings& controls);
    /** Loads full settings when the action registry is already populated. */
    static UserSettings loadOrDefault();
    /** Writes settings to the user config path. */
    bool save() const;
};

const char* windowModeId(WindowMode mode);
const char* windowModeLabel(WindowMode mode);

/** Applies window size / mode flags before InitWindow. */
void prepareGraphicsInit(const GraphicsSettings& settings);
/** Applies graphics settings to an already open window. */
void applyGraphicsSettings(const GraphicsSettings& settings);

}
