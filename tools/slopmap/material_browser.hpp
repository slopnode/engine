#pragma once

#include "assets/asset_store.hpp"
#include "editor.hpp"
#include "editor_settings.hpp"
#include "material_thumb_atlas.hpp"

#include <functional>
#include <string>
#include <vector>

namespace slopmap {

struct TexturePanel;

enum class AssetPickerKind {
    Material,
    Texture,
};

struct AssetPickerOptions {
    bool skyMaterialsOnly = false;
    bool allowNone = false;
    const char* initialFilter = "";
};

struct MaterialBrowserResult {
    bool applied = false;
    bool requestRescan = false;
};

struct MaterialBrowser {
    std::vector<std::string> materials;
    std::vector<std::string> textures;
    std::vector<std::string> folderPrefixes;
    char filter[128] = {};
    int folderScopeIndex = 0;
    bool skyMaterialsOnly = false;
    MaterialThumbAtlas thumbs;
    bool thumbsDirty = true;

    bool pickerOpen = false;
    AssetPickerKind pickerKind = AssetPickerKind::Material;
    bool pickerSkyOnly = false;
    bool pickerAllowNone = false;
    std::function<void(const std::string&)> pickerCallback;
    std::string browseFolderPath;

    void rescan(const slopengine::AssetStore& assets);
    void openPicker(
        AssetPickerKind kind,
        std::function<void(const std::string&)> callback,
        AssetPickerOptions options = {});
    MaterialBrowserResult drawPopup(
        slopengine::AssetStore& assets,
        EditorSettings& settings);
    MaterialBrowserResult drawSurfaceSection(
        Editor& editor,
        slopengine::AssetStore& assets,
        EditorSettings& settings,
        TexturePanel& texturePanel,
        float bodyHeight);
};

std::string selectionMaterialLabel(const EditorDocument& doc);
bool applyMaterialToSelection(Editor& editor, const std::string& materialPath);

}
