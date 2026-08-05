#pragma once

#include "editor.hpp"

#include <raylib.h>

namespace slopmap {

struct PlaceTool {
    bool hoverValid = false;
    ConstructionPlane hoverPlane{};
    Vector3 hoverPoint{};

    void resetHover();
    void update(
        Editor& editor,
        slopengine::AssetStore& assets,
        const Camera3D& camera,
        bool uiWantsMouse,
        bool uiWantsKeyboard);
    void drawPreview(Vector3 eye, float lineWidth) const;

private:
    bool needsPosition(const Editor& editor) const;
    void updateHover(Editor& editor, const Camera3D& camera);
};

}
