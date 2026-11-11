#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace slopdocs {

using SymbolLinker = std::function<std::string(std::string_view name)>;

std::string highlightScheme(std::string_view source, const SymbolLinker& linker);

std::string highlightGlsl(std::string_view source);

std::string escapeHtml(std::string_view text);

}
