#include "test_assert.hpp"

#include "input/action_registry.hpp"
#include "map/brush.hpp"
#include "physics/components.hpp"
#include "physics/motor_rules.hpp"
#include "physics/physics_world.hpp"
#include "script/motor_script.hpp"

#include <s7.h>

#include <string>
#include <vector>

namespace slopengine {

namespace {

bool parses(s7_scheme* sc, const char* text, std::vector<MotorRule>& rules) {
    std::vector<std::string> messages;
    return parseMotorRules(sc, s7_eval_c_string(sc, text), rules, messages) && messages.empty();
}

}

void runMotorRulesTests() {
    actionRegistry().clear();
    actionRegistry().registerCoreActions();
    actionRegistry().registerPackageAction("sprint", "Sprint", 0);
    s7_scheme* sc = s7_init();
    std::vector<MotorRule> rules;
    CHECK(parses(sc,
        "'((sprint (action \"sprint\") (hold) (when forward (or active grounded)) (speed 1.6))"
        "  (jump (action \"Jump\") (press) (when grounded (not (rule sprint))) (launch 7.7)))",
        rules));
    CHECK(rules.size() == 2 && rules[0].speed == 1.6f && rules[1].launch && rules[1].launchSpeed == 7.7f);
    CHECK(rules.size() == 2 && rules[1].when.size() == 2 && rules[1].when[1].children[0].rule == 0);
    CHECK_FALSE(parses(sc, "'((a (action \"nope\") (hold)))", rules));
    CHECK_FALSE(parses(sc, "'((a (hold)))", rules));
    CHECK_FALSE(parses(sc, "'((a (action \"Jump\") (hold) (when flying)))", rules));
    CHECK_FALSE(parses(sc, "'((a (action \"Jump\") (press) (when (rule b))))", rules));
    CHECK_FALSE(parses(sc, "'((a (action \"Jump\") (press) (speed 0)))", rules));
    CHECK_FALSE(parses(sc, "'((a (action \"Jump\") (press) (launch 1e9)))", rules));
    CHECK_FALSE(parses(sc, "'((a (action \"Jump\") (press)) (a (action \"Jump\") (hold)))", rules));
    CHECK_FALSE(parses(sc, "'((a (action \"Jump\") (press) (hold)))", rules));
    CHECK_FALSE(parses(sc, "'((a (action \"Jump\") (press) (when grounded) (when airborne)))", rules));
    CHECK_FALSE(parses(sc, "'((a (action \"Jump\") (hold) (shape 0.3 0.5)))", rules));
    CHECK_FALSE(parses(sc, "'((a (action \"Jump\") (hold) (shape 0.3 0.5 1.0 2.0)))", rules));
    CHECK_FALSE(parses(sc, "'((a (action \"Jump\") (hold) (shape 0.3 -0.5 1.0)))", rules));
    CHECK(parses(sc, "'()", rules) && rules.empty());

    CHECK(parses(sc,
        "'((sprint (action \"sprint\") (hold) (when forward (or active grounded)) (speed 1.6))"
        "  (jump (action \"Jump\") (press) (when grounded) (launch 7.7)))",
        rules));
    motorRules() = rules;
    const std::uint64_t hash = motorRulesHash();
    motorRules()[0].speed = 1.5f;
    CHECK(motorRulesHash() != hash);
    motorRules() = rules;

    PhysicsWorld physics;
    physics.addStaticBrushes({makeBrushBox("floor", {-20.0f, -0.25f, -20.0f}, {20.0f, 0.0f, 20.0f}, "mat/a", {})});
    CharacterMotor motor{};
    const std::uint64_t id = 7;
    physics.createCharacter(id, 0.0f, 0.05f, 0.0f, motor);
    CharacterStep settle{};
    settle.id = id;
    settle.motor = &motor;
    for (int i = 0; i < 10; ++i) {
        physics.stepCharacterAlone(settle);
    }
    CHECK(physics.characterSupported(id));
    std::vector<char> down(static_cast<std::size_t>(actionRegistry().size()), 0);
    std::vector<char> pressed(down.size(), 0);
    const auto sprintSlot = static_cast<std::size_t>(actionRegistry().indexOf("sprint"));
    const auto jumpSlot = static_cast<std::size_t>(Action::Jump);
    MotorRuleState state{};
    down[sprintSlot] = 1;
    stepMotorRules(physics, id, motor, state, MotorInput{0.0f, &down, &pressed}, true);
    CHECK(state.active == 0 && motor.speedScale == 1.0f);
    stepMotorRules(physics, id, motor, state, MotorInput{1.0f, &down, &pressed}, true);
    CHECK(state.active == 1u && motor.speedScale == 1.6f);
    stepMotorRules(physics, id, motor, state, MotorInput{1.0f, &down, &pressed}, false);
    CHECK(state.active == 0 && motor.speedScale == 1.0f);
    stepMotorRules(physics, id, motor, state, MotorInput{1.0f, &down, &pressed}, true);
    pressed[jumpSlot] = 1;
    stepMotorRules(physics, id, motor, state, MotorInput{1.0f, &down, &pressed}, true);
    CHECK(state.active == 3u && physics.characterVelocity(id).GetY() > 7.0f);
    pressed[jumpSlot] = 0;
    physics.stepCharacterAlone(settle);
    CHECK_FALSE(physics.characterSupported(id));
    stepMotorRules(physics, id, motor, state, MotorInput{1.0f, &down, &pressed}, true);
    CHECK(state.active == 1u && motor.speedScale == 1.6f);
    MotorRuleState fresh{};
    fresh.active = 0;
    stepMotorRules(physics, id, motor, fresh, MotorInput{1.0f, &down, &pressed}, true);
    CHECK(fresh.active == 0 && motor.speedScale == 1.0f);
    state.enabled = ~1u;
    stepMotorRules(physics, id, motor, state, MotorInput{1.0f, &down, &pressed}, true);
    CHECK(state.active == 0 && motor.speedScale == 1.0f);

    CHECK(parses(sc, "'((crouch (action \"Descend\") (hold) (when (or active grounded)) (shape 0.3 0.4 1.0)))", rules));
    motorRules() = rules;
    PhysicsWorld low;
    low.addStaticBrushes({makeBrushBox("floor", {-20.0f, -0.25f, -20.0f}, {20.0f, 0.0f, 20.0f}, "mat/a", {}),
                          makeBrushBox("roof", {3.0f, 1.3f, -2.0f}, {7.0f, 1.6f, 2.0f}, "mat/a", {})});
    CharacterMotor body{};
    body.radius = 0.3f;
    body.height = 1.2f;
    body.eyeHeight = 1.65f;
    const std::uint64_t who = 9;
    low.createCharacter(who, 0.0f, 0.05f, 0.0f, body);
    CharacterStep stand{};
    stand.id = who;
    stand.motor = &body;
    for (int i = 0; i < 10; ++i) {
        low.stepCharacterAlone(stand);
    }
    std::vector<char> held(down.size(), 0);
    std::vector<char> none(down.size(), 0);
    held[static_cast<std::size_t>(Action::Descend)] = 1;
    MotorRuleState crouch{};
    stepMotorRules(low, who, body, crouch, MotorInput{0.0f, &held, &none}, true);
    CHECK(crouch.shapedRule == 0 && crouch.active == 1u);
    CHECK(body.height == 0.4f && body.eyeHeight == 1.0f && crouch.standHeight == 1.2f && crouch.standEye == 1.65f);
    stepMotorRules(low, who, body, crouch, MotorInput{0.0f, &none, &none}, false);
    CHECK(crouch.shapedRule == 0 && body.height == 0.4f);
    stepMotorRules(low, who, body, crouch, MotorInput{0.0f, &none, &none}, true);
    CHECK(crouch.shapedRule == -1 && crouch.active == 0 && body.height == 1.2f && body.eyeHeight == 1.65f);
    stepMotorRules(low, who, body, crouch, MotorInput{0.0f, &held, &none}, true);
    CHECK(crouch.shapedRule == 0 && body.height == 0.4f);
    low.setCharacterPosition(who, 5.0f, 0.0f, 0.0f);
    for (int i = 0; i < 5; ++i) {
        low.stepCharacterAlone(stand);
    }
    stepMotorRules(low, who, body, crouch, MotorInput{0.0f, &none, &none}, true);
    CHECK(crouch.shapedRule == 0 && crouch.active == 1u && body.height == 0.4f);
    low.setCharacterPosition(who, -5.0f, 0.0f, 0.0f);
    for (int i = 0; i < 5; ++i) {
        low.stepCharacterAlone(stand);
    }
    stepMotorRules(low, who, body, crouch, MotorInput{0.0f, &none, &none}, true);
    CHECK(crouch.shapedRule == -1 && body.height == 1.2f);

    motorRules().clear();
    s7_quit(sc);
    actionRegistry().clear();
}

}
