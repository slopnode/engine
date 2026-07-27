#pragma once

#include "map/handler_binding.hpp"
#include "render/components.hpp"
#include "script/script_scope.hpp"

#include <string>
#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

bool tryCallSchemeProc(s7_scheme* scheme, std::string_view name, ScriptScope scope);
bool tryCallSchemeProc1String(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg,
    ScriptScope scope);
bool tryCallSchemeProc1Integer(
    s7_scheme* scheme,
    std::string_view name,
    int arg,
    ScriptScope scope);
bool tryCallSchemeProc1Real(
    s7_scheme* scheme,
    std::string_view name,
    double arg,
    ScriptScope scope);
bool tryCallSchemeProc2String(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    const std::string& arg1,
    ScriptScope scope);
/** Call named proc with two strings; missing proc => true. Returns Scheme truthiness. */
bool tryCallSchemeProc2StringTruthy(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    const std::string& arg1,
    ScriptScope scope);
bool tryCallSchemeProc1String1Alist(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    const std::vector<HandlerArg>& args,
    ScriptScope scope);
bool tryCallSchemeProc2String1Alist(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    const std::string& arg1,
    const std::vector<HandlerArg>& args,
    ScriptScope scope);

/** Catalog handlers get an alist last arg; legacy names keep old arity. */
bool tryCallMapHandlerUse(
    s7_scheme* scheme,
    HandlerBinding binding,
    const std::string& entityId,
    ScriptScope scope);
/** Call can-use predicate; returns whether activation is allowed. Empty binding => true. */
bool tryCallMapHandlerCanUse(
    s7_scheme* scheme,
    HandlerBinding binding,
    const std::string& entityId,
    ScriptScope scope);
bool tryCallMapHandlerEnterExit(
    s7_scheme* scheme,
    HandlerBinding binding,
    const std::string& thingId,
    const std::string& otherId,
    ScriptScope scope);
bool tryCallSchemeProc1String3Reals(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    double x,
    double y,
    double z,
    ScriptScope scope);
/** Like 1String3Reals, plus a 5th arg: hit target id string, or #f when @p hitTarget is empty. */
bool tryCallSchemeProc1String3Reals1OptString(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    double x,
    double y,
    double z,
    std::string_view hitTarget,
    ScriptScope scope);

ViewCanvas parseViewCanvasFromScheme(s7_scheme* scheme);
HudCanvas parseHudCanvasFromScheme(s7_scheme* scheme);

}
