#include "script/script_scope.hpp"

#include <s7.h>

#include <cstdio>

namespace slopengine {

namespace {

thread_local ScriptScope g_currentScope = ScriptScope::None;

uint32_t capsFor(ScriptScope scope) {
    switch (scope) {
    case ScriptScope::Boot:
        return static_cast<uint32_t>(ScriptCap::PackageLoad);
    case ScriptScope::Startup:
        return static_cast<uint32_t>(ScriptCap::StartupQuery) |
               static_cast<uint32_t>(ScriptCap::PackageLoad) |
               static_cast<uint32_t>(ScriptCap::MapControl) |
               static_cast<uint32_t>(ScriptCap::SaveIo) |
               static_cast<uint32_t>(ScriptCap::ReadWorld) |
               static_cast<uint32_t>(ScriptCap::UiDraw);
    case ScriptScope::World:
        return static_cast<uint32_t>(ScriptCap::WorldMutate) |
               static_cast<uint32_t>(ScriptCap::FpPresent) |
               static_cast<uint32_t>(ScriptCap::Audio) |
               static_cast<uint32_t>(ScriptCap::InputQuery) |
               static_cast<uint32_t>(ScriptCap::ReadWorld) |
               static_cast<uint32_t>(ScriptCap::MapControl) |
               static_cast<uint32_t>(ScriptCap::SaveIo);
    case ScriptScope::Hud:
        return static_cast<uint32_t>(ScriptCap::HudDraw) |
               static_cast<uint32_t>(ScriptCap::InputQuery) |
               static_cast<uint32_t>(ScriptCap::ReadWorld);
    case ScriptScope::Ui:
        return static_cast<uint32_t>(ScriptCap::UiDraw) |
               static_cast<uint32_t>(ScriptCap::SaveIo) |
               static_cast<uint32_t>(ScriptCap::MapControl) |
               static_cast<uint32_t>(ScriptCap::PackageLoad) |
               static_cast<uint32_t>(ScriptCap::ReadWorld) |
               static_cast<uint32_t>(ScriptCap::StartupQuery);
    case ScriptScope::MapAuthor:
    case ScriptScope::None:
        return 0;
    }
    return 0;
}

const char* capName(ScriptCap cap) {
    switch (cap) {
    case ScriptCap::HudDraw:
        return "hud-draw";
    case ScriptCap::UiDraw:
        return "ui-draw";
    case ScriptCap::SaveIo:
        return "save-io";
    case ScriptCap::MapControl:
        return "map-control";
    case ScriptCap::WorldMutate:
        return "world-mutate";
    case ScriptCap::FpPresent:
        return "fp-present";
    case ScriptCap::Audio:
        return "audio";
    case ScriptCap::InputQuery:
        return "input-query";
    case ScriptCap::PackageLoad:
        return "package-load";
    case ScriptCap::StartupQuery:
        return "startup-query";
    case ScriptCap::ReadWorld:
        return "read-world";
    }
    return "unknown";
}

} // namespace

ScriptScopeGuard::ScriptScopeGuard(ScriptScope scope)
    : previous_(g_currentScope) {
    g_currentScope = scope;
}

ScriptScopeGuard::~ScriptScopeGuard() {
    g_currentScope = previous_;
}

ScriptScope currentScriptScope() {
    return g_currentScope;
}

bool scriptScopeAllows(ScriptScope scope, ScriptCap cap) {
    return (capsFor(scope) & static_cast<uint32_t>(cap)) != 0;
}

bool requireCap(s7_scheme* sc, ScriptCap cap) {
    if (scriptScopeAllows(g_currentScope, cap)) {
        return true;
    }
    if (sc != nullptr) {
        char message[96];
        std::snprintf(message, sizeof(message), "capability denied: %s", capName(cap));
        s7_error(
            sc,
            s7_make_symbol(sc, "capability"),
            s7_list(sc, 1, s7_make_string(sc, message)));
    }
    return false;
}

}
