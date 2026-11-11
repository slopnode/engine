#include "test_assert.hpp"

#include "camera/components.hpp"
#include "input/action_registry.hpp"
#include "input/input_command.hpp"
#include "input/input_state.hpp"
#include "input/player_command.hpp"
#include "script/bot_script.hpp"
#include "script/proc_role.hpp"
#include "script/script_scope.hpp"

#include <s7.h>

#include <cmath>

namespace slopengine {

void runBotScriptTests() {
    actionRegistry().clear();
    actionRegistry().registerCoreActions();
    actionRegistry().registerPackageAction("attack", "Attack", 0);
    flecs::world world;
    world.component<LocalPlayer>();
    InputState input{};
    input.resize(actionRegistry().size());
    world.set<InputState>(std::move(input));
    world.set<InputCommand>({});
    const flecs::entity player = world.entity("Player").add<LocalPlayer>().set<FirstPersonController>({});
    s7_scheme* sc = s7_init();
    {
        BotScript bot(world, sc, 7, []() { return std::vector<BotKnownEntity>{{"crate-1", false}, {"player:player-2", true}}; });
        CHECK(bot.load(
            "(define bot-only 1)"
            "(define seen '())"
            "(define (bot-tick dt)"
            "  (set! seen (list (bot-seed) (bot-known) (and (member \"attack\" (bot-actions)) #t) dt))"
            "  (when (equal? seen '(7 ((\"crate-1\" . thing) (\"player:player-2\" . player)) #t 0.25))"
            "    (bot-press! \"Interact\"))"
            "  (bot-move! 2.0 -0.5)"
            "  (bot-look! 7.0 3.0)"
            "  (bot-hold! \"attack\" #t)"
            "  (bot-press! \"Jump\")"
            "  (bot-hold! \"no-such-action\" #t))",
            "test"));
        bot.tick(0.25f);
        const InputCommand& command = world.get<InputCommand>();
        CHECK(command.moveForward == 1.0f && command.moveStrafe == -0.5f);
        const InputState& state = world.get<InputState>();
        CHECK(state.down(Action::MoveForward) && state.down(Action::MoveLeft) && !state.down(Action::MoveRight));
        CHECK(state.down(actionRegistry().indexOf("attack")));
        CHECK(state.actionPressedPending[static_cast<std::size_t>(Action::Jump)] == 1);
        const FirstPersonController& controller = player.get<FirstPersonController>();
        CHECK(std::fabs(controller.yaw - std::remainder(7.0f, 6.2831853f)) < 1.0e-5f && controller.pitch == 1.55f);
        CHECK(state.actionPressedPending[static_cast<std::size_t>(Action::Interact)] == 1);
        CHECK(s7_name_to_value(sc, "bot-only") == s7_undefined(sc));
        CHECK(scriptScopeAllows(ScriptScope::Bot, ScriptCap::BotInput));
        CHECK_FALSE(scriptScopeAllows(ScriptScope::Bot, ScriptCap::WorldMutate));
        CHECK_FALSE(scriptScopeAllows(ScriptScope::World, ScriptCap::BotInput));
        CHECK_FALSE(scriptScopeAllows(ScriptScope::Present, ScriptCap::BotInput));
        world.get_mut<InputCommand>() = {};
        {
            ScriptScopeGuard present(ScriptScope::Present);
            s7_eval_c_string(sc, "(catch #t (lambda () (bot-move! 1.0 1.0)) (lambda args #f))");
        }
        CHECK(world.get<InputCommand>().moveForward == 0.0f);
        BotScript empty(world, sc, 1, nullptr);
        CHECK_FALSE(empty.load("(define (not-a-tick dt) dt)", "empty"));
    }
    s7_quit(sc);
    actionRegistry().clear();
}

}
