#pragma once

#include <flecs.h>
#include <string>

namespace slopengine {

struct Interactable {
    std::string prompt = "Interact";
    std::string eventName;
    float maxDistance = 5.0f;
};

struct InteractionTarget {
    flecs::entity entity{};
    float distance = 0.0f;
    std::string prompt;
    std::string eventName;
};

}