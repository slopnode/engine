#pragma once

#include "game/user_settings.hpp"

#include <flecs.h>
#include <string>
#include <vector>

namespace slopengine {

struct ConsoleState {
    bool open = false;
    char inputBuffer[512]{};
    std::vector<std::string> log;
};

struct QuitRequest {
    bool requested = false;
};

struct SettingsUiState {
    bool graphicsOpen = false;
    bool controlsOpen = false;
    GraphicsSettings graphicsDraft{};
    ControlsSettings controlsDraft{};
    int rebindingAction = -1;
};

}
