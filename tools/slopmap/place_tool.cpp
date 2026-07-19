#include "place_tool.hpp"

#include "map/prefab.hpp"

namespace slopmap {

namespace {

const char* placementIdPrefix(slopengine::PlacementKind kind) {
    switch (kind) {
    case slopengine::PlacementKind::PlayerStart:
        return "start";
    case slopengine::PlacementKind::Prop:
        return "prop";
    case slopengine::PlacementKind::Usable:
        return "usable";
    case slopengine::PlacementKind::PointLight:
        return "point-light";
    case slopengine::PlacementKind::SpotLight:
        return "spot-light";
    case slopengine::PlacementKind::AreaLight:
        return "area-light";
    case slopengine::PlacementKind::Sun:
        return "sun";
    case slopengine::PlacementKind::Prefab:
        return "prefab";
    }
    return "placement";
}

bool presentationReady(const Editor& editor, slopengine::AssetStore& assets, std::string& error) {
    const bool haveSprite = !editor.placeSpritePath.empty();
    const bool haveGeo = !editor.placeGeoPath.empty();
    if (haveSprite == haveGeo) {
        error = "Place: pick a sprite or geo in Library → Placements, then click the viewport";
        return false;
    }
    if (haveSprite && !assets.hasSprite(editor.placeSpritePath)) {
        error = "Place: missing sprite " + editor.placeSpritePath;
        return false;
    }
    if (haveGeo && !assets.hasGeo(editor.placeGeoPath)) {
        error = "Place: missing geo " + editor.placeGeoPath;
        return false;
    }
    return true;
}

} // namespace

void PlaceTool::update(
    Editor& editor,
    slopengine::AssetStore& assets,
    const Camera3D& camera,
    bool uiWantsMouse,
    bool uiWantsKeyboard) {
    (void)uiWantsKeyboard;
    if (editor.mode != EditorMode::Place) {
        return;
    }
    if (uiWantsMouse) {
        return;
    }

    if (editor.placeTarget == PlaceTarget::PrefabInstance) {
        if (editor.scene != EditorScene::Level) {
            editor.statusMessage = "Place: CSG prefabs only in Level scene";
            return;
        }
        if (editor.placePrefabPath.empty()) {
            editor.statusMessage = "Place: select a prefab in Prefabs, or a kind in Placements";
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
            editor.statusMessage = "Place: click the ground grid";
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
        editor.doc().selectedPlacement = -1;
        editor.markDirty();
        editor.rebuildPreview(assets);
        editor.mode = EditorMode::Select;
        editor.statusMessage = "Placed " + editor.doc().instances.back().id + " (" +
            editor.placePrefabPath + ") — G move, R rotate yaw";
        return;
    }

    if (!editor.placePlacementKind.has_value()) {
        editor.statusMessage = "Place: select a placement kind in Library → Placements";
        return;
    }

    const slopengine::PlacementKind kind = *editor.placePlacementKind;
    if (slopengine::placementKindNeedsPresentation(kind)) {
        std::string error;
        if (!presentationReady(editor, assets, error)) {
            editor.statusMessage = error;
            if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                return;
            }
            return;
        }
    }

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (slopengine::placementKindNeedsPresentation(kind)) {
            editor.statusMessage = std::string("Place ") + slopengine::placementKindName(kind) +
                ": click viewport (" +
                (!editor.placeSpritePath.empty() ? editor.placeSpritePath : editor.placeGeoPath) +
                ")";
        } else {
            editor.statusMessage =
                std::string("Place ") + slopengine::placementKindName(kind) + ": click viewport";
        }
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
        editor.statusMessage = "Place: click the ground grid";
        return;
    }

    slopengine::Placement placement =
        slopengine::placementKindIsLight(kind) ? slopengine::makeDefaultLightPlacement(kind)
                                               : slopengine::Placement{};
    placement.kind = kind;
    placement.id = editor.allocatePlacementId(placementIdPrefix(kind));
    placement.at = snapToGrid(hit, editor.gridSize);
    placement.haveAt = true;
    if (kind == slopengine::PlacementKind::PlayerStart) {
        placement.yaw = 3.141592653589793f;
    }
    if (slopengine::placementKindNeedsPresentation(kind)) {
        placement.sprite = editor.placeSpritePath;
        placement.geo = editor.placeGeoPath;
        placement.frame = "A";
    }

    editor.doc().placements.push_back(std::move(placement));
    editor.doc().selection = SelectionTarget::Placement;
    editor.doc().selectedPlacement = static_cast<int>(editor.doc().placements.size()) - 1;
    editor.doc().selectedBrush = -1;
    editor.doc().selectedFace = -1;
    editor.doc().selectedInstance = -1;
    editor.markDirty();
    editor.mode = EditorMode::Select;
    editor.statusMessage =
        "Placed " + editor.doc().placements.back().id + " — G move, R rotate yaw";
}

}
