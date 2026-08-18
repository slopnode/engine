#include "collider_preview.hpp"

#include "render/sprite_billboard.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cmath>

namespace slopthing {

namespace {

constexpr float kPi = 3.14159265358979323846f;

void drawBillboardQuad(const slopengine::SpriteBillboard& billboard) {
    if (billboard.texture == nullptr) {
        return;
    }
    const BlendMode blend = billboard.blend == slopengine::SpriteBlendMode::Additive
        ? BLEND_ADD_COLORS
        : BLEND_ALPHA;
    BeginBlendMode(blend);
    const Texture2D& texture = *billboard.texture;
    const Rectangle source = billboard.source;
    const float texW = static_cast<float>(texture.width);
    const float texH = static_cast<float>(texture.height);
    const Vector2 texcoords[4] = {
        {source.x / texW, (source.y + source.height) / texH},
        {(source.x + source.width) / texW, (source.y + source.height) / texH},
        {(source.x + source.width) / texW, source.y / texH},
        {source.x / texW, source.y / texH},
    };
    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < 4; ++i) {
        rlColor4ub(billboard.tint.r, billboard.tint.g, billboard.tint.b, billboard.tint.a);
        rlTexCoord2f(texcoords[i].x, texcoords[i].y);
        rlVertex3f(billboard.points[i].x, billboard.points[i].y, billboard.points[i].z);
    }
    rlEnd();
    rlSetTexture(0);
    EndBlendMode();
}

/** Picks the frame id this thing would actually render: its explicit `frame`,
 * else the first frame of its `anim` clip, else the sprite's first frame. */
std::string resolveFrameId(
    const NodePtr& alist, slopengine::AssetStore& assets, const std::string& spritePath,
    const slopengine::SpriteAsset& asset) {
    if (auto frame = getStr(alist, "frame")) {
        return *frame;
    }
    if (auto anim = getStr(alist, "anim")) {
        if (assets.hasSpriteAnim(spritePath)) {
            if (const slopengine::SpriteAnimBank* bank = assets.getSpriteAnimBank(spritePath)) {
                auto it = bank->clipIndexByName.find(*anim);
                if (it != bank->clipIndexByName.end() &&
                    !bank->clips[it->second].frames.empty()) {
                    return bank->clips[it->second].frames.front().id;
                }
            }
        }
    }
    if (!asset.frames.empty()) {
        return asset.frames.front().id;
    }
    return "";
}

Vector3 readTriggerSize(const NodePtr& alist) {
    Vector3 size{1.0f, 1.0f, 1.0f};
    NodePtr pair = alistFindPair(alist, "trigger-size");
    if (!pair) {
        return size;
    }
    const std::vector<NodePtr> items = listItems(pair->cdr);
    float xyz[3] = {1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 3 && i < static_cast<int>(items.size()); ++i) {
        const NodePtr& item = items[static_cast<std::size_t>(i)];
        if (item->kind == NodeKind::Float) {
            xyz[i] = static_cast<float>(item->floatVal);
        } else if (item->kind == NodeKind::Int) {
            xyz[i] = static_cast<float>(item->intVal);
        }
    }
    return {xyz[0], xyz[1], xyz[2]};
}

void drawMotorCollider(const NodePtr& block, Color color) {
    const float radius = static_cast<float>(getFloat(block, "radius").value_or(0.3));
    const float height = static_cast<float>(getFloat(block, "height").value_or(1.1));
    const std::string hull = getStr(block, "hull").value_or("capsule");
    if (hull == "sphere") {
        DrawSphereWires({0.0f, radius, 0.0f}, radius, 12, 16, color);
    } else {
        DrawCapsuleWires({0.0f, radius, 0.0f}, {0.0f, radius + height, 0.0f}, radius, 8, 16, color);
    }
}

void drawTriggerCollider(const NodePtr& alist, Color color) {
    const Vector3 size = readTriggerSize(alist);
    DrawCubeWiresV({0.0f, size.y * 0.5f, 0.0f}, size, color);
}

} // namespace

bool thingHasColliderPreview(const NodePtr& alist) {
    return alistHasKey(alist, "sprite") &&
        (hasBlock(alist, "motor") || alistHasKey(alist, "trigger-size"));
}

bool ensureRenderTexture(RenderTexture2D& target, int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (target.id != 0 && target.texture.width == width && target.texture.height == height) {
        return false;
    }
    if (target.id != 0) {
        UnloadRenderTexture(target);
        target = {};
    }
    target = LoadRenderTexture(width, height);
    return true;
}

Vector3 OrbitCamera::position() const {
    const float cp = std::cos(pitch);
    return {
        target.x + std::sin(yaw) * cp * distance,
        target.y + std::sin(pitch) * distance,
        target.z + std::cos(yaw) * cp * distance,
    };
}

Camera3D OrbitCamera::toRaylib() const {
    Camera3D camera{};
    camera.position = position();
    camera.target = target;
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 55.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

void OrbitCamera::update(bool allowInput) {
    if (!allowInput) {
        return;
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        const Vector2 delta = GetMouseDelta();
        yaw -= delta.x * lookSensitivity;
        pitch -= delta.y * lookSensitivity;
        pitch = std::clamp(pitch, -1.45f, 1.45f);
        while (yaw > kPi) {
            yaw -= 2.0f * kPi;
        }
        while (yaw < -kPi) {
            yaw += 2.0f * kPi;
        }
    }
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        distance = std::clamp(distance * (1.0f - wheel * zoomSensitivity), 0.5f, 40.0f);
    }
}

void ColliderPreview::draw(
    Editor& editor, slopengine::AssetStore& assets, RenderTexture2D& target, bool allowInput) {
    camera.update(allowInput);

    BeginTextureMode(target);
    ClearBackground(Color{22, 24, 28, 255});

    const Camera3D rayCam = camera.toRaylib();
    BeginMode3D(rayCam);
    DrawGrid(20, 1.0f);
    DrawLine3D({0.0f, 0.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, RED);
    DrawLine3D({0.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}, GREEN);
    DrawLine3D({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.5f}, BLUE);

    const ThingEntry* thing = editor.selected();
    if (thing != nullptr) {
        const NodePtr& alist = thing->alist;

        if (auto spritePath = getStr(alist, "sprite")) {
            const slopengine::SpriteAsset* asset = assets.getSpriteAsset(*spritePath);
            const slopengine::SpriteAtlas* atlas = assets.getSpriteAtlas(*spritePath);
            if (asset != nullptr && atlas != nullptr) {
                const std::string frameId = resolveFrameId(alist, assets, *spritePath, *asset);
                if (!frameId.empty()) {
                    slopengine::GlobalTransformation global{};
                    const auto billboard = slopengine::resolveSpriteBillboard(
                        *asset,
                        *atlas,
                        frameId,
                        0.0f,
                        global,
                        rayCam.position,
                        slopengine::horizontalCameraYaw(rayCam.position, rayCam.target));
                    if (billboard && billboard->texture != nullptr) {
                        drawBillboardQuad(*billboard);
                    }
                }
            }
        }

        if (hasBlock(alist, "motor")) {
            drawMotorCollider(blockAlist(alist, "motor"), Color{255, 170, 60, 255});
        }
        if (alistHasKey(alist, "trigger-size")) {
            drawTriggerCollider(alist, Color{70, 200, 255, 255});
        }
    }

    EndMode3D();
    EndTextureMode();
}

}
