#pragma once

#include "editor.hpp"

#include <raylib.h>

#include <vector>

namespace slopmap {

enum class ClipPhase {
    Idle,
    PickingP0,
    PickingP1,
    Preview,
};

enum class ClipKeepMode {
    Front,
    Back,
    Both,
};

struct ClipTool {
    ClipPhase phase = ClipPhase::Idle;
    ClipKeepMode keepMode = ClipKeepMode::Front;
    ConstructionPlane construction{};
    Vector3 point0{};
    Vector3 point1{};
    Vector3 planeNormal{};
    bool planeFlipped = false;
    std::vector<int> brushIndices;

    void reset();
    void beginFromSelection(Editor& editor);
    void update(Editor& editor, const Camera3D& camera, bool uiWantsMouse, bool uiWantsKeyboard);
    void drawPreview(const Editor& editor, Vector3 eye, float lineWidth) const;
    bool active() const { return phase != ClipPhase::Idle; }

private:
    void commit(Editor& editor);
    void refreshPlane();
    void setStatus(Editor& editor) const;
    bool hitConstruction(Editor& editor, const Camera3D& camera, Vector3& outHit) const;
    const char* keepModeLabel() const;
};

}
