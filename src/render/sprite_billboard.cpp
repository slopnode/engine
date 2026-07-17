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

std::optional<SpriteBillboard> resolveSpriteBillboard(
    const SpriteInstance& sprite,
    const GlobalTransformation& global,
    const Lens& lens,
    AssetStore& assets) {
    if (sprite.sprite.empty()) {
        return std::nullopt;
    }

    const SpriteAsset* asset = assets.getSpriteAsset(sprite.sprite);
    const SpriteAtlas* atlas = assets.getSpriteAtlas(sprite.sprite);
    if (asset == nullptr || atlas == nullptr || atlas->textures.empty()) {
        return std::nullopt;
    }

    const SpriteFrame* frame = findSpriteFrame(*asset, sprite.frame);
    if (frame == nullptr) {
        return std::nullopt;
    }

    const Vector3 position = translationFromMatrix(global.matrix);
    const Vector3 toCamera{
        lens.camera.position.x - position.x,
        0.0f,
        lens.camera.position.z - position.z,
    };
    if (Vector3LengthSqr(toCamera) < 0.000001f) {
        return std::nullopt;
    }

    const float viewYaw = std::atan2(toCamera.x, toCamera.z);
    const int rotation = doomRotationFromViewYaw(viewYaw - sprite.facingYaw);
    const SpriteRotation* selected = selectSpriteRotation(*frame, rotation);
    if (selected == nullptr) {
        return std::nullopt;
    }

    const auto rectIt = atlas->rects.find(selected->texturePath);
    if (rectIt == atlas->rects.end()) {
        return std::nullopt;
    }

    const SpriteAtlasRect& atlasRect = rectIt->second;
    if (atlasRect.atlasIndex < 0 ||
        atlasRect.atlasIndex >= static_cast<int>(atlas->textures.size())) {
        return std::nullopt;
    }

    const Texture2D& texture = atlas->textures[static_cast<std::size_t>(atlasRect.atlasIndex)];
    if (texture.id == 0) {
        return std::nullopt;
    }

    Rectangle source = atlasRect.source;
    if (selected->mirror) {
        source.x += source.width;
        source.width = -source.width;
    }

    const Vector3 scale = scaleFromMatrix(global.matrix);
    const float pixelsPerMeter = asset->pixelsPerMeter > 0.0f ? asset->pixelsPerMeter : 64.0f;
    const float pixelW =
        selected->pixelWidth > 0 ? static_cast<float>(selected->pixelWidth) : std::fabs(source.width);
    const float pixelH =
        selected->pixelHeight > 0 ? static_cast<float>(selected->pixelHeight)
                                  : std::fabs(source.height);
    const Vector2 size{
        (pixelW / pixelsPerMeter) * scale.x,
        (pixelH / pixelsPerMeter) * scale.y,
    };
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return std::nullopt;
    }

    const Matrix matView = MatrixLookAt(lens.camera.position, lens.camera.target, lens.camera.up);
    Vector3 right{matView.m0, matView.m4, matView.m8};
    right = Vector3Scale(right, size.x);
    const Vector3 up{0.0f, size.y, 0.0f};

    const Vector2 origin{size.x * 0.5f, 0.0f};
    const Vector3 origin3D = Vector3Add(
        Vector3Scale(Vector3Normalize(right), origin.x),
        Vector3Scale(Vector3Normalize(up), origin.y));

    SpriteBillboard billboard{};
    billboard.size = size;
    billboard.position = position;
    billboard.mirror = selected->mirror;
    billboard.pixelWidth = selected->pixelWidth > 0 ? selected->pixelWidth
                                                    : static_cast<int>(std::fabs(source.width));
    billboard.pixelHeight = selected->pixelHeight > 0 ? selected->pixelHeight
                                                      : static_cast<int>(std::fabs(source.height));
    billboard.texture = &texture;
    billboard.source = source;

    const auto maskIt = atlas->hitmasks.find(selected->texturePath);
    if (maskIt != atlas->hitmasks.end()) {
        billboard.hitmask = &maskIt->second;
    }

    Vector3 points[4] = {
        Vector3Zero(),
        right,
        Vector3Add(up, right),
        up,
    };
    for (int i = 0; i < 4; ++i) {
        billboard.points[i] = Vector3Add(Vector3Subtract(points[i], origin3D), position);
    }

    return billboard;
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
