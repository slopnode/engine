#include "placement_draw.hpp"

#include "assets/sprite_loader.hpp"

#include <cmath>
#include <limits>
#include <optional>

namespace slopmap {

namespace {

Vector3 placementPosition(const slopengine::Placement& placement) {
    return placement.haveAt ? placement.at : Vector3{0.0f, 1.0f, 0.0f};
}

Color kindColor(slopengine::PlacementKind kind, bool selected) {
    if (selected) {
        return Color{255, 200, 80, 255};
    }
    switch (kind) {
    case slopengine::PlacementKind::PlayerStart:
        return Color{80, 220, 120, 255};
    case slopengine::PlacementKind::Prop:
        return Color{160, 180, 220, 255};
    case slopengine::PlacementKind::Usable:
        return Color{220, 160, 80, 255};
    case slopengine::PlacementKind::PointLight:
        return Color{255, 230, 120, 255};
    case slopengine::PlacementKind::SpotLight:
        return Color{255, 180, 60, 255};
    case slopengine::PlacementKind::AreaLight:
        return Color{255, 140, 200, 255};
    case slopengine::PlacementKind::Sun:
        return Color{255, 255, 160, 255};
    case slopengine::PlacementKind::Prefab:
        return Color{140, 140, 200, 255};
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
    const slopengine::Placement& placement,
    Color tint) {
    const Vector3 pos = placementPosition(placement);
    if (!placement.geo.empty() && assets.hasGeo(placement.geo)) {
        Model model = assets.getGeoModel(placement.geo);
        const float yawDeg = placement.yaw * RAD2DEG;
        DrawModelEx(model, pos, {0.0f, 1.0f, 0.0f}, yawDeg, {1.0f, 1.0f, 1.0f}, tint);
        return;
    }
    if (!placement.sprite.empty()) {
        const slopengine::SpriteAsset* asset = assets.getSpriteAsset(placement.sprite);
        const slopengine::SpriteAtlas* atlas = assets.getSpriteAtlas(placement.sprite);
        if (asset != nullptr && atlas != nullptr && !atlas->textures.empty()) {
            const std::string frameId = placement.frame.empty() ? "A" : placement.frame;
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

void drawLightGizmo(const slopengine::Placement& placement, Color color) {
    const Vector3 pos = placementPosition(placement);
    DrawSphere(pos, 0.12f, color);
    switch (placement.kind) {
    case slopengine::PlacementKind::PointLight:
        DrawCircle3D(pos, std::min(placement.range, 2.0f), {0.0f, 1.0f, 0.0f}, 90.0f, color);
        break;
    case slopengine::PlacementKind::SpotLight: {
        drawYawArrow(pos, placement.yaw, color);
        const float reach = std::min(placement.range, 2.0f);
        const Vector3 tip = {
            pos.x + std::sin(placement.yaw) * reach,
            pos.y,
            pos.z + std::cos(placement.yaw) * reach,
        };
        DrawLine3D(pos, tip, color);
        break;
    }
    case slopengine::PlacementKind::AreaLight: {
        const float hx = placement.size.x * 0.5f;
        const float hz = placement.size.y * 0.5f;
        DrawLine3D({pos.x - hx, pos.y, pos.z - hz}, {pos.x + hx, pos.y, pos.z - hz}, color);
        DrawLine3D({pos.x + hx, pos.y, pos.z - hz}, {pos.x + hx, pos.y, pos.z + hz}, color);
        DrawLine3D({pos.x + hx, pos.y, pos.z + hz}, {pos.x - hx, pos.y, pos.z + hz}, color);
        DrawLine3D({pos.x - hx, pos.y, pos.z + hz}, {pos.x - hx, pos.y, pos.z - hz}, color);
        break;
    }
    case slopengine::PlacementKind::Sun: {
        const float yaw = placement.haveAngles ? placement.angles.y : placement.yaw;
        const float pitch = placement.haveAngles ? placement.angles.x : -0.7f;
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

void drawPlacements(
    slopengine::AssetStore& assets,
    const std::vector<slopengine::Placement>& placements,
    int selectedPlacement,
    const Camera3D& camera) {
    for (std::size_t i = 0; i < placements.size(); ++i) {
        const slopengine::Placement& placement = placements[i];
        const bool selected = static_cast<int>(i) == selectedPlacement;
        const Color color = kindColor(placement.kind, selected);
        const Vector3 pos = placementPosition(placement);

        switch (placement.kind) {
        case slopengine::PlacementKind::PlayerStart:
            DrawCube(pos, 0.25f, 0.5f, 0.25f, color);
            drawYawArrow(pos, placement.yaw, color);
            break;
        case slopengine::PlacementKind::Prop:
        case slopengine::PlacementKind::Usable:
            drawSpriteOrGeo(assets, camera, placement, color);
            if (selected) {
                DrawSphereWires(pos, 0.35f, 6, 6, color);
            }
            break;
        case slopengine::PlacementKind::PointLight:
        case slopengine::PlacementKind::SpotLight:
        case slopengine::PlacementKind::AreaLight:
        case slopengine::PlacementKind::Sun:
            drawLightGizmo(placement, color);
            break;
        case slopengine::PlacementKind::Prefab:
            DrawCubeWires(pos, 0.4f, 0.4f, 0.4f, color);
            break;
        }
    }
}

std::optional<int> pickPlacement(
    const std::vector<slopengine::Placement>& placements,
    Ray ray,
    float* outDistance) {
    float bestT = std::numeric_limits<float>::max();
    int best = -1;
    for (std::size_t i = 0; i < placements.size(); ++i) {
        const Vector3 pos = placementPosition(placements[i]);
        const BoundingBox box = {
            {pos.x - 0.35f, pos.y - 0.1f, pos.z - 0.35f},
            {pos.x + 0.35f, pos.y + 1.0f, pos.z + 0.35f},
        };
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
