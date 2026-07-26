#include "script/hook_registry.hpp"

#include "script/package_load_context.hpp"
#include "script/script_scope.hpp"

#include <s7.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace slopengine {

namespace {

struct HookContrib {
    s7_pointer proc = nullptr;
    unsigned int gcLoc = 0;
    bool protected_ = false;
    PackageRole role = PackageRole::Mod;
};

struct HookEntry {
    s7_pointer owner = nullptr;
    unsigned int ownerGcLoc = 0;
    bool ownerProtected = false;
    std::vector<HookContrib> contribs;
};

std::unordered_map<std::string, HookEntry> g_hooks;

const char* kEngineHooks[] = {
    "prepare-first-person",
    "on-startup",
    "on-map-ready",
    "tick",
    "draw-hud",
    "draw-title",
    "draw-file-menu",
    "draw-pause-menu",
    "draw-debug-menu",
    "draw-modals",
    "on-sprite-hint",
    "on-sight",
    "sight-filter",
};

void callOwnerThenContribs(s7_scheme* scheme, std::string_view name, s7_pointer args) {
    const std::string nameStr(name);
    const auto it = g_hooks.find(nameStr);
    if (it == g_hooks.end()) {
        return;
    }
    HookEntry& entry = it->second;
    s7_pointer owner = entry.owner;
    const s7_pointer live = s7_name_to_value(scheme, nameStr.c_str());
    if (s7_is_procedure(live)) {
        owner = live;
    }
    if (owner != nullptr && s7_is_procedure(owner)) {
        ScriptRoleGuard roleGuard(PackageRole::Base);
        s7_call(scheme, owner, args);
    }
    for (const HookContrib& contrib : entry.contribs) {
        if (contrib.proc != nullptr && s7_is_procedure(contrib.proc)) {
            ScriptRoleGuard roleGuard(contrib.role);
            s7_call(scheme, contrib.proc, args);
        }
    }
}

s7_pointer g_hook_add(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::PackageLoad)) {
        return s7_f(sc);
    }
    const s7_pointer nameCell = s7_car(args);
    const s7_pointer procCell = s7_cadr(args);
    if (!s7_is_symbol(nameCell)) {
        return s7_wrong_type_arg_error(sc, "hook-add", 1, nameCell, "symbol");
    }
    if (!s7_is_procedure(procCell)) {
        return s7_wrong_type_arg_error(sc, "hook-add", 2, procCell, "procedure");
    }
    const char* name = s7_symbol_name(nameCell);
    if (name == nullptr || !hookAdd(sc, name, procCell)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "hook"),
            s7_list(sc, 1, s7_make_string(sc, "unknown or invalid hook")));
    }
    return s7_t(sc);
}

} // namespace

bool isEngineHookName(std::string_view name) {
    for (const char* hook : kEngineHooks) {
        if (name == hook) {
            return true;
        }
    }
    return false;
}

bool hookAdd(s7_scheme* scheme, std::string_view name, s7_pointer proc) {
    if (scheme == nullptr || !isEngineHookName(name) || proc == nullptr || !s7_is_procedure(proc)) {
        return false;
    }
    HookContrib contrib{};
    contrib.proc = proc;
    contrib.gcLoc = s7_gc_protect(scheme, proc);
    contrib.protected_ = true;
    contrib.role = currentPackageRole();
    g_hooks[std::string(name)].contribs.push_back(contrib);
    return true;
}

void captureHookOwners(s7_scheme* scheme) {
    if (scheme == nullptr) {
        return;
    }
    for (const char* hook : kEngineHooks) {
        const s7_pointer proc = s7_name_to_value(scheme, hook);
        if (!s7_is_procedure(proc)) {
            continue;
        }
        HookEntry& entry = g_hooks[hook];
        if (entry.ownerProtected) {
            s7_gc_unprotect_at(scheme, entry.ownerGcLoc);
            entry.ownerProtected = false;
        }
        entry.owner = proc;
        entry.ownerGcLoc = s7_gc_protect(scheme, proc);
        entry.ownerProtected = true;
    }
}

void clearHookRegistry(s7_scheme* scheme) {
    if (scheme != nullptr) {
        for (auto& [name, entry] : g_hooks) {
            (void)name;
            if (entry.ownerProtected) {
                s7_gc_unprotect_at(scheme, entry.ownerGcLoc);
                entry.ownerProtected = false;
            }
            for (HookContrib& contrib : entry.contribs) {
                if (contrib.protected_) {
                    s7_gc_unprotect_at(scheme, contrib.gcLoc);
                    contrib.protected_ = false;
                }
            }
        }
    }
    g_hooks.clear();
}

void callHook(s7_scheme* scheme, std::string_view name, ScriptScope scope) {
    if (scheme == nullptr) {
        return;
    }
    ScriptScopeGuard guard(scope);
    callOwnerThenContribs(scheme, name, s7_nil(scheme));
}

void callHook1String(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg,
    ScriptScope scope) {
    if (scheme == nullptr) {
        return;
    }
    ScriptScopeGuard guard(scope);
    callOwnerThenContribs(scheme, name, s7_list(scheme, 1, s7_make_string(scheme, arg.c_str())));
}

void callHook1Real(
    s7_scheme* scheme,
    std::string_view name,
    double arg,
    ScriptScope scope) {
    if (scheme == nullptr) {
        return;
    }
    ScriptScopeGuard guard(scope);
    callOwnerThenContribs(scheme, name, s7_list(scheme, 1, s7_make_real(scheme, arg)));
}

void callHook2String(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    const std::string& arg1,
    ScriptScope scope) {
    if (scheme == nullptr) {
        return;
    }
    ScriptScopeGuard guard(scope);
    callOwnerThenContribs(
        scheme,
        name,
        s7_list(
            scheme,
            2,
            s7_make_string(scheme, arg0.c_str()),
            s7_make_string(scheme, arg1.c_str())));
}

bool callHook2StringAllTruthy(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    const std::string& arg1,
    ScriptScope scope) {
    if (scheme == nullptr) {
        return true;
    }
    const auto it = g_hooks.find(std::string(name));
    if (it == g_hooks.end()) {
        return true;
    }
    ScriptScopeGuard guard(scope);
    const s7_pointer args = s7_list(
        scheme,
        2,
        s7_make_string(scheme, arg0.c_str()),
        s7_make_string(scheme, arg1.c_str()));
    HookEntry& entry = it->second;
    auto truthy = [scheme](s7_pointer result) -> bool {
        if (result == nullptr) {
            return false;
        }
        if (s7_is_boolean(result)) {
            return s7_boolean(scheme, result);
        }
        return !s7_is_null(scheme, result) && result != s7_f(scheme);
    };
    if (entry.owner != nullptr && s7_is_procedure(entry.owner)) {
        ScriptRoleGuard roleGuard(PackageRole::Base);
        if (!truthy(s7_call(scheme, entry.owner, args))) {
            return false;
        }
    }
    for (const HookContrib& contrib : entry.contribs) {
        if (contrib.proc != nullptr && s7_is_procedure(contrib.proc)) {
            ScriptRoleGuard roleGuard(contrib.role);
            if (!truthy(s7_call(scheme, contrib.proc, args))) {
                return false;
            }
        }
    }
    return true;
}

void bindHookApi(s7_scheme* scheme) {
    if (scheme == nullptr) {
        return;
    }
    s7_define_function(scheme, "hook-add", g_hook_add, 2, 0, false, "(hook-add hook-symbol proc)");
}

}
