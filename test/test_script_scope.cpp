#include "test_assert.hpp"

#include "script/script_scope.hpp"

namespace slopengine {

void runScriptScopeTests() {
    CHECK_EQ(static_cast<int>(currentScriptScope()), static_cast<int>(ScriptScope::None));

    {
        ScriptScopeGuard hud(ScriptScope::Hud);
        CHECK_EQ(static_cast<int>(currentScriptScope()), static_cast<int>(ScriptScope::Hud));
        CHECK(scriptScopeAllows(ScriptScope::Hud, ScriptCap::HudDraw));
        CHECK(scriptScopeAllows(ScriptScope::Hud, ScriptCap::InputQuery));
        CHECK(scriptScopeAllows(ScriptScope::Hud, ScriptCap::ReadWorld));
        CHECK_FALSE(scriptScopeAllows(ScriptScope::Hud, ScriptCap::SaveIo));
        CHECK_FALSE(scriptScopeAllows(ScriptScope::Hud, ScriptCap::MapControl));
        CHECK_FALSE(scriptScopeAllows(ScriptScope::Hud, ScriptCap::WorldMutate));
        CHECK_FALSE(scriptScopeAllows(ScriptScope::Hud, ScriptCap::UiDraw));
    }
    CHECK_EQ(static_cast<int>(currentScriptScope()), static_cast<int>(ScriptScope::None));

    {
        ScriptScopeGuard world(ScriptScope::World);
        CHECK(scriptScopeAllows(ScriptScope::World, ScriptCap::SaveIo));
        CHECK(scriptScopeAllows(ScriptScope::World, ScriptCap::Audio));
        CHECK(scriptScopeAllows(ScriptScope::World, ScriptCap::WorldMutate));
        CHECK(scriptScopeAllows(ScriptScope::World, ScriptCap::MapControl));
        CHECK_FALSE(scriptScopeAllows(ScriptScope::World, ScriptCap::HudDraw));
        CHECK_FALSE(scriptScopeAllows(ScriptScope::World, ScriptCap::UiDraw));
    }

    {
        ScriptScopeGuard outer(ScriptScope::Ui);
        CHECK_EQ(static_cast<int>(currentScriptScope()), static_cast<int>(ScriptScope::Ui));
        {
            ScriptScopeGuard inner(ScriptScope::Hud);
            CHECK_EQ(static_cast<int>(currentScriptScope()), static_cast<int>(ScriptScope::Hud));
        }
        CHECK_EQ(static_cast<int>(currentScriptScope()), static_cast<int>(ScriptScope::Ui));
    }
    CHECK_EQ(static_cast<int>(currentScriptScope()), static_cast<int>(ScriptScope::None));
}

}
