#pragma once

#include "script/script_scope.hpp"

#include <s7.h>

#include <string>
#include <string_view>

namespace slopengine {

bool isEngineHookName(std::string_view name);
bool hookAdd(s7_scheme* scheme, std::string_view name, s7_pointer proc);
void captureHookOwners(s7_scheme* scheme);
void clearHookRegistry(s7_scheme* scheme);

void callHook(s7_scheme* scheme, std::string_view name, ScriptScope scope);
void callHook1String(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg,
    ScriptScope scope);
void callHook1Real(
    s7_scheme* scheme,
    std::string_view name,
    double arg,
    ScriptScope scope);
void callHook2String(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    const std::string& arg1,
    ScriptScope scope);

/** Call owner then contribs; false if any returns falsey. Missing hooks => true. */
bool callHook2StringAllTruthy(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    const std::string& arg1,
    ScriptScope scope);

void bindHookApi(s7_scheme* scheme);

}
