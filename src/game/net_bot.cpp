#include "game/net_bot.hpp"

#include "camera/components.hpp"
#include "input/action_registry.hpp"
#include "input/input_command.hpp"
#include "input/input_state.hpp"
#include "input/player_command.hpp"

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

constexpr const char* kSlots[] = {"slot-1", "slot-2", "slot-3", "slot-4", "slot-5", "slot-6", "slot-7"};

int actionSlot(std::string_view id) {
    return actionRegistry().indexOf(id);
}

}

bool parseNetBotKind(std::string_view text, NetBotKind& out) {
    if (text == "still") {
        out = NetBotKind::Still;
    } else if (text == "walk") {
        out = NetBotKind::Walk;
    } else if (text == "fight") {
        out = NetBotKind::Fight;
    } else {
        return false;
    }
    return true;
}

NetBot::NetBot(NetBotKind kind, std::uint32_t seed) : kind_(kind), random_(seed) {
    pickCourse();
    nextBurst_ = 1.0f;
    nextJump_ = 2.0f;
    nextSwitch_ = 3.0f;
}

void NetBot::pickCourse() {
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> span(1.0f, 4.0f);
    courseLeft_ = span(random_);
    forward_ = std::fabs(unit(random_)) > 0.2f ? (unit(random_) > -0.6f ? 1.0f : -1.0f) : 0.0f;
    strafe_ = std::fabs(unit(random_)) > 0.5f ? (unit(random_) > 0.0f ? 1.0f : -1.0f) : 0.0f;
    turnRate_ = unit(random_) * 2.5f;
    sprint_ = unit(random_) > 0.4f;
}

void NetBot::press(flecs::world& world, std::string_view action) {
    const int slot = actionSlot(action);
    InputState& input = world.get_mut<InputState>();
    if (slot >= 0 && static_cast<std::size_t>(slot) < input.actionPressedPending.size()) {
        input.actionPressedPending[static_cast<std::size_t>(slot)] = 1;
    }
}

void NetBot::hold(flecs::world& world, std::string_view action, bool down) {
    const int slot = actionSlot(action);
    InputState& input = world.get_mut<InputState>();
    if (slot >= 0 && static_cast<std::size_t>(slot) < input.actionDown.size()) {
        input.actionDown[static_cast<std::size_t>(slot)] = down ? 1 : 0;
    }
}

void NetBot::drive(flecs::world& world, float frameDt) {
    if (!world.has<InputState>() || !world.has<InputCommand>() || kind_ == NetBotKind::Still) {
        return;
    }
    const float dt = std::max(0.0f, frameDt);
    time_ += dt;
    courseLeft_ -= dt;
    if (courseLeft_ <= 0.0f) {
        pickCourse();
    }
    InputCommand& command = world.get_mut<InputCommand>();
    command.moveForward = forward_;
    command.moveStrafe = strafe_;
    hold(world, actionId(Action::MoveForward), forward_ > 0.0f);
    hold(world, actionId(Action::MoveBackward), forward_ < 0.0f);
    hold(world, actionId(Action::MoveRight), strafe_ > 0.0f);
    hold(world, actionId(Action::MoveLeft), strafe_ < 0.0f);
    hold(world, "sprint", sprint_);
    const flecs::entity player = findLocalPlayer(world);
    if (player.is_valid()) {
        if (FirstPersonController* controller = player.try_get_mut<FirstPersonController>()) {
            controller->yaw = std::remainder(controller->yaw + turnRate_ * dt, 6.2831853f);
            controller->pitch = 0.25f * std::sin(time_ * 0.7f);
        }
    }
    if (kind_ != NetBotKind::Fight) {
        return;
    }
    std::uniform_real_distribution<float> span(0.5f, 3.0f);
    if (time_ >= nextJump_) {
        press(world, actionId(Action::Jump));
        nextJump_ = time_ + span(random_) * 2.0f;
    }
    if (time_ >= nextSwitch_) {
        press(world, kSlots[std::uniform_int_distribution<int>(0, 6)(random_)]);
        nextSwitch_ = time_ + span(random_) * 3.0f;
    }
    if (burstLeft_ > 0.0f) {
        burstLeft_ -= dt;
        hold(world, "attack", burstLeft_ > 0.0f);
    } else if (time_ >= nextBurst_) {
        burstLeft_ = span(random_) * 0.5f;
        press(world, "attack");
        hold(world, "attack", true);
        nextBurst_ = time_ + span(random_);
    }
}

}
