#pragma once

#include <string>
#include <string_view>

struct s7_scheme;

namespace slopengine {

bool tryCallSchemeProc(s7_scheme* scheme, std::string_view name);
bool tryCallSchemeProc1String(s7_scheme* scheme, std::string_view name, const std::string& arg);

}
