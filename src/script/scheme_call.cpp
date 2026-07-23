#include "script/scheme_call.hpp"

#include <s7.h>

namespace slopengine {

namespace {

bool parseCanvasSize(s7_scheme* scheme, const char* name, int& outWidth, int& outHeight) {
    if (scheme == nullptr || name == nullptr) {
        return false;
    }

    const s7_pointer value = s7_name_to_value(scheme, name);
    if (!s7_is_pair(value)) {
        return false;
    }

    const s7_pointer wCell = s7_car(value);
    const s7_pointer rest = s7_cdr(value);
    if (!s7_is_number(wCell) || !s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return false;
    }

    const int width = static_cast<int>(s7_number_to_integer(scheme, wCell));
    const int height = static_cast<int>(s7_number_to_integer(scheme, s7_car(rest)));
    if (width <= 0 || height <= 0) {
        return false;
    }

    outWidth = width;
    outHeight = height;
    return true;
}

} // namespace

bool tryCallSchemeProc(s7_scheme* scheme, std::string_view name, ScriptScope scope) {
    if (scheme == nullptr || name.empty()) {
        return false;
    }

    const s7_pointer func = s7_name_to_value(scheme, std::string(name).c_str());
    if (!s7_is_procedure(func)) {
        return false;
    }

    ScriptScopeGuard guard(scope);
    s7_call(scheme, func, s7_nil(scheme));
    return true;
}

bool tryCallSchemeProc1String(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg,
    ScriptScope scope) {
    if (scheme == nullptr || name.empty()) {
        return false;
    }

    const s7_pointer func = s7_name_to_value(scheme, std::string(name).c_str());
    if (!s7_is_procedure(func)) {
        return false;
    }

    ScriptScopeGuard guard(scope);
    s7_call(scheme, func, s7_list(scheme, 1, s7_make_string(scheme, arg.c_str())));
    return true;
}

bool tryCallSchemeProc1Integer(
    s7_scheme* scheme,
    std::string_view name,
    int arg,
    ScriptScope scope) {
    if (scheme == nullptr || name.empty()) {
        return false;
    }

    const s7_pointer func = s7_name_to_value(scheme, std::string(name).c_str());
    if (!s7_is_procedure(func)) {
        return false;
    }

    ScriptScopeGuard guard(scope);
    s7_call(scheme, func, s7_list(scheme, 1, s7_make_integer(scheme, arg)));
    return true;
}

bool tryCallSchemeProc1Real(
    s7_scheme* scheme,
    std::string_view name,
    double arg,
    ScriptScope scope) {
    if (scheme == nullptr || name.empty()) {
        return false;
    }

    const s7_pointer func = s7_name_to_value(scheme, std::string(name).c_str());
    if (!s7_is_procedure(func)) {
        return false;
    }

    ScriptScopeGuard guard(scope);
    s7_call(scheme, func, s7_list(scheme, 1, s7_make_real(scheme, arg)));
    return true;
}

bool tryCallSchemeProc2String(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    const std::string& arg1,
    ScriptScope scope) {
    if (scheme == nullptr || name.empty()) {
        return false;
    }

    const s7_pointer func = s7_name_to_value(scheme, std::string(name).c_str());
    if (!s7_is_procedure(func)) {
        return false;
    }

    ScriptScopeGuard guard(scope);
    s7_call(
        scheme,
        func,
        s7_list(
            scheme,
            2,
            s7_make_string(scheme, arg0.c_str()),
            s7_make_string(scheme, arg1.c_str())));
    return true;
}

bool tryCallSchemeProc1String3Reals(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    double x,
    double y,
    double z,
    ScriptScope scope) {
    if (scheme == nullptr || name.empty()) {
        return false;
    }

    const s7_pointer func = s7_name_to_value(scheme, std::string(name).c_str());
    if (!s7_is_procedure(func)) {
        return false;
    }

    ScriptScopeGuard guard(scope);
    s7_call(
        scheme,
        func,
        s7_list(
            scheme,
            4,
            s7_make_string(scheme, arg0.c_str()),
            s7_make_real(scheme, x),
            s7_make_real(scheme, y),
            s7_make_real(scheme, z)));
    return true;
}

ViewCanvas parseViewCanvasFromScheme(s7_scheme* scheme) {
    ViewCanvas canvas{};
    int width = 0;
    int height = 0;
    if (parseCanvasSize(scheme, "*view-canvas*", width, height)) {
        canvas.width = width;
        canvas.height = height;
    }
    return canvas;
}

HudCanvas parseHudCanvasFromScheme(s7_scheme* scheme) {
    HudCanvas canvas{};
    int width = 0;
    int height = 0;
    if (parseCanvasSize(scheme, "*hud-canvas*", width, height)) {
        canvas.width = width;
        canvas.height = height;
    }
    return canvas;
}

}
