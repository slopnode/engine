#include "game/app.hpp"

#include "camera/camera_module.hpp"
#include "input/input_module.hpp"
#include "interact/interact_module.hpp"
#include "physics/physics_module.hpp"
#include "render/render_module.hpp"
#include "ui/ui_module.hpp"
#include "ui/ui_state.hpp"

#include "rlImGui.h"

#include <raylib.h>
#include <s7.h>

namespace slopengine {

App::App(AppConfig config)
    : config_{std::move(config)}
    , userSettings_{UserSettings::loadOrDefault()}
    , assetStore_{config_}
    , world_{}
    , physicsWorld_{std::make_unique<PhysicsWorld>()} {
    init_window();
    rlImGuiSetup(true);
    init_script();
    world_.component<UserSettings>();
    world_.set<UserSettings>(userSettings_);
    registerInputModule(world_);
    registerUiModule(world_);
    registerPhysicsModule(world_, physicsWorld_.get());
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
    if (!assetStore_.loadData(scheme_, "items")) {
        TraceLog(LOG_WARNING, "SCRIPT: data/items.s7 not loaded");
    }
    if (!assetStore_.loadScript(scheme_, "things")) {
        TraceLog(LOG_WARNING, "SCRIPT: things.s7 not loaded");
    }
}

void App::shutdown() {
    unregisterPhysicsModule(world_);
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
