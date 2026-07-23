#include "script/scheme_harden.hpp"

#include <s7.h>

namespace slopengine {

namespace {

s7_pointer g_disabled(s7_scheme* sc, s7_pointer) {
    return s7_error(
        sc,
        s7_make_symbol(sc, "disabled"),
        s7_list(sc, 1, s7_make_string(sc, "disabled in slopengine")));
}

void stubDisabled(s7_scheme* sc, const char* name) {
    s7_define_function(sc, name, g_disabled, 0, 0, true, "disabled in slopengine");
}

void disableAutoload(s7_scheme* sc) {
    s7_eval_c_string(sc, "(set! (*s7* 'autoloading) #f)");
}

} // namespace

void hardenSchemeRuntime(s7_scheme* scheme) {
    if (scheme == nullptr) {
        return;
    }

    static const char* kDisabled[] = {
        "open-input-file",
        "open-output-file",
        "call-with-input-file",
        "call-with-output-file",
        "with-input-from-file",
        "with-output-to-file",
        "load",
        "autoload",
        "eval",
        "eval-string",
        "exit",
        "emergency-exit",
        "system",
        "getenv",
        "delete-file",
        "directory?",
        "file-exists?",
        "directory->list",
        "file-mtime",
    };

    for (const char* name : kDisabled) {
        stubDisabled(scheme, name);
    }

    s7_symbol_set_value(scheme, s7_make_symbol(scheme, "*load-path*"), s7_nil(scheme));
    s7_symbol_set_value(scheme, s7_make_symbol(scheme, "*autoload*"), s7_nil(scheme));
    s7_symbol_set_value(scheme, s7_make_symbol(scheme, "*cload-directory*"), s7_make_string(scheme, ""));
    disableAutoload(scheme);
}

}
