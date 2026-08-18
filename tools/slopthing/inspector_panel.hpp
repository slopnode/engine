#pragma once

#include "editor.hpp"

#include "assets/asset_store.hpp"

#include "imgui.h"

namespace slopthing {

void drawInspectorPanel(
    Editor& editor, slopengine::AssetStore& assets, ImFont* monoFont, float bodyHeight);

}
