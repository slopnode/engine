#include "script/motor_script.hpp"

#include "assets/asset_store.hpp"
#include "core/log.hpp"
#include "input/action_registry.hpp"
#include "script/player_target.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include <s7.h>

#include <cmath>
#include <utility>

namespace slopengine {

namespace {

flecs::world* g_motorWorld = nullptr;

struct PendingRuleRef {
    MotorCond* cond = nullptr;
    std::string name;
};

bool parseCond(s7_scheme* sc, s7_pointer form, MotorCond& out, std::vector<PendingRuleRef>& refs, std::string& error) {
    if (s7_is_symbol(form)) {
        const std::string name = s7_symbol_name(form);
        if (name == "grounded") {
            out.kind = MotorCondKind::Grounded;
        } else if (name == "airborne") {
            out.kind = MotorCondKind::Airborne;
        } else if (name == "forward") {
            out.kind = MotorCondKind::Forward;
        } else if (name == "active") {
            out.kind = MotorCondKind::Active;
        } else {
            error = "unknown condition " + name;
            return false;
        }
        return true;
    }
    if (!s7_is_pair(form) || !s7_is_symbol(s7_car(form))) {
        error = "a condition is a symbol or a list starting with not, and, or or rule";
        return false;
    }
    const std::string head = s7_symbol_name(s7_car(form));
    s7_pointer rest = s7_cdr(form);
    if (head == "rule") {
        if (!s7_is_pair(rest) || !s7_is_symbol(s7_car(rest)) || !s7_is_null(sc, s7_cdr(rest))) {
            error = "(rule NAME) takes one rule name";
            return false;
        }
        out.kind = MotorCondKind::Rule;
        refs.push_back({&out, s7_symbol_name(s7_car(rest))});
        return true;
    }
    if (head == "not") {
        out.kind = MotorCondKind::Not;
    } else if (head == "and") {
        out.kind = MotorCondKind::And;
    } else if (head == "or") {
        out.kind = MotorCondKind::Or;
    } else {
        error = "unknown condition " + head;
        return false;
    }
    for (; s7_is_pair(rest); rest = s7_cdr(rest)) {
        out.children.emplace_back();
    }
    if (out.kind == MotorCondKind::Not && out.children.size() != 1) {
        error = "(not ...) takes one condition";
        return false;
    }
    std::size_t index = 0;
    for (rest = s7_cdr(form); s7_is_pair(rest); rest = s7_cdr(rest), ++index) {
        if (!parseCond(sc, s7_car(rest), out.children[index], refs, error)) {
            return false;
        }
    }
    return true;
}

bool finiteReal(s7_pointer value, float lo, float hi, float& out) {
    if (!s7_is_real(value)) {
        return false;
    }
    const double real = s7_real(value);
    if (!std::isfinite(real) || real <= lo || real > hi) {
        return false;
    }
    out = static_cast<float>(real);
    return true;
}

bool parseRule(s7_scheme* sc, s7_pointer form, MotorRule& rule, std::vector<PendingRuleRef>& refs, std::string& error) {
    if (!s7_is_pair(form) || !s7_is_symbol(s7_car(form))) {
        error = "a rule is (name property ...)";
        return false;
    }
    rule.name = s7_symbol_name(s7_car(form));
    bool haveTrigger = false;
    for (s7_pointer rest = s7_cdr(form); s7_is_pair(rest); rest = s7_cdr(rest)) {
        const s7_pointer property = s7_car(rest);
        if (!s7_is_pair(property) || !s7_is_symbol(s7_car(property))) {
            error = rule.name + ": a property is a list such as (action \"id\")";
            return false;
        }
        const std::string key = s7_symbol_name(s7_car(property));
        const s7_pointer args = s7_cdr(property);
        if (key == "action") {
            if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
                error = rule.name + ": (action \"id\") takes an action id string";
                return false;
            }
            rule.action = s7_string(s7_car(args));
        } else if (key == "hold" || key == "press") {
            if (haveTrigger) {
                error = rule.name + ": give one of (hold) or (press)";
                return false;
            }
            haveTrigger = true;
            rule.trigger = key == "hold" ? MotorTrigger::Hold : MotorTrigger::Press;
        } else if (key == "when") {
            if (!rule.when.empty()) {
                error = rule.name + ": give one (when ...)";
                return false;
            }
            for (s7_pointer cond = args; s7_is_pair(cond); cond = s7_cdr(cond)) {
                rule.when.emplace_back();
            }
            std::size_t index = 0;
            for (s7_pointer cond = args; s7_is_pair(cond); cond = s7_cdr(cond), ++index) {
                if (!parseCond(sc, s7_car(cond), rule.when[index], refs, error)) {
                    error = rule.name + ": " + error;
                    return false;
                }
            }
        } else if (key == "speed") {
            if (!s7_is_pair(args) || !finiteReal(s7_car(args), 0.0f, 10.0f, rule.speed)) {
                error = rule.name + ": (speed scale) takes a number above 0 and at most 10";
                return false;
            }
        } else if (key == "launch") {
            if (!s7_is_pair(args) || !finiteReal(s7_car(args), 0.0f, 100.0f, rule.launchSpeed)) {
                error = rule.name + ": (launch speed) takes a number above 0 and at most 100";
                return false;
            }
            rule.launch = true;
        } else if (key == "shape") {
            if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args)) ||
                !s7_is_null(sc, s7_cdddr(args)) || !finiteReal(s7_car(args), 0.0f, 5.0f, rule.shapeRadius) ||
                !finiteReal(s7_cadr(args), 0.0f, 10.0f, rule.shapeHeight) ||
                !finiteReal(s7_caddr(args), 0.0f, 10.0f, rule.shapeEye)) {
                error = rule.name + ": (shape radius height eye) takes three numbers above 0";
                return false;
            }
            rule.shape = true;
        } else {
            error = rule.name + ": unknown property " + key;
            return false;
        }
    }
    if (rule.action.empty()) {
        error = rule.name + ": needs (action \"id\")";
        return false;
    }
    rule.actionIndex = actionRegistry().indexOf(rule.action);
    if (rule.actionIndex < 0) {
        error = rule.name + ": no action declared as " + rule.action;
        return false;
    }
    return true;
}

flecs::entity motorTarget(s7_scheme* sc, ArgReader& in) {
    if (g_motorWorld == nullptr) {
        return {};
    }
    if (in.more()) {
        const char* id = in.string("player id string");
        return in.ok() && id != nullptr ? scriptPlayer(*g_motorWorld, s7_list(sc, 1, s7_make_string(sc, id))) : flecs::entity{};
    }
    return scriptPlayer(*g_motorWorld);
}

s7_pointer g_motor_rule_enable(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "motor-rule-enable!", args);
    const char* name = in.name("rule name");
    const bool on = in.truthy("enabled");
    if (!in.ok()) {
        return in.error();
    }
    const int rule = findMotorRule(name);
    const flecs::entity player = motorTarget(sc, in);
    if (!in.ok()) {
        return in.error();
    }
    if (rule < 0 || !player.is_valid()) {
        return s7_f(sc);
    }
    MotorRuleState state = player.has<MotorRuleState>() ? player.get<MotorRuleState>() : MotorRuleState{};
    const std::uint32_t bit = 1u << rule;
    state.enabled = on ? (state.enabled | bit) : (state.enabled & ~bit);
    if (!on) {
        state.active &= ~bit;
    }
    player.set<MotorRuleState>(state);
    return s7_t(sc);
}

s7_pointer queryRule(s7_scheme* sc, s7_pointer args, const char* proc, bool active) {
    ArgReader in(sc, proc, args);
    const char* name = in.name("rule name");
    if (!in.ok()) {
        return in.error();
    }
    const int rule = findMotorRule(name);
    const flecs::entity player = motorTarget(sc, in);
    if (!in.ok()) {
        return in.error();
    }
    if (rule < 0 || !player.is_valid()) {
        return s7_f(sc);
    }
    const MotorRuleState state = player.has<MotorRuleState>() ? player.get<MotorRuleState>() : MotorRuleState{};
    return s7_make_boolean(sc, ((active ? state.active : state.enabled) & (1u << rule)) != 0);
}

s7_pointer g_motor_rule_active(s7_scheme* sc, s7_pointer args) {
    return queryRule(sc, args, "motor-rule-active?", true);
}

s7_pointer g_motor_rule_enabled(s7_scheme* sc, s7_pointer args) {
    return queryRule(sc, args, "motor-rule-enabled?", false);
}

constexpr SchemeBinding kMotorBindings[] = {
    {"motor-rule-enable!", g_motor_rule_enable, 2, 1, false, "(motor-rule-enable! rule on [id])", ScriptCap::WorldMutate},
    {"motor-rule-active?", g_motor_rule_active, 1, 1, false, "(motor-rule-active? rule [id])", ScriptCap::ReadWorld},
    {"motor-rule-enabled?", g_motor_rule_enabled, 1, 1, false, "(motor-rule-enabled? rule [id])", ScriptCap::ReadWorld},
};

}

bool parseMotorRules(s7_scheme* sc, s7_pointer list, std::vector<MotorRule>& out, std::vector<std::string>& messages) {
    out.clear();
    if (s7_is_null(sc, list)) {
        return true;
    }
    if (!s7_is_pair(list)) {
        messages.push_back("*motor-rules* must be a list of rules");
        return false;
    }
    std::vector<PendingRuleRef> refs;
    std::size_t count = 0;
    for (s7_pointer rest = list; s7_is_pair(rest); rest = s7_cdr(rest)) {
        ++count;
    }
    if (count > kMaxMotorRules) {
        messages.push_back("*motor-rules* holds at most " + std::to_string(kMaxMotorRules) + " rules");
        return false;
    }
    out.resize(count);
    std::size_t index = 0;
    for (s7_pointer rest = list; s7_is_pair(rest); rest = s7_cdr(rest), ++index) {
        std::string error;
        if (!parseRule(sc, s7_car(rest), out[index], refs, error)) {
            messages.push_back(error);
            out.clear();
            return false;
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (out[earlier].name == out[index].name) {
                messages.push_back("two rules are named " + out[index].name);
                out.clear();
                return false;
            }
        }
    }
    for (PendingRuleRef& ref : refs) {
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (out[i].name == ref.name) {
                ref.cond->rule = static_cast<int>(i);
            }
        }
        if (ref.cond->rule < 0) {
            messages.push_back("(rule " + ref.name + ") names no rule");
            out.clear();
            return false;
        }
    }
    return true;
}

void loadPackageMotorRules(s7_scheme* sc, AssetStore& assets) {
    motorRules().clear();
    if (sc == nullptr) {
        return;
    }
    const std::string baseId{assets.basePackageId()};
    s7_define(sc, s7_rootlet(sc), s7_make_symbol(sc, "*motor-rules*"), s7_nil(sc));
    if (baseId.empty() || !assets.loadDataFromPackage(sc, baseId, "motor")) {
        return;
    }
    std::vector<std::string> messages;
    std::vector<MotorRule> rules;
    if (parseMotorRules(sc, s7_name_to_value(sc, "*motor-rules*"), rules, messages)) {
        motorRules() = std::move(rules);
        Log::debug(LogCategory::Script, "MOTOR: %zu rules", motorRules().size());
    }
    for (const std::string& message : messages) {
        Log::warning(LogCategory::Script, "MOTOR: %s", message.c_str());
    }
}

void bindMotorRuleApi(flecs::world& world, s7_scheme* sc) {
    g_motorWorld = &world;
    world.component<MotorRuleState>();
    if (sc != nullptr) {
        registerBindings(sc, kMotorBindings);
    }
}

}
