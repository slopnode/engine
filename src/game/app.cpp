#include "game/app.hpp"

#include "camera/camera_module.hpp"
#include "input/input_module.hpp"
#include "interact/interact_module.hpp"
#include "render/render_module.hpp"
#include "ui/ui_module.hpp"

#include "rlImGui.h"

#include <raylib.h>
#include <s7.h>

namespace slopengine {

App::App(AppConfig config)
    : config_{std::move(config)}
    , assetStore_{config_}
    , world_{} {
    init_window();
    rlImGuiSetup(true);
    init_script();
    registerInputModule(world_);
    registerUiModule(world_);
    registerCameraModule(world_);
    registerInteractModule(world_);
    registerRenderModule(world_, assetStore_);
    running_ = true;
}

App::~App() {
    shutdown();
}

int App::run() {
    while (running_ && !WindowShouldClose()) {
        world_.progress();
    }

    return 0;
}

void App::init_window() {
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "slopengine");
}

void App::init_script() {
    scheme_ = s7_init();
    if (!assetStore_.loadScript(scheme_, "init.s7")) {
        TraceLog(LOG_WARNING, "SCRIPT: init.s7 not loaded");
    }
}

void App::shutdown() {
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
