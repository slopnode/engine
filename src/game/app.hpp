#pragma once

#include "assets/asset_store.hpp"
#include "audio/audio_world.hpp"
#include "game/app_config.hpp"
#include "game/host_bots.hpp"
#include "game/net_control.hpp"
#include "game/user_settings.hpp"
#include "net/net_session.hpp"
#include "physics/physics_world.hpp"

#include <flecs.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

struct s7_scheme;

namespace slopengine {

class BotScript;

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
    void takePendingMapLoad();
    void startNetwork(const std::filesystem::path& settingsFilePath);
    void openNetSession(
        const std::string& listenAddress,
        const std::string& connectAddress,
        const std::string& sessionKind,
        int maxPlayers);
    void closeNetSession();
    std::string hostMapFor(const NetControlRequest& request) const;
    void takeNetControl();
    void updateNetStatus();
    void reportNetFailure(const std::string& title, const std::string& message, const std::string& reason);
    NetProfile localProfile() const;
    void startBot();
    void startHostBots(int count, std::string behavior);
    int runDedicated();

    AppConfig config_;
    UserSettings userSettings_;
    AssetStore assetStore_;
    flecs::world world_;
    std::unique_ptr<PhysicsWorld> physicsWorld_;
    std::unique_ptr<AudioWorld> audioWorld_;
    s7_scheme* scheme_ = nullptr;
    std::unique_ptr<NetSession> net_;
    std::unique_ptr<BotScript> bot_;
    HostBots hostBots_;
    std::filesystem::path settingsFilePath_;
    std::string netAddress_;
    std::string netError_;
    NetProfile netProfileApplied_;
    bool netStopPending_ = false;
    bool netWasConnected_ = false;
    std::uint64_t netLogMark_ = 0;
    bool running_ = false;
};

}
