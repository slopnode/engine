#include "render/sprite_billboard.hpp"

#include <cmath>

#include <raymath.h>

namespace slopengine {

namespace {

Vector3 translationFromMatrix(const Matrix& matrix) {
    return {matrix.m12, matrix.m13, matrix.m14};
}

Vector3 scaleFromMatrix(const Matrix& matrix) {
    const Vector3 xAxis{matrix.m0, matrix.m1, matrix.m2};
    const Vector3 yAxis{matrix.m4, matrix.m5, matrix.m6};
    const Vector3 zAxis{matrix.m8, matrix.m9, matrix.m10};
    return {
        Vector3Length(xAxis),
        Vector3Length(yAxis),
        Vector3Length(zAxis),
    };
}

bool uvFromQuadHit(const Vector3 hit, const Vector3 points[4], float& u, float& v) {
    const Vector3 origin = points[0];
    const Vector3 right = Vector3Subtract(points[1], points[0]);
    const Vector3 up = Vector3Subtract(points[3], points[0]);
    const float rightLenSq = Vector3LengthSqr(right);
    const float upLenSq = Vector3LengthSqr(up);
    if (rightLenSq < 1.0e-12f || upLenSq < 1.0e-12f) {
        return false;
    }

    const Vector3 local = Vector3Subtract(hit, origin);
    u = Vector3DotProduct(local, right) / rightLenSq;
    v = Vector3DotProduct(local, up) / upLenSq;
    return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
}

bool pixelFromUv(
    const SpriteBillboard& billboard,
    float u,
    float v,
    int& pixelX,
    int& pixelY) {
    const int width = billboard.pixelWidth > 0 ? billboard.pixelWidth
        : (billboard.hitmask != nullptr ? billboard.hitmask->width : 0);
    const int height = billboard.pixelHeight > 0 ? billboard.pixelHeight
        : (billboard.hitmask != nullptr ? billboard.hitmask->height : 0);
    if (width <= 0 || height <= 0) {
        return false;
    }

    int x = static_cast<int>(std::floor(u * static_cast<float>(width)));
    int y = static_cast<int>(std::floor((1.0f - v) * static_cast<float>(height)));
    if (x >= width) {
        x = width - 1;
    }
    if (y >= height) {
        y = height - 1;
    }
    if (x < 0 || y < 0) {
        return false;
    }
    if (billboard.mirror) {
        x = width - 1 - x;
    }
    pixelX = x;
    pixelY = y;
    return true;
}

} // namespace

namespace {

struct EffectivePose {
    float rotationDeg = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float translateX = 0.0f;
    float translateY = 0.0f;
};

EffectivePose effectivePoseFromRotation(
    const SpriteRotation& selected,
    const SpriteRotation* next,
    const SpriteAnimTween* tween) {
    const float curRotation = selected.rotationDeg + selected.animRotationDeg;
    const float curScaleX = selected.scaleX * selected.animScaleX;
    const float curScaleY = selected.scaleY * selected.animScaleY;
    const float curTranslateX = selected.translateX + selected.animTranslateX;
    const float curTranslateY = selected.translateY + selected.animTranslateY;

    if (tween == nullptr || next == nullptr || !tween->hasTween()) {
        return EffectivePose{curRotation, curScaleX, curScaleY, curTranslateX, curTranslateY};
    }

    const float blend = tween->blend;
    const float nextRotation = next->rotationDeg + next->animRotationDeg;
    const float nextScaleX = next->scaleX * next->animScaleX;
    const float nextScaleY = next->scaleY * next->animScaleY;
    const float nextTranslateX = next->translateX + next->animTranslateX;
    const float nextTranslateY = next->translateY + next->animTranslateY;

    return EffectivePose{
        tween->tweenRotation ? curRotation + (nextRotation - curRotation) * blend : curRotation,
        tween->tweenScale ? curScaleX + (nextScaleX - curScaleX) * blend : curScaleX,
        tween->tweenScale ? curScaleY + (nextScaleY - curScaleY) * blend : curScaleY,
        tween->tweenTranslate ? curTranslateX + (nextTranslateX - curTranslateX) * blend
                              : curTranslateX,
        tween->tweenTranslate ? curTranslateY + (nextTranslateY - curTranslateY) * blend
                              : curTranslateY,
    };
}

const SpriteRotation* nextRotationForTween(
    const SpriteAsset& asset,
    int rotation,
    const SpriteAnimTween* tween) {
    if (tween == nullptr || !tween->hasTween() || tween->nextFrame.empty()) {
        return nullptr;
    }
    const SpriteFrame* nextFrame = findSpriteFrame(asset, tween->nextFrame);
    if (nextFrame == nullptr) {
        return nullptr;
    }
    return selectSpriteRotation(*nextFrame, rotation);
}

std::optional<SpriteBillboard> buildBillboardFromRotation(
    const SpriteAsset& asset,
    const SpriteAtlas& atlas,
    const SpriteRotation& selected,
    bool frameFullbright,
    const SpriteRotation* next,
    const SpriteAnimTween* tween,
    const GlobalTransformation& global,
    Vector3 viewPosition,
    float facingYaw,
    Vector3 viewTarget,
    Vector3 viewUp) {
    const Vector3 position = translationFromMatrix(global.matrix);

    const auto rectIt = atlas.rects.find(selected.texturePath);
    if (rectIt == atlas.rects.end()) {
        return std::nullopt;
    }

    const SpriteAtlasRect& atlasRect = rectIt->second;
    if (atlasRect.atlasIndex < 0 ||
        atlasRect.atlasIndex >= static_cast<int>(atlas.textures.size())) {
        return std::nullopt;
    }

    const Texture2D& texture = atlas.textures[static_cast<std::size_t>(atlasRect.atlasIndex)];
    if (texture.id == 0) {
        return std::nullopt;
    }

    Rectangle source = atlasRect.source;
    if (selected.mirror) {
        source.x += source.width;
        source.width = -source.width;
    }

    const EffectivePose pose = effectivePoseFromRotation(selected, next, tween);
    const Vector3 entityScale = scaleFromMatrix(global.matrix);
    const float pixelsPerMeter = asset.pixelsPerMeter > 0.0f ? asset.pixelsPerMeter : 64.0f;
    const float pixelW =
        selected.pixelWidth > 0 ? static_cast<float>(selected.pixelWidth) : std::fabs(source.width);
    const float pixelH =
        selected.pixelHeight > 0 ? static_cast<float>(selected.pixelHeight)
                                 : std::fabs(source.height);
    const Vector2 size{
        (pixelW / pixelsPerMeter) * entityScale.x * pose.scaleX,
        (pixelH / pixelsPerMeter) * entityScale.y * pose.scaleY,
    };
    if (size.x <= 0.0f || size.y <= 0.0f || pixelW <= 0.0f || pixelH <= 0.0f) {
        return std::nullopt;
    }

    Vector3 rightDir{};
    Vector3 upDir{0.0f, 1.0f, 0.0f};
    if (asset.billboardMode == SpriteBillboardMode::Fixed) {
        rightDir = {std::cos(facingYaw), 0.0f, -std::sin(facingYaw)};
        rightDir = Vector3Normalize(rightDir);
    } else if (asset.billboardMode == SpriteBillboardMode::Screen) {
        const Vector3 camForward = Vector3Subtract(viewTarget, viewPosition);
        if (Vector3LengthSqr(camForward) < 0.000001f) {
            return std::nullopt;
        }
        const Matrix matCam = MatrixLookAt(viewPosition, viewTarget, viewUp);
        rightDir = Vector3Normalize(Vector3{matCam.m0, matCam.m4, matCam.m8});
        upDir = Vector3Normalize(Vector3{matCam.m1, matCam.m5, matCam.m9});
    } else if (asset.billboardMode == SpriteBillboardMode::View) {
        const float cameraYaw = horizontalCameraYaw(viewPosition, viewTarget);
        rightDir = {-std::cos(cameraYaw), 0.0f, std::sin(cameraYaw)};
        rightDir = Vector3Normalize(rightDir);
    } else {
        const Vector3 toCamera{
            viewPosition.x - position.x,
            0.0f,
            viewPosition.z - position.z,
        };
        if (Vector3LengthSqr(toCamera) < 0.000001f) {
            return std::nullopt;
        }
        const Matrix matView = MatrixLookAt(viewPosition, position, {0.0f, 1.0f, 0.0f});
        rightDir = Vector3Normalize(Vector3{matView.m0, matView.m4, matView.m8});
    }
    if (pose.rotationDeg != 0.0f) {
        const float rad = pose.rotationDeg * DEG2RAD;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        const Vector3 rotatedRight =
            Vector3Add(Vector3Scale(rightDir, c), Vector3Scale(upDir, s));
        const Vector3 rotatedUp =
            Vector3Add(Vector3Scale(rightDir, -s), Vector3Scale(upDir, c));
        rightDir = Vector3Normalize(rotatedRight);
        upDir = Vector3Normalize(rotatedUp);
    }
    Vector3 right = Vector3Scale(rightDir, size.x);
    Vector3 up = Vector3Scale(upDir, size.y);

    float ox = selected.hasOffset ? static_cast<float>(selected.offsetX) : pixelW * 0.5f;
    const float oy = selected.hasOffset ? static_cast<float>(selected.offsetY) : pixelH;
    if (selected.mirror) {
        ox = pixelW - 1.0f - ox;
    }
    const Vector2 origin{
        (ox / pixelW) * size.x,
        ((pixelH - oy) / pixelH) * size.y,
    };
    const Vector3 origin3D = Vector3Add(
        Vector3Scale(rightDir, origin.x),
        Vector3Scale(upDir, origin.y));
    const Vector3 translate3D = Vector3Add(
        Vector3Scale(rightDir, pose.translateX / pixelsPerMeter),
        Vector3Scale(upDir, -pose.translateY / pixelsPerMeter));

    SpriteBillboard billboard{};
    billboard.size = size;
    billboard.position = Vector3Add(position, translate3D);
    billboard.mirror = selected.mirror;
    billboard.pixelWidth = static_cast<int>(pixelW);
    billboard.pixelHeight = static_cast<int>(pixelH);
    billboard.texture = &texture;
    billboard.source = source;
    billboard.fullbright = asset.fullbright || frameFullbright;
    billboard.blend = asset.blend;
    billboard.tint = asset.tint;

    const auto maskIt = atlas.hitmasks.find(selected.texturePath);
    if (maskIt != atlas.hitmasks.end()) {
        billboard.hitmask = &maskIt->second;
    }
    const auto brightIt = atlas.brightTextures.find(selected.texturePath);
    if (brightIt != atlas.brightTextures.end() && brightIt->second.id != 0) {
        billboard.brightTexture = &brightIt->second;
    }
    const auto partMaskIt = atlas.partMaskTextures.find(selected.texturePath);
    if (partMaskIt != atlas.partMaskTextures.end() && partMaskIt->second.id != 0) {
        billboard.partMaskTexture = &partMaskIt->second;
    }

    Vector3 points[4] = {
        Vector3Zero(),
        right,
        Vector3Add(up, right),
        up,
    };
    for (int i = 0; i < 4; ++i) {
        billboard.points[i] =
            Vector3Add(Vector3Subtract(points[i], origin3D), billboard.position);
    }

    return billboard;
}

} // namespace

float horizontalCameraYaw(Vector3 eye, Vector3 target) {
    const float dx = target.x - eye.x;
    const float dz = target.z - eye.z;
    if (dx * dx + dz * dz < 0.000001f) {
        return 0.0f;
    }
    return std::atan2(dx, dz);
}

Vector3 viewTargetFromYaw(Vector3 viewPosition, float cameraYaw) {
    return Vector3{
        viewPosition.x + std::sin(cameraYaw),
        viewPosition.y,
        viewPosition.z + std::cos(cameraYaw),
    };
}

std::optional<SpriteBillboard> resolveSpriteBillboard(
    const SpriteAsset& asset,
    const SpriteAtlas& atlas,
    std::string_view frameId,
    float facingYaw,
    const GlobalTransformation& global,
    Vector3 viewPosition,
    float cameraYaw,
    const SpriteAnimTween* tween) {
    if (atlas.textures.empty()) {
        return std::nullopt;
    }

    const SpriteFrame* frame = findSpriteFrame(asset, frameId);
    if (frame == nullptr) {
        return std::nullopt;
    }

    const Vector3 position = translationFromMatrix(global.matrix);
    const Vector3 toCamera{
        viewPosition.x - position.x,
        0.0f,
        viewPosition.z - position.z,
    };
    int rotation = 0;
    if (asset.billboardMode == SpriteBillboardMode::View ||
        asset.billboardMode == SpriteBillboardMode::Screen) {
        rotation = 0;
    } else if (Vector3LengthSqr(toCamera) >= 0.000001f) {
        const float viewYaw = std::atan2(toCamera.x, toCamera.z);
        rotation = doomRotationFromViewYaw(viewYaw - facingYaw);
    } else if (asset.billboardMode == SpriteBillboardMode::Face) {
        return std::nullopt;
    }
    const SpriteRotation* selected = selectSpriteRotation(*frame, rotation);
    if (selected == nullptr) {
        return std::nullopt;
    }

    const SpriteRotation* next = nextRotationForTween(asset, rotation, tween);
    const Vector3 viewTarget = viewTargetFromYaw(viewPosition, cameraYaw);
    return buildBillboardFromRotation(
        asset,
        atlas,
        *selected,
        frame->fullbright,
        next,
        tween,
        global,
        viewPosition,
        facingYaw,
        viewTarget,
        Vector3{0.0f, 1.0f, 0.0f});
}

std::optional<SpriteBillboard> resolveSpriteBillboardForcedRot(
    const SpriteAsset& asset,
    const SpriteAtlas& atlas,
    std::string_view frameId,
    int rotation,
    float facingYaw,
    const GlobalTransformation& global,
    Vector3 viewPosition,
    float cameraYaw,
    const SpriteAnimTween* tween) {
    if (atlas.textures.empty()) {
        return std::nullopt;
    }

    const SpriteFrame* frame = findSpriteFrame(asset, frameId);
    if (frame == nullptr) {
        return std::nullopt;
    }

    const SpriteRotation* selected = selectSpriteRotation(*frame, rotation);
    if (selected == nullptr) {
        return std::nullopt;
    }

    const SpriteRotation* next = nextRotationForTween(asset, rotation, tween);
    const Vector3 viewTarget = viewTargetFromYaw(viewPosition, cameraYaw);
    return buildBillboardFromRotation(
        asset,
        atlas,
        *selected,
        frame->fullbright,
        next,
        tween,
        global,
        viewPosition,
        facingYaw,
        viewTarget,
        Vector3{0.0f, 1.0f, 0.0f});
}

std::optional<SpriteBillboard> resolveSpriteBillboard(
    const SpriteInstance& sprite,
    const GlobalTransformation& global,
    Vector3 viewPosition,
    float cameraYaw,
    AssetStore& assets,
    const SpriteAnimTween* tween) {
    if (sprite.sprite.empty()) {
        return std::nullopt;
    }

    const SpriteAsset* asset = assets.getSpriteAsset(sprite.sprite);
    const SpriteAtlas* atlas = assets.getSpriteAtlas(sprite.sprite);
    if (asset == nullptr || atlas == nullptr) {
        return std::nullopt;
    }

    return resolveSpriteBillboard(
        *asset,
        *atlas,
        sprite.frame,
        sprite.facingYaw,
        global,
        viewPosition,
        cameraYaw,
        tween);
}

std::optional<SpriteBillboard> resolveSpriteBillboard(
    const SpriteInstance& sprite,
    const GlobalTransformation& global,
    const Lens& lens,
    AssetStore& assets,
    const SpriteAnimTween* tween) {
    if (sprite.sprite.empty()) {
        return std::nullopt;
    }

    const SpriteAsset* asset = assets.getSpriteAsset(sprite.sprite);
    const SpriteAtlas* atlas = assets.getSpriteAtlas(sprite.sprite);
    if (asset == nullptr || atlas == nullptr) {
        return std::nullopt;
    }

    const SpriteFrame* frame = findSpriteFrame(*asset, sprite.frame);
    if (frame == nullptr) {
        return std::nullopt;
    }

    const Vector3 viewPosition = lens.camera.position;
    const Vector3 position = translationFromMatrix(global.matrix);
    const Vector3 toCamera{
        viewPosition.x - position.x,
        0.0f,
        viewPosition.z - position.z,
    };
    int rotation = 0;
    if (asset->billboardMode == SpriteBillboardMode::View ||
        asset->billboardMode == SpriteBillboardMode::Screen) {
        rotation = 0;
    } else if (Vector3LengthSqr(toCamera) >= 0.000001f) {
        const float viewYaw = std::atan2(toCamera.x, toCamera.z);
        rotation = doomRotationFromViewYaw(viewYaw - sprite.facingYaw);
    } else if (asset->billboardMode == SpriteBillboardMode::Face) {
        return std::nullopt;
    }
    const SpriteRotation* selected = selectSpriteRotation(*frame, rotation);
    if (selected == nullptr) {
        return std::nullopt;
    }

    const SpriteRotation* next = nextRotationForTween(*asset, rotation, tween);
    return buildBillboardFromRotation(
        *asset,
        *atlas,
        *selected,
        frame->fullbright,
        next,
        tween,
        global,
        viewPosition,
        sprite.facingYaw,
        lens.camera.target,
        lens.camera.up);
}

std::optional<ViewSpriteFrame> resolveViewSpriteFrame(
    const SpriteAsset& asset,
    const SpriteAtlas& atlas,
    std::string_view frameId,
    int rotation) {
    if (atlas.textures.empty()) {
        return std::nullopt;
    }

    const SpriteFrame* frame = findSpriteFrame(asset, frameId);
    if (frame == nullptr) {
        return std::nullopt;
    }

    const SpriteRotation* selected = selectSpriteRotation(*frame, rotation);
    if (selected == nullptr) {
        return std::nullopt;
    }

    const auto rectIt = atlas.rects.find(selected->texturePath);
    if (rectIt == atlas.rects.end()) {
        return std::nullopt;
    }

    const SpriteAtlasRect& atlasRect = rectIt->second;
    if (atlasRect.atlasIndex < 0 ||
        atlasRect.atlasIndex >= static_cast<int>(atlas.textures.size())) {
        return std::nullopt;
    }

    const Texture2D& texture = atlas.textures[static_cast<std::size_t>(atlasRect.atlasIndex)];
    if (texture.id == 0) {
        return std::nullopt;
    }

    Rectangle source = atlasRect.source;
    if (selected->mirror) {
        source.x += source.width;
        source.width = -source.width;
    }

    ViewSpriteFrame result{};
    result.texture = &texture;
    result.source = source;
    result.pixelWidth = selected->pixelWidth > 0 ? selected->pixelWidth
                                                 : static_cast<int>(std::fabs(source.width));
    result.pixelHeight = selected->pixelHeight > 0 ? selected->pixelHeight
                                                   : static_cast<int>(std::fabs(source.height));
    result.hasOffset = selected->hasOffset;
    result.offsetX = selected->offsetX;
    result.offsetY = selected->offsetY;
    result.mirror = selected->mirror;
    result.rotationDeg = selected->rotationDeg;
    result.scaleX = selected->scaleX;
    result.scaleY = selected->scaleY;
    result.translateX = selected->translateX;
    result.translateY = selected->translateY;
    result.animRotationDeg = selected->animRotationDeg;
    result.animScaleX = selected->animScaleX;
    result.animScaleY = selected->animScaleY;
    result.animTranslateX = selected->animTranslateX;
    result.animTranslateY = selected->animTranslateY;
    result.fullbright = asset.fullbright || frame->fullbright;
    const auto brightIt = atlas.brightTextures.find(selected->texturePath);
    if (brightIt != atlas.brightTextures.end() && brightIt->second.id != 0) {
        result.brightTexture = &brightIt->second;
    }
    if (result.pixelWidth <= 0 || result.pixelHeight <= 0) {
        return std::nullopt;
    }
    if (!result.hasOffset) {
        result.offsetX = result.pixelWidth / 2;
        result.offsetY = result.pixelHeight;
    }
    return result;
}

std::optional<ViewSpriteFrame> resolveViewSpriteFrame(
    const SpriteAsset& asset,
    const SpriteAtlas& atlas,
    std::string_view frameId) {
    return resolveViewSpriteFrame(asset, atlas, frameId, 0);
}

std::optional<ViewSpriteFrame> resolveViewSpriteFrame(
    const SpriteInstance& sprite,
    AssetStore& assets) {
    if (sprite.sprite.empty()) {
        return std::nullopt;
    }

    const SpriteAsset* asset = assets.getSpriteAsset(sprite.sprite);
    const SpriteAtlas* atlas = assets.getSpriteAtlas(sprite.sprite);
    if (asset == nullptr || atlas == nullptr) {
        return std::nullopt;
    }

    return resolveViewSpriteFrame(*asset, *atlas, sprite.frame);
}

std::optional<SpriteBillboardHit> raycastSpriteBillboard(
    const Ray& ray,
    const SpriteBillboard& billboard,
    float maxDistance) {
    if (billboard.hitmask == nullptr) {
        return std::nullopt;
    }

    const RayCollision collision = GetRayCollisionQuad(
        ray,
        billboard.points[0],
        billboard.points[1],
        billboard.points[2],
        billboard.points[3]);
    if (!collision.hit || collision.distance < 0.0f || collision.distance > maxDistance) {
        return std::nullopt;
    }

    float u = 0.0f;
    float v = 0.0f;
    if (!uvFromQuadHit(collision.point, billboard.points, u, v)) {
        return std::nullopt;
    }

    int pixelX = 0;
    int pixelY = 0;
    if (!pixelFromUv(billboard, u, v, pixelX, pixelY)) {
        return std::nullopt;
    }

    const std::uint8_t part = hitmaskPartAt(*billboard.hitmask, pixelX, pixelY);
    if (part == 0) {
        return std::nullopt;
    }

    SpriteBillboardHit hit{};
    hit.distance = collision.distance;
    hit.point = collision.point;
    hit.pixelX = pixelX;
    hit.pixelY = pixelY;
    hit.part = part;
    hit.partName = hitmaskPartName(*billboard.hitmask, part);
    return hit;
}

std::optional<SpriteBillboardHit> raycastSpriteBillboardXZ(
    const Ray& ray,
    const SpriteBillboard& billboard,
    float maxDistance) {
    // The billboard's bottom edge (points[0] -> points[1]) is always horizontal in world
    // space, and shares its XZ position with the top edge, so it doubles as the sprite's
    // full horizontal footprint regardless of pitch.
    const float ax = billboard.points[0].x;
    const float az = billboard.points[0].z;
    const float segX = billboard.points[1].x - ax;
    const float segZ = billboard.points[1].z - az;

    const float ox = ray.position.x;
    const float oz = ray.position.z;
    const float dx = ray.direction.x;
    const float dz = ray.direction.z;

    const float det = segX * dz - segZ * dx;
    if (std::fabs(det) < 1.0e-9f) {
        return std::nullopt;
    }

    const float ex = ox - ax;
    const float ez = oz - az;

    const float segT = (ex * dz - ez * dx) / det;
    if (segT < 0.0f || segT > 1.0f) {
        return std::nullopt;
    }

    const float rayT = (ex * segZ - segX * ez) / det;
    if (rayT < 0.0f || rayT > maxDistance) {
        return std::nullopt;
    }

    SpriteBillboardHit hit{};
    hit.distance = rayT;
    hit.point = Vector3Add(ray.position, Vector3Scale(ray.direction, rayT));
    hit.part = 1;
    hit.partName = "body";
    return hit;
}

void drawSpriteMaskDebug(const SpriteBillboard& billboard) {
    const Color boundsColor{255, 255, 255, 255};
    DrawLine3D(billboard.points[0], billboard.points[1], boundsColor);
    DrawLine3D(billboard.points[1], billboard.points[2], boundsColor);
    DrawLine3D(billboard.points[2], billboard.points[3], boundsColor);
    DrawLine3D(billboard.points[3], billboard.points[0], boundsColor);

    if (billboard.hitmask == nullptr || billboard.hitmask->width <= 0 ||
        billboard.hitmask->height <= 0) {
        return;
    }

    const SpriteHitmask& mask = *billboard.hitmask;
    const Vector3 origin = billboard.points[0];
    const Vector3 right = Vector3Subtract(billboard.points[1], billboard.points[0]);
    const Vector3 up = Vector3Subtract(billboard.points[3], billboard.points[0]);
    const float invW = 1.0f / static_cast<float>(mask.width);
    const float invH = 1.0f / static_cast<float>(mask.height);

    auto corner = [&](float u, float v) {
        return Vector3Add(origin, Vector3Add(Vector3Scale(right, u), Vector3Scale(up, v)));
    };

    auto samplePart = [&](int x, int y) -> std::uint8_t {
        if (x < 0 || y < 0 || x >= mask.width || y >= mask.height) {
            return 0;
        }
        int sampleX = x;
        if (billboard.mirror) {
            sampleX = mask.width - 1 - x;
        }
        return hitmaskPartAt(mask, sampleX, y);
    };

    static constexpr Color kPartColors[] = {
        {80, 180, 255, 255},
        {80, 255, 120, 255},
        {255, 80, 80, 255},
        {255, 220, 40, 255},
        {220, 80, 255, 255},
        {40, 255, 220, 255},
    };

    const int partCount = static_cast<int>(mask.partNames.size());
    for (int partIndex = 0; partIndex < partCount; ++partIndex) {
        const std::uint8_t part = static_cast<std::uint8_t>(partIndex + 1);
        const Color color = kPartColors[partIndex % (sizeof(kPartColors) / sizeof(kPartColors[0]))];

        for (int y = 0; y < mask.height; ++y) {
            for (int x = 0; x < mask.width; ++x) {
                if (samplePart(x, y) != part) {
                    continue;
                }

                const float u0 = static_cast<float>(x) * invW;
                const float u1 = static_cast<float>(x + 1) * invW;
                const float v0 = 1.0f - static_cast<float>(y + 1) * invH;
                const float v1 = 1.0f - static_cast<float>(y) * invH;

                if (samplePart(x - 1, y) != part) {
                    DrawLine3D(corner(u0, v0), corner(u0, v1), color);
                }
                if (samplePart(x + 1, y) != part) {
                    DrawLine3D(corner(u1, v0), corner(u1, v1), color);
                }
                if (samplePart(x, y - 1) != part) {
                    DrawLine3D(corner(u0, v1), corner(u1, v1), color);
                }
                if (samplePart(x, y + 1) != part) {
                    DrawLine3D(corner(u0, v0), corner(u1, v0), color);
                }
            }
        }
    }
}

} // namespace slopengine
