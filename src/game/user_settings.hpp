#pragma once

#include "input/actions.hpp"

#include <filesystem>
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
    int maxDynamicLights = 32;
    int maxShadowedDynamicLights = 8;
    int shadowMapResolution = 512;
};

/** Key / mouse bindings for each registered action. */
struct ControlsSettings {
    std::vector<int> binds;

    ControlsSettings();
    /** Returns defaults from the current ActionRegistry. */
    static ControlsSettings defaults();
};

/** Persistent user settings (graphics + controls) for one profile. */
struct UserSettings {
    GraphicsSettings graphics{};
    ControlsSettings controls{};
    /** Where save() writes; set by the caller from core::profileSettingsPath(). */
    std::filesystem::path settingsFilePath{};

    static UserSettings defaults();
    /** Loads graphics from @p settingsFilePath (or defaults if absent). */
    static GraphicsSettings loadGraphicsOrDefault(const std::filesystem::path& settingsFilePath);
    /** Merges [controls] from @p settingsFilePath onto @p controls by action id. */
    static void mergeControlsFromDisk(
        ControlsSettings& controls,
        const std::filesystem::path& settingsFilePath);
    /** Writes settings to settingsFilePath, creating parent directories as needed. */
    bool save() const;
};

const char* windowModeId(WindowMode mode);
const char* windowModeLabel(WindowMode mode);

/** Applies window size / mode flags before InitWindow. */
void prepareGraphicsInit(const GraphicsSettings& settings);
/** Applies graphics settings to an already open window. */
void applyGraphicsSettings(const GraphicsSettings& settings);

}
