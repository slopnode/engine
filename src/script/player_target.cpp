#include "script/player_target.hpp"

#include "net/net_state.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"
#include "script/proc_role.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include <string>
#include <vector>

namespace slopengine {

namespace {

constexpr std::size_t kMaxScriptPlayerDepth = 64;

flecs::world* g_playerWorld = nullptr;

}

ScriptPlayerGuard::ScriptPlayerGuard(std::string id) : previous_(std::move(scriptPlayerStack().current)) {
    scriptPlayerStack().current = std::move(id);
}

ScriptPlayerGuard::~ScriptPlayerGuard() {
    scriptPlayerStack().current = std::move(previous_);
}

const std::string& currentScriptPlayer() {
    return scriptPlayerStack().current;
}

bool scriptPlayerIsLocal() {
    return currentScriptPlayer() == kLocalNetPlayerId;
}

flecs::entity scriptPlayer(const flecs::world& world) {
    return scriptPlayer(world, nullptr);
}

flecs::entity scriptPlayer(const flecs::world& world, s7_pointer args) {
    const char* id = currentScriptPlayer().c_str();
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

bool knownScriptPlayer(s7_pointer args) {
    return g_playerWorld != nullptr && s7_is_pair(args) && s7_is_string(s7_car(args)) &&
        scriptPlayer(*g_playerWorld, args).is_valid();
}

s7_pointer g_restore_script_player(s7_scheme* sc, s7_pointer) {
    ScriptPlayerStack& players = scriptPlayerStack();
    if (players.saved.size() > players.floor) {
        players.current = std::move(players.saved.back());
        players.saved.pop_back();
    }
    return s7_unspecified(sc);
}

s7_pointer g_check_script_player(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    return s7_make_boolean(sc, knownScriptPlayer(args));
}

s7_pointer g_enter_script_player(s7_scheme* sc, s7_pointer args) {
    ScriptPlayerStack& players = scriptPlayerStack();
    if (!requireCap(sc, ScriptCap::ReadWorld) || !knownScriptPlayer(args) ||
        players.saved.size() >= kMaxScriptPlayerDepth) {
        return s7_error(sc, s7_make_symbol(sc, "with-script-player"),
                        s7_list(sc, 1, s7_make_string(sc, "with-script-player: can't switch to that player")));
    }
    players.saved.push_back(players.current);
    players.current = s7_string(s7_car(args));
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
            8,
            s7_make_symbol(scheme, "known"),
            s7_make_function(scheme, "with-script-player", g_check_script_player, 1, 0, false, ""),
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
            "(lambda (id thunk) (if (known id) (wind (lambda () (enter id)) thunk leave) #f))",
            env));
}

}
