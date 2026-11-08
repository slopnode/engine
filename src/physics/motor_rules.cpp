#include "physics/motor_rules.hpp"

#include "physics/physics_world.hpp"

#include <cstring>

namespace slopengine {

namespace {

struct CondContext {
    bool grounded = false;
    float forward = 0.0f;
    std::uint32_t previous = 0;
    std::size_t self = 0;
};

bool evaluate(const MotorCond& cond, const CondContext& context) {
    switch (cond.kind) {
    case MotorCondKind::Grounded:
        return context.grounded;
    case MotorCondKind::Airborne:
        return !context.grounded;
    case MotorCondKind::Forward:
        return context.forward > 0.0f;
    case MotorCondKind::Active:
        return (context.previous & (1u << context.self)) != 0;
    case MotorCondKind::Rule:
        return cond.rule >= 0 && (context.previous & (1u << cond.rule)) != 0;
    case MotorCondKind::Not:
        return !cond.children.empty() && !evaluate(cond.children.front(), context);
    case MotorCondKind::And:
        for (const MotorCond& child : cond.children) {
            if (!evaluate(child, context)) {
                return false;
            }
        }
        return true;
    case MotorCondKind::Or:
        for (const MotorCond& child : cond.children) {
            if (evaluate(child, context)) {
                return true;
            }
        }
        return false;
    }
    return false;
}

bool flagAt(const std::vector<char>* flags, int index) {
    return flags != nullptr && index >= 0 && static_cast<std::size_t>(index) < flags->size() &&
        (*flags)[static_cast<std::size_t>(index)] != 0;
}

void hashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

void hashCond(std::uint64_t& hash, const MotorCond& cond) {
    const int kind = static_cast<int>(cond.kind);
    hashBytes(hash, &kind, sizeof(kind));
    hashBytes(hash, &cond.rule, sizeof(cond.rule));
    const std::size_t children = cond.children.size();
    hashBytes(hash, &children, sizeof(children));
    for (const MotorCond& child : cond.children) {
        hashCond(hash, child);
    }
}

}

std::vector<MotorRule>& motorRules() {
    static std::vector<MotorRule> rules;
    return rules;
}

int findMotorRule(std::string_view name) {
    const std::vector<MotorRule>& rules = motorRules();
    for (std::size_t i = 0; i < rules.size(); ++i) {
        if (rules[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::uint64_t motorRulesHash() {
    std::uint64_t hash = 14695981039346656037ull;
    for (const MotorRule& rule : motorRules()) {
        hashBytes(hash, rule.name.data(), rule.name.size());
        hashBytes(hash, rule.action.data(), rule.action.size());
        const int trigger = static_cast<int>(rule.trigger);
        hashBytes(hash, &trigger, sizeof(trigger));
        hashBytes(hash, &rule.speed, sizeof(rule.speed));
        hashBytes(hash, &rule.launch, sizeof(rule.launch));
        hashBytes(hash, &rule.launchSpeed, sizeof(rule.launchSpeed));
        hashBytes(hash, &rule.shape, sizeof(rule.shape));
        hashBytes(hash, &rule.shapeRadius, sizeof(rule.shapeRadius));
        hashBytes(hash, &rule.shapeHeight, sizeof(rule.shapeHeight));
        hashBytes(hash, &rule.shapeEye, sizeof(rule.shapeEye));
        for (const MotorCond& cond : rule.when) {
            hashCond(hash, cond);
        }
    }
    return hash;
}

namespace {

bool reshape(PhysicsWorld& physics, std::uint64_t id, CharacterMotor& motor, float radius, float height, float eye) {
    CharacterMotor resized = motor;
    resized.radius = radius;
    resized.height = height;
    if (!physics.resizeCharacter(id, resized)) {
        return false;
    }
    motor.radius = radius;
    motor.height = height;
    motor.eyeHeight = eye;
    return true;
}

}

void stepMotorRules(PhysicsWorld& physics, std::uint64_t id, CharacterMotor& motor, MotorRuleState& state,
                    const MotorInput& input, bool allowMove) {
    const std::vector<MotorRule>& rules = motorRules();
    CondContext context;
    context.grounded = physics.characterSupported(id);
    context.forward = input.forward;
    context.previous = state.active;
    std::uint32_t wanted = 0;
    for (std::size_t i = 0; i < rules.size() && i < kMaxMotorRules; ++i) {
        const MotorRule& rule = rules[i];
        if (!allowMove || (state.enabled & (1u << i)) == 0) {
            continue;
        }
        const bool triggered = rule.trigger == MotorTrigger::Hold ? flagAt(input.down, rule.actionIndex)
                                                                  : flagAt(input.pressed, rule.actionIndex);
        if (!triggered) {
            continue;
        }
        context.self = i;
        bool pass = true;
        for (const MotorCond& cond : rule.when) {
            if (!evaluate(cond, context)) {
                pass = false;
                break;
            }
        }
        if (pass) {
            wanted |= 1u << i;
        }
    }
    if (state.shapedRule >= 0 && static_cast<std::size_t>(state.shapedRule) >= rules.size()) {
        state.shapedRule = -1;
    }
    if (state.shapedRule >= 0) {
        const std::uint32_t bit = 1u << state.shapedRule;
        if ((wanted & bit) == 0) {
            if (allowMove && reshape(physics, id, motor, state.standRadius, state.standHeight, state.standEye)) {
                state.shapedRule = -1;
            } else {
                wanted |= bit;
            }
        }
    }
    for (std::size_t i = 0; i < rules.size() && i < kMaxMotorRules; ++i) {
        const std::uint32_t bit = 1u << i;
        if ((wanted & bit) == 0 || !rules[i].shape || static_cast<int>(i) == state.shapedRule) {
            continue;
        }
        if (state.shapedRule >= 0) {
            wanted &= ~bit;
            continue;
        }
        const float radius = motor.radius;
        const float height = motor.height;
        const float eye = motor.eyeHeight;
        if (reshape(physics, id, motor, rules[i].shapeRadius, rules[i].shapeHeight, rules[i].shapeEye)) {
            state.shapedRule = static_cast<int>(i);
            state.standRadius = radius;
            state.standHeight = height;
            state.standEye = eye;
        } else {
            wanted &= ~bit;
        }
    }
    float scale = 1.0f;
    for (std::size_t i = 0; i < rules.size() && i < kMaxMotorRules; ++i) {
        if ((wanted & (1u << i)) == 0) {
            continue;
        }
        scale *= rules[i].speed;
        if (rules[i].launch && (state.active & (1u << i)) == 0) {
            physics.launchCharacter(id, rules[i].launchSpeed);
        }
    }
    state.active = wanted;
    motor.speedScale = scale;
}

}
