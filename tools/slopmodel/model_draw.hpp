#pragma once

#include "assets/asset_store.hpp"
#include "editor.hpp"

#include <raylib.h>

#include <string>
#include <vector>

namespace slopmodel {

/** The engine's view-model shader with a fixed key light, so previews read with form. */
struct ShadedDraw {
    Shader shader{};

    void init(slopengine::AssetStore& assets);
    void release();
    bool valid() const;
};

/** Draws @p model at @p transform, through the shaded pass when @p shaded is set. */
void drawGeoModel(Model& model, const Matrix& transform, const ShadedDraw* shaded, Color tint, bool twoSided);

/** Draws joints and bones of @p model's current pose, highlighting @p highlightBone. */
void drawSkeleton(const Model& model, int highlightBone);

/** Draws the main model, its attachment markers and previews, and optional skeleton. */
struct SceneDraw {
    const Editor* editor = nullptr;
    const Camera3D* camera = nullptr;
    Vector2 screenSize{};
    struct Marker {
        Vector2 screen{};
        std::string name;
        bool selected = false;
    };
    std::vector<Marker> markers;
};

void drawEditorScene(Editor& editor, const Matrix& modelMatrix, const ShadedDraw* shaded, SceneDraw& out);

/** Draws the labels collected by drawEditorScene; call outside BeginMode3D. */
void drawSceneLabels(const SceneDraw& scene);

}
