#pragma once

#include "editor.hpp"

#include <raylib.h>

#include <cstddef>

namespace slopmap {

enum class CreatePhase {
    Idle,
    DrawingBase,
    Extruding,
    AwaitingParams,
};

struct CreateTool {
    CreatePhase phase = CreatePhase::Idle;
    ConstructionPlane plane{};
    Vector3 corner0{};
    Vector3 corner1{};
    float thickness = 0.0f;
    bool thicknessFromNumeric = false;
    Vector2 thicknessGrabScreen{};
    float thicknessAtGrab = 0.0f;
    bool hoverValid = false;
    Vector3 pendingMins{};
    Vector3 pendingMaxs{};

    void reset();
    void update(Editor& editor, const Camera3D& camera, bool uiWantsMouse, bool uiWantsKeyboard);
    void drawPreview(Vector3 eye, float lineWidth) const;
    bool active() const {
        return phase != CreatePhase::Idle && phase != CreatePhase::AwaitingParams;
    }
    void commitPending(Editor& editor);
    void setStatus(Editor& editor) const;
    bool formatCreateMetrics(char* buf, std::size_t bufSize) const;

private:
    bool footprintBounds(Vector3& mins, Vector3& maxs) const;
    bool finalBounds(Vector3& mins, Vector3& maxs) const;
    void beginCommit(Editor& editor);
    void handleNumeric(Editor& editor, bool uiWantsKeyboard);
};

}
