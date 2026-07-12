#pragma once

#include <vector>

namespace slopengine {

enum class InputContext {
    Gameplay,
    PauseMenu,
    InteractUI,
    Console,
};

struct InputContextStack {
    std::vector<InputContext> stack{InputContext::Gameplay};

    void push(InputContext context) {
        stack.push_back(context);
    }

    void pop(InputContext context) {
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            if (*it == context) {
                stack.erase(std::next(it).base());
                return;
            }
        }
    }

    bool contains(InputContext context) const {
        for (InputContext entry : stack) {
            if (entry == context) {
                return true;
            }
        }
        return false;
    }

    InputContext top() const {
        return stack.back();
    }

    bool allowsGameplay() const {
        return top() == InputContext::Gameplay;
    }

    bool blocksWorldInput() const {
        return !allowsGameplay();
    }
};

}