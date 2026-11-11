#pragma once

#include <cstddef>
#include <string_view>

extern const unsigned char slopdocs_prelude[];
extern const std::size_t slopdocs_prelude_size;
extern const unsigned char slopdocs_theme_css[];
extern const std::size_t slopdocs_theme_css_size;
extern const unsigned char slopdocs_docs_js[];
extern const std::size_t slopdocs_docs_js_size;
extern const unsigned char slopdocs_engine_names[];
extern const std::size_t slopdocs_engine_names_size;

namespace slopdocs {

inline std::string_view preludeSource() {
    return {reinterpret_cast<const char*>(slopdocs_prelude), slopdocs_prelude_size};
}

inline std::string_view themeCss() {
    return {reinterpret_cast<const char*>(slopdocs_theme_css), slopdocs_theme_css_size};
}

inline std::string_view docsJs() {
    return {reinterpret_cast<const char*>(slopdocs_docs_js), slopdocs_docs_js_size};
}

inline std::string_view engineNamesText() {
    return {reinterpret_cast<const char*>(slopdocs_engine_names), slopdocs_engine_names_size};
}

}
