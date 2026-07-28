#pragma once

#include "editor.hpp"

#include <raylib.h>

namespace slopmap {

enum class PunchPhase {
    Idle,
    DrawingRect,
    ExtrudingDepth,
};

struct PunchTool {
    PunchPhase phase = PunchPhase::Idle;
    int brushIndex = -1;
    slopengine::BrushBoxSide faceSide = slopengine::BrushBoxSide::South;
    ConstructionPlane plane{};
    Vector3 corner0{};
    Vector3 corner1{};
    float depth = 0.0f;
    float maxDepth = 0.0f;
    float u0 = 0.0f;
    float u1 = 0.0f;
    float v0 = 0.0f;
    float v1 = 0.0f;
    bool depthFromNumeric = false;
    Vector2 depthGrabScreen{};
    float depthAtGrab = 0.0f;

    void reset();
    void beginFromSelection(Editor& editor);
    void update(Editor& editor, const Camera3D& camera, bool uiWantsMouse, bool uiWantsKeyboard);
    void drawPreview() const;
    bool active() const { return phase != PunchPhase::Idle; }

private:
    void commit(Editor& editor);
    void handleNumeric(Editor& editor, bool uiWantsKeyboard);
    void setDepthStatus(Editor& editor) const;
    bool projectToFaceUV(Vector3 world, float& outU, float& outV) const;
};

}
