#pragma once

#include <cstdint>

struct s7_scheme;

namespace slopengine {

enum class ScriptScope {
    None,
    Boot,
    Startup,
    World,
    Hud,
    Ui,
    MapAuthor,
};

enum class ScriptCap : uint32_t {
    HudDraw = 1u << 0,
    UiDraw = 1u << 1,
    SaveIo = 1u << 2,
    MapControl = 1u << 3,
    WorldMutate = 1u << 4,
    FpPresent = 1u << 5,
    Audio = 1u << 6,
    InputQuery = 1u << 7,
    PackageLoad = 1u << 8,
    StartupQuery = 1u << 9,
    ReadWorld = 1u << 10,
};

class ScriptScopeGuard {
public:
    explicit ScriptScopeGuard(ScriptScope scope);
    ~ScriptScopeGuard();

    ScriptScopeGuard(const ScriptScopeGuard&) = delete;
    ScriptScopeGuard& operator=(const ScriptScopeGuard&) = delete;

private:
    ScriptScope previous_;
};

ScriptScope currentScriptScope();
bool scriptScopeAllows(ScriptScope scope, ScriptCap cap);
bool requireCap(s7_scheme* sc, ScriptCap cap);

}
