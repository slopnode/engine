#include "test_assert.hpp"

#include "core/package.hpp"
#include "script/hook_registry.hpp"
#include "script/proc_role.hpp"
#include "script/script_scope.hpp"

#include <s7.h>

#include <string>

namespace slopengine {

namespace {

s7_pointer g_current_role(s7_scheme* sc, s7_pointer) {
    return s7_make_integer(sc, static_cast<s7_int>(currentScriptRole()));
}

std::string symbolResult(s7_scheme* sc, const char* form) {
    const s7_pointer result = s7_eval_c_string(sc, form);
    if (!s7_is_symbol(result)) {
        return {};
    }
    return s7_symbol_name(result);
}

void loadAs(s7_scheme* sc, PackageRole role, const char* source) {
    const ProcRoleSnapshot before = snapshotProcRoles(sc);
    {
        ScriptScopeGuard scope(ScriptScope::Boot);
        ScriptCallGuard guard(sc, role);
        s7_eval_c_string(sc, (std::string("(begin ") + source + ")").c_str());
    }
    stampProcRoles(sc, before, role);
}

int roleInt(PackageRole role) {
    return static_cast<int>(role);
}

} // namespace

void runProcRoleTests() {
    s7_scheme* sc = s7_init();
    CHECK(sc != nullptr);
    clearProcRoles();
    s7_define_function(sc, "current-role", g_current_role, 0, 0, false, "");

    CHECK(!snapshotProcRoles(sc).empty());

    loadAs(sc, PackageRole::Base, "(define (base-helper) 'base) (define (tick) (base-helper))");
    CHECK_EQ(roleInt(roleForProc("base-helper")), roleInt(PackageRole::Base));

    loadAs(
        sc,
        PackageRole::Mod,
        "(define (mod-helper) 'mod) (define (base-helper) 'mod) (set! car cdr)");
    CHECK_EQ(roleInt(roleForProc("mod-helper")), roleInt(PackageRole::Mod));
    CHECK_EQ(roleInt(roleForProc("base-helper")), roleInt(PackageRole::Base));
    CHECK_EQ(symbolResult(sc, "(base-helper)"), std::string("base"));
    CHECK_EQ(symbolResult(sc, "(car '(a b))"), std::string("a"));
    CHECK_EQ(symbolResult(sc, "(mod-helper)"), std::string("mod"));

    {
        ScriptCallGuard mod(sc, PackageRole::Mod);
        s7_eval_c_string(sc, "(set! base-helper (lambda () 'mod))");
        CHECK_EQ(symbolResult(sc, "(base-helper)"), std::string("mod"));
        {
            ScriptCallGuard base(sc, PackageRole::Base);
            CHECK_EQ(symbolResult(sc, "(base-helper)"), std::string("base"));
            s7_eval_c_string(sc, "(set! tick (lambda () 'base-next))");
        }
        CHECK_EQ(roleInt(currentScriptRole()), roleInt(PackageRole::Mod));
    }
    CHECK_EQ(symbolResult(sc, "(base-helper)"), std::string("base"));
    CHECK_EQ(symbolResult(sc, "(tick)"), std::string("base-next"));
    CHECK_EQ(roleInt(currentScriptRole()), roleInt(PackageRole::Base));

    bindRoleApi(sc);
    loadAs(sc, PackageRole::Base, "(define base-proc (lambda () (current-role))) (define-macro (base-mac) ''base)");
    CHECK(s7_boolean(sc, s7_eval_c_string(sc, "(eq? base-proc (as-caller-role base-proc))")));
    loadAs(
        sc,
        PackageRole::Mod,
        "(define wrapped (as-caller-role (lambda () (current-role))))"
        "(define-macro (base-mac) ''mod)"
        "(set! (hook-functions *unbound-variable-hook*) (list (lambda (h) (set! (h 'result) 'mod))))");
    CHECK_EQ(symbolResult(sc, "(base-mac)"), std::string("base"));
    CHECK(s7_is_null(sc, s7_eval_c_string(sc, "(hook-functions *unbound-variable-hook*)")));
    {
        ScriptCallGuard base(sc, PackageRole::Base);
        const s7_pointer role = s7_eval_c_string(sc, "(wrapped)");
        CHECK(s7_is_integer(role) && s7_integer(role) == roleInt(PackageRole::Mod));
        CHECK_EQ(roleInt(currentScriptRole()), roleInt(PackageRole::Base));
    }

    clearHookRegistry(sc);
    bindHookApi(sc);
    loadAs(sc, PackageRole::Base, "(define owner-role #f) (define (on-startup) (set! owner-role (current-role)))");
    captureHookOwners(sc);
    loadAs(
        sc,
        PackageRole::Mod,
        "(define contrib-role #f) (define late-role #f)"
        "(hook-add 'on-startup (lambda () (set! contrib-role (current-role))))"
        "(define (on-startup) 'hijacked)");
    {
        ScriptScopeGuard scope(ScriptScope::Ui);
        ScriptRoleGuard mod(PackageRole::Mod);
        s7_eval_c_string(sc, "(hook-add 'on-startup (lambda () (set! late-role (current-role))))");
    }
    callHook(sc, "on-startup", ScriptScope::Startup);
    const auto intValue = [sc](const char* name) {
        const s7_pointer value = s7_name_to_value(sc, name);
        return s7_is_integer(value) ? static_cast<int>(s7_integer(value)) : -1;
    };
    CHECK_EQ(intValue("owner-role"), roleInt(PackageRole::Base));
    CHECK_EQ(intValue("contrib-role"), roleInt(PackageRole::Mod));
    CHECK_EQ(intValue("late-role"), roleInt(PackageRole::Mod));

    clearHookRegistry(sc);
    clearProcRoles();
    s7_quit(sc);
}

}
