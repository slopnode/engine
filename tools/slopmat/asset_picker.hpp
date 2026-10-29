#pragma once

#include "editor.hpp"

#include "assets/asset_store.hpp"

#include <functional>
#include <string>
#include <vector>

namespace slopmat {

/** Modal list picker over virtual paths with a preview of the hovered item. */
struct AssetPicker {
    std::vector<std::string> items;
    std::string title;
    std::string filter;
    std::string iconId = "picture";
    std::string hovered;
    DocKind previewKind = DocKind::Texture;
    bool open = false;
    std::function<void(const std::string&)> onPick;

    void show(std::string popupTitle, std::vector<std::string> paths, std::string icon, DocKind kind, std::function<void(const std::string&)> callback);
    void draw(slopengine::AssetStore& assets);
};

}
