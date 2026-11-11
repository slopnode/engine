#include "script/bot_script.hpp"

#include "camera/components.hpp"
#include "input/action_registry.hpp"
#include "input/input_command.hpp"
#include "input/input_state.hpp"
#include "input/player_command.hpp"
#include "script/proc_role.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include <s7.h>

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

constexpr float kMaxBotPitch = 1.55f;

flecs::world* g_botWorld = nullptr;
std::uint32_t g_botSeed = 0;
std::function<std::vector<BotKnownEntity>()> g_botKnown;

bool botReady() {
    return g_botWorld != nullptr && g_botWorld->has<InputState>() && g_botWorld->has<InputCommand>();
}

int botAction(const char* id) {
    return id == nullptr ? -1 : actionRegistry().indexOf(id);
}

s7_pointer g_bot_move(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "bot-move!", args);
    const float forward = in.realf("forward number");
    const float strafe = in.realf("strafe number");
    const float up = in.optRealf(0.0f);
    if (!in.ok()) {
        return in.error();
    }
    if (!botReady() || !std::isfinite(forward) || !std::isfinite(strafe) || !std::isfinite(up)) {
        return s7_f(sc);
    }
    InputCommand& command = g_botWorld->get_mut<InputCommand>();
    command.moveForward = std::clamp(forward, -1.0f, 1.0f);
    command.moveStrafe = std::clamp(strafe, -1.0f, 1.0f);
    command.moveUp = std::clamp(up, -1.0f, 1.0f);
    InputState& input = g_botWorld->get_mut<InputState>();
    const auto set = [&input](Action action, bool down) {
        const auto index = static_cast<std::size_t>(action);
        if (index < input.actionDown.size()) {
            input.actionDown[index] = down ? 1 : 0;
        }
    };
    set(Action::MoveForward, command.moveForward > 0.0f);
    set(Action::MoveBackward, command.moveForward < 0.0f);
    set(Action::MoveRight, command.moveStrafe > 0.0f);
    set(Action::MoveLeft, command.moveStrafe < 0.0f);
    return s7_t(sc);
}

s7_pointer g_bot_look(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "bot-look!", args);
    const float yaw = in.realf("yaw number");
    const float pitch = in.realf("pitch number");
    if (!in.ok()) {
        return in.error();
    }
    if (!botReady() || !std::isfinite(yaw) || !std::isfinite(pitch)) {
        return s7_f(sc);
    }
    const flecs::entity player = findLocalPlayer(*g_botWorld);
    FirstPersonController* controller = player.is_valid() ? player.try_get_mut<FirstPersonController>() : nullptr;
    if (controller == nullptr) {
        return s7_f(sc);
    }
    controller->yaw = std::remainder(yaw, 6.2831853f);
    controller->pitch = std::clamp(pitch, -kMaxBotPitch, kMaxBotPitch);
    return s7_t(sc);
}

s7_pointer g_bot_view(s7_scheme* sc, s7_pointer) {
    if (!botReady()) {
        return s7_f(sc);
    }
    const flecs::entity player = findLocalPlayer(*g_botWorld);
    const FirstPersonController* controller = player.is_valid() ? player.try_get<FirstPersonController>() : nullptr;
    if (controller == nullptr) {
        return s7_f(sc);
    }
    return s7_list(sc, 2, s7_make_real(sc, controller->yaw), s7_make_real(sc, controller->pitch));
}

s7_pointer g_bot_hold(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "bot-hold!", args);
    const char* id = in.string("action id string");
    const bool down = in.truthy("down");
    if (!in.ok()) {
        return in.error();
    }
    const int index = botAction(id);
    if (!botReady() || index < 0) {
        return s7_f(sc);
    }
    InputState& input = g_botWorld->get_mut<InputState>();
    if (static_cast<std::size_t>(index) >= input.actionDown.size()) {
        return s7_f(sc);
    }
    input.actionDown[static_cast<std::size_t>(index)] = down ? 1 : 0;
    return s7_t(sc);
}

s7_pointer g_bot_press(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "bot-press!", args);
    const char* id = in.string("action id string");
    if (!in.ok()) {
        return in.error();
    }
    const int index = botAction(id);
    if (!botReady() || index < 0) {
        return s7_f(sc);
    }
    InputState& input = g_botWorld->get_mut<InputState>();
    if (static_cast<std::size_t>(index) >= input.actionPressedPending.size()) {
        return s7_f(sc);
    }
    input.actionPressedPending[static_cast<std::size_t>(index)] = 1;
    return s7_t(sc);
}

s7_pointer g_bot_release_all(s7_scheme* sc, s7_pointer) {
    if (!botReady()) {
        return s7_f(sc);
    }
    InputState& input = g_botWorld->get_mut<InputState>();
    std::fill(input.actionDown.begin(), input.actionDown.end(), 0);
    InputCommand& command = g_botWorld->get_mut<InputCommand>();
    command.moveForward = 0.0f;
    command.moveStrafe = 0.0f;
    command.moveUp = 0.0f;
    return s7_t(sc);
}

s7_pointer g_bot_actions(s7_scheme* sc, s7_pointer) {
    s7_pointer out = s7_nil(sc);
    const std::vector<ActionDef>& actions = actionRegistry().actions();
    for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
        out = s7_cons(sc, s7_make_string(sc, it->id.c_str()), out);
    }
    return out;
}

s7_pointer g_bot_known(s7_scheme* sc, s7_pointer) {
    s7_pointer out = s7_nil(sc);
    if (!g_botKnown) {
        return out;
    }
    const std::vector<BotKnownEntity> known = g_botKnown();
    for (auto it = known.rbegin(); it != known.rend(); ++it) {
        out = s7_cons(sc, s7_cons(sc, s7_make_string(sc, it->name.c_str()),
                                  s7_make_symbol(sc, it->player ? "player" : "thing")), out);
    }
    return out;
}

s7_pointer g_bot_seed(s7_scheme* sc, s7_pointer) {
    return s7_make_integer(sc, static_cast<s7_int>(g_botSeed));
}

constexpr SchemeBinding kBotBindings[] = {
    {"bot-seed", g_bot_seed, 0, 0, false, "(bot-seed)", ScriptCap::BotInput},
    {"bot-move!", g_bot_move, 2, 1, false, "(bot-move! forward strafe [up])", ScriptCap::BotInput},
    {"bot-look!", g_bot_look, 2, 0, false, "(bot-look! yaw pitch)", ScriptCap::BotInput},
    {"bot-view", g_bot_view, 0, 0, false, "(bot-view)", ScriptCap::BotInput},
    {"bot-hold!", g_bot_hold, 2, 0, false, "(bot-hold! action down)", ScriptCap::BotInput},
    {"bot-press!", g_bot_press, 1, 0, false, "(bot-press! action)", ScriptCap::BotInput},
    {"bot-release-all!", g_bot_release_all, 0, 0, false, "(bot-release-all!)", ScriptCap::BotInput},
    {"bot-actions", g_bot_actions, 0, 0, false, "(bot-actions)", ScriptCap::BotInput},
    {"bot-known", g_bot_known, 0, 0, false, "(bot-known)", ScriptCap::BotInput},
};

}

BotScript::BotScript(flecs::world& world, s7_scheme* scheme, std::uint32_t seed,
                     std::function<std::vector<BotKnownEntity>()> known)
    : scheme_(scheme) {
    g_botWorld = &world;
    g_botSeed = seed;
    g_botKnown = std::move(known);
    registerBindings(scheme_, kBotBindings);
    environment_ = s7_inlet(scheme_, s7_nil(scheme_));
    protectedAt_ = static_cast<long long>(s7_gc_protect(scheme_, environment_));
}

BotScript::~BotScript() {
    if (protectedAt_ >= 0) {
        s7_gc_unprotect_at(scheme_, static_cast<unsigned int>(protectedAt_));
    }
    g_botWorld = nullptr;
    g_botKnown = nullptr;
}

bool BotScript::load(const std::string& source, const std::string& name) {
    name_ = name;
    ScriptCallGuard role(scheme_, PackageRole::Mod);
    ScriptScopeGuard scope(ScriptScope::Bot);
    const std::string wrapped = "(begin\n" + source + "\n)";
    s7_eval_c_string_with_environment(scheme_, wrapped.c_str(), environment_);
    return s7_is_procedure(s7_let_ref(scheme_, environment_, s7_make_symbol(scheme_, "bot-tick")));
}

void BotScript::tick(float dt) {
    const s7_pointer hook = s7_let_ref(scheme_, environment_, s7_make_symbol(scheme_, "bot-tick"));
    if (!s7_is_procedure(hook)) {
        return;
    }
    ScriptCallGuard role(scheme_, PackageRole::Mod);
    ScriptScopeGuard scope(ScriptScope::Bot);
    s7_call(scheme_, hook, s7_list(scheme_, 1, s7_make_real(scheme_, static_cast<double>(dt))));
}

}
