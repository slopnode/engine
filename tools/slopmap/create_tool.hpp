#pragma once

#include "editor.hpp"

#include <raylib.h>

namespace slopmap {

enum class CreatePhase {
    Idle,
    DrawingBase,
    Extruding,
};

struct CreateTool {
    CreatePhase phase = CreatePhase::Idle;
    ConstructionPlane plane{};
    Vector3 corner0{};
    Vector3 corner1{};
    float thickness = 0.0f;
    bool thicknessFromNumeric = false;
    Vector2 extrudeMouseStart{};
    float extrudeThicknessStart = 0.0f;

    void reset();
    void update(Editor& editor, const Camera3D& camera, bool uiWantsMouse, bool uiWantsKeyboard);
    void drawPreview() const;
    bool active() const { return phase != CreatePhase::Idle; }

private:
    bool footprintBounds(Vector3& mins, Vector3& maxs) const;
    bool finalBounds(Vector3& mins, Vector3& maxs) const;
    void commit(Editor& editor);
    void handleNumeric(Editor& editor, bool uiWantsKeyboard);
};

}
