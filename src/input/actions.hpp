#pragma once

namespace slopengine {

/** Engine-owned bindable actions. Indices match ActionRegistry core slots. */
enum class Action {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Jump,
    Descend,
    Pause,
    Interact,
    Console,
    MainMenu,
    Screenshot,
    Count
};

/** Number of core actions (excludes Count). */
inline constexpr int coreActionCount = static_cast<int>(Action::Count);

}
