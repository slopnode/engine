#pragma once

#include "assets/asset_store.hpp"
#include "game/app_config.hpp"

#include <flecs.h>

struct s7_scheme;

namespace slopengine {

/** Top-level application entry point. */
class App {
public:
    /** Creates the window, script runtime, and render module from @p config. */
    explicit App(AppConfig config);

    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /** Runs the main loop until the window closes. Returns the process exit code. */
    int run();

    /** Returns the asset store used by the application. */
    AssetStore& assetStore() { return assetStore_; }

    /** Returns the asset store used by the application. */
    const AssetStore& assetStore() const { return assetStore_; }

private:
    void init_window();
    void init_script();
    void shutdown();

    AppConfig config_;
    AssetStore assetStore_;
    flecs::world world_;
    s7_scheme* scheme_ = nullptr;
    bool running_ = false;
};

}
