#pragma once

#include "render/components.hpp"

#include <string>
#include <string_view>

struct s7_scheme;

namespace slopengine {

bool tryCallSchemeProc(s7_scheme* scheme, std::string_view name);
bool tryCallSchemeProc1String(s7_scheme* scheme, std::string_view name, const std::string& arg);
bool tryCallSchemeProc2String(
    s7_scheme* scheme,
    std::string_view name,
    const std::string& arg0,
    const std::string& arg1);

/** Reads *view-canvas* from Scheme; defaults to 320x200 if missing/invalid. */
ViewCanvas parseViewCanvasFromScheme(s7_scheme* scheme);

}
