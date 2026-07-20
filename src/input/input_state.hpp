#pragma once

#include "input/actions.hpp"

#include <raylib.h>

namespace slopengine {

/** Per-frame action edges and mouse delta sampled from the current bindings. */
struct InputState {
    bool actionPressed[actionCount]{}; /**< True on the frame the action goes down. */
    bool actionDown[actionCount]{};    /**< True while the action key is held. */
    Vector2 mouseDelta{};

    /** True if @p action was pressed this frame. */
    bool pressed(Action action) const {
        return actionPressed[static_cast<int>(action)];
    }

    /** True if @p action is currently held. */
    bool down(Action action) const {
        return actionDown[static_cast<int>(action)];
    }
};

}
