#pragma once

#include "input/actions.hpp"
#include "input/input_command.hpp"
#include "input/input_state.hpp"

#include <flecs.h>

#include <cstdint>
#include <vector>

namespace slopengine {

struct LocalPlayer {};

struct PlayerCommand {
    std::uint64_t tick = 0;
    float moveForward = 0.0f;
    float moveStrafe = 0.0f;
    float moveUp = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::vector<char> actionDown;
    std::vector<char> actionPressed;

    bool down(int index) const {
        return index >= 0 && index < static_cast<int>(actionDown.size()) &&
            actionDown[static_cast<std::size_t>(index)] != 0;
    }

    bool pressed(int index) const {
        return index >= 0 && index < static_cast<int>(actionPressed.size()) &&
            actionPressed[static_cast<std::size_t>(index)] != 0;
    }

    bool down(Action action) const {
        return down(static_cast<int>(action));
    }

    bool pressed(Action action) const {
        return pressed(static_cast<int>(action));
    }
};

inline void buildPlayerCommand(
    PlayerCommand& out,
    InputState& input,
    const InputCommand& device,
    float yaw,
    float pitch,
    bool gameplayAllowed,
    std::uint64_t tick) {
    out.tick = tick;
    out.yaw = yaw;
    out.pitch = pitch;
    if (gameplayAllowed) {
        out.moveForward = device.moveForward;
        out.moveStrafe = device.moveStrafe;
        out.moveUp = device.moveUp;
        out.actionDown = input.actionDown;
        out.actionPressed = input.actionPressedPending;
    } else {
        out.moveForward = 0.0f;
        out.moveStrafe = 0.0f;
        out.moveUp = 0.0f;
        out.actionDown.assign(input.actionDown.size(), 0);
        out.actionPressed.assign(input.actionDown.size(), 0);
    }
    input.clearPendingPresses();
}

inline flecs::entity findLocalPlayer(const flecs::world& world) {
    flecs::entity found;
    world.each([&found](flecs::entity entity, LocalPlayer) {
        if (!found.is_valid()) {
            found = entity;
        }
    });
    return found;
}

inline const PlayerCommand* localPlayerCommand(const flecs::world& world) {
    const flecs::entity player = findLocalPlayer(world);
    return player.is_valid() ? player.try_get<PlayerCommand>() : nullptr;
}

}
