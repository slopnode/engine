#include "script/player_target.hpp"

#include "net/net_state.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include <string>
#include <vector>

namespace slopengine {

namespace {

std::string g_scriptPlayer = kLocalNetPlayerId;
flecs::world* g_playerWorld = nullptr;
std::vector<std::string> g_restorePlayers;

}

ScriptPlayerGuard::ScriptPlayerGuard(std::string id) : previous_(std::move(g_scriptPlayer)) {
    g_scriptPlayer = std::move(id);
}

ScriptPlayerGuard::~ScriptPlayerGuard() {
    g_scriptPlayer = std::move(previous_);
}

const std::string& currentScriptPlayer() {
    return g_scriptPlayer;
}

bool scriptPlayerIsLocal() {
    return g_scriptPlayer == kLocalNetPlayerId;
}

flecs::entity scriptPlayer(const flecs::world& world) {
    return scriptPlayer(world, nullptr);
}

flecs::entity scriptPlayer(const flecs::world& world, s7_pointer args) {
    const char* id = g_scriptPlayer.c_str();
    if (args != nullptr && s7_is_pair(args) && s7_is_string(s7_car(args))) {
        id = s7_string(s7_car(args));
    }
    const flecs::entity player = world.lookup(id);
    if (!player.is_valid() || (!player.has<PlayerBody>() && std::string_view(id) != kLocalNetPlayerId)) {
        return {};
    }
    return player;
}

bool scriptPlayerFeet(const flecs::world& world, flecs::entity player, float& x, float& y, float& z) {
    if (world.has<PhysicsContext>()) {
        const PhysicsWorld* physics = world.get<PhysicsContext>().world;
        const std::uint64_t id = static_cast<std::uint64_t>(player.id());
        if (physics != nullptr && physics->hasCharacter(id)) {
            const JPH::RVec3 feet = physics->characterPosition(id);
            x = static_cast<float>(feet.GetX());
            y = static_cast<float>(feet.GetY());
            z = static_cast<float>(feet.GetZ());
            return true;
        }
    }
    if (!player.has<Lens>()) {
        return false;
    }
    const Lens& lens = player.get<Lens>();
    const float eye = player.has<CharacterMotor>() ? player.get<CharacterMotor>().eyeHeight : 1.7f;
    x = lens.camera.position.x;
    y = lens.camera.position.y - eye;
    z = lens.camera.position.z;
    return true;
}

namespace {

s7_pointer g_script_player(s7_scheme* sc, s7_pointer) {
    return s7_make_string(sc, currentScriptPlayer().c_str());
}

s7_pointer g_restore_script_player(s7_scheme* sc, s7_pointer) {
    if (!g_restorePlayers.empty()) {
        g_scriptPlayer = std::move(g_restorePlayers.back());
        g_restorePlayers.pop_back();
    }
    return s7_unspecified(sc);
}

s7_pointer g_enter_script_player(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    ArgReader in(sc, "with-script-player", args);
    const char* id = in.string("player id string");
    if (g_playerWorld == nullptr || !scriptPlayer(*g_playerWorld, args).is_valid()) {
        return s7_f(sc);
    }
    g_restorePlayers.push_back(g_scriptPlayer);
    g_scriptPlayer = id;
    return s7_t(sc);
}

constexpr SchemeBinding kScriptPlayerBindings[] = {
    {"script-player", g_script_player, 0, 0, false, "(script-player)", ScriptCap::ReadWorld},
};

}

void bindScriptPlayerApi(flecs::world& world, s7_scheme* scheme) {
    g_playerWorld = &world;
    if (scheme == nullptr) {
        return;
    }
    registerBindings(scheme, kScriptPlayerBindings);
    const s7_pointer env = s7_inlet(
        scheme,
        s7_list(
            scheme,
            6,
            s7_make_symbol(scheme, "enter"),
            s7_make_function(scheme, "with-script-player", g_enter_script_player, 1, 0, false, ""),
            s7_make_symbol(scheme, "leave"),
            s7_make_function(scheme, "with-script-player", g_restore_script_player, 0, 0, false, ""),
            s7_make_symbol(scheme, "wind"),
            s7_name_to_value(scheme, "dynamic-wind")));
    s7_define_variable(
        scheme,
        "with-script-player",
        s7_eval_c_string_with_environment(
            scheme,
            "(lambda (id thunk) (if (enter id) (wind (lambda () #f) thunk leave) #f))",
            env));
}

}
