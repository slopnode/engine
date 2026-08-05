#pragma once

#include "game/user_settings.hpp"

#include <flecs.h>
#include <string>
#include <vector>

namespace slopengine {

/** In-game console open state, input buffer, and log lines.
 *  @ingroup ui_components
 */
struct ConsoleState {
    bool open = false;
    char inputBuffer[512]{};
    std::vector<std::string> log;
};

/** Singleton set when the player requests application quit.
 *  @ingroup ui_components
 */
struct QuitRequest {
    bool requested = false;
};

/** Deferred framebuffer capture after EndDrawing.
 *  @ingroup ui_components
 */
struct ScreenshotRequest {
    bool pending = false;
};

/** Draft settings and rebinding state for the settings UI.
 *  @ingroup ui_components
 */
struct SettingsUiState {
    bool graphicsOpen = false;
    bool controlsOpen = false;
    GraphicsSettings graphicsDraft{};
    ControlsSettings controlsDraft{};
    int rebindingAction = -1;
    bool rebindingWaitMouseRelease = false;
};

/** Debug overlay toggles and entity inspector selection.
 *  @ingroup ui_components
 */
struct DebugUiState {
    bool showBspOutlines = false;
    bool showBspLeafFaces = false;
    bool showBspPortals = false;
    bool showBspSurfaceFaces = false;
    bool showBspCurrentLeafOnly = false;
    bool showVisFaces = false;
    bool showVisCurrentLeafOnly = false;
    bool showSpriteMasks = false;
    bool showSpriteAim = false;
    bool showGraphs = false;
    bool showPerformance = false;
    bool unlit = false;
    bool noclip = false;
    bool hideHud = false;
    bool hideFpScene = false;
    bool entityListOpen = false;
    bool entityDetailOpen = false;
    flecs::entity inspectedEntity{};
};

}
