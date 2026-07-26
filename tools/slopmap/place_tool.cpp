#include "place_tool.hpp"

#include "map/prefab.hpp"
#include "map/thing_def_registry.hpp"

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
    case slopengine::ThingKind::Pickup:
        return "pickup";
    case slopengine::ThingKind::Actor:
        return "actor";
    case slopengine::ThingKind::Mover:
        return "mover";
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
    case slopengine::ThingKind::Marker:
        return "marker";
    }
    return "thing";
}

const char* placeKindLabel(const Editor& editor, slopengine::ThingKind kind) {
    if (kind == slopengine::ThingKind::Prop) {
        if (editor.placePresentation == PlacePresentation::Sprite) {
            return "sprite";
        }
        if (editor.placePresentation == PlacePresentation::Geo) {
            return "geo";
        }
    }
    return slopengine::thingKindName(kind);
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
            editor.placePrefabPath + ") — G move, R rotate";
        return;
    }

    if (!editor.placeThingKind.has_value()) {
        editor.statusMessage = "Place: select a thing kind in Library → Things";
        return;
    }

    const slopengine::ThingKind kind = *editor.placeThingKind;
    const char* label = placeKindLabel(editor, kind);

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (slopengine::thingKindNeedsPresentation(kind)) {
            editor.statusMessage =
                std::string("Place ") + label + ": click viewport, then set asset in Properties";
        } else {
            editor.statusMessage = std::string("Place ") + label + ": click viewport";
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
    const slopengine::ThingDef* catalogDef = nullptr;
    if (!editor.placeThingType.empty()) {
        catalogDef = slopengine::thingDefRegistry().find(editor.placeThingType);
    }
    if (catalogDef != nullptr) {
        slopengine::applyThingDef(*catalogDef, thing);
        thing.id = editor.allocateThingId(catalogDef->id.c_str());
        thing.at = snapToGrid(hit, editor.gridSize);
        thing.haveAt = true;
        editor.doc().things.push_back(std::move(thing));
        const int index = static_cast<int>(editor.doc().things.size()) - 1;
        editor.selectEntity({EntityRef::Kind::Thing, index}, false);
        editor.markDirty();
        editor.markThingCompileDirty(catalogDef->kind);
        editor.mode = EditorMode::Select;
        editor.statusMessage =
            "Placed " + editor.doc().things.back().id + " (" + catalogDef->id + ")";
        return;
    }

    if (slopengine::thingKindIsLight(kind)) {
        thing = slopengine::makeDefaultLightThing(kind);
    } else if (kind == slopengine::ThingKind::SoundSource) {
        thing = slopengine::makeDefaultSoundSourceThing();
    } else if (kind == slopengine::ThingKind::Marker) {
        thing = slopengine::makeDefaultMarkerThing();
    }
    thing.kind = kind;
    thing.id = editor.allocateThingId(thingIdPrefix(kind));
    thing.at = snapToGrid(hit, editor.gridSize);
    thing.haveAt = true;
    if (kind == slopengine::ThingKind::PlayerStart) {
        thing.yaw = 3.141592653589793f;
    }
    if (slopengine::thingKindNeedsPresentation(kind)) {
        thing.sprite.clear();
        thing.geo.clear();
        thing.frame = "A";
    }
    if (kind == slopengine::ThingKind::Actor) {
        thing.tags = {"actor"};
        thing.haveMotor = true;
    }
    if (kind == slopengine::ThingKind::Mover) {
        thing.haveMoverOpenOffset = true;
        thing.moverOpenOffset = {0.0f, 0.0f, -1.2f};
        thing.haveMoverCollideSize = true;
        thing.moverCollideSize = {1.0f, 2.2f, 0.12f};
        thing.haveMoverCollideCenter = true;
        thing.moverCollideCenter = {0.0f, 1.1f, 0.0f};
        thing.haveMoverDuration = true;
        thing.moverDuration = 0.8f;
        thing.moverBlockMode = "shove";
        thing.havePrompt = true;
        thing.prompt = "Open";
        thing.onUse = slopengine::HandlerBinding{"on-use-mover-toggle", {}};
    }
    if (kind == slopengine::ThingKind::Trigger) {
        thing.haveTriggerSize = true;
        thing.triggerSize = {1.0f, 1.0f, 1.0f};
    }
    if (kind == slopengine::ThingKind::Pickup) {
        thing.haveTriggerSize = true;
        thing.triggerSize = {1.0f, 1.5f, 1.0f};
    }

    if (kind == slopengine::ThingKind::Prop &&
        (editor.placePresentation == PlacePresentation::Sprite ||
         editor.placePresentation == PlacePresentation::Geo)) {
        editor.propChannelLock[thing.id] = editor.placePresentation;
    }
    editor.doc().things.push_back(std::move(thing));
    const int index = static_cast<int>(editor.doc().things.size()) - 1;
    editor.selectEntity({EntityRef::Kind::Thing, index}, false);
    editor.markDirty();
    editor.markThingCompileDirty(kind);
    editor.mode = EditorMode::Select;
    if (kind == slopengine::ThingKind::Prop &&
        editor.placePresentation == PlacePresentation::Sprite) {
        editor.statusMessage =
            "Placed " + editor.doc().things.back().id + " — set sprite in Properties";
    } else if (
        kind == slopengine::ThingKind::Prop &&
        editor.placePresentation == PlacePresentation::Geo) {
        editor.statusMessage =
            "Placed " + editor.doc().things.back().id + " — set geo in Properties";
    } else if (slopengine::thingKindNeedsPresentation(kind)) {
        editor.statusMessage =
            "Placed " + editor.doc().things.back().id + " — set sprite/geo in Properties";
    } else {
        editor.statusMessage =
            "Placed " + editor.doc().things.back().id + " — G move, R rotate";
    }
}

}
