#include "game/app.hpp"

#include "audio/audio_module.hpp"
#include "camera/camera_module.hpp"
#include "core/log.hpp"
#include "core/package.hpp"
#include "core/user_paths.hpp"
#include "game/game_state.hpp"
#include "game/loading_session.hpp"
#include "game/package_cli.hpp"
#include "game/script_boot.hpp"
#include "script/hook_registry.hpp"
#include "script/proc_role.hpp"
#include "input/action_registry.hpp"
#include "input/input_module.hpp"
#include "interact/interact_module.hpp"
#include "map/map_handler_registry.hpp"
#include "map/thing_def_registry.hpp"
#include "map/map_script.hpp"
#include "map/map_scene.hpp"
#include "physics/physics_module.hpp"
#include "physics/motored_body.hpp"
#include "physics/sight_module.hpp"
#include "navigation/nav_module.hpp"
#include "particles/particle_module.hpp"
#include "fx/trail.hpp"
#include "render/components.hpp"
#include "render/render_module.hpp"
#include "render/underwater_effect.hpp"
#include "script/save_script.hpp"
#include "script/scheme_call.hpp"
#include "script/scheme_harden.hpp"
#include "script/script_scope.hpp"
#include "ui/imgui_fonts.hpp"
#include "ui/ui_module.hpp"
#include "ui/ui_state.hpp"

#include "rlImGui.h"

#include <raylib.h>
#include <s7.h>

#include <stdexcept>
#include <string>

namespace slopengine {

App::App(AppConfig config)
    : config_{std::move(config)}
    , userSettings_{}
    , assetStore_{config_}
    , world_{}
    , physicsWorld_{std::make_unique<PhysicsWorld>()}
    , audioWorld_{std::make_unique<AudioWorld>()} {
    // mountPackages() throws if either is missing, so [0]/[1] are always present here.
    const std::vector<Package>& mountedPackages = assetStore_.packages();
    const std::filesystem::path settingsFilePath =
        profileSettingsPath(mountedPackages[0], mountedPackages[1], config_.profile);
    const bool hadProfileSettings = std::filesystem::exists(settingsFilePath);
    userSettings_.settingsFilePath = settingsFilePath;

    actionRegistry().registerCoreActions();
    userSettings_.graphics = UserSettings::loadGraphicsOrDefault(settingsFilePath);
    init_window();
    if (!audioWorld_->init()) {
        TraceLog(LOG_WARNING, "AUDIO: continuing without audio device");
    }
    ImFont* consoleMonoFont =
        setupImGuiWithUiAndMonoFont(assetStore_, kDefaultUiFontPath, kMonoUiFontPath, true, "slopengine");
    init_script();
    userSettings_.controls = ControlsSettings::defaults();
    UserSettings::mergeControlsFromDisk(userSettings_.controls, settingsFilePath);
    if (!hadProfileSettings) {
        // First time this profile has been used for this engine/base-game pair: write a
        // baseline settings.cfg now that graphics + the full (core + package) action set
        // are known, instead of leaving the profile directory empty until the user opens
        // the in-game settings UI.
        userSettings_.save();
    }
    world_.component<UserSettings>();
    world_.set<UserSettings>(userSettings_);
    registerInputModule(world_);
    registerUiModule(world_, config_.debug, config_.profile, consoleMonoFont);
    registerPhysicsModule(world_, physicsWorld_.get());
    registerSightModule(world_);
    registerNavModule(world_);
    registerAudioModule(world_, audioWorld_.get(), assetStore_, scheme_);
    registerCameraModule(world_);
    registerInteractModule(world_);
    registerParticleModule(world_, assetStore_);
    registerTrailModule(world_);
    registerRenderModule(world_, assetStore_, config_, scheme_);
    registerLoadingSessionModule(world_);
    registerUnderwaterEffectModule(world_);
    registerMotoredBodySystem(world_);
    registerScriptBoot(world_, assetStore_, scheme_);
    callOnStartup(world_);
    running_ = true;
}

App::~App() {
    shutdown();
}

int App::run() {
    while (running_ && !WindowShouldClose()) {
        if (hasActiveLoadingSession(world_)) {
            advanceLoadingSession(world_, assetStore_, scheme_);
        } else if (auto pendingMap = takeRequestedMapLoad()) {
            if (shouldShowLoadingScreen(pendingMap->reason)) {
                beginLoadingSession(world_, assetStore_, scheme_, pendingMap->mapName, pendingMap->reason);
            } else {
                changeMap(world_, assetStore_, scheme_, pendingMap->mapName, pendingMap->reason);
            }
        }
        world_.progress();
        if (world_.get<QuitRequest>().requested) {
            break;
        }
    }

    return 0;
}

void App::init_window() {
    Log::init(config_.verbose ? LogLevel::Debug : LogLevel::Info);
    Log::addDefaultConsoleSink();
    prepareGraphicsInit(userSettings_.graphics);
    InitWindow(userSettings_.graphics.width, userSettings_.graphics.height, "slopengine");
    SetExitKey(KEY_NULL);
    applyGraphicsSettings(userSettings_.graphics);
}

void App::init_script() {
    scheme_ = s7_init();
    hardenSchemeRuntime(scheme_);
    bindPackageApi(assetStore_, scheme_);
    ScriptScopeGuard bootScope(ScriptScope::Boot);
    const std::string baseId{assetStore_.basePackageId()};
    if (baseId.empty()) {
        throw std::runtime_error("SCRIPT: base package id missing");
    }

    if (!assetStore_.loadScript(scheme_, "lang")) {
        TraceLog(LOG_WARNING, "SCRIPT: lang.s7 not loaded");
    }
    if (!assetStore_.loadScriptFromPackage(scheme_, baseId, "init")) {
        TraceLog(LOG_WARNING, "SCRIPT: init.s7 not loaded");
    }
    if (!assetStore_.loadDataFromPackage(scheme_, baseId, "actions")) {
        TraceLog(LOG_WARNING, "SCRIPT: data/actions.s7 not loaded");
    } else if (!registerPackageActionsFromScheme(scheme_)) {
        TraceLog(LOG_WARNING, "SCRIPT: failed to register package actions");
    }
    for (const Package& package : assetStore_.packages()) {
        if (package.role() != PackageRole::Mod) {
            continue;
        }
        if (assetStore_.loadDataFromPackage(scheme_, package.meta().id, "actions")) {
            if (!registerPackageActionsFromScheme(scheme_)) {
                TraceLog(
                    LOG_WARNING,
                    "SCRIPT: failed to register actions from mod '%s'",
                    package.meta().id.c_str());
            }
        }
    }
    mapHandlerRegistry().clear();
    if (!assetStore_.loadDataFromPackage(scheme_, baseId, "map-handlers")) {
        TraceLog(LOG_WARNING, "SCRIPT: data/map-handlers.s7 not loaded");
    } else if (!registerPackageMapHandlersFromScheme(scheme_)) {
        TraceLog(LOG_WARNING, "SCRIPT: failed to register package map handlers");
    }
    for (const Package& package : assetStore_.packages()) {
        if (package.role() != PackageRole::Mod) {
            continue;
        }
        if (assetStore_.loadDataFromPackage(scheme_, package.meta().id, "map-handlers")) {
            if (!registerPackageMapHandlersFromScheme(scheme_)) {
                TraceLog(
                    LOG_WARNING,
                    "SCRIPT: failed to register map handlers from mod '%s'",
                    package.meta().id.c_str());
            }
        }
    }
    loadPackageThings(scheme_, assetStore_);
    loadPackageNavProfiles(scheme_, assetStore_);
    if (!assetStore_.loadDataFromPackage(scheme_, baseId, "items")) {
        TraceLog(LOG_WARNING, "SCRIPT: data/items.s7 not loaded");
    }
    if (!assetStore_.loadDataFromPackage(scheme_, baseId, "view")) {
        TraceLog(LOG_WARNING, "SCRIPT: data/view.s7 not loaded");
    }
    world_.component<ViewCanvas>();
    world_.set<ViewCanvas>(parseViewCanvasFromScheme(scheme_));
    world_.component<HudCanvas>();
    world_.set<HudCanvas>(parseHudCanvasFromScheme(scheme_));

    std::vector<PackageCliFlag> cliSchema;
    if (assetStore_.loadDataFromPackage(scheme_, baseId, "cli")) {
        cliSchema = parsePackageCliFromScheme(scheme_);
    } else {
        TraceLog(LOG_INFO, "SCRIPT: data/cli.s7 not loaded (no package CLI flags)");
    }

    std::string cliError;
    if (!config_.parsePackageArgs(cliSchema, cliError)) {
        AppConfig::printUsage(config_.programName.c_str(), cliSchema);
        throw std::invalid_argument(cliError.empty() ? "invalid package CLI" : cliError);
    }
    bindStartupApi(scheme_, config_.packageArgs);

    if (!assetStore_.loadScriptFromPackage(scheme_, baseId, "things")) {
        TraceLog(LOG_WARNING, "SCRIPT: things.s7 not loaded");
    }
}

void App::shutdown() {
    unloadMapScene(world_);

    unregisterAudioModule(world_);
    unregisterPhysicsModule(world_);

    // Finalizes flecs now, while the GL context is still alive, so any
    // remaining ECS singleton with GPU-owning state (e.g. PostProcessState,
    // DynamicLightShadowState) unloads its textures/shaders here instead of
    // later when world_ is destroyed as an App member, after CloseWindow().
    world_.release();

    if (audioWorld_) {
        audioWorld_->deinit();
        audioWorld_.reset();
    }
    physicsWorld_.reset();

    if (scheme_) {
        clearHookRegistry(scheme_);
        clearProcRoles();
        clearScriptingErrors();
        s7_quit(scheme_);
        scheme_ = nullptr;
    }

    if (IsWindowReady()) {
        assetStore_.releaseGpuResources();
        rlImGuiShutdown();
        CloseWindow();
    }
}

}
