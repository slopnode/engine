#include "game/app.hpp"

#include "audio/audio_module.hpp"
#include "camera/camera_module.hpp"
#include "input/action_registry.hpp"
#include "input/input_module.hpp"
#include "interact/interact_module.hpp"
#include "physics/physics_module.hpp"
#include "render/components.hpp"
#include "render/render_module.hpp"
#include "script/scheme_call.hpp"
#include "ui/imgui_fonts.hpp"
#include "ui/ui_module.hpp"
#include "ui/ui_state.hpp"

#include "rlImGui.h"

#include <raylib.h>
#include <s7.h>

namespace slopengine {

App::App(AppConfig config)
    : config_{std::move(config)}
    , userSettings_{}
    , assetStore_{config_}
    , world_{}
    , physicsWorld_{std::make_unique<PhysicsWorld>()}
    , audioWorld_{std::make_unique<AudioWorld>()} {
    actionRegistry().registerCoreActions();
    userSettings_.graphics = UserSettings::loadGraphicsOrDefault();
    init_window();
    if (!audioWorld_->init()) {
        TraceLog(LOG_WARNING, "AUDIO: continuing without audio device");
    }
    setupImGuiWithUiFont(assetStore_, kDefaultUiFontPath, true);
    init_script();
    userSettings_.controls = ControlsSettings::defaults();
    UserSettings::mergeControlsFromDisk(userSettings_.controls);
    world_.component<UserSettings>();
    world_.set<UserSettings>(userSettings_);
    registerInputModule(world_);
    registerUiModule(world_);
    registerPhysicsModule(world_, physicsWorld_.get());
    registerAudioModule(world_, audioWorld_.get(), assetStore_, scheme_);
    registerCameraModule(world_);
    registerInteractModule(world_);
    registerRenderModule(world_, assetStore_, config_, scheme_);
    running_ = true;
}

App::~App() {
    shutdown();
}

int App::run() {
    while (running_ && !WindowShouldClose()) {
        world_.progress();
        if (world_.get<QuitRequest>().requested) {
            break;
        }
    }

    return 0;
}

void App::init_window() {
    prepareGraphicsInit(userSettings_.graphics);
    InitWindow(userSettings_.graphics.width, userSettings_.graphics.height, "slopengine");
    SetExitKey(KEY_NULL);
    applyGraphicsSettings(userSettings_.graphics);
}

void App::init_script() {
    scheme_ = s7_init();
    if (!assetStore_.loadScript(scheme_, "init")) {
        TraceLog(LOG_WARNING, "SCRIPT: init.s7 not loaded");
    }
    if (!assetStore_.loadData(scheme_, "actions")) {
        TraceLog(LOG_WARNING, "SCRIPT: data/actions.s7 not loaded");
    } else if (!registerPackageActionsFromScheme(scheme_)) {
        TraceLog(LOG_WARNING, "SCRIPT: failed to register package actions");
    }
    if (!assetStore_.loadData(scheme_, "items")) {
        TraceLog(LOG_WARNING, "SCRIPT: data/items.s7 not loaded");
    }
    if (!assetStore_.loadData(scheme_, "view")) {
        TraceLog(LOG_WARNING, "SCRIPT: data/view.s7 not loaded");
    }
    world_.component<ViewCanvas>();
    world_.set<ViewCanvas>(parseViewCanvasFromScheme(scheme_));
    world_.component<HudCanvas>();
    world_.set<HudCanvas>(parseHudCanvasFromScheme(scheme_));
    if (!assetStore_.loadScript(scheme_, "things")) {
        TraceLog(LOG_WARNING, "SCRIPT: things.s7 not loaded");
    }
}

void App::shutdown() {
    unregisterAudioModule(world_);
    unregisterPhysicsModule(world_);
    if (audioWorld_) {
        audioWorld_->deinit();
        audioWorld_.reset();
    }
    physicsWorld_.reset();

    if (scheme_) {
        s7_quit(scheme_);
        scheme_ = nullptr;
    }

    if (IsWindowReady()) {
        rlImGuiShutdown();
        CloseWindow();
    }
}

}
