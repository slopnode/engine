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

ViewCanvas parseViewCanvasFromScheme(s7_scheme* scheme) {
    ViewCanvas canvas{};
    if (scheme == nullptr) {
        return canvas;
    }

    const s7_pointer value = s7_name_to_value(scheme, "*view-canvas*");
    if (!s7_is_pair(value)) {
        return canvas;
    }

    const s7_pointer wCell = s7_car(value);
    const s7_pointer rest = s7_cdr(value);
    if (!s7_is_number(wCell) || !s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return canvas;
    }

    const int width = static_cast<int>(s7_number_to_integer(scheme, wCell));
    const int height = static_cast<int>(s7_number_to_integer(scheme, s7_car(rest)));
    if (width <= 0 || height <= 0) {
        return canvas;
    }

    canvas.width = width;
    canvas.height = height;
    return canvas;
}

}
