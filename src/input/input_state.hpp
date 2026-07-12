#pragma once

#include "input/actions.hpp"

#include <raylib.h>

namespace slopengine {

struct InputState {
    bool actionPressed[actionCount]{};
    bool actionDown[actionCount]{};
    Vector2 mouseDelta{};

    bool pressed(Action action) const {
        return actionPressed[static_cast<int>(action)];
    }

    bool down(Action action) const {
        return actionDown[static_cast<int>(action)];
    }
};

}