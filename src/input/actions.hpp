#pragma once

namespace slopengine {

enum class Action {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Jump,
    Pause,
    Interact,
    Console,
    Count
};

inline constexpr int actionCount = static_cast<int>(Action::Count);

}