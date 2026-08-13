#include "fx/trail.hpp"

#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "render/fx_local_light.hpp"
#include "render/view_sprite_attachment.hpp"

#include <algorithm>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

namespace slopengine {

namespace {

Vector3 segmentRight(Vector3 dir, Vector3 toCam, Vector3 fallbackRight) {
    const Vector3 cross = Vector3CrossProduct(dir, toCam);
    if (Vector3Length(cross) < 1.0e-5f) {
        return fallbackRight;
    }
    return Vector3Normalize(cross);
}

Color multiplyTint(Color base, Color scene) {
    return {
        static_cast<unsigned char>(
            std::clamp(static_cast<int>(base.r) * static_cast<int>(scene.r) / 255, 0, 255)),
        static_cast<unsigned char>(
            std::clamp(static_cast<int>(base.g) * static_cast<int>(scene.g) / 255, 0, 255)),
        static_cast<unsigned char>(
            std::clamp(static_cast<int>(base.b) * static_cast<int>(scene.b) / 255, 0, 255)),
        base.a,
    };
}

} // namespace

flecs::entity spawnTrailFp(
    flecs::world& world,
    const char* id,
    flecs::entity hostViewSprite,
    const std::string& attachName,
    float depth,
    Vector3 endPoint,
    std::string_view texturePath,
    float lifetime,
    float width,
    bool mapOwned) {
    const auto start = resolveViewSpriteAttachmentWorld(world, hostViewSprite, attachName, depth);
    if (!start) {
        TraceLog(
            LOG_WARNING,
            "spawnTrailFp: host missing attach point '%s' or view pose",
            attachName.c_str());
        return {};
    }

    flecs::entity entity = world.entity(id);
    TrailEffect trail{};
    trail.points = {*start, endPoint};
    trail.texturePath = std::string(texturePath);
    trail.width = width;
    trail.lifetime = lifetime;
    trail.age = 0.0f;
    entity.set<TrailEffect>(trail);
    if (mapOwned) {
        entity.add<MapOwned>();
    }
    return entity;
}

flecs::entity spawnTrail(
    flecs::world& world,
    const char* id,
    Vector3 startPoint,
    Vector3 endPoint,
    std::string_view texturePath,
    float lifetime,
    float width,
    bool mapOwned) {
    flecs::entity entity = world.entity(id);
    TrailEffect trail{};
    trail.points = {startPoint, endPoint};
    trail.texturePath = std::string(texturePath);
    trail.width = width;
    trail.lifetime = lifetime;
    trail.age = 0.0f;
    entity.set<TrailEffect>(trail);
    if (mapOwned) {
        entity.add<MapOwned>();
    }
    return entity;
}

void registerTrailModule(flecs::world& world) {
    world.component<TrailEffect>();

    world.system("TrailEffectUpdate")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!isPlaying(world)) {
                return;
            }
            const float dt = GetFrameTime();
            world.each([dt](flecs::entity entity, TrailEffect& trail) {
                trail.age += dt;
                if (trail.age >= trail.lifetime) {
                    entity.destruct();
                }
            });
        });
}

void drawTrailEffects(flecs::world& world, AssetStore& assets, const Camera3D& camera, bool unlit) {
    bool began = false;

    const Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
    const Vector3 fallbackRight = Vector3Normalize(Vector3{matView.m0, matView.m4, matView.m8});

    world.each([&](const TrailEffect& trail) {
        if (trail.points.size() < 2 || !assets.hasTexture(trail.texturePath)) {
            return;
        }
        Texture2D texture = assets.getTexture(trail.texturePath);
        if (texture.id == 0) {
            return;
        }

        const float fadeAlpha =
            1.0f - std::clamp(trail.age / trail.lifetime, 0.0f, 1.0f);
        const unsigned char alpha =
            static_cast<unsigned char>(std::clamp(fadeAlpha, 0.0f, 1.0f) * 255.0f);
        if (alpha == 0) {
            return;
        }

        const std::size_t count = trail.points.size();
        std::vector<Vector3> dirs(count - 1);
        float totalLength = 0.0f;
        std::vector<float> cumulative(count, 0.0f);
        for (std::size_t i = 0; i + 1 < count; ++i) {
            const Vector3 delta = Vector3Subtract(trail.points[i + 1], trail.points[i]);
            const float len = Vector3Length(delta);
            dirs[i] = len > 1.0e-6f ? Vector3Scale(delta, 1.0f / len) : Vector3{0.0f, 0.0f, 1.0f};
            totalLength += len;
            cumulative[i + 1] = totalLength;
        }
        if (totalLength <= 1.0e-6f) {
            return;
        }

        std::vector<Vector3> rights(count);
        for (std::size_t i = 0; i < count; ++i) {
            Vector3 dir;
            if (i == 0) {
                dir = dirs[0];
            } else if (i == count - 1) {
                dir = dirs[count - 2];
            } else {
                dir = Vector3Normalize(Vector3Add(dirs[i - 1], dirs[i]));
            }
            const Vector3 toCam = Vector3Normalize(Vector3Subtract(camera.position, trail.points[i]));
            rights[i] = segmentRight(dir, toCam, fallbackRight);
        }

        std::vector<Color> tints(count, trail.color);
        if (!unlit) {
            for (std::size_t i = 0; i < count; ++i) {
                const Color scene = sampleReceiverTintColor(world, trail.points[i], false, 64.0f);
                tints[i] = multiplyTint(trail.color, scene);
            }
        }

        if (!began) {
            rlDrawRenderBatchActive();
            rlDisableShader();
            rlDisableDepthMask();
            rlDisableBackfaceCulling();
            BeginBlendMode(BLEND_ALPHA);
            began = true;
        }

        rlSetTexture(texture.id);
        for (std::size_t i = 0; i + 1 < count; ++i) {
            const Vector3 r0 = Vector3Scale(rights[i], trail.width * 0.5f);
            const Vector3 r1 = Vector3Scale(rights[i + 1], trail.width * 0.5f);
            const float u0 = cumulative[i] / totalLength;
            const float u1 = cumulative[i + 1] / totalLength;

            // Texture is uniform along its U axis and feathered along V (soft width edges),
            // so U carries length position and V carries the +/-right offset.
            const Vector3 p0a = Vector3Add(trail.points[i], r0);
            const Vector3 p0b = Vector3Subtract(trail.points[i], r0);
            const Vector3 p1a = Vector3Add(trail.points[i + 1], r1);
            const Vector3 p1b = Vector3Subtract(trail.points[i + 1], r1);

            const Color c0 = tints[i];
            const Color c1 = tints[i + 1];

            rlBegin(RL_QUADS);
            rlColor4ub(c0.r, c0.g, c0.b, alpha);
            rlTexCoord2f(u0, 1.0f);
            rlVertex3f(p0b.x, p0b.y, p0b.z);
            rlColor4ub(c0.r, c0.g, c0.b, alpha);
            rlTexCoord2f(u0, 0.0f);
            rlVertex3f(p0a.x, p0a.y, p0a.z);
            rlColor4ub(c1.r, c1.g, c1.b, alpha);
            rlTexCoord2f(u1, 0.0f);
            rlVertex3f(p1a.x, p1a.y, p1a.z);
            rlColor4ub(c1.r, c1.g, c1.b, alpha);
            rlTexCoord2f(u1, 1.0f);
            rlVertex3f(p1b.x, p1b.y, p1b.z);
            rlEnd();
        }
        rlSetTexture(0);
    });

    if (began) {
        rlDrawRenderBatchActive();
        EndBlendMode();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
    }
}

}
