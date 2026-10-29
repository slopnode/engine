#pragma once

#include "asset_picker.hpp"
#include "browser.hpp"
#include "editor.hpp"

#include "assets/asset_store.hpp"

namespace slopmat {

void drawMaterialInspector(Editor& editor, slopengine::AssetStore& assets, AssetPicker& picker, const Browser& browser);

}
