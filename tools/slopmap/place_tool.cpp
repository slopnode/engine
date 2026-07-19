#include "place_tool.hpp"

#include "map/prefab.hpp"

namespace slopmap {

void PlaceTool::update(
    Editor& editor,
    slopengine::AssetStore& assets,
    const Camera3D& camera,
    bool uiWantsMouse,
    bool uiWantsKeyboard) {
    (void)uiWantsKeyboard;
    if (editor.mode != EditorMode::Place || editor.scene != EditorScene::Level) {
        return;
    }
    if (uiWantsMouse) {
        return;
    }
    if (editor.placePrefabPath.empty()) {
        editor.statusMessage = "Place: select a prefab in the Prefabs panel";
        return;
    }
    if (!assets.hasPrefabCsg(editor.placePrefabPath)) {
        editor.statusMessage = "Place: prefab not found: " + editor.placePrefabPath;
        return;
    }

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return;
    }

    const ConstructionPlane plane = constructionPlaneForView(
        editor.viewPlane == ViewPlane::PerspectiveY0 ? ViewPlane::Top : editor.viewPlane);
    Vector3 hit{};
    if (!rayPlaneIntersection(
            mouseRay(camera, editor.contentViewport),
            plane.origin,
            plane.normal,
            hit)) {
        return;
    }

    slopengine::PrefabInstance instance;
    instance.path = editor.placePrefabPath;
    instance.id = editor.allocatePrefabId();
    instance.at = snapToGrid(hit, editor.gridSize);
    instance.angles = {};
    editor.doc().instances.push_back(std::move(instance));
    editor.doc().selection = SelectionTarget::Instance;
    editor.doc().selectedInstance = static_cast<int>(editor.doc().instances.size()) - 1;
    editor.doc().selectedBrush = -1;
    editor.doc().selectedFace = -1;
    editor.markDirty();
    editor.rebuildPreview(assets);
    editor.mode = EditorMode::Select;
    editor.statusMessage = "Placed " + editor.doc().instances.back().id + " (" +
        editor.placePrefabPath + ") — G move, R rotate yaw";
}

}
