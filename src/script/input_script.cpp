#include "script/input_script.hpp"

#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include "input/action_registry.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"

#include <s7.h>

namespace slopengine {

namespace {

flecs::world* g_inputWorld = nullptr;

enum class ActionQuery {
    Down,
    Pressed,
};

s7_pointer queryAction(s7_scheme* sc, s7_pointer args, const char* proc, ActionQuery query) {
    if (g_inputWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, proc, args);
    const char* id = in.string("string");
    if (!g_inputWorld->has<InputContextStack>() || !g_inputWorld->has<InputState>()) {
        return s7_f(sc);
    }
    if (!g_inputWorld->get<InputContextStack>().allowsGameplay()) {
        return s7_f(sc);
    }
    const int index = actionRegistry().indexOf(id);
    if (index < 0) {
        return s7_f(sc);
    }
    const InputState& input = g_inputWorld->get<InputState>();
    const bool active = (query == ActionQuery::Pressed) ? input.pressed(index) : input.down(index);
    return s7_make_boolean(sc, active);
}

s7_pointer g_action_down(s7_scheme* sc, s7_pointer args) {
    return queryAction(sc, args, "action-down?", ActionQuery::Down);
}

s7_pointer g_action_pressed(s7_scheme* sc, s7_pointer args) {
    return queryAction(sc, args, "action-pressed?", ActionQuery::Pressed);
}

constexpr SchemeBinding kInputBindings[] = {
    {"action-down?", g_action_down, 1, 0, false, "(action-down? id)", ScriptCap::InputQuery},
    {"action-pressed?", g_action_pressed, 1, 0, false, "(action-pressed? id)", ScriptCap::InputQuery},
};

} // namespace

void bindInputApi(flecs::world& world, s7_scheme* scheme) {
    g_inputWorld = &world;
    if (scheme == nullptr) {
        return;
    }
    registerBindings(scheme, kInputBindings);
}

}
