#include "test_assert.hpp"

#include "script/scheme_harden.hpp"

#include <s7.h>

#include <string>

namespace slopengine {

namespace {

bool isProcedure(s7_scheme* sc, const char* name) {
    return s7_is_procedure(s7_name_to_value(sc, name));
}

bool callDisabled(s7_scheme* sc, const char* form) {
    const std::string wrapped =
        std::string("(let ((r (catch #t (lambda () ") + form +
        ") (lambda args (eq? (car args) (quote disabled)))))) r)";
    const s7_pointer result = s7_eval_c_string(sc, wrapped.c_str());
    return result != nullptr && s7_is_boolean(result) && s7_boolean(sc, result);
}

bool symbolEquals(s7_scheme* sc, const char* form, const char* name) {
    const s7_pointer result = s7_eval_c_string(sc, form);
    return s7_is_symbol(result) && std::string(s7_symbol_name(result)) == name;
}

} // namespace

void runSchemeHardenTests() {
    s7_scheme* sc = s7_init();
    CHECK(sc != nullptr);
    hardenSchemeRuntime(sc);

    CHECK(!scriptingErrorsOccurred());

    CHECK(isProcedure(sc, "system"));
    CHECK(isProcedure(sc, "load"));
    CHECK(isProcedure(sc, "open-input-file"));
    CHECK(isProcedure(sc, "eval"));

    CHECK(callDisabled(sc, "(system \"echo hi\")"));
    CHECK(callDisabled(sc, "(load \"nope.s7\")"));
    CHECK(callDisabled(sc, "(open-input-file \"/etc/passwd\")"));
    CHECK(callDisabled(sc, "(eval '(+ 1 2))"));
    CHECK(callDisabled(sc, "(#_eval '(+ 1 2))"));
    CHECK(callDisabled(sc, "(#_open-input-file \"/etc/passwd\")"));
    CHECK(callDisabled(sc, "(#_system \"echo hi\")"));
    CHECK(callDisabled(sc, "(with-let (unlet) (eval '(+ 1 2)))"));
    CHECK(callDisabled(sc, "(with-let (unlet) (load \"nope.s7\"))"));
    CHECK(callDisabled(sc, "(openlet (inlet 'a 1))"));
    CHECK(callDisabled(sc, "(#_openlet (inlet 'a 1))"));
    s7_eval_c_string(sc, "(define harden-x 'base)");
    s7_eval_c_string(sc, "(set! (symbol-access 'harden-x) (lambda (s v) 'hijack))");
    s7_eval_c_string(sc, "(set! (#_symbol-access 'harden-x) (lambda (s v) 'hijack))");
    s7_eval_c_string(sc, "(set! harden-x 'next)");
    CHECK(symbolEquals(sc, "harden-x", "next"));
    s7_eval_c_string(sc, "(define (harden-f) 'base)");
    s7_eval_c_string(sc, "(set! (procedure-setter harden-f) (lambda (v) 'hijack))");
    CHECK(callDisabled(sc, "(procedure-setter harden-f)"));
    clearScriptingErrors();
    CHECK(!scriptingErrorsOccurred());

    s7_eval_c_string(sc, "(system \"echo hi\")");
    CHECK(scriptingErrorsOccurred());

    clearScriptingErrors();
    CHECK(!scriptingErrorsOccurred());

    s7_quit(sc);
}

}
