#pragma once

#include <flecs.h>

namespace slopengine {

class AssetStore;
struct DebugUiState;

/** Debug-only live editor for the installed SlopUiTheme, with export to the base package. */
void drawThemeEditor(flecs::world world, AssetStore& assets, DebugUiState& debugUi);

}
