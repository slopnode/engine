#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"

#include <raylib.h>

#include <vector>

namespace slopmap {

struct MapPreview {
    Model model{};
    bool valid = false;

    void clear();
    void rebuild(slopengine::AssetStore& assets, const std::vector<slopengine::Brush>& brushes);
    void draw(
        bool wireframe,
        const std::vector<slopengine::Brush>& brushes,
        int selectedBrush) const;
};

Color brushOutlineColor(const slopengine::Brush& brush, bool selected);
void drawBrushFaceOutlines(const slopengine::Brush& brush, Color color);
void drawBrushAabbWires(const slopengine::Brush& brush, Color color);
void drawAabbWires(Vector3 mins, Vector3 maxs, Color color);
void drawAabbSolid(Vector3 mins, Vector3 maxs, Color color);
void drawGridY0(float halfExtent, float step, Color color);

}
