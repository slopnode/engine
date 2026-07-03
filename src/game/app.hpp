#pragma once

#include "assets/asset_store.hpp"
#include "game/app_config.hpp"

#include <flecs.h>

struct s7_scheme;

namespace daggerlike {

class App {
public:
    explicit App(AppConfig config);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int run();

    AssetStore& assetStore() { return assetStore_; }
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
