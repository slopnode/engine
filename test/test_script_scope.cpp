#include "test_assert.hpp"

#include "core/package.hpp"
#include "script/script_scope.hpp"

namespace slopengine {

void runScriptScopeTests() {
    CHECK_EQ(static_cast<int>(currentScriptScope()), static_cast<int>(ScriptScope::None));
    CHECK_EQ(static_cast<int>(currentScriptRole()), static_cast<int>(PackageRole::Base));

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

    CHECK(scriptAllows(ScriptScope::World, PackageRole::Base, ScriptCap::WorldMutate));
    CHECK(scriptAllows(ScriptScope::World, PackageRole::Base, ScriptCap::SaveIo));
    CHECK(scriptAllows(ScriptScope::World, PackageRole::Base, ScriptCap::MapControl));
    CHECK(scriptAllows(ScriptScope::World, PackageRole::Mod, ScriptCap::WorldMutate));
    CHECK_FALSE(scriptAllows(ScriptScope::World, PackageRole::Mod, ScriptCap::SaveIo));
    CHECK_FALSE(scriptAllows(ScriptScope::World, PackageRole::Mod, ScriptCap::MapControl));
    CHECK(scriptAllows(ScriptScope::World, PackageRole::Engine, ScriptCap::SaveIo));
    CHECK_FALSE(scriptAllows(ScriptScope::Hud, PackageRole::Base, ScriptCap::SaveIo));

    {
        ScriptScopeGuard world(ScriptScope::World);
        ScriptRoleGuard mod(PackageRole::Mod);
        CHECK_EQ(static_cast<int>(currentScriptRole()), static_cast<int>(PackageRole::Mod));
        CHECK(requireCap(nullptr, ScriptCap::WorldMutate));
        CHECK_FALSE(requireCap(nullptr, ScriptCap::SaveIo));
        CHECK_FALSE(requireCap(nullptr, ScriptCap::MapControl));
    }
    CHECK_EQ(static_cast<int>(currentScriptRole()), static_cast<int>(PackageRole::Base));

    {
        ScriptScopeGuard world(ScriptScope::World);
        ScriptRoleGuard base(PackageRole::Base);
        CHECK(requireCap(nullptr, ScriptCap::SaveIo));
        CHECK(requireCap(nullptr, ScriptCap::MapControl));
    }
}

}
