#pragma once

#include <flecs.h>

#include <vector>

namespace slopengine {

class PhysicsWorld;

struct CommandMove {
    float forward = 0.0f;
    float strafe = 0.0f;
    float up = 0.0f;
    float yaw = 0.0f;
    const std::vector<char>* down = nullptr;
    const std::vector<char>* pressed = nullptr;
};

bool stepCommandMove(PhysicsWorld& physics, flecs::entity body, const CommandMove& move);

}
