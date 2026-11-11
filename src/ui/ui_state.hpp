#pragma once

#include "game/user_settings.hpp"

#include <flecs.h>
#include <string>
#include <unordered_map>
#include <vector>

struct ImFont;

namespace slopengine {

/** Static session info set once at startup, for display in the UI.
 *  @ingroup ui_components
 */
struct EngineSessionInfo {
    std::string profile;
};

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
    bool hostOpen = false;
    bool hostPending = false;
    int hostSession = 0;
    int hostMap = 0;
    int hostPort = 27960;
    int hostMaxPlayers = 0;
    int hostBots = 0;
    int hostBotBehavior = 0;
    bool joinOpen = false;
    bool joinPending = false;
    char joinAddress[128] = {};
    bool profileOpen = false;
    char profileName[64] = {};
    std::vector<std::string> profileValues;
    bool aboutOpen = false;
    bool creditsOpen = false;
};

/** Debug overlay toggles and entity inspector selection.
 *  @ingroup ui_components
 */
struct DebugUiState {
    /** Whether the Debug menu is shown at all; set once at startup from `--debug`. */
    bool menuAvailable = false;
    bool showBspOutlines = false;
    bool showBspLeafFaces = false;
    bool showBspPortals = false;
    bool showBspSurfaceFaces = false;
    bool showBspCurrentLeafOnly = false;
    bool showSpriteMasks = false;
    bool showSpriteAim = false;
    bool showSkeletons = false;
    bool showLightProbesFine = false;
    bool showLightProbesCoarse = false;
    bool showLightProbeSampleTaps = false;
    bool showGraphs = false;
    bool showNavPaths = false;
    /** Which baked nav profiles (MapNavProfiles, keyed by profile name) the "Nav Polys"
     *  debug submenu currently draws; absent/false = hidden. Lets several profiles be
     *  shown at once to compare their erosion against each other. */
    std::unordered_map<std::string, bool> shownNavProfiles;
    bool showActorColliders = false;
    bool showAudioSources = false;
    bool showAudioOcclusionMesh = false;
    bool showPerformance = false;
    bool themeEditorOpen = false;
    bool unlit = false;
    bool mapMeshCull = true;
    bool noclip = false;
    bool freeCamera = false;
    bool hideHud = false;
    bool hideFpScene = false;
    bool entityListOpen = false;
    bool entityDetailOpen = false;
    flecs::entity inspectedEntity{};
};

}
