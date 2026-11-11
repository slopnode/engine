#include "net/command_move.hpp"

#include "camera/components.hpp"
#include "physics/character_wish.hpp"
#include "physics/components.hpp"
#include "physics/motor_rules.hpp"
#include "physics/physics_world.hpp"

#include <algorithm>
#include <cstdint>

namespace slopengine {

bool stepCommandMove(PhysicsWorld& physics, flecs::entity body, const CommandMove& move) {
    const std::uint64_t id = static_cast<std::uint64_t>(body.id());
    CharacterMotor* motor = body.try_get_mut<CharacterMotor>();
    if (motor == nullptr || !physics.hasCharacter(id)) {
        return false;
    }
    const FirstPersonController* controller = body.try_get<FirstPersonController>();
    const bool allowMove = controller == nullptr || controller->allowMove;
    const float forward = std::clamp(move.forward, -1.0f, 1.0f);
    const float strafe = std::clamp(move.strafe, -1.0f, 1.0f);
    const float up = std::clamp(move.up, -1.0f, 1.0f);
    MotorRuleState rules = body.has<MotorRuleState>() ? body.get<MotorRuleState>() : MotorRuleState{};
    const float eye = motor->eyeHeight;
    stepMotorRules(physics, id, *motor, rules, MotorInput{forward, move.down, move.pressed}, allowMove);
    body.set<MotorRuleState>(rules);
    motor = body.try_get_mut<CharacterMotor>();
    if (FirstPersonController* view = body.try_get_mut<FirstPersonController>(); view != nullptr && motor->eyeHeight != eye) {
        view->eyeHeight = motor->eyeHeight;
    }
    if (allowMove) {
        applyMoveWish(*motor, forward, strafe, up, move.yaw);
    } else {
        motor->wishX = 0.0f;
        motor->wishY = 0.0f;
        motor->wishZ = 0.0f;
    }
    CharacterStep step{};
    step.id = id;
    step.motor = motor;
    physics.stepCharacterAlone(step);
    return true;
}

}
