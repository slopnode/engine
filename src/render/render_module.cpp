#include "render/render_module.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "assets/sprite_loader.hpp"
#include "camera/components.hpp"
#include "map/csg_script.hpp"
#include "map/bsp.hpp"
#include "map/entity_script.hpp"
#include "map/light_components.hpp"
#include "map/light_sample.hpp"
#include "physics/components.hpp"
#include "physics/map_collision.hpp"
#include "physics/physics_module.hpp"
#include "render/animation_player.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"
#include "render/dynamic_light_shadows.hpp"
#include "render/sprite_animator.hpp"
#include "render/sprite_billboard.hpp"
#include "render/transform.hpp"
#include "script/script_context.hpp"
#include "ui/ui_module.hpp"

#include "ui/ui_state.hpp"
#include "rlImGui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rlgl.h>

namespace slopengine {

namespace {

struct RenderContext {
    flecs::query<Model3D, GlobalTransformation> worldModelQuery;
    flecs::query<SpriteInstance, GlobalTransformation> worldSpriteQuery;
};

void drawSkeletonOverlay(const Model& model, const AnimationPlayer* animationPlayer) {
    if (model.skeleton.boneCount <= 0 || model.skeleton.bones == nullptr) {
        return;
    }

    const Transform* jointPoses = model.skeleton.bindPose;
    if (animationPlayer != nullptr && animationPlayer->playing && model.currentPose != nullptr) {
        jointPoses = model.currentPose;
    }

    if (jointPoses == nullptr) {
        return;
    }

    const float jointRadius = 0.12f;
    const Color boneColor = {255, 220, 0, 255};
    const Color jointColor = {255, 64, 64, 255};

    rlDisableDepthTest();
    rlDisableDepthMask();

    for (int boneIndex = 0; boneIndex < model.skeleton.boneCount; ++boneIndex) {
        const Vector3 jointPosition = jointPoses[boneIndex].translation;
        DrawSphereWires(jointPosition, jointRadius, 6, 8, jointColor);

        const int parentIndex = model.skeleton.bones[boneIndex].parent;
        if (parentIndex >= 0 && parentIndex < model.skeleton.boneCount) {
            const Vector3 parentPosition = jointPoses[parentIndex].translation;
            DrawLine3D(parentPosition, jointPosition, boneColor);
        }
    }

    rlEnableDepthMask();
    rlEnableDepthTest();
}

Color bspLeafDebugColor(std::int32_t leafIndex, bool solid, unsigned char alpha) {
    const std::uint32_t h = static_cast<std::uint32_t>(leafIndex) * 2654435761u;
    Color color{
        static_cast<unsigned char>(40 + (h & 0x7Fu)),
        static_cast<unsigned char>(40 + ((h >> 8) & 0x7Fu)),
        static_cast<unsigned char>(40 + ((h >> 16) & 0x7Fu)),
        alpha,
    };
    if (solid) {
        color.r = static_cast<unsigned char>(std::min(255, static_cast<int>(color.r) + 80));
        color.g = static_cast<unsigned char>(color.g / 2);
        color.b = static_cast<unsigned char>(color.b / 2);
    }
    return color;
}

void drawDebugPolygon(const std::vector<Vector3>& verts, Color color) {
    if (verts.size() < 3) {
        return;
    }
    for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
        DrawTriangle3D(verts[0], verts[i], verts[i + 1], color);
        DrawTriangle3D(verts[0], verts[i + 1], verts[i], color);
    }
}

void drawDebugPolygonOutline(const std::vector<Vector3>& verts, Color color) {
    if (verts.size() < 2) {
        return;
    }
    for (std::size_t i = 0; i < verts.size(); ++i) {
        DrawLine3D(verts[i], verts[(i + 1) % verts.size()], color);
    }
}

void drawBspLeafFaces(const BspLeaf& leaf, Color color) {
    for (const auto& face : leaf.faces) {
        drawDebugPolygon(face, color);
    }
}

bool portalPolygonBetweenLeaves(const BspLeaf& a, const BspLeaf& b, std::vector<Vector3>& out) {
    constexpr float kEps = 1e-3f;
    auto normalOf = [](const std::vector<Vector3>& verts) -> Vector3 {
        if (verts.size() < 3) {
            return {};
        }
        Vector3 accum{};
        for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
            const Vector3 e0{
                verts[i].x - verts[0].x,
                verts[i].y - verts[0].y,
                verts[i].z - verts[0].z,
            };
            const Vector3 e1{
                verts[i + 1].x - verts[0].x,
                verts[i + 1].y - verts[0].y,
                verts[i + 1].z - verts[0].z,
            };
            accum.x += e0.y * e1.z - e0.z * e1.y;
            accum.y += e0.z * e1.x - e0.x * e1.z;
            accum.z += e0.x * e1.y - e0.y * e1.x;
        }
        const float len = std::sqrt(accum.x * accum.x + accum.y * accum.y + accum.z * accum.z);
        if (len < 1e-8f) {
            return {};
        }
        return {accum.x / len, accum.y / len, accum.z / len};
    };

    for (const auto& fa : a.faces) {
        const Vector3 na = normalOf(fa);
        if (na.x == 0.0f && na.y == 0.0f && na.z == 0.0f) {
            continue;
        }
        for (const auto& fb : b.faces) {
            const Vector3 nb = normalOf(fb);
            const float align = na.x * nb.x + na.y * nb.y + na.z * nb.z;
            if (align > -0.99f) {
                continue;
            }
            Vector3 ca{};
            for (const Vector3& v : fa) {
                ca.x += v.x;
                ca.y += v.y;
                ca.z += v.z;
            }
            ca.x /= static_cast<float>(fa.size());
            ca.y /= static_cast<float>(fa.size());
            ca.z /= static_cast<float>(fa.size());
            Vector3 cb{};
            for (const Vector3& v : fb) {
                cb.x += v.x;
                cb.y += v.y;
                cb.z += v.z;
            }
            cb.x /= static_cast<float>(fb.size());
            cb.y /= static_cast<float>(fb.size());
            cb.z /= static_cast<float>(fb.size());
            const float dx = ca.x - cb.x;
            const float dy = ca.y - cb.y;
            const float dz = ca.z - cb.z;
            if (std::fabs(dx * na.x + dy * na.y + dz * na.z) > kEps * 8.0f) {
                continue;
            }
            out = fa;
            return true;
        }
    }
    return false;
}

void drawDebugQuadOutline(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color) {
    DrawLine3D(a, b, color);
    DrawLine3D(b, c, color);
    DrawLine3D(c, d, color);
    DrawLine3D(d, a, color);
}

void drawBspDebugOverlays(const BspTree& tree, const DebugUiState& debugUi, std::int32_t currentLeaf) {
    const bool any = debugUi.showBspOutlines || debugUi.showBspLeafFaces || debugUi.showBspPortals
        || debugUi.showBspSurfaceFaces;
    if (!any) {
        return;
    }

    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();

    const std::int32_t leafCount = static_cast<std::int32_t>(tree.leaves.size());
    for (std::int32_t i = 0; i < leafCount; ++i) {
        if (debugUi.showBspCurrentLeafOnly && i != currentLeaf) {
            continue;
        }
        const BspLeaf& leaf = tree.leaves[static_cast<std::size_t>(i)];
        if (debugUi.showBspOutlines) {
            Color color = leaf.solid ? Color{180, 60, 60, 255} : Color{60, 180, 220, 255};
            if (i == currentLeaf) {
                color = leaf.solid ? Color{255, 220, 40, 255} : Color{40, 255, 120, 255};
            }
            DrawBoundingBox(BoundingBox{leaf.mins, leaf.maxs}, color);
        }
        if (debugUi.showBspLeafFaces) {
            const unsigned char alpha = i == currentLeaf ? static_cast<unsigned char>(140)
                                                        : static_cast<unsigned char>(70);
            drawBspLeafFaces(leaf, bspLeafDebugColor(i, leaf.solid, alpha));
        }
    }

    if (debugUi.showBspPortals) {
        for (std::int32_t i = 0; i < leafCount; ++i) {
            const BspLeaf& leaf = tree.leaves[static_cast<std::size_t>(i)];
            if (leaf.solid) {
                continue;
            }
            if (debugUi.showBspCurrentLeafOnly && i != currentLeaf) {
                continue;
            }
            for (std::int32_t neighbor : leaf.neighbors) {
                if (neighbor <= i && !debugUi.showBspCurrentLeafOnly) {
                    continue;
                }
                if (neighbor < 0 || neighbor >= leafCount) {
                    continue;
                }
                const BspLeaf& other = tree.leaves[static_cast<std::size_t>(neighbor)];
                if (other.solid) {
                    continue;
                }
                std::vector<Vector3> portal;
                if (!portalPolygonBetweenLeaves(leaf, other, portal)) {
                    continue;
                }
                const bool involvesCurrent = i == currentLeaf || neighbor == currentLeaf;
                const Color portalColor = involvesCurrent ? Color{255, 200, 40, 160}
                                                         : Color{255, 80, 220, 100};
                drawDebugPolygon(portal, portalColor);
            }
        }
    }

    if (debugUi.showBspSurfaceFaces) {
        for (std::size_t faceIndex = 0; faceIndex < tree.surfaceFaces.size(); ++faceIndex) {
            const BspSurfaceFace& face = tree.surfaceFaces[faceIndex];
            if (debugUi.showBspCurrentLeafOnly && face.emptyLeaf != currentLeaf) {
                continue;
            }
            const Color fill = bspLeafDebugColor(static_cast<std::int32_t>(faceIndex), false, 90);
            const Color outline = face.emptyLeaf == currentLeaf ? Color{40, 255, 120, 255}
                                                               : Color{255, 255, 255, 220};
            drawDebugPolygon(face.vertices, fill);
            drawDebugPolygonOutline(face.vertices, outline);
        }
    }

    rlEnableDepthMask();
    EndBlendMode();
}

void renderWorldModel(
    flecs::entity entity,
    Model3D& model,
    GlobalTransformation& globalTransform,
    const Lens& lens) {
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloatV(globalTransform.matrix).v);

    if (entity.has<MapLightmapState>()) {
        const MapLightmapState& lightmaps = entity.get<MapLightmapState>();
        if (lightmaps.available && model.model.materialCount > 0) {
            Shader shader = model.model.materials[0].shader;
            if (shader.id != 0) {
                if (shader.locs[SHADER_LOC_MATRIX_MODEL] < 0) {
                    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
                }
                if (shader.locs[SHADER_LOC_MATRIX_MODEL] >= 0) {
                    SetShaderValueMatrix(
                        shader,
                        shader.locs[SHADER_LOC_MATRIX_MODEL],
                        globalTransform.matrix);
                }
            }
        }
    }

    if (entity.has<ShaderCavity>()) {
        ShaderCavity& shader = entity.get_mut<ShaderCavity>();
        const int modelLoc = GetShaderLocation(shader.shader, "model");
        const int viewLoc = GetShaderLocation(shader.shader, "view");
        const int projectionLoc = GetShaderLocation(shader.shader, "projection");
        SetShaderValueMatrix(shader.shader, modelLoc, globalTransform.matrix);
        SetShaderValueMatrix(shader.shader, viewLoc, GetCameraMatrix(lens.camera));
        SetShaderValueMatrix(
            shader.shader,
            projectionLoc,
            MatrixPerspective(
                lens.camera.fovy,
                static_cast<float>(GetScreenWidth()) / static_cast<float>(GetScreenHeight()),
                0.1f,
                100.0f));
    }

    DrawModel(model.model, Vector3Zero(), 1.0f, model.color);
    rlPopMatrix();
}

Vector3 translationFromMatrix(const Matrix& matrix) {
    return {matrix.m12, matrix.m13, matrix.m14};
}

void drawWorldSprite(
    const SpriteInstance& sprite,
    const GlobalTransformation& global,
    const Lens& lens,
    AssetStore& assets,
    const MapLighting* lighting,
    const BspTree* bspTree,
    const std::vector<RankedDynamicLight>* dynamicLights,
    bool unlit) {
    const auto billboard = resolveSpriteBillboard(sprite, global, lens, assets);
    if (!billboard || billboard->texture == nullptr) {
        return;
    }

    Color colorFeet = WHITE;
    Color colorHead = WHITE;
    if (!unlit && lighting != nullptr && lighting->available && bspTree != nullptr) {
        const Vector3 feetOrigin{billboard->position.x, billboard->position.y + 0.05f, billboard->position.z};
        if (auto feet = sampleMapLight(*lighting, *bspTree, feetOrigin, {0.0f, -1.0f, 0.0f}, 2.0f)) {
            colorFeet = *feet;
        }

        const Vector3 headPos{
            billboard->position.x,
            billboard->position.y + billboard->size.y,
            billboard->position.z};
        Vector3 headDir{
            lens.camera.position.x - headPos.x,
            0.0f,
            lens.camera.position.z - headPos.z,
        };
        const float headLenSq = Vector3LengthSqr(headDir);
        if (headLenSq > 1e-8f) {
            headDir = Vector3Scale(headDir, 1.0f / std::sqrt(headLenSq));
            if (auto head = sampleMapLight(*lighting, *bspTree, headPos, headDir, 4.0f)) {
                colorHead = *head;
            } else {
                colorHead = colorFeet;
            }
        } else {
            colorHead = colorFeet;
        }
    }

    if (!unlit && dynamicLights != nullptr && !dynamicLights->empty()) {
        const auto addDynamic = [&](Color base, Vector3 point) {
            const Vector3 dyn = evaluateDynamicLightsAtPoint(*dynamicLights, point, {0.0f, 1.0f, 0.0f});
            return Color{
                static_cast<unsigned char>(std::clamp(
                    static_cast<int>(base.r) + static_cast<int>(dyn.x * 255.0f),
                    0,
                    255)),
                static_cast<unsigned char>(std::clamp(
                    static_cast<int>(base.g) + static_cast<int>(dyn.y * 255.0f),
                    0,
                    255)),
                static_cast<unsigned char>(std::clamp(
                    static_cast<int>(base.b) + static_cast<int>(dyn.z * 255.0f),
                    0,
                    255)),
                base.a,
            };
        };
        const Vector3 feetPoint{
            billboard->position.x,
            billboard->position.y + 0.05f,
            billboard->position.z};
        const Vector3 headPoint{
            billboard->position.x,
            billboard->position.y + billboard->size.y,
            billboard->position.z};
        colorFeet = addDynamic(colorFeet, feetPoint);
        colorHead = addDynamic(colorHead, headPoint);
    }

    const Texture2D& texture = *billboard->texture;
    const Rectangle source = billboard->source;
    const float texW = static_cast<float>(texture.width);
    const float texH = static_cast<float>(texture.height);
    const Vector2 texcoords[4] = {
        {source.x / texW, (source.y + source.height) / texH},
        {(source.x + source.width) / texW, (source.y + source.height) / texH},
        {(source.x + source.width) / texW, source.y / texH},
        {source.x / texW, source.y / texH},
    };
    const Color colors[4] = {colorFeet, colorFeet, colorHead, colorHead};

    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < 4; ++i) {
        rlColor4ub(colors[i].r, colors[i].g, colors[i].b, colors[i].a);
        rlTexCoord2f(texcoords[i].x, texcoords[i].y);
        rlVertex3f(billboard->points[i].x, billboard->points[i].y, billboard->points[i].z);
    }
    rlEnd();
    rlSetTexture(0);
}

Ray spriteAimRay(const Lens& lens) {
    Ray ray{};
    ray.position = lens.camera.position;
    ray.direction = Vector3Normalize(Vector3Subtract(lens.camera.target, lens.camera.position));
    return ray;
}

constexpr float kSpriteAimMaxDistance = 100.0f;

std::string drawSpriteDebugOverlays(
    const Lens& lens,
    AssetStore& assets,
    const DebugUiState& debugUi,
    flecs::query<SpriteInstance, GlobalTransformation>& spriteQuery) {
    std::string aimStatus;
    if (!debugUi.showSpriteMasks && !debugUi.showSpriteAim) {
        return aimStatus;
    }

    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthTest();
    rlDisableDepthMask();

    if (debugUi.showSpriteMasks) {
        spriteQuery.each(
            [&](flecs::entity entity, SpriteInstance& sprite, GlobalTransformation& global) {
                if (!entity.has<WorldSpace>()) {
                    return;
                }
                if (const auto billboard = resolveSpriteBillboard(sprite, global, lens, assets)) {
                    drawSpriteMaskDebug(*billboard);
                }
            });
    }

    if (debugUi.showSpriteAim) {
        const Ray ray = spriteAimRay(lens);
        std::optional<SpriteBillboardHit> bestHit;
        std::string hitSprite;
        std::string hitFrame;
        float bestDistance = kSpriteAimMaxDistance;

        spriteQuery.each(
            [&](flecs::entity entity, SpriteInstance& sprite, GlobalTransformation& global) {
                if (!entity.has<WorldSpace>()) {
                    return;
                }
                const auto billboard = resolveSpriteBillboard(sprite, global, lens, assets);
                if (!billboard) {
                    return;
                }
                if (const auto hit = raycastSpriteBillboard(ray, *billboard, bestDistance)) {
                    bestDistance = hit->distance;
                    bestHit = *hit;
                    hitSprite = sprite.sprite;
                    hitFrame = sprite.frame;
                }
            });

        const Vector3 rayEnd = bestHit
            ? bestHit->point
            : Vector3Add(ray.position, Vector3Scale(ray.direction, kSpriteAimMaxDistance));
        DrawLine3D(ray.position, rayEnd, bestHit ? Color{80, 255, 120, 255} : Color{255, 220, 40, 255});

        if (bestHit) {
            DrawSphereWires(bestHit->point, 0.04f, 6, 6, Color{80, 255, 120, 255});
            const Vector3 tickEnd = Vector3Add(bestHit->point, Vector3Scale(ray.direction, -0.12f));
            DrawLine3D(bestHit->point, tickEnd, Color{255, 255, 255, 255});
            char buffer[256];
            std::snprintf(
                buffer,
                sizeof(buffer),
                "Sprite aim: %s frame=%s part=%s px=(%d,%d) dist=%.2f",
                hitSprite.c_str(),
                hitFrame.c_str(),
                bestHit->partName.c_str(),
                bestHit->pixelX,
                bestHit->pixelY,
                bestHit->distance);
            aimStatus = buffer;
        } else {
            aimStatus = "Sprite aim: no hit";
        }
    }

    rlEnableDepthMask();
    rlEnableDepthTest();
    EndBlendMode();
    return aimStatus;
}

void registerComponents(flecs::world& world) {
    world.component<LocalTransformation>()
        .add(flecs::With, world.component<GlobalTransformation>());

    world.component<GlobalTransformation>();
    world.component<WorldSpace>();
    world.component<Lens>();
    world.component<Spin>();
    world.component<Model3D>();
    world.component<SpriteInstance>();
    world.component<SpriteAnimator>();
    world.component<AnimationPlayer>();
    world.component<AnimationClipFlipTest>();
    world.component<PointLight>();
    world.component<SpotLight>();
    world.component<AreaLight>();
    world.component<SunLight>();
    world.component<DynamicLight>();
    world.component<ShaderCavity>()
        .on_remove([](flecs::iter&, size_t, ShaderCavity& shader) {
            if (shader.shader.id != 0) {
                UnloadShader(shader.shader);
            }
            shader.shader = {};
        });
}

void registerSpinSystem(flecs::world& world) {
    world.system<LocalTransformation, Spin>("ApplySpin")
        .kind(flecs::OnUpdate)
        .each([](LocalTransformation& local, Spin& spin) {
            const Vector3 axis = Vector3Normalize(spin.axis);
            const float angle = spin.speed * GetFrameTime();
            const Quaternion delta = QuaternionFromAxisAngle(axis, angle);
            local.rotation = QuaternionMultiply(local.rotation, delta);
        });
}

void registerAnimationSystems(flecs::world& world) {
    world.system<Model3D, AnimationPlayer>("AdvanceAnimationPlayer")
        .kind(flecs::OnUpdate)
        .each([](flecs::iter& it, size_t, Model3D& model3d, AnimationPlayer& player) {
            const bool startedThisFrame = player.justStarted;
            player.justStarted = false;
            player.justFinished = false;

            if (!player.playing || player.clipName.empty() || player.animBankPath.empty()) {
                return;
            }

            AssetServices& services = it.world().get_mut<AssetServices>();
            if (services.store == nullptr) {
                return;
            }

            const AnimBank* bank = services.store->getAnimBank(player.animBankPath);
            if (bank == nullptr) {
                return;
            }

            const auto clipIndex = bank->clipIndexByName.find(player.clipName);
            if (clipIndex == bank->clipIndexByName.end()) {
                return;
            }

            const std::size_t index = clipIndex->second;
            if (index >= bank->clips.size() || index >= bank->clipMeta.size()) {
                return;
            }

            const AnimClip& clipMeta = bank->clipMeta[index];
            const ModelAnimation& clip = bank->clips[index];
            if (clip.keyframeCount <= 0 || clipMeta.fps <= 0.0f) {
                return;
            }

            float clipDuration = clipMeta.duration;
            if (clipDuration <= 0.0f) {
                const int lastFrameIndex = clip.keyframeCount - 1;
                clipDuration =
                    static_cast<float>(lastFrameIndex > 0 ? lastFrameIndex : 1) / clipMeta.fps;
            }

            std::string skeletonPath = bank->skeletonId;
            if (skeletonPath.empty() && !player.animBankPath.empty()) {
                const auto slash = player.animBankPath.find_last_of('/');
                skeletonPath = slash == std::string::npos ? player.animBankPath
                                                          : player.animBankPath.substr(0, slash);
            }
            const std::vector<Matrix>* bindMatrices = services.store->getSkeletonBindMatrices(skeletonPath);
            const std::vector<std::vector<Matrix>>* matrixKeyframes = nullptr;
            if (index < bank->matrixClips.size() && !bank->matrixClips[index].keyframes.empty()) {
                matrixKeyframes = &bank->matrixClips[index].keyframes;
            }

            if (startedThisFrame && model3d.model.boneMatrices != nullptr) {
                updateRiggedModelAnimation(
                    model3d.model,
                    clip,
                    0.0f,
                    bindMatrices,
                    matrixKeyframes);
            }

            player.time += GetFrameTime() * player.speed;
            if (clipDuration > 0.0f && player.time >= clipDuration) {
                if (player.loop) {
                    player.time = std::fmod(player.time, clipDuration);
                } else {
                    player.time = clipDuration;
                    player.playing = false;
                    player.justFinished = true;
                }
            }

            const float frame = player.time * clipMeta.fps;
            updateRiggedModelAnimation(
                model3d.model,
                clip,
                frame,
                bindMatrices,
                matrixKeyframes);
        });
}

void registerAnimationClipFlipTestSystem(flecs::world& world) {
    world.system<AnimationPlayer, AnimationClipFlipTest>("AnimationClipFlipTest")
        .kind(flecs::PostUpdate)
        .each([](AnimationPlayer& player, AnimationClipFlipTest& controller) {
            if (!player.justFinished) {
                return;
            }

            if (player.clipName == controller.clipA) {
                player.play(controller.clipB, false);
            } else {
                player.play(controller.clipA, false);
            }
        });
}

void registerSpriteAnimatorSystem(flecs::world& world) {
    world.system<SpriteAnimator, SpriteInstance>("AdvanceSpriteAnimator")
        .kind(flecs::OnUpdate)
        .each([](flecs::iter& it, size_t, SpriteAnimator& animator, SpriteInstance& sprite) {
            animator.justFinished = false;
            const bool startedThisFrame = animator.justStarted;
            animator.justStarted = false;

            if (!animator.playing || animator.clipName.empty() || animator.animPath.empty()) {
                return;
            }

            AssetServices& services = it.world().get_mut<AssetServices>();
            if (services.store == nullptr) {
                return;
            }

            const SpriteAnimBank* bank = services.store->getSpriteAnimBank(animator.animPath);
            if (bank == nullptr) {
                return;
            }

            const auto clipIt = bank->clipIndexByName.find(animator.clipName);
            if (clipIt == bank->clipIndexByName.end() || clipIt->second >= bank->clips.size()) {
                return;
            }

            const SpriteAnimClip& clip = bank->clips[clipIt->second];
            if (clip.frames.empty() || clip.fps <= 0.0f) {
                return;
            }

            const bool useLoop = animator.loop;
            if (!startedThisFrame) {
                animator.time += GetFrameTime() * animator.speed;
            }

            const float frameFloat = animator.time * clip.fps;
            const int frameCount = static_cast<int>(clip.frames.size());
            int frameIndex = static_cast<int>(std::floor(frameFloat));

            if (useLoop) {
                frameIndex %= frameCount;
                if (frameIndex < 0) {
                    frameIndex += frameCount;
                }
            } else if (frameIndex >= frameCount) {
                frameIndex = frameCount - 1;
                animator.playing = false;
                animator.justFinished = true;
                animator.time = static_cast<float>(frameCount) / clip.fps;
            }

            sprite.frame = clip.frames[static_cast<std::size_t>(frameIndex)];
        });
}

void registerTransformSystems(flecs::world& world) {
    world.system("UpdateGlobalTransforms")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            it.world().each([](flecs::entity entity) {
                if (entity.has(flecs::ChildOf)) {
                    return;
                }
                if (!entity.has<LocalTransformation>() || !entity.has<GlobalTransformation>()) {
                    return;
                }
                updateTransform(
                    entity,
                    entity.get_mut<LocalTransformation>(),
                    entity.get_mut<GlobalTransformation>());
            });
        });
}

void registerRenderSystems(flecs::world& world) {
    world.system("StartDrawing")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter&) {
            BeginDrawing();
            ClearBackground(BLACK);
        });

    world.system<const Lens>("LensWorld")
        .with<WorldSpace>()
        .kind(flecs::PostUpdate)
        .each([](flecs::iter& it, size_t, const Lens& lens) {
            flecs::world world = it.world();
            RenderContext& context = world.get_mut<RenderContext>();
            const bool unlit =
                world.has<DebugUiState>() && world.get<DebugUiState>().unlit;

            std::vector<RankedDynamicLight> rankedLights;
            if (!unlit) {
                std::vector<RankedDynamicLight> candidates;
                world.each([&](flecs::entity entity,
                               const DynamicLight& light,
                               const LocalTransformation& local) {
                    if (!entity.has<WorldSpace>() || light.intensity <= 0.0f) {
                        return;
                    }
                    RankedDynamicLight ranked{};
                    ranked.light = light;
                    ranked.position = local.position;
                    ranked.direction = dynamicLightDirectionFromRotation(local.rotation);
                    ranked.linearRgb = dynamicLightLinearRgb(light);
                    candidates.push_back(ranked);
                });
                rankedLights = rankDynamicLights(candidates, lens.camera.position);
                static int sLastDynCount = -1;
                if (static_cast<int>(rankedLights.size()) != sLastDynCount) {
                    sLastDynCount = static_cast<int>(rankedLights.size());
                    TraceLog(LOG_INFO, "MAP: active dynamic lights=%d", sLastDynCount);
                    for (const RankedDynamicLight& light : rankedLights) {
                        TraceLog(
                            LOG_INFO,
                            "MAP: dyn light pos=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f) intensity=%.2f kind=%s",
                            light.position.x,
                            light.position.y,
                            light.position.z,
                            light.direction.x,
                            light.direction.y,
                            light.direction.z,
                            light.light.intensity,
                            light.light.kind == DynamicLightKind::Spot ? "spot" : "point");
                    }
                }
            }

            if (world.has<DynamicLightFrameState>()) {
                world.get_mut<DynamicLightFrameState>().lights = rankedLights;
            }

            BeginMode3D(lens.camera);
            context.worldModelQuery.each([&](flecs::entity modelEntity, Model3D& model, GlobalTransformation& global) {
                if (!modelEntity.has<WorldSpace>()) {
                    return;
                }
                if (modelEntity.has<MapLightmapState>()) {
                    const MapLightmapState& lightmaps = modelEntity.get<MapLightmapState>();
                    if (lightmaps.available && model.model.materialCount > 0) {
                        Shader& mapShader = model.model.materials[0].shader;
                        if (mapShader.id != 0) {
                            if (world.has<DynamicLightFrameState>()) {
                                DynamicLightFrameState& frameState =
                                    world.get_mut<DynamicLightFrameState>();
                                if (!frameState.bindings.resolved) {
                                    resolveDynamicLightShaderBindings(mapShader, frameState.bindings);
                                }
                                uploadDynamicLightsToShader(
                                    mapShader,
                                    frameState.bindings,
                                    rankedLights,
                                    nullptr);
                            }
                            if (lightmaps.useLightmapLoc >= 0) {
                                const int useLightmap = (!unlit) ? 1 : 0;
                                SetShaderValue(
                                    mapShader,
                                    lightmaps.useLightmapLoc,
                                    &useLightmap,
                                    SHADER_UNIFORM_INT);
                            }
                        }
                    }
                }
                renderWorldModel(modelEntity, model, global, lens);
            });
            context.worldModelQuery.each([&](flecs::entity modelEntity, Model3D& model, GlobalTransformation& global) {
                if (!modelEntity.has<WorldSpace>() || !modelEntity.has<AnimationPlayer>()) {
                    return;
                }
                const AnimationPlayer& animationPlayer = modelEntity.get<AnimationPlayer>();
                rlPushMatrix();
                rlMultMatrixf(MatrixToFloatV(global.matrix).v);
                drawSkeletonOverlay(model.model, &animationPlayer);
                rlPopMatrix();
            });

            std::string spriteAimStatus;
            if (world.has<AssetServices>() && world.get<AssetServices>().store != nullptr) {
                AssetStore& assets = *world.get_mut<AssetServices>().store;
                const MapLighting* lighting =
                    world.has<MapLighting>() ? &world.get<MapLighting>() : nullptr;
                const BspTree* bspTree =
                    world.has<MapBsp>() ? &world.get<MapBsp>().tree : nullptr;
                const std::vector<RankedDynamicLight>* dynamicLights =
                    (!unlit && world.has<DynamicLightFrameState>())
                        ? &world.get<DynamicLightFrameState>().lights
                        : nullptr;

                struct SpriteDrawItem {
                    const SpriteInstance* sprite = nullptr;
                    const GlobalTransformation* global = nullptr;
                    float distSq = 0.0f;
                };
                std::vector<SpriteDrawItem> spriteDrawList;
                spriteDrawList.reserve(32);
                context.worldSpriteQuery.each(
                    [&](flecs::entity spriteEntity, SpriteInstance& sprite, GlobalTransformation& global) {
                        if (!spriteEntity.has<WorldSpace>()) {
                            return;
                        }
                        const Vector3 position = translationFromMatrix(global.matrix);
                        const float dx = position.x - lens.camera.position.x;
                        const float dy = position.y - lens.camera.position.y;
                        const float dz = position.z - lens.camera.position.z;
                        spriteDrawList.push_back(SpriteDrawItem{
                            &sprite,
                            &global,
                            dx * dx + dy * dy + dz * dz,
                        });
                    });
                std::sort(
                    spriteDrawList.begin(),
                    spriteDrawList.end(),
                    [](const SpriteDrawItem& a, const SpriteDrawItem& b) {
                        return a.distSq > b.distSq;
                    });

                BeginBlendMode(BLEND_ALPHA);
                rlDisableDepthMask();
                for (const SpriteDrawItem& item : spriteDrawList) {
                    drawWorldSprite(
                        *item.sprite,
                        *item.global,
                        lens,
                        assets,
                        lighting,
                        bspTree,
                        dynamicLights,
                        unlit);
                }
                rlEnableDepthMask();
                EndBlendMode();

                if (world.has<DebugUiState>()) {
                    spriteAimStatus = drawSpriteDebugOverlays(
                        lens,
                        assets,
                        world.get<DebugUiState>(),
                        context.worldSpriteQuery);
                }
            }

            if (world.has<DebugUiState>() && world.has<MapBsp>()) {
                const DebugUiState& debugUi = world.get<DebugUiState>();
                const MapBsp& mapBsp = world.get<MapBsp>();
                std::int32_t currentLeaf = -1;
                flecs::entity camera = world.lookup("Player");
                if (camera.is_valid() && camera.has<Lens>()) {
                    currentLeaf = pointLeaf(mapBsp.tree, camera.get<Lens>().camera.position);
                }
                drawBspDebugOverlays(mapBsp.tree, debugUi, currentLeaf);
            }

            EndMode3D();

            if (!spriteAimStatus.empty()) {
                const int screenW = GetScreenWidth();
                const int fontSize = 18;
                const int textW = MeasureText(spriteAimStatus.c_str(), fontSize);
                DrawText(
                    spriteAimStatus.c_str(),
                    (screenW - textW) / 2,
                    24,
                    fontSize,
                    Color{80, 255, 120, 255});
                const int cx = screenW / 2;
                const int cy = GetScreenHeight() / 2;
                DrawLine(cx - 8, cy, cx + 8, cy, Color{255, 255, 255, 200});
                DrawLine(cx, cy - 8, cx, cy + 8, Color{255, 255, 255, 200});
            }
        });

    world.system("ImGuiOverlay")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            prepareUiInput(it.world());
            rlImGuiBegin();
            drawUi(it.world());
            rlImGuiEnd();
        });

    world.system("EndDrawing")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (world.has<MapBsp>()) {
                const MapBsp& mapBsp = world.get<MapBsp>();
                Vector3 sample{0.0f, 1.5f, 0.0f};
                flecs::entity camera = world.lookup("Player");
                if (camera.is_valid() && camera.has<Lens>()) {
                    sample = camera.get<Lens>().camera.position;
                }
                const std::int32_t leaf = pointLeaf(mapBsp.tree, sample);
                const bool empty = leafIsEmpty(mapBsp.tree, leaf);
                const int neighborCount =
                    leaf >= 0 ? static_cast<int>(leafNeighbors(mapBsp.tree, leaf).size()) : 0;
                const char* label = TextFormat(
                    "BSP leaf %d (%s) neighbors %d",
                    leaf,
                    empty ? "empty" : (leaf < 0 ? "out" : "solid"),
                    neighborCount);
                constexpr int kFontSize = 16;
                constexpr int kPad = 8;
                const int barHeight = kFontSize + kPad * 2;
                const int screenW = GetScreenWidth();
                const int screenH = GetScreenHeight();
                DrawRectangle(0, screenH - barHeight, screenW, barHeight, Color{0, 0, 0, 160});
                DrawText(label, kPad, screenH - barHeight + kPad, kFontSize, LIME);
            }
            EndDrawing();
        });
}

void spawnMainCamera(flecs::world& world, Vector3 position, Vector3 target, float yaw) {
    Lens lens{};
    lens.camera.position = position;
    lens.camera.target = target;
    lens.camera.up = {0.0f, 1.0f, 0.0f};
    lens.camera.fovy = 75.0f;
    lens.camera.projection = CAMERA_PERSPECTIVE;

    FirstPersonController controller{};
    controller.yaw = yaw;
    controller.pitch = -0.05f;

    world.entity("Player")
        .add<PlayerCamera>()
        .add<WorldSpace>()
        .set<Lens>(lens)
        .set<FirstPersonController>(controller);
}

void registerMapScene(flecs::world& world, AssetStore& assets, s7_scheme* scheme, std::string_view mapName) {
    auto loaded = loadAndCompileMap(scheme, assets, mapName);
    if (!loaded) {
        TraceLog(LOG_WARNING, "MAP: failed to spawn map '%.*s'", static_cast<int>(mapName.size()), mapName.data());
        return;
    }

    MapLightmapState lightmapState{};
    lightmapState.available = loaded->hasLightmaps;
    lightmapState.useLightmapLoc = loaded->useLightmapLoc;
    lightmapState.lightmapShader = loaded->lightmapShader;

    world.entity("MapStatic")
        .add<WorldSpace>()
        .set<LocalTransformation>({
            .position = {0.0f, 0.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        })
        .set<Model3D>({loaded->model, WHITE})
        .set<MapLightmapState>(lightmapState);

    if (loaded->hasLightmaps) {
        world.set<DynamicLightShadowState>(createDynamicLightShadowState(assets));
    }
    {
        DynamicLightFrameState frameState{};
        if (loaded->hasLightmaps && loaded->lightmapShader.id != 0) {
            resolveDynamicLightShaderBindings(loaded->lightmapShader, frameState.bindings);
        }
        world.set<DynamicLightFrameState>(std::move(frameState));
    }

    MapBsp mapBsp{std::move(loaded->bsp)};
    Color ambientColor{
        static_cast<unsigned char>(std::clamp(loaded->meta.ambient.x * 255.0f, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(loaded->meta.ambient.y * 255.0f, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(loaded->meta.ambient.z * 255.0f, 0.0f, 255.0f)),
        255,
    };
    world.set<MapLighting>(buildMapLighting(
        mapBsp.tree,
        std::move(loaded->rad),
        std::move(loaded->lightmapAtlasImages),
        ambientColor));
    world.set<MapBsp>(std::move(mapBsp));

    if (world.has<PhysicsContext>()) {
        PhysicsWorld* physics = world.get_mut<PhysicsContext>().world;
        if (physics != nullptr) {
            addStaticBrushes(*physics, loaded->brushes);
        }
    }

    const PlayerStart playerStart = loadMapEntities(scheme, world, assets, mapName);

    CharacterMotor motor{};
    FirstPersonController controller{};
    controller.yaw = playerStart.yaw;
    controller.pitch = -0.05f;
    controller.eyeHeight = motor.eyeHeight;
    controller.moveSpeed = motor.moveSpeed;

    const float cosPitch = std::cos(controller.pitch);
    const Vector3 forward = {
        std::sin(controller.yaw) * cosPitch,
        std::sin(controller.pitch),
        std::cos(controller.yaw) * cosPitch,
    };

    Lens lens{};
    lens.camera.position = {
        playerStart.position.x,
        playerStart.position.y + motor.eyeHeight,
        playerStart.position.z,
    };
    lens.camera.target = Vector3Add(lens.camera.position, forward);
    lens.camera.up = {0.0f, 1.0f, 0.0f};
    lens.camera.fovy = 75.0f;
    lens.camera.projection = CAMERA_PERSPECTIVE;

    world.entity("Player")
        .add<PlayerCamera>()
        .add<WorldSpace>()
        .set<Lens>(lens)
        .set<FirstPersonController>(controller)
        .set<CharacterMotor>(motor);

    if (world.has<PhysicsContext>()) {
        PhysicsWorld* physics = world.get_mut<PhysicsContext>().world;
        if (physics != nullptr) {
            physics->createPlayerCharacter(
                playerStart.position.x,
                playerStart.position.y,
                playerStart.position.z,
                motor);
        }
    }
}

}

void registerRenderModule(
    flecs::world& world,
    AssetStore& assets,
    const AppConfig& config,
    s7_scheme* scheme) {
    registerComponents(world);

    world.set<AssetServices>(AssetServices{&assets});
    world.set<ScriptContext>(ScriptContext{scheme});
    world.set<RenderContext>({
        world.query<Model3D, GlobalTransformation>(),
        world.query<SpriteInstance, GlobalTransformation>(),
    });

    registerSpinSystem(world);
    registerAnimationSystems(world);
    registerAnimationClipFlipTestSystem(world);
    registerSpriteAnimatorSystem(world);
    registerTransformSystems(world);
    registerRenderSystems(world);

    if (config.map) {
        registerMapScene(world, assets, scheme, *config.map);
    } else {
        TraceLog(LOG_WARNING, "RENDER: no --map specified; spawning free camera only");
        spawnMainCamera(world, {0.0f, 1.7f, 8.0f}, {0.0f, 1.7f, 7.0f}, PI);
    }
}

}
