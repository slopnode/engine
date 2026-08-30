#pragma once

#include "editor.hpp"
#include "map/brush.hpp"

#include <raylib.h>

#include <string>
#include <vector>

namespace slopmap {

enum class ExtrudePhase {
    Idle,
    Dragging,
};

enum class ExtrudeSweepMode {
    Normal,
    Profile,
};

/** New brush glued onto the source vs. reshaping the source brush(es) in place
 *  (the selected face's own vertices move, propagated to every other face of
 *  that brush sharing a corner — a push/pull edit, not an addition). */
enum class ExtrudeTargetMode {
    NewBrush,
    InPlace,
};

struct ExtrudeTool {
    /** One merged coplanar boundary loop and everything needed to sweep it. */
    struct Group {
        std::vector<Vector3> polygon; /**< Outward winding, matching planeNormal. */
        std::vector<Vector3> normalDirs; /**< planeNormal repeated per vertex. */
        std::vector<Vector3> profileDirs; /**< Per-vertex taper direction; valid only if profileAvailable. */
        bool profileAvailable = false;
        /** Per polygon vertex, every selected brush that owns a vertex at that
         *  position — used by InPlace mode to reshape each source brush. */
        std::vector<std::vector<int>> vertexOwnerBrushes;
    };

    ExtrudePhase phase = ExtrudePhase::Idle;
    ExtrudeSweepMode sweepMode = ExtrudeSweepMode::Normal;
    ExtrudeTargetMode targetMode = ExtrudeTargetMode::NewBrush;
    std::vector<Group> groups;
    Vector3 planeNormal{};
    Vector3 dragOrigin{};
    std::string material;
    slopengine::BrushRole role = slopengine::BrushRole::Hull;
    float depth = 0.0f;
    Vector2 depthGrabScreen{};
    float depthAtGrab = 0.0f;
    bool depthFromNumeric = false;

    void reset();
    void beginFromSelection(Editor& editor);
    void update(Editor& editor, const Camera3D& camera, bool uiWantsMouse, bool uiWantsKeyboard);
    void drawPreview(const Editor& editor, Vector3 eye, float lineWidth) const;
    bool active() const { return phase != ExtrudePhase::Idle; }

private:
    void commit(Editor& editor);
    void handleNumeric(Editor& editor, bool uiWantsKeyboard);
    void setStatus(Editor& editor) const;
};

}
