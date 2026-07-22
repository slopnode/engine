#include "place_tool.hpp"

#include "map/prefab.hpp"

namespace slopmap {

namespace {

const char* thingIdPrefix(slopengine::ThingKind kind) {
    switch (kind) {
    case slopengine::ThingKind::PlayerStart:
        return "start";
    case slopengine::ThingKind::Prop:
        return "prop";
    case slopengine::ThingKind::Usable:
        return "usable";
    case slopengine::ThingKind::Actor:
        return "actor";
    case slopengine::ThingKind::Trigger:
        return "trigger";
    case slopengine::ThingKind::PointLight:
        return "point-light";
    case slopengine::ThingKind::SpotLight:
        return "spot-light";
    case slopengine::ThingKind::AreaLight:
        return "area-light";
    case slopengine::ThingKind::Sun:
        return "sun";
    case slopengine::ThingKind::Prefab:
        return "prefab";
    case slopengine::ThingKind::SoundSource:
        return "sound-source";
    }
    return "thing";
}

bool presentationReady(const Editor& editor, slopengine::AssetStore& assets, std::string& error) {
    const bool haveSprite = !editor.placeSpritePath.empty();
    const bool haveGeo = !editor.placeGeoPath.empty();
    if (haveSprite == haveGeo) {
        error = "Place: pick a sprite or geo in Library → Things, then click the viewport";
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
            editor.statusMessage = "Place: select a prefab in Prefabs, or a kind in Things";
            return;
        }
        if (!assets.hasPrefabCsg(editor.placePrefabPath)) {
            editor.statusMessage = "Place: prefab not found: " + editor.placePrefabPath;
            return;
        }

        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            return;
        }

        const ConstructionPlane plane = constructionPlaneForView(editor.viewPlane, editor.gridPlane);
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
        const int index = static_cast<int>(editor.doc().instances.size()) - 1;
        editor.selectEntity({EntityRef::Kind::Instance, index}, false);
        editor.markDirty();
        editor.markBspDirty();
        editor.rebuildPreview(assets);
        editor.mode = EditorMode::Select;
        editor.statusMessage = "Placed " + editor.doc().instances.back().id + " (" +
            editor.placePrefabPath + ") — G move, R rotate yaw";
        return;
    }

    if (!editor.placeThingKind.has_value()) {
        editor.statusMessage = "Place: select a thing kind in Library → Things";
        return;
    }

    const slopengine::ThingKind kind = *editor.placeThingKind;
    if (slopengine::thingKindNeedsPresentation(kind)) {
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
        if (slopengine::thingKindNeedsPresentation(kind)) {
            editor.statusMessage = std::string("Place ") + slopengine::thingKindName(kind) +
                ": click viewport (" +
                (!editor.placeSpritePath.empty() ? editor.placeSpritePath : editor.placeGeoPath) +
                ")";
        } else {
            editor.statusMessage =
                std::string("Place ") + slopengine::thingKindName(kind) + ": click viewport";
        }
        return;
    }

    const ConstructionPlane plane = constructionPlaneForView(editor.viewPlane, editor.gridPlane);
    Vector3 hit{};
    if (!rayPlaneIntersection(
            mouseRay(camera, editor.contentViewport),
            plane.origin,
            plane.normal,
            hit)) {
        editor.statusMessage = "Place: click the ground grid";
        return;
    }

    slopengine::Thing thing{};
    if (slopengine::thingKindIsLight(kind)) {
        thing = slopengine::makeDefaultLightThing(kind);
    } else if (kind == slopengine::ThingKind::SoundSource) {
        thing = slopengine::makeDefaultSoundSourceThing();
    }
    thing.kind = kind;
    thing.id = editor.allocateThingId(thingIdPrefix(kind));
    thing.at = snapToGrid(hit, editor.gridSize);
    thing.haveAt = true;
    if (kind == slopengine::ThingKind::PlayerStart) {
        thing.yaw = 3.141592653589793f;
    }
    if (slopengine::thingKindNeedsPresentation(kind)) {
        thing.sprite = editor.placeSpritePath;
        thing.geo = editor.placeGeoPath;
        thing.frame = "A";
    }
    if (kind == slopengine::ThingKind::Actor) {
        thing.tags = {"actor"};
        thing.haveMotor = true;
    }

    editor.doc().things.push_back(std::move(thing));
    const int index = static_cast<int>(editor.doc().things.size()) - 1;
    editor.selectEntity({EntityRef::Kind::Thing, index}, false);
    editor.markDirty();
    editor.markThingCompileDirty(kind);
    editor.mode = EditorMode::Select;
    editor.statusMessage =
        "Placed " + editor.doc().things.back().id + " — G move, R rotate yaw";
}

}
