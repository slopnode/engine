#pragma once

namespace slopengine {

/** Named gameplay and UI actions bound to keys in user settings. */
enum class Action {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Jump,        /**< Bound by default; not used by the character motor yet. */
    Pause,
    Interact,
    Console,
    MainMenu,
    Flashlight,
    Count
};

/** Number of bindable actions (excludes Count). */
inline constexpr int actionCount = static_cast<int>(Action::Count);

}
