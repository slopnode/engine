#include "input/action_registry.hpp"

#include "input/bind_code.hpp"

#include <raylib.h>
#include <s7.h>

#include <cstring>
#include <string>

namespace slopengine {

namespace {

ActionRegistry g_actionRegistry;

struct CoreActionInfo {
    Action action;
    const char* id;
    const char* label;
    int defaultBind;
};

constexpr CoreActionInfo kCoreActions[] = {
    {Action::MoveForward, "MoveForward", "Move Forward", KEY_W},
    {Action::MoveBackward, "MoveBackward", "Move Backward", KEY_S},
    {Action::MoveLeft, "MoveLeft", "Move Left", KEY_A},
    {Action::MoveRight, "MoveRight", "Move Right", KEY_D},
    {Action::Jump, "Jump", "Jump", KEY_SPACE},
    {Action::Pause, "Pause", "Pause", KEY_NULL},
    {Action::Interact, "Interact", "Interact", KEY_E},
    {Action::Console, "Console", "Console", KEY_GRAVE},
    {Action::MainMenu, "MainMenu", "Main Menu", KEY_F1},
    {Action::Screenshot, "Screenshot", "Screenshot", KEY_F12},
};

bool readAssocString(s7_scheme* scheme, s7_pointer alist, const char* key, std::string& out) {
    if (!s7_is_pair(alist) && !s7_is_null(scheme, alist)) {
        return false;
    }
    const s7_pointer pair = s7_assoc(scheme, s7_make_symbol(scheme, key), alist);
    if (!s7_is_pair(pair)) {
        return false;
    }
    const s7_pointer value = s7_cdr(pair);
    if (!s7_is_string(value)) {
        return false;
    }
    out = s7_string(value);
    return true;
}

bool readStringValue(s7_scheme* scheme, s7_pointer value, std::string& out) {
    (void)scheme;
    if (s7_is_string(value)) {
        out = s7_string(value);
        return true;
    }
    return false;
}

}

ActionRegistry& actionRegistry() {
    return g_actionRegistry;
}

void ActionRegistry::clear() {
    actions_.clear();
    coreCount_ = 0;
}

void ActionRegistry::registerCoreActions() {
    clear();
    actions_.reserve(sizeof(kCoreActions) / sizeof(kCoreActions[0]));
    for (const CoreActionInfo& info : kCoreActions) {
        const int expected = static_cast<int>(info.action);
        if (static_cast<int>(actions_.size()) != expected) {
            TraceLog(LOG_ERROR, "ACTIONS: core action index mismatch for %s", info.id);
        }
        actions_.push_back(ActionDef{
            info.id,
            info.label,
            info.defaultBind,
            false,
        });
    }
    coreCount_ = static_cast<int>(actions_.size());
}

bool ActionRegistry::registerPackageAction(std::string id, std::string label, int defaultBind) {
    if (id.empty()) {
        return false;
    }
    if (indexOf(id) >= 0) {
        TraceLog(LOG_WARNING, "ACTIONS: duplicate action id '%s' ignored", id.c_str());
        return false;
    }
    if (label.empty()) {
        label = id;
    }
    actions_.push_back(ActionDef{
        std::move(id),
        std::move(label),
        defaultBind,
        true,
    });
    return true;
}

const ActionDef& ActionRegistry::at(int index) const {
    return actions_.at(static_cast<std::size_t>(index));
}

int ActionRegistry::indexOf(std::string_view id) const {
    for (int i = 0; i < size(); ++i) {
        if (actions_[static_cast<std::size_t>(i)].id == id) {
            return i;
        }
    }
    return -1;
}

bool registerPackageActionsFromScheme(s7_scheme* scheme) {
    if (scheme == nullptr) {
        return false;
    }

    const s7_pointer catalog = s7_name_to_value(scheme, "*package-actions*");
    if (s7_is_null(scheme, catalog)) {
        return true;
    }
    if (!s7_is_pair(catalog)) {
        return false;
    }

    for (s7_pointer cursor = catalog; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry)) {
            continue;
        }

        std::string id;
        if (!readStringValue(scheme, s7_car(entry), id) || id.empty()) {
            continue;
        }

        const s7_pointer props = s7_cdr(entry);
        std::string label;
        std::string defaultToken;
        readAssocString(scheme, props, "label", label);
        readAssocString(scheme, props, "default", defaultToken);

        const int bind = parseBindToken(defaultToken);
        actionRegistry().registerPackageAction(std::move(id), std::move(label), bind);
    }

    return true;
}

const char* actionId(Action action) {
    return actionIdAt(static_cast<int>(action));
}

const char* actionLabel(Action action) {
    return actionLabelAt(static_cast<int>(action));
}

const char* actionIdAt(int index) {
    const ActionRegistry& registry = actionRegistry();
    if (index < 0 || index >= registry.size()) {
        return "Unknown";
    }
    return registry.at(index).id.c_str();
}

const char* actionLabelAt(int index) {
    const ActionRegistry& registry = actionRegistry();
    if (index < 0 || index >= registry.size()) {
        return "Unknown";
    }
    return registry.at(index).label.c_str();
}

}
