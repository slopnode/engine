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
#include "script/input_script.hpp"
#include "script/script_context.hpp"
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
#include "net/conditioned_transport.hpp"
#include "net/enet_transport.hpp"
#include "net/net_state.hpp"
#include "particles/particle_module.hpp"
#include "fx/trail.hpp"
#include "fx/tracer.hpp"
#include "render/animation_player.hpp"
#include "render/animation_systems.hpp"
#include "render/components.hpp"
#include "render/render_module.hpp"
#include "render/underwater_effect.hpp"
#include "script/bot_script.hpp"
#include "script/motor_script.hpp"
#include "script/save_script.hpp"
#include "script/scheme_call.hpp"
#include "script/scheme_harden.hpp"
#include "script/script_scope.hpp"
#include "script/thing_script.hpp"
#include "ui/imgui_fonts.hpp"
#include "ui/slopui_world.hpp"
#include "ui/ui_module.hpp"
#include "ui/ui_state.hpp"

#include "rlImGui.h"

#include <raylib.h>
#include <s7.h>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace slopengine {

namespace {

volatile std::sig_atomic_t g_dedicatedStop = 0;

void requestDedicatedStop(int) {
    g_dedicatedStop = 1;
}

class NetKeepAlive {
public:
    explicit NetKeepAlive(NetSession* net) {
        if (net != nullptr) {
            thread_ = std::thread([this, net] {
                std::unique_lock lock(mutex_);
                while (!wake_.wait_for(lock, std::chrono::milliseconds(20), [this] { return stop_; })) {
                    net->keepAlive();
                }
            });
        }
    }

    ~NetKeepAlive() {
        {
            const std::lock_guard lock(mutex_);
            stop_ = true;
        }
        wake_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    NetKeepAlive(const NetKeepAlive&) = delete;
    NetKeepAlive& operator=(const NetKeepAlive&) = delete;

private:
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::thread thread_;
};

std::uint32_t botSeed() {
    return static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
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
        world_.add<Headless>();
        TraceLog(LOG_INFO, "DEDICATED: audio, drawing and local input disabled");
    } else if (!audioWorld_->init()) {
        TraceLog(LOG_WARNING, "AUDIO: continuing without audio device");
    } else if (const BusConfig* busConfig = assetStore_.getBusConfig()) {
        audioWorld_->loadBusConfig(*busConfig);
    }
    if (!config_.dedicated) {
        setupImGuiWithUiFont(assetStore_, kDefaultUiFontPath, true, "slopengine");
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    }
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
            {
                const NetKeepAlive keepAlive(net_.get());
                advanceLoadingSession(world_, assetStore_, scheme_);
            }
            resetSimClock(world_);
            if (!hasActiveLoadingSession(world_) && net_ && net_->role() == NetRole::Server) {
                net_->serverMapLoaded();
            }
        } else {
            takeNetControl();
            takePendingMapLoad();
        }
        if (net_ && net_->role() == NetRole::Client) {
            runFrameWithoutTicks(world_, GetFrameTime(), [this](float frameDt) {
                net_->clientFrame(frameDt);
                runCosmeticTicks(world_, frameDt);
            });
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
    if (net_ && net_->role() == NetRole::Server) {
        net_->serverMapLoading();
    }
    if (!config_.dedicated && shouldShowLoadingScreen(pendingMap->reason)) {
        beginLoadingSession(world_, assetStore_, scheme_, pendingMap->mapName, pendingMap->reason);
    } else {
        {
            const NetKeepAlive keepAlive(net_.get());
            changeMap(world_, assetStore_, scheme_, pendingMap->mapName, pendingMap->reason);
        }
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
    settingsFilePath_ = settingsFilePath;
    NetSession::registerSystems(world_, [this]() { return net_.get(); });
    world_.set<NetStatus>({});
    if (config_.listenAddress.empty() && config_.connectAddress.empty()) {
        return;
    }
    if (!config_.listenAddress.empty() && !config_.connectAddress.empty()) {
        throw std::invalid_argument("--listen and --connect can't be used together");
    }
    if (!config_.bot.empty() && (!config_.dedicated || config_.connectAddress.empty())) {
        throw std::invalid_argument("--bot needs --dedicated and --connect");
    }
    openNetSession(config_.listenAddress, config_.connectAddress, config_.sessionKind, config_.maxPlayers);
    if (!config_.bot.empty()) {
        startBot();
    }
    updateNetStatus();
}

void App::openNetSession(
    const std::string& listenAddress,
    const std::string& connectAddress,
    const std::string& sessionKind,
    int maxPlayers) {
    netLogMark_ = Log::mark();
    netWasConnected_ = false;
    NetSessionConfig net;
    net.listenAddress = listenAddress;
    net.connectAddress = connectAddress;
    net.serverKeyPath = settingsFilePath_.parent_path() / "server.key";
    net.knownServersPath = settingsFilePath_.parent_path() / "known_servers";
    if (!connectAddress.empty()) {
        SecureKey secret{};
        if (loadOrCreateServerKey(settingsFilePath_.parent_path() / "client.key", secret)) {
            net.identitySecret = secret;
        } else {
            Log::warning(LogCategory::General, "NET: no client key in %s, servers won't recognise a rejoin",
                         settingsFilePath_.parent_path().string().c_str());
        }
    }
    if (!config_.serverKey.empty()) {
        SecureKey pinned{};
        if (!parseSecureKeyHex(config_.serverKey, pinned)) {
            throw std::invalid_argument("--server-key needs a 64-digit hex key");
        }
        net.pinnedServerKey = pinned;
    }
    net.hello.engineVersion = kSlopengineVersion;
    net.profile = localProfile();
    netProfileApplied_ = net.profile;
    net.hello.schemaHash = netSchema().hash() ^ (motorRulesHash() * 0x9e3779b97f4a7c15ull);
    for (const Package& package : assetStore_.packages()) {
        net.hello.packages.push_back({package.meta().id, package.meta().version});
    }
    const std::vector<NetSessionKind>& kinds = netSchema().sessionKinds();
    if (!sessionKind.empty() || !kinds.empty()) {
        const NetSessionKind* kind = sessionKind.empty() ? &kinds.front() : netSchema().findSessionKind(sessionKind);
        if (kind == nullptr) {
            throw std::invalid_argument("session " + sessionKind + " isn't declared in *net-sessions*");
        }
        net.sessionKind = kind->name;
        net.playerCeiling = maxPlayers > 0 ? std::min(maxPlayers, kind->maxPlayers) : kind->maxPlayers;
        net.maxPlayers = net.playerCeiling;
    } else {
        net.playerCeiling = maxPlayers > 0 ? std::min(maxPlayers, kMaxNetPlayers) : kMaxNetPlayers;
        net.maxPlayers = maxPlayers > 0 ? net.playerCeiling : 8;
    }
    net.hostPlayerVisible = !config_.dedicated;
    net.firstClientTakesMapPlayer = config_.dedicated;
    net.loadMap = [](const std::string& mapId, const std::string& reason) { requestMapLoad(mapId, reason); };
    net.disconnected = [this](const std::string& reason) {
        if (bot_) {
            enterMenu(world_);
            return;
        }
        netError_ = reason;
        netStopPending_ = true;
        requestMapLoad("", "menu");
        if (netWasConnected_) {
            reportNetFailure("Disconnected", "Disconnected from '" + netAddress_ + "'.", reason);
        } else {
            reportNetFailure("Connection Failed", "Failed to connect to '" + netAddress_ + "'.", reason);
        }
    };
    net.spawnPresentation = [this](const std::string& name, const NetSpawnRecipe& recipe) {
        return spawnNetPresentation(world_, name, recipe);
    };
    net.applyEffect = [](flecs::entity entity, const NetEffect& effect, bool makePiece) {
        const Vector3 push{effect.push[0], effect.push[1], effect.push[2]};
        if (effect.kind == NetEffectKind::Sever) {
            severActorPart(entity, effect.bone, push, effect.speed, effect.spin, effect.lifetime, makePiece);
        } else if (effect.kind == NetEffectKind::Hide) {
            hideActorBone(entity, effect.bone);
        } else if (effect.kind == NetEffectKind::Attach) {
            attachActorGeo(entity, effect.attachment);
        } else if (effect.kind == NetEffectKind::Detach) {
            detachActorGeo(entity, effect.attachment.slot);
        } else {
            ragdollActorBody(entity, push, effect.speed, effect.bone, effect.lifetime);
        }
    };
    net.corpsePose = [](flecs::entity entity) { return captureCorpsePose(entity); };
    net.applyCorpse = [](flecs::entity entity, const std::vector<NetPose>& bones) { applyCorpsePose(entity, bones); };
    net.predict = [this](const PlayerCommand& command) {
        if (!world_.has<ScriptContext>() || world_.get<ScriptContext>().scheme == nullptr) {
            return;
        }
        PredictedCommandGuard guard(command);
        callHook1Real(world_.get<ScriptContext>().scheme, "predict", static_cast<double>(kSimTickDt),
                      ScriptScope::Predict);
    };
    net.clipDuration = [](flecs::entity entity) {
        const AnimationPlayer* animation = entity.try_get<AnimationPlayer>();
        return animation != nullptr ? animationClipDuration(entity.world(), *animation) : 0.0f;
    };
    EnetTransportOptions transport;
    transport.maxPeers = static_cast<std::size_t>(net.playerCeiling) + net.maxPendingClients;
    transport.maxPeersPerAddress = static_cast<std::size_t>(config_.netPeersPerAddress);
    net.statsSeconds = config_.netStatsSeconds;
    net.maxRewindTicks = static_cast<std::uint64_t>(std::lround(config_.netMaxRewindMs / 1000.0f / kSimTickDt));
    net.rejoinHoldTicks = static_cast<std::uint64_t>(std::lround(config_.netRejoinHoldSeconds / kSimTickDt));
    std::unique_ptr<NetTransport> wire = createEnetTransport(transport);
    const NetConditions conditions{config_.netLagMs, config_.netJitterMs, config_.netLoss};
    if (conditions.active()) {
        TraceLog(LOG_INFO, "NET: simulating %.0f ms lag, %.0f ms jitter, %.0f%% loss on what this process sends",
            conditions.lagMs, conditions.jitterMs, conditions.lossFraction * 100.0f);
        wire = createConditionedTransport(std::move(wire), conditions, botSeed());
    }
    net_ = std::make_unique<NetSession>(world_, std::move(net), std::move(wire));
    if (!net_->start()) {
        net_.reset();
        resetNetSessionState(world_);
        throw std::runtime_error(
            listenAddress.empty() ? "could not connect to " + connectAddress : "could not listen on " + listenAddress);
    }
    netAddress_ = listenAddress.empty() ? connectAddress : listenAddress;
}

void App::closeNetSession() {
    if (!net_) {
        return;
    }
    bot_.reset();
    net_.reset();
    netAddress_.clear();
    resetNetSessionState(world_);
}

std::string App::hostMapFor(const NetControlRequest& request) const {
    if (!request.map.empty()) {
        return request.map;
    }
    const std::vector<NetSessionKind>& kinds = netSchema().sessionKinds();
    const NetSessionKind* kind = request.session.empty()
        ? (kinds.empty() ? nullptr : &kinds.front())
        : netSchema().findSessionKind(request.session);
    if (kind != nullptr && !kind->maps.empty()) {
        return kind->maps.front();
    }
    if (world_.has<CurrentMap>() && !world_.get<CurrentMap>().id.empty() && !isMenu(world_)) {
        return world_.get<CurrentMap>().id;
    }
    const std::vector<AssetStore::MapListEntry> maps = assetStore_.listMaps();
    return maps.empty() ? std::string{} : maps.front().id;
}

void App::takeNetControl() {
    if (netStopPending_) {
        netStopPending_ = false;
        closeNetSession();
    }
    std::optional<NetControlRequest> request = takeNetControlRequest();
    if (request) {
        if (request->kind == NetControlKind::Leave) {
            const bool wasOnline = net_ != nullptr;
            closeNetSession();
            netError_.clear();
            if (wasOnline) {
                requestMapLoad("", "menu");
            }
        } else if (request->kind == NetControlKind::Host) {
            const std::string map = hostMapFor(*request);
            const bool wasOnline = net_ != nullptr;
            closeNetSession();
            netError_.clear();
            if (map.empty()) {
                netError_ = "there is no map to host";
                reportNetFailure("Host Failed", "Failed to host a game.", netError_);
            } else {
                try {
                    openNetSession(request->address, "", request->session, request->maxPlayers);
                    requestMapLoad(map, "fresh");
                } catch (const std::exception& error) {
                    netError_ = error.what();
                    reportNetFailure("Host Failed", "Failed to host a game.", netError_);
                }
            }
            if (!net_ && wasOnline) {
                requestMapLoad("", "menu");
            }
        } else {
            const bool wasOnline = net_ != nullptr;
            closeNetSession();
            netError_.clear();
            try {
                openNetSession("", request->address, "", 0);
            } catch (const std::exception& error) {
                netError_ = error.what();
                reportNetFailure("Connection Failed", "Failed to connect to '" + request->address + "'.", netError_);
            }
            if (!net_ && wasOnline) {
                requestMapLoad("", "menu");
            }
        }
    }
    if (net_) {
        NetProfile profile = localProfile();
        if (!(profile == netProfileApplied_)) {
            netProfileApplied_ = profile;
            net_->setLocalProfile(std::move(profile));
        }
    }
    updateNetStatus();
}

NetProfile App::localProfile() const {
    const UiSettings& ui = world_.has<UserSettings>() ? world_.get<UserSettings>().ui : userSettings_.ui;
    NetProfile profile;
    profile.name = sanitizeNetDisplayName(ui.playerName);
    if (profile.name.empty()) {
        profile.name = kLocalNetPlayerId;
    }
    const std::vector<const NetField*> fields = netSchema().profileFields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        NetValue value = fields[i]->defaultValue;
        const auto found = ui.profileValues.find(fields[i]->name);
        if (found != ui.profileValues.end()) {
            NetValue parsed;
            if (netValueFromText(fields[i]->type, found->second, parsed)) {
                value = std::move(parsed);
            }
        }
        profile.fields.emplace_back(static_cast<int>(i), std::move(value));
    }
    return profile;
}

void App::reportNetFailure(const std::string& title, const std::string& message, const std::string& reason) {
    if (config_.dedicated) {
        return;
    }
    reportError(world_, title, message, reason.empty() ? std::string() : "Reason: " + reason,
                Log::warningsSince(netLogMark_));
}

void App::updateNetStatus() {
    NetStatus status{};
    status.error = netError_;
    if (net_) {
        status.address = netAddress_;
        if (net_->role() == NetRole::Server) {
            status.kind = NetStatusKind::Hosting;
            status.players = net_->serverPlayerCount();
            status.maxPlayers = net_->serverPlayerLimit();
        } else if (net_->role() == NetRole::Client) {
            status.kind = net_->clientConnected() ? NetStatusKind::Connected : NetStatusKind::Connecting;
            netWasConnected_ = netWasConnected_ || net_->clientConnected();
        }
        if (world_.has<NetSessionLimits>()) {
            status.session = world_.get<NetSessionLimits>().kind;
        }
    }
    world_.set<NetStatus>(std::move(status));
    if (!net_) {
        const NetProfile profile = localProfile();
        NetRoster roster{{{kLocalNetPlayerId, profile.name}}};
        if (!world_.has<NetRoster>() || !(world_.get<NetRoster>().entries == roster.entries)) {
            world_.set<NetRoster>(std::move(roster));
        }
        setNetProfile(world_, kLocalNetPlayerId, profile);
    }
}



void App::startBot() {
    std::string source;
    const std::filesystem::path file(config_.bot);
    std::error_code error;
    if (file.has_extension() && std::filesystem::is_regular_file(file, error)) {
        std::ifstream in(file, std::ios::binary);
        source.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    } else if (config_.bot.find_first_of("/\\.") == std::string::npos) {
        source = assetStore_.getScriptSource("bots/" + config_.bot);
    }
    if (source.empty()) {
        throw std::invalid_argument("--bot " + config_.bot + " names no scripts/bots/*.s7 in the mounted packages and no file");
    }
    bot_ = std::make_unique<BotScript>(world_, scheme_, botSeed(), [this]() {
        std::vector<BotKnownEntity> out;
        for (auto& [name, player] : net_->clientKnownEntities()) {
            out.push_back({std::move(name), player});
        }
        return out;
    });
    if (!bot_->load(source, config_.bot)) {
        throw std::invalid_argument("--bot " + config_.bot + " doesn't define bot-tick");
    }
    TraceLog(LOG_INFO, "NET: playing as bot %s", config_.bot.c_str());
}

int App::runDedicated() {
    std::signal(SIGINT, requestDedicatedStop);
    std::signal(SIGTERM, requestDedicatedStop);
    auto last = std::chrono::steady_clock::now();
    while (running_ && g_dedicatedStop == 0) {
        takeNetControl();
        takePendingMapLoad();
        const auto now = std::chrono::steady_clock::now();
        const float frameDt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (net_ && net_->role() == NetRole::Client) {
            if (bot_) {
                bot_->tick(frameDt);
            }
            net_->clientFrame(frameDt);
            runCosmeticTicks(world_, frameDt);
            presentSimEye(world_);
        } else {
            if (net_ && net_->role() == NetRole::Server) {
                net_->serverPoll();
            }
            runSimulationTicks(world_, frameDt);
        }
        if (world_.get<QuitRequest>().requested) {
            break;
        }
        const double untilNextTick = net_ && net_->role() == NetRole::Client
            ? net_->clientSecondsUntilCommand()
            : kSimTickDt - world_.get<SimClock>().accumulator;
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
    loadPackageMotorRules(scheme_, assetStore_);
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
        TraceLog(LOG_DEBUG, "SCRIPT: data/cli.s7 not loaded (no package CLI flags)");
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
