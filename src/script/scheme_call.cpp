#include "script/scheme_call.hpp"

#include <s7.h>

namespace slopengine {

bool tryCallSchemeProc(s7_scheme* scheme, std::string_view name) {
    if (scheme == nullptr || name.empty()) {
        return false;
    }

    const s7_pointer func = s7_name_to_value(scheme, std::string(name).c_str());
    if (!s7_is_procedure(func)) {
        return false;
    }

    s7_call(scheme, func, s7_nil(scheme));
    return true;
}

bool tryCallSchemeProc1String(s7_scheme* scheme, std::string_view name, const std::string& arg) {
    if (scheme == nullptr || name.empty()) {
        return false;
    }

    const s7_pointer func = s7_name_to_value(scheme, std::string(name).c_str());
    if (!s7_is_procedure(func)) {
        return false;
    }

    s7_call(scheme, func, s7_list(scheme, 1, s7_make_string(scheme, arg.c_str())));
    return true;
}

}
