#include "thing_draw.hpp"

#include "assets/icon_atlas.hpp"
#include "assets/sprite_loader.hpp"
#include "physics/components.hpp"
#include "ui/icon_ui.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace slopmap {

namespace {

Vector3 thingPosition(const slopengine::Thing& thing) {
    return thing.haveAt ? thing.at : Vector3{0.0f, 1.0f, 0.0f};
}

slopengine::CharacterMotor motorFromThing(const slopengine::Thing& thing) {
    slopengine::CharacterMotor motor{};
    if (thing.haveMotor || thing.kind == slopengine::ThingKind::Actor) {
        motor.radius = thing.motorRadius;
        motor.height = thing.motorHeight;
        motor.hull = thing.motorHull;
    }
    if (motor.radius <= 0.0f) {
        motor.radius = 0.3f;
    }
    if (motor.height <= 0.0f) {
        motor.height = 1.1f;
    }
    return motor;
}

float motorTotalHeight(const slopengine::CharacterMotor& motor) {
    return motor.height + 2.0f * motor.radius;
}

Vector3 motorColliderCenter(Vector3 feet, const slopengine::CharacterMotor& motor) {
    return {feet.x, feet.y + motorTotalHeight(motor) * 0.5f, feet.z};
}

BoundingBox motorColliderBounds(Vector3 feet, const slopengine::CharacterMotor& motor) {
    const float radius = motor.radius;
    const float totalHeight = motorTotalHeight(motor);
    return {
        {feet.x - radius, feet.y, feet.z - radius},
        {feet.x + radius, feet.y + totalHeight, feet.z + radius},
    };
}

void drawMotorCollider(Vector3 feet, const slopengine::CharacterMotor& motor, Color color) {
    const float radius = motor.radius;
    const float height = motor.height;
    if (motor.hull == slopengine::CharacterHull::Box) {
        const float totalHeight = motorTotalHeight(motor);
        const Vector3 center = motorColliderCenter(feet, motor);
        DrawCubeWires(center, radius * 2.0f, totalHeight, radius * 2.0f, color);
        return;
    }
    const Vector3 start{feet.x, feet.y + radius, feet.z};
    const Vector3 end{feet.x, feet.y + radius + height, feet.z};
    DrawCapsuleWires(start, end, radius, 8, 12, color);
}

Color kindColor(slopengine::ThingKind kind, bool selected) {
    if (selected) {
        return Color{255, 200, 80, 255};
    }
    switch (kind) {
    case slopengine::ThingKind::PlayerStart:
        return Color{80, 220, 120, 255};
    case slopengine::ThingKind::Prop:
        return Color{160, 180, 220, 255};
    case slopengine::ThingKind::Usable:
        return Color{220, 160, 80, 255};
    case slopengine::ThingKind::Pickup:
        return Color{180, 220, 100, 255};
    case slopengine::ThingKind::Actor:
        return Color{220, 100, 100, 255};
    case slopengine::ThingKind::Mover:
        return Color{220, 140, 60, 255};
    case slopengine::ThingKind::Trigger:
        return Color{80, 200, 220, 255};
    case slopengine::ThingKind::PointLight:
        return Color{255, 230, 120, 255};
    case slopengine::ThingKind::SpotLight:
        return Color{255, 180, 60, 255};
    case slopengine::ThingKind::AreaLight:
        return Color{255, 140, 200, 255};
    case slopengine::ThingKind::Sun:
        return Color{255, 255, 160, 255};
    case slopengine::ThingKind::AmbientLight:
        return Color{180, 200, 255, 255};
    case slopengine::ThingKind::Skybox:
        return Color{120, 180, 255, 255};
    case slopengine::ThingKind::Prefab:
        return Color{140, 140, 200, 255};
    case slopengine::ThingKind::SoundSource:
        return Color{120, 220, 255, 255};
    case slopengine::ThingKind::Marker:
        return Color{200, 120, 255, 255};
    case slopengine::ThingKind::Particle:
        return Color{160, 255, 180, 255};
    }
    return WHITE;
}

void drawYawArrow(Vector3 origin, float yaw, Color color) {
    const Vector3 tip = {
        origin.x + std::sin(yaw) * 0.6f,
        origin.y,
        origin.z + std::cos(yaw) * 0.6f,
    };
    DrawLine3D(origin, tip, color);
}

void drawSpriteOrGeo(
    slopengine::AssetStore& assets,
    const Camera3D& camera,
    const slopengine::Thing& thing,
    Color tint) {
    const Vector3 pos = thingPosition(thing);
    if (!thing.brush.empty()) {
        const Vector3 size = thing.haveMoverCollideSize ? thing.moverCollideSize
                                                       : Vector3{0.4f, 0.4f, 0.4f};
        DrawCubeWires(pos, size.x, size.y, size.z, tint);
        return;
    }
    if (!thing.geo.empty() && assets.hasGeo(thing.geo)) {
        Model model = assets.getGeoModel(thing.geo);
        if (model.meshCount > 0) {
            const float yawDeg = thing.yaw * RAD2DEG;
            Vector3 scale{1.0f, 1.0f, 1.0f};
            if (thing.kind == slopengine::ThingKind::Mover) {
                scale = thing.moverCollideSize;
            }
            DrawModelEx(model, pos, {0.0f, 1.0f, 0.0f}, yawDeg, scale, tint);
            return;
        }
    }
    if (!thing.sprite.empty()) {
        const slopengine::SpriteAsset* asset = assets.getSpriteAsset(thing.sprite);
        const slopengine::SpriteAtlas* atlas = assets.getSpriteAtlas(thing.sprite);
        if (asset != nullptr && atlas != nullptr && !atlas->textures.empty()) {
            const std::string frameId = thing.frame.empty() ? "A" : thing.frame;
            const slopengine::SpriteFrame* frame = slopengine::findSpriteFrame(*asset, frameId);
            if (frame != nullptr) {
                const slopengine::SpriteRotation* selected = nullptr;
                for (int i = 0; i < slopengine::kSpriteRotationCount; ++i) {
                    if (frame->rotations[i].has_value()) {
                        selected = &(*frame->rotations[i]);
                        break;
                    }
                }
                if (selected != nullptr) {
                    const auto rectIt = atlas->rects.find(selected->texturePath);
                    if (rectIt != atlas->rects.end()) {
                        const slopengine::SpriteAtlasRect& rect = rectIt->second;
                        if (rect.atlasIndex >= 0 &&
                            rect.atlasIndex < static_cast<int>(atlas->textures.size())) {
                            const Texture2D& tex =
                                atlas->textures[static_cast<std::size_t>(rect.atlasIndex)];
                            const float ppm =
                                asset->pixelsPerMeter > 0.0f ? asset->pixelsPerMeter : 64.0f;
                            const float pixelW = selected->pixelWidth > 0
                                ? static_cast<float>(selected->pixelWidth)
                                : std::fabs(rect.source.width);
                            const float pixelH = selected->pixelHeight > 0
                                ? static_cast<float>(selected->pixelHeight)
                                : std::fabs(rect.source.height);
                            const float width = pixelW / ppm;
                            const float height = pixelH / ppm;
                            const Vector3 center = {pos.x, pos.y + height * 0.5f, pos.z};
                            DrawBillboardRec(
                                camera,
                                tex,
                                rect.source,
                                center,
                                {width, height},
                                tint);
                            return;
                        }
                    }
                }
            }
        }
    }
    DrawSphere(pos, 0.15f, tint);
}

Color lightColor(const slopengine::Thing& thing, bool selected) {
    const float r = std::clamp(thing.color.x, 0.0f, 1.0f);
    const float g = std::clamp(thing.color.y, 0.0f, 1.0f);
    const float b = std::clamp(thing.color.z, 0.0f, 1.0f);
    Color color = {
        static_cast<unsigned char>(r * 255.0f),
        static_cast<unsigned char>(g * 255.0f),
        static_cast<unsigned char>(b * 255.0f),
        255,
    };
    if (selected) {
        color.r = static_cast<unsigned char>(std::min(255, static_cast<int>(color.r) + 40));
        color.g = static_cast<unsigned char>(std::min(255, static_cast<int>(color.g) + 40));
        color.b = static_cast<unsigned char>(std::min(255, static_cast<int>(color.b) + 40));
    }
    return color;
}

void drawLightGizmo(
    slopengine::AssetStore& assets,
    const Camera3D& camera,
    const slopengine::Thing& thing,
    bool selected,
    bool showGizmos) {
    const Vector3 pos = thingPosition(thing);
    const Color color = lightColor(thing, selected);
    const char* iconId =
        thing.kind == slopengine::ThingKind::AmbientLight ? "weather_sun" : "lightbulb";
    if (!drawBillboardIcon(assets, camera, pos, iconId, color)) {
        DrawSphere(pos, 0.12f, color);
    }
    if (!showGizmos) {
        return;
    }
    switch (thing.kind) {
    case slopengine::ThingKind::PointLight: {
        const float radius = std::max(thing.range, 0.01f);
        DrawSphereWires(pos, radius, 8, 8, color);
        break;
    }
    case slopengine::ThingKind::SpotLight: {
        drawYawArrow(pos, thing.yaw, color);
        const float reach = std::max(thing.range, 0.01f);
        const Vector3 tip = {
            pos.x + std::sin(thing.yaw) * reach,
            pos.y,
            pos.z + std::cos(thing.yaw) * reach,
        };
        DrawLine3D(pos, tip, color);
        DrawSphereWires(pos, reach, 8, 8, Fade(color, 0.55f));
        break;
    }
    case slopengine::ThingKind::AreaLight: {
        const float hx = thing.size.x * 0.5f;
        const float hz = thing.size.y * 0.5f;
        DrawLine3D({pos.x - hx, pos.y, pos.z - hz}, {pos.x + hx, pos.y, pos.z - hz}, color);
        DrawLine3D({pos.x + hx, pos.y, pos.z - hz}, {pos.x + hx, pos.y, pos.z + hz}, color);
        DrawLine3D({pos.x + hx, pos.y, pos.z + hz}, {pos.x - hx, pos.y, pos.z + hz}, color);
        DrawLine3D({pos.x - hx, pos.y, pos.z + hz}, {pos.x - hx, pos.y, pos.z - hz}, color);
        break;
    }
    case slopengine::ThingKind::AmbientLight: {
        if (showGizmos) {
            DrawSphereWires(pos, 0.45f, 8, 8, Fade(color, 0.55f));
        }
        break;
    }
    case slopengine::ThingKind::Sun: {
        const float yaw = thing.haveAngles ? thing.angles.y : thing.yaw;
        const float pitch = thing.haveAngles ? thing.angles.x : -0.7f;
        const Vector3 dir = {
            std::sin(yaw) * std::cos(pitch),
            -std::sin(pitch),
            std::cos(yaw) * std::cos(pitch),
        };
        DrawLine3D(pos, {pos.x + dir.x * 1.2f, pos.y + dir.y * 1.2f, pos.z + dir.z * 1.2f}, color);
        break;
    }
    default:
        break;
    }
}

} // namespace

bool drawBillboardIcon(
    slopengine::AssetStore& assets,
    const Camera3D& camera,
    Vector3 pos,
    const char* iconId,
    Color tint,
    float size) {
    const slopengine::IconAtlas* atlas = assets.getIconAtlas(slopengine::kDefaultIconSet);
    if (atlas == nullptr || atlas->texture.id == 0) {
        return false;
    }
    const auto rect = slopengine::findIconRect(*atlas, iconId);
    if (!rect.has_value()) {
        return false;
    }
    DrawBillboardRec(camera, atlas->texture, *rect, pos, {size, size}, tint);
    return true;
}

void drawThings(
    slopengine::AssetStore& assets,
    const std::vector<slopengine::Thing>& things,
    const std::vector<int>& selectedThings,
    const Camera3D& camera,
    bool showGizmos) {
    for (std::size_t i = 0; i < things.size(); ++i) {
        const slopengine::Thing& thing = things[i];
        const bool selected =
            std::find(selectedThings.begin(), selectedThings.end(), static_cast<int>(i)) !=
            selectedThings.end();
        const Color color = kindColor(thing.kind, selected);
        const Vector3 pos = thingPosition(thing);

        switch (thing.kind) {
        case slopengine::ThingKind::PlayerStart: {
            const slopengine::CharacterMotor motor{};
            drawMotorCollider(pos, motor, color);
            const Vector3 iconPos = motorColliderCenter(pos, motor);
            if (!drawBillboardIcon(assets, camera, iconPos, "user", color)) {
                DrawCube(iconPos, 0.25f, 0.5f, 0.25f, color);
            }
            if (showGizmos) {
                drawYawArrow(pos, thing.yaw, color);
            }
            break;
        }
        case slopengine::ThingKind::Actor:
            drawSpriteOrGeo(assets, camera, thing, WHITE);
            if (thing.haveMotor) {
                drawMotorCollider(pos, motorFromThing(thing), color);
            }
            if (showGizmos && selected) {
                DrawSphereWires(pos, 0.35f, 6, 6, color);
            }
            break;
        case slopengine::ThingKind::Prop:
        case slopengine::ThingKind::Usable:
        case slopengine::ThingKind::Mover:
            drawSpriteOrGeo(assets, camera, thing, WHITE);
            if (showGizmos && selected) {
                DrawSphereWires(pos, 0.35f, 6, 6, color);
            }
            break;
        case slopengine::ThingKind::Pickup:
            drawSpriteOrGeo(assets, camera, thing, WHITE);
            if (showGizmos && (!thing.onEnter.empty() || thing.haveTriggerSize)) {
                const Vector3 size =
                    thing.haveTriggerSize ? thing.triggerSize : Vector3{1.0f, 1.0f, 1.0f};
                DrawCubeWires(pos, size.x, size.y, size.z, color);
            }
            if (showGizmos && selected) {
                DrawSphereWires(pos, 0.35f, 6, 6, color);
            }
            break;
        case slopengine::ThingKind::Trigger: {
            if (showGizmos) {
                const Vector3 size =
                    thing.haveTriggerSize ? thing.triggerSize : Vector3{1.0f, 1.0f, 1.0f};
                DrawCubeWires(pos, size.x, size.y, size.z, color);
            } else if (!drawBillboardIcon(assets, camera, pos, "shape_square", color)) {
                DrawSphere(pos, 0.12f, color);
            }
            break;
        }
        case slopengine::ThingKind::PointLight:
        case slopengine::ThingKind::SpotLight:
        case slopengine::ThingKind::AreaLight:
        case slopengine::ThingKind::Sun:
        case slopengine::ThingKind::AmbientLight:
            drawLightGizmo(assets, camera, thing, selected, showGizmos);
            break;
        case slopengine::ThingKind::Skybox: {
            if (!drawBillboardIcon(assets, camera, pos, "image", color)) {
                DrawCubeWires(pos, 0.5f, 0.5f, 0.5f, color);
            }
            break;
        }
        case slopengine::ThingKind::Prefab:
            DrawCubeWires(pos, 0.4f, 0.4f, 0.4f, color);
            break;
        case slopengine::ThingKind::SoundSource: {
            if (!drawBillboardIcon(assets, camera, pos, "sound", color)) {
                DrawSphere(pos, 0.18f, color);
            }
            if (showGizmos) {
                const float radius = std::max(thing.maxDistance, 0.01f);
                DrawSphereWires(pos, radius, 8, 8, color);
                if (thing.minDistance > 0.01f && thing.minDistance < radius) {
                    DrawSphereWires(pos, thing.minDistance, 6, 6, Fade(color, 0.45f));
                }
            }
            break;
        }
        case slopengine::ThingKind::Marker: {
            DrawSphere(pos, 0.12f, color);
            if (showGizmos) {
                DrawLine3D(
                    Vector3{pos.x - 0.25f, pos.y, pos.z},
                    Vector3{pos.x + 0.25f, pos.y, pos.z},
                    color);
                DrawLine3D(
                    Vector3{pos.x, pos.y - 0.25f, pos.z},
                    Vector3{pos.x, pos.y + 0.25f, pos.z},
                    color);
                DrawLine3D(
                    Vector3{pos.x, pos.y, pos.z - 0.25f},
                    Vector3{pos.x, pos.y, pos.z + 0.25f},
                    color);
                drawYawArrow(pos, thing.yaw, color);
            }
            break;
        }
        case slopengine::ThingKind::Particle: {
            if (!drawBillboardIcon(assets, camera, pos, "weather_clouds", color)) {
                DrawSphere(pos, 0.14f, color);
            }
            if (showGizmos) {
                DrawSphereWires(pos, 0.35f, 8, 8, Fade(color, 0.55f));
                drawYawArrow(pos, thing.yaw, color);
            }
            break;
        }
        }
    }
}

std::optional<int> pickThing(
    const std::vector<slopengine::Thing>& things,
    Ray ray,
    float* outDistance) {
    float bestT = std::numeric_limits<float>::max();
    int best = -1;
    for (std::size_t i = 0; i < things.size(); ++i) {
        const Vector3 pos = thingPosition(things[i]);
        BoundingBox box = {
            {pos.x - 0.35f, pos.y - 0.1f, pos.z - 0.35f},
            {pos.x + 0.35f, pos.y + 1.0f, pos.z + 0.35f},
        };
        if (things[i].kind == slopengine::ThingKind::PlayerStart) {
            box = motorColliderBounds(pos, {});
        } else if (things[i].kind == slopengine::ThingKind::Actor && things[i].haveMotor) {
            box = motorColliderBounds(pos, motorFromThing(things[i]));
        }
        const RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < bestT) {
            bestT = hit.distance;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) {
        return std::nullopt;
    }
    if (outDistance != nullptr) {
        *outDistance = bestT;
    }
    return best;
}

}
