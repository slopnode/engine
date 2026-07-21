#include "script/input_script.hpp"

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
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, proc, 1, s7_car(args), "string");
    }
    if (!g_inputWorld->has<InputContextStack>() || !g_inputWorld->has<InputState>()) {
        return s7_f(sc);
    }
    if (!g_inputWorld->get<InputContextStack>().allowsGameplay()) {
        return s7_f(sc);
    }
    const int index = actionRegistry().indexOf(s7_string(s7_car(args)));
    if (index < 0) {
        return s7_f(sc);
    }
    const InputState& input = g_inputWorld->get<InputState>();
    const bool active = (query == ActionQuery::Pressed) ? input.pressed(index) : input.down(index);
    return active ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_action_down(s7_scheme* sc, s7_pointer args) {
    return queryAction(sc, args, "action-down?", ActionQuery::Down);
}

s7_pointer g_action_pressed(s7_scheme* sc, s7_pointer args) {
    return queryAction(sc, args, "action-pressed?", ActionQuery::Pressed);
}

} // namespace

void bindInputApi(flecs::world& world, s7_scheme* scheme) {
    g_inputWorld = &world;
    if (scheme == nullptr) {
        return;
    }

    s7_define_function(scheme, "action-down?", g_action_down, 1, 0, false, "(action-down? id)");
    s7_define_function(scheme, "action-pressed?", g_action_pressed, 1, 0, false,
                       "(action-pressed? id)");
}

}
