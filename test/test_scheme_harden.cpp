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

} // namespace

void runSchemeHardenTests() {
    s7_scheme* sc = s7_init();
    CHECK(sc != nullptr);
    hardenSchemeRuntime(sc);

    CHECK(isProcedure(sc, "system"));
    CHECK(isProcedure(sc, "load"));
    CHECK(isProcedure(sc, "open-input-file"));
    CHECK(isProcedure(sc, "eval"));

    CHECK(callDisabled(sc, "(system \"echo hi\")"));
    CHECK(callDisabled(sc, "(load \"nope.s7\")"));
    CHECK(callDisabled(sc, "(open-input-file \"/etc/passwd\")"));
    CHECK(callDisabled(sc, "(eval '(+ 1 2))"));

    s7_quit(sc);
}

}
