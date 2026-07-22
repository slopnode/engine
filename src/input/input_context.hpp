#pragma once

#include <vector>

namespace slopengine {

/** Modal input layer. Only the top of the stack receives gameplay-style input. */
enum class InputContext {
    Gameplay,   /**< Move, look, interact, flashlight. */
    PauseMenu,
    InteractUI, /**< Inspect / use UI blocking the world. */
    Console,
    MainMenu,
};

/** Stack of input contexts. Gameplay is allowed only when it is on top.
 *  @ingroup input_components
 */
struct InputContextStack {
    std::vector<InputContext> stack{InputContext::Gameplay};

    /** Pushes @p context onto the stack. */
    void push(InputContext context) {
        stack.push_back(context);
    }

    /** Removes the topmost matching @p context. */
    void pop(InputContext context) {
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            if (*it == context) {
                stack.erase(std::next(it).base());
                return;
            }
        }
    }

    /** True when @p context appears anywhere on the stack. */
    bool contains(InputContext context) const {
        for (InputContext entry : stack) {
            if (entry == context) {
                return true;
            }
        }
        return false;
    }

    /** Returns the active (top) context. */
    InputContext top() const {
        return stack.back();
    }

    /** True when the top context is Gameplay. */
    bool allowsGameplay() const {
        return top() == InputContext::Gameplay;
    }

    /** True when world move/look should be ignored. */
    bool blocksWorldInput() const {
        return !allowsGameplay();
    }
};

}
