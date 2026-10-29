#pragma once

#include <flecs.h>

struct s7_scheme;

namespace slopengine {

/** Binds theme-color / theme-font / theme-font-size lookups against the installed SlopUiTheme. */
void bindThemeApi(flecs::world& world, s7_scheme* scheme);

}
