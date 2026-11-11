#pragma once

#include "physics/components.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

class PhysicsWorld;

inline constexpr std::size_t kMaxMotorRules = 32;

enum class MotorCondKind {
    Grounded,
    Airborne,
    Forward,
    Active,
    Rule,
    Not,
    And,
    Or,
};

struct MotorCond {
    MotorCondKind kind = MotorCondKind::Grounded;
    int rule = -1;
    std::vector<MotorCond> children;
};

enum class MotorTrigger {
    Hold,
    Press,
};

struct MotorRule {
    std::string name;
    std::string action;
    int actionIndex = -1;
    MotorTrigger trigger = MotorTrigger::Hold;
    std::vector<MotorCond> when;
    float speed = 1.0f;
    bool launch = false;
    float launchSpeed = 0.0f;
    bool shape = false;
    float shapeRadius = 0.0f;
    float shapeHeight = 0.0f;
    float shapeEye = 0.0f;
};

struct MotorRuleState {
    std::uint32_t enabled = 0xffffffffu;
    std::uint32_t active = 0;
    int shapedRule = -1;
    float standRadius = 0.0f;
    float standHeight = 0.0f;
    float standEye = 0.0f;
};

struct MotorInput {
    float forward = 0.0f;
    const std::vector<char>* down = nullptr;
    const std::vector<char>* pressed = nullptr;
};

std::vector<MotorRule>& motorRules();
int findMotorRule(std::string_view name);
std::uint64_t motorRulesHash();
void stepMotorRules(PhysicsWorld& physics, std::uint64_t id, CharacterMotor& motor, MotorRuleState& state,
                    const MotorInput& input, bool allowMove);

}
