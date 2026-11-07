#include "game/app.hpp"

#include "audio/audio_module.hpp"
#include "camera/camera_module.hpp"
#include "core/log.hpp"
#include "core/package.hpp"
#include "core/engine_version.hpp"
#include "core/sim_clock.hpp"
#include "core/user_paths.hpp"
#include "game/game_state.hpp"
#include "game/loading_session.hpp"
#include "game/package_cli.hpp"
#include "game/script_boot.hpp"
#include "game/sim_interpolation.hpp"
#include "game/sim_pipeline.hpp"
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
#include "net/net_schema.hpp"
#include "net/enet_transport.hpp"
#include "net/net_state.hpp"
#include "particles/particle_module.hpp"
#include "fx/trail.hpp"
#include "fx/tracer.hpp"
#include "render/components.hpp"
#include "render/render_module.hpp"
#include "render/underwater_effect.hpp"
#include "script/save_script.hpp"
#include "script/scheme_call.hpp"
#include "script/scheme_harden.hpp"
#include "script/script_scope.hpp"
#include "ui/imgui_fonts.hpp"
#include "ui/slopui_world.hpp"
#include "ui/ui_module.hpp"
#include "ui/ui_state.hpp"

#include "rlImGui.h"

#include <raylib.h>
#include <s7.h>

#include <chrono>
#include <csignal>
#include <stdexcept>
#include <string>
#include <thread>

namespace slopengine {

namespace {

volatile std::sig_atomic_t g_dedicatedStop = 0;

void requestDedicatedStop(int) {
    g_dedicatedStop = 1;
}

}

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
    userSettings_.ui = loadUiSettings(settingsFilePath);
    init_window();
    if (config_.dedicated) {
        TraceLog(LOG_INFO, "DEDICATED: audio, drawing and local input disabled");
    } else if (!audioWorld_->init()) {
        TraceLog(LOG_WARNING, "AUDIO: continuing without audio device");
    } else if (const BusConfig* busConfig = assetStore_.getBusConfig()) {
        audioWorld_->loadBusConfig(*busConfig);
    }
    setupImGuiWithUiFont(assetStore_, kDefaultUiFontPath, true, "slopengine");
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    init_script();
    registerNetState(world_);
    installSlopUiTheme(world_, assetStore_, scheme_, userSettings_.ui.theme);
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
    registerSimPipelines(world_);
    registerSimInterpolation(world_);
    registerInputModule(world_);
    startNetwork(settingsFilePath);
    registerUiModule(world_, config_.debug, config_.profile);
    registerPhysicsModule(world_, physicsWorld_.get());
    registerSightModule(world_);
    registerNavModule(world_);
    registerAudioModule(world_, audioWorld_.get(), assetStore_, scheme_);
    registerCameraModule(world_);
    registerInteractModule(world_);
    registerParticleModule(world_, assetStore_);
    registerTrailModule(world_);
    registerTracerModule(world_);
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
    if (config_.dedicated) {
        return runDedicated();
    }
    while (running_ && !WindowShouldClose()) {
        if (hasActiveLoadingSession(world_)) {
            advanceLoadingSession(world_, assetStore_, scheme_);
            resetSimClock(world_);
            if (!hasActiveLoadingSession(world_) && net_ && net_->role() == NetRole::Server) {
                net_->serverMapLoaded();
            }
        } else {
            takePendingMapLoad();
        }
        if (net_ && net_->role() == NetRole::Client) {
            runFrameWithoutTicks(world_, GetFrameTime(), [this](float frameDt) { net_->clientFrame(frameDt); });
        } else {
            if (net_ && net_->role() == NetRole::Server) {
                net_->serverPoll();
            }
            runFramePipelines(world_, GetFrameTime());
        }
        if (world_.get<QuitRequest>().requested) {
            break;
        }
    }

    return 0;
}

void App::takePendingMapLoad() {
    auto pendingMap = takeRequestedMapLoad();
    if (!pendingMap) {
        return;
    }
    if (!config_.dedicated && shouldShowLoadingScreen(pendingMap->reason)) {
        beginLoadingSession(world_, assetStore_, scheme_, pendingMap->mapName, pendingMap->reason);
    } else {
        changeMap(world_, assetStore_, scheme_, pendingMap->mapName, pendingMap->reason);
        if (net_ && net_->role() == NetRole::Server) {
            net_->serverMapLoaded();
        }
        if (config_.dedicated && world_.has<CurrentMap>() &&
            world_.get<CurrentMap>().id == pendingMap->mapName) {
            TraceLog(
                LOG_INFO,
                "DEDICATED: map '%s' running at %d ticks per second",
                pendingMap->mapName.c_str(),
                static_cast<int>(1.0f / kSimTickDt + 0.5f));
        }
    }
    resetSimClock(world_);
}

void App::startNetwork(const std::filesystem::path& settingsFilePath) {
    if (config_.listenAddress.empty() && config_.connectAddress.empty()) {
        return;
    }
    if (!config_.listenAddress.empty() && !config_.connectAddress.empty()) {
        throw std::invalid_argument("--listen and --connect can't be used together");
    }
    NetSessionConfig net;
    net.listenAddress = config_.listenAddress;
    net.connectAddress = config_.connectAddress;
    net.serverKeyPath = settingsFilePath.parent_path() / "server.key";
    net.knownServersPath = settingsFilePath.parent_path() / "known_servers";
    if (!config_.serverKey.empty()) {
        SecureKey pinned{};
        if (!parseSecureKeyHex(config_.serverKey, pinned)) {
            throw std::invalid_argument("--server-key needs a 64-digit hex key");
        }
        net.pinnedServerKey = pinned;
    }
    net.hello.engineVersion = kSlopengineVersion;
    net.hello.schemaHash = netSchema().hash();
    for (const Package& package : assetStore_.packages()) {
        net.hello.packages.push_back({package.meta().id, package.meta().version});
    }
    const std::vector<NetSessionKind>& kinds = netSchema().sessionKinds();
    if (!config_.sessionKind.empty() || !kinds.empty()) {
        const NetSessionKind* kind =
            config_.sessionKind.empty() ? &kinds.front() : netSchema().findSessionKind(config_.sessionKind);
        if (kind == nullptr) {
            throw std::invalid_argument("--session " + config_.sessionKind + " isn't declared in *net-sessions*");
        }
        net.sessionKind = kind->name;
        net.playerCeiling = config_.maxPlayers > 0 ? std::min(config_.maxPlayers, kind->maxPlayers) : kind->maxPlayers;
        net.maxPlayers = net.playerCeiling;
    } else {
        net.playerCeiling = config_.maxPlayers > 0 ? std::min(config_.maxPlayers, kMaxNetPlayers) : kMaxNetPlayers;
        net.maxPlayers = config_.maxPlayers > 0 ? net.playerCeiling : 8;
    }
    net.hostPlayerVisible = !config_.dedicated;
    net.firstClientTakesMapPlayer = config_.dedicated;
    net.loadMap = [](const std::string& mapId, const std::string& reason) { requestMapLoad(mapId, reason); };
    net.disconnected = [this](const std::string&) { enterMenu(world_); };
    EnetTransportOptions transport;
    transport.maxPeers = static_cast<std::size_t>(net.playerCeiling) + net.maxPendingClients;
    net_ = std::make_unique<NetSession>(world_, std::move(net), createEnetTransport(transport));
    net_->registerSystems();
    if (!net_->start()) {
        throw std::runtime_error("network could not start");
    }
}

int App::runDedicated() {
    std::signal(SIGINT, requestDedicatedStop);
    std::signal(SIGTERM, requestDedicatedStop);
    double last = GetTime();
    while (running_ && g_dedicatedStop == 0) {
        takePendingMapLoad();
        const double now = GetTime();
        const float frameDt = static_cast<float>(now - last);
        last = now;
        if (net_ && net_->role() == NetRole::Client) {
            net_->clientFrame(frameDt);
        } else {
            if (net_ && net_->role() == NetRole::Server) {
                net_->serverPoll();
            }
            runSimulationTicks(world_, frameDt);
        }
        if (world_.get<QuitRequest>().requested) {
            break;
        }
        const double untilNextTick = kSimTickDt - world_.get<SimClock>().accumulator;
        if (untilNextTick > 0.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(untilNextTick));
        }
    }
    TraceLog(LOG_INFO, "DEDICATED: stopping after %llu ticks",
        static_cast<unsigned long long>(world_.get<SimClock>().tick));
    return 0;
}

void App::init_window() {
    Log::init(config_.verbose ? LogLevel::Debug : LogLevel::Info);
    Log::addDefaultConsoleSink();
    if (config_.dedicated) {
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(320, 240, "slopengine dedicated");
        SetExitKey(KEY_NULL);
        return;
    }
    prepareGraphicsInit(userSettings_.graphics);
    InitWindow(userSettings_.graphics.width, userSettings_.graphics.height, "slopengine");
    SetExitKey(KEY_NULL);
    applyGraphicsSettings(userSettings_.graphics);
}

void App::init_script() {
    scheme_ = s7_init();
    hardenSchemeRuntime(scheme_);
    bindRoleApi(scheme_);
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
    loadPackageNetSchema(scheme_, assetStore_);
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
