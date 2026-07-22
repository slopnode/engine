#pragma once

#include "input/actions.hpp"

#include <raylib.h>

#include <vector>

namespace slopengine {

/** Per-frame action edges and mouse delta sampled from the current bindings.
 *  @ingroup input_components
 */
struct InputState {
    std::vector<char> actionPressed;
    std::vector<char> actionDown;
    Vector2 mouseDelta{};

    void resize(int count) {
        actionPressed.assign(static_cast<std::size_t>(count), 0);
        actionDown.assign(static_cast<std::size_t>(count), 0);
    }

    bool pressed(int index) const {
        if (index < 0 || index >= static_cast<int>(actionPressed.size())) {
            return false;
        }
        return actionPressed[static_cast<std::size_t>(index)] != 0;
    }

    bool down(int index) const {
        if (index < 0 || index >= static_cast<int>(actionDown.size())) {
            return false;
        }
        return actionDown[static_cast<std::size_t>(index)] != 0;
    }

    bool pressed(Action action) const {
        return pressed(static_cast<int>(action));
    }

    bool down(Action action) const {
        return down(static_cast<int>(action));
    }
};

}
