#include "place_tool.hpp"

#include "map/prefab.hpp"
#include "map/thing_def_registry.hpp"
#include "preview.hpp"

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
    case slopengine::ThingKind::AmbientLight:
        return "ambient-light";
    case slopengine::ThingKind::DynamicPointLight:
        return "dynamic-point-light";
    case slopengine::ThingKind::DynamicSpotLight:
        return "dynamic-spot-light";
    case slopengine::ThingKind::Skybox:
        return "skybox";
    case slopengine::ThingKind::Prefab:
        return "prefab";
    case slopengine::ThingKind::SoundSource:
        return "sound-source";
    case slopengine::ThingKind::Marker:
        return "marker";
    case slopengine::ThingKind::Particle:
        return "particle";
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

void PlaceTool::resetHover() {
    hoverValid = false;
    hoverPlane = {};
    hoverPoint = {};
}

bool PlaceTool::needsPosition(const Editor& editor) const {
    if (editor.mode != EditorMode::Place) {
        return false;
    }
    if (editor.placeTarget == PlaceTarget::PrefabInstance) {
        return true;
    }
    if (!editor.placeThingKind.has_value()) {
        return false;
    }
    const slopengine::ThingKind kind = *editor.placeThingKind;
    return kind != slopengine::ThingKind::Skybox && kind != slopengine::ThingKind::Sun &&
        kind != slopengine::ThingKind::AmbientLight;
}

void PlaceTool::updateHover(Editor& editor, const Camera3D& camera) {
    hoverValid = false;
    if (!needsPosition(editor)) {
        return;
    }
    const Ray ray = mouseRay(camera, editor.contentViewport);
    if (pickConstructionPlane(editor, ray, hoverPlane, hoverPoint)) {
        hoverValid = true;
    }
}

void PlaceTool::drawPreview(Vector3 eye, float lineWidth) const {
    if (!hoverValid) {
        return;
    }
    const float normalStub = std::max(0.25f, lineWidth * 14.0f);
    drawConstructionPlaneGizmo(
        hoverPoint,
        hoverPlane.axisU,
        hoverPlane.axisV,
        hoverPlane.normal,
        eye,
        lineWidth,
        normalStub);
}

void PlaceTool::update(
    Editor& editor,
    slopengine::AssetStore& assets,
    const Camera3D& camera,
    bool uiWantsMouse,
    bool uiWantsKeyboard) {
    (void)uiWantsKeyboard;
    if (editor.mode != EditorMode::Place) {
        resetHover();
        return;
    }

    if (!uiWantsMouse) {
        updateHover(editor, camera);
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
        if (!assets.hasPrefabSource(editor.placePrefabPath)) {
            editor.statusMessage = "Place: prefab not found: " + editor.placePrefabPath;
            return;
        }

        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (hoverValid) {
                editor.statusMessage = "Place prefab: click to confirm";
            } else {
                editor.statusMessage = "Place prefab: aim at the grid or a brush face";
            }
            return;
        }

        if (!hoverValid) {
            editor.statusMessage = "Place: aim at the grid or a brush face";
            return;
        }

        editor.prepareEdit();
        slopengine::PrefabInstance instance;
        instance.path = editor.placePrefabPath;
        instance.id = editor.allocatePrefabId();
        instance.at = hoverPoint;
        instance.angles = {};
        editor.doc().instances.push_back(std::move(instance));
        const int index = static_cast<int>(editor.doc().instances.size()) - 1;
        editor.selectEntity({EntityRef::Kind::Instance, index}, false);
        editor.markDirty();
        editor.markBspDirty();
        editor.rebuildPreview(assets);
        editor.endEdit();
        editor.mode = EditorMode::Select;
        resetHover();
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
    const bool positioned = needsPosition(editor);

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (positioned) {
            if (hoverValid) {
                editor.statusMessage = std::string("Place ") + label + ": click to confirm";
            } else {
                editor.statusMessage = std::string("Place ") + label + ": aim at the grid or a brush face";
            }
        } else if (slopengine::thingKindNeedsPresentation(kind)) {
            editor.statusMessage =
                std::string("Place ") + label + ": click viewport, then set asset in Properties";
        } else {
            editor.statusMessage = std::string("Place ") + label + ": click viewport";
        }
        return;
    }

    Vector3 hit{};
    if (positioned) {
        if (!hoverValid) {
            editor.statusMessage = "Place: aim at the grid or a brush face";
            return;
        }
        hit = hoverPoint;
    } else {
        const ConstructionPlane plane = constructionPlaneForView(editor.viewPlane, editor.gridPlane);
        if (!rayPlaneIntersection(
                mouseRay(camera, editor.contentViewport),
                plane.origin,
                plane.normal,
                hit)) {
            editor.statusMessage = "Place: click the viewport";
            return;
        }
        hit = snapOnConstructionPlane(hit, plane, editor.gridSize);
    }

    slopengine::Thing thing{};
    const slopengine::ThingDef* catalogDef = nullptr;
    if (!editor.placeThingType.empty()) {
        catalogDef = slopengine::thingDefRegistry().find(editor.placeThingType);
    }
    if (catalogDef != nullptr) {
        editor.prepareEdit();
        slopengine::applyThingDef(*catalogDef, thing);
        thing.id = editor.allocateThingId(catalogDef->id.c_str());
        thing.at = hit;
        thing.haveAt = true;
        editor.doc().things.push_back(std::move(thing));
        const int index = static_cast<int>(editor.doc().things.size()) - 1;
        editor.selectEntity({EntityRef::Kind::Thing, index}, false);
        editor.markDirty();
        editor.markThingCompileDirty(catalogDef->kind);
        editor.endEdit();
        editor.mode = EditorMode::Select;
        resetHover();
        editor.statusMessage =
            "Placed " + editor.doc().things.back().id + " (" + catalogDef->id + ")";
        return;
    }

    editor.prepareEdit();
    if (slopengine::thingKindIsLight(kind)) {
        thing = slopengine::makeDefaultLightThing(kind);
        if (kind == slopengine::ThingKind::AmbientLight) {
            thing.color = {0.08f, 0.08f, 0.09f};
        } else if (kind == slopengine::ThingKind::Sun) {
            thing.haveAngles = true;
            thing.angles = {-0.7f, 0.4f, 0.0f};
            thing.yaw = thing.angles.y;
        }
    } else if (kind == slopengine::ThingKind::SoundSource) {
        thing = slopengine::makeDefaultSoundSourceThing();
    } else if (kind == slopengine::ThingKind::Marker) {
        thing = slopengine::makeDefaultMarkerThing();
    } else if (kind == slopengine::ThingKind::Particle) {
        thing = slopengine::makeDefaultParticleThing();
    } else if (kind == slopengine::ThingKind::Skybox) {
        thing = slopengine::makeDefaultSkyboxThing();
    }
    thing.kind = kind;
    thing.id = editor.allocateThingId(thingIdPrefix(kind));
    if (kind == slopengine::ThingKind::Skybox || kind == slopengine::ThingKind::Sun ||
        kind == slopengine::ThingKind::AmbientLight) {
        thing.haveAt = false;
    } else {
        thing.at = hit;
        thing.haveAt = true;
    }
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
    editor.endEdit();
    editor.mode = EditorMode::Select;
    resetHover();
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
