#pragma once

#include "input/actions.hpp"

#include <string>
#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

struct ActionDef {
    std::string id;
    std::string label;
    int defaultBind = 0;
    bool isPackage = false;
};

class ActionRegistry {
public:
    void clear();
    void registerCoreActions();
    bool registerPackageAction(std::string id, std::string label, int defaultBind);

    int size() const { return static_cast<int>(actions_.size()); }
    int coreCount() const { return coreCount_; }

    const ActionDef& at(int index) const;
    int indexOf(std::string_view id) const;

    const std::vector<ActionDef>& actions() const { return actions_; }

private:
    std::vector<ActionDef> actions_;
    int coreCount_ = 0;
};

ActionRegistry& actionRegistry();

/** Loads *package-actions* from Scheme and registers them. */
bool registerPackageActionsFromScheme(s7_scheme* scheme);

const char* actionId(Action action);
const char* actionLabel(Action action);
const char* actionIdAt(int index);
const char* actionLabelAt(int index);

}
