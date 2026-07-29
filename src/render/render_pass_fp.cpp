#include "render/render_pass_fp.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "camera/components.hpp"
#include "map/bsp.hpp"
#include "render/render_pass_world.hpp"
#include "map/light_components.hpp"
#include "map/light_sample.hpp"
#include "physics/components.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"
#include "render/dynamic_light_shadows.hpp"
#include "render/fx_local_light.hpp"
#include "render/hud.hpp"
#include "render/sprite_animator.hpp"
#include "render/sprite_billboard.hpp"
#include "script/first_person_script.hpp"
#include "script/hook_registry.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "external/glad.h"

namespace slopengine {

namespace {

Color sampleFirstPersonRadTint(
    Vector3 feetOrigin,
    const MapLighting* lighting,
    const std::vector<RankedDynamicLight>* dynamicLights,
    const FxLightFrameState* fxLights,
    bool unlit) {
    if (unlit) {
        return WHITE;
    }

    Color tint = lighting != nullptr ? lighting->ambient : WHITE;
    if (lighting != nullptr && lighting->available) {
        constexpr Vector3 kDown{0.0f, -1.0f, 0.0f};
        constexpr float kMaxDist = 2.5f;
        const Vector3 offsets[] = {
            {0.0f, 0.0f, 0.0f},
            {0.12f, 0.0f, 0.0f},
            {-0.12f, 0.0f, 0.0f},
            {0.0f, 0.0f, 0.12f},
            {0.0f, 0.0f, -0.12f},
        };
        int sampleCount = 0;
        int sumR = 0;
        int sumG = 0;
        int sumB = 0;
        for (const Vector3& offset : offsets) {
            const Vector3 origin = Vector3Add(feetOrigin, offset);
            if (auto sampled = sampleMapLight(*lighting, origin, kDown, kMaxDist)) {
                sumR += sampled->r;
                sumG += sampled->g;
                sumB += sampled->b;
                ++sampleCount;
            }
        }
        if (sampleCount > 0) {
            tint = Color{
                static_cast<unsigned char>(sumR / sampleCount),
                static_cast<unsigned char>(sumG / sampleCount),
                static_cast<unsigned char>(sumB / sampleCount),
                255,
            };
        }
    }

    const QuadBvh* occlusionBvh =
        (lighting != nullptr && lighting->available && !lighting->surfaceBvh.empty())
            ? &lighting->surfaceBvh
            : nullptr;
    tint = addLinearRgbToColor(
        tint,
        evaluateOverlayLightsAtPoint(
            dynamicLights, fxLights, feetOrigin, {0.0f, 1.0f, 0.0f}, occlusionBvh));
    return tint;
}

Color smoothFirstPersonRadTint(FirstPersonScene& scene, Color target, float dt) {
    const Vector3 targetRgb{
        static_cast<float>(target.r) / 255.0f,
        static_cast<float>(target.g) / 255.0f,
        static_cast<float>(target.b) / 255.0f,
    };
    if (!scene.radTintInitialized) {
        scene.radTintSmoothed = targetRgb;
        scene.radTintInitialized = true;
    } else {
        constexpr float kTintSmoothHz = 6.0f;
        const float alpha = 1.0f - std::exp(-kTintSmoothHz * std::max(dt, 0.0f));
        scene.radTintSmoothed.x += (targetRgb.x - scene.radTintSmoothed.x) * alpha;
        scene.radTintSmoothed.y += (targetRgb.y - scene.radTintSmoothed.y) * alpha;
        scene.radTintSmoothed.z += (targetRgb.z - scene.radTintSmoothed.z) * alpha;
    }
    return Color{
        static_cast<unsigned char>(std::clamp(scene.radTintSmoothed.x * 255.0f, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(scene.radTintSmoothed.y * 255.0f, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(scene.radTintSmoothed.z * 255.0f, 0.0f, 255.0f)),
        255,
    };
}

} // namespace

void drawFirstPersonPass(
    flecs::world& world,
    RenderContext& context,
    const Lens& lens,
    bool unlit) {
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    glClear(GL_DEPTH_BUFFER_BIT);

    Camera3D eyeCam{};
    eyeCam.position = {0.0f, 0.0f, 0.0f};
    eyeCam.target = {0.0f, 0.0f, 1.0f};
    eyeCam.up = {0.0f, 1.0f, 0.0f};
    eyeCam.fovy = lens.camera.fovy;
    eyeCam.projection = CAMERA_PERSPECTIVE;

    Color fpTint = WHITE;
    flecs::entity playerEntity{};
    if (world.has<PlayerEntity>()) {
        playerEntity = world.get<PlayerEntity>().entity;
    }
    const bool hasFpScene =
        playerEntity.is_valid() && playerEntity.has<FirstPersonScene>();
    const bool useRadTint =
        hasFpScene && playerEntity.get<FirstPersonScene>().useRadTint;
    const bool useShading =
        hasFpScene && playerEntity.get<FirstPersonScene>().useShading;
    if (useRadTint) {
        float eyeHeight = 1.7f;
        if (playerEntity.has<FirstPersonController>()) {
            eyeHeight = playerEntity.get<FirstPersonController>().eyeHeight;
        }
        if (playerEntity.has<CharacterMotor>()) {
            eyeHeight = playerEntity.get<CharacterMotor>().eyeHeight;
        }
        const Vector3 feetOrigin{
            lens.camera.position.x,
            lens.camera.position.y - eyeHeight + 0.05f,
            lens.camera.position.z,
        };
        const MapLighting* fpLighting =
            world.has<MapLighting>() ? &world.get<MapLighting>() : nullptr;
        const std::vector<RankedDynamicLight>* fpDyn =
            (!unlit && world.has<DynamicLightFrameState>())
                ? &world.get<DynamicLightFrameState>().lights
                : nullptr;
        const FxLightFrameState* fpFx =
            (!unlit && world.has<FxLightFrameState>()) ? &world.get<FxLightFrameState>() : nullptr;
        const Color targetTint =
            sampleFirstPersonRadTint(feetOrigin, fpLighting, fpDyn, fpFx, unlit);
        fpTint = smoothFirstPersonRadTint(
            playerEntity.get_mut<FirstPersonScene>(),
            targetTint,
            GetFrameTime());
    }

    FirstPersonViewShader* viewShader =
        world.has<FirstPersonViewShader>() ? &world.get_mut<FirstPersonViewShader>()
                                           : nullptr;
    const bool shadedDraw = useShading && viewShader != nullptr && viewShader->valid();
    if (shadedDraw) {
        const Vector3 probeRgb{
            static_cast<float>(fpTint.r) / 255.0f,
            static_cast<float>(fpTint.g) / 255.0f,
            static_cast<float>(fpTint.b) / 255.0f,
        };
        const Vector3 keyDir = Vector3Normalize({0.4f, 0.85f, 0.35f});
        const float ambient = 0.4f;
        const float keyStrength = 0.75f;
        const float rimStrength = 0.15f;
        if (viewShader->probeRgbLoc >= 0) {
            SetShaderValue(
                viewShader->shader,
                viewShader->probeRgbLoc,
                &probeRgb,
                SHADER_UNIFORM_VEC3);
        }
        if (viewShader->ambientLoc >= 0) {
            SetShaderValue(
                viewShader->shader,
                viewShader->ambientLoc,
                &ambient,
                SHADER_UNIFORM_FLOAT);
        }
        if (viewShader->keyDirLoc >= 0) {
            SetShaderValue(
                viewShader->shader,
                viewShader->keyDirLoc,
                &keyDir,
                SHADER_UNIFORM_VEC3);
        }
        if (viewShader->keyStrengthLoc >= 0) {
            SetShaderValue(
                viewShader->shader,
                viewShader->keyStrengthLoc,
                &keyStrength,
                SHADER_UNIFORM_FLOAT);
        }
        if (viewShader->rimStrengthLoc >= 0) {
            SetShaderValue(
                viewShader->shader,
                viewShader->rimStrengthLoc,
                &rimStrength,
                SHADER_UNIFORM_FLOAT);
        }
    }

    BeginMode3D(eyeCam);
    rlScalef(-1.0f, 1.0f, 1.0f);
    rlDisableBackfaceCulling();
    context.viewModelQuery.each([&](flecs::entity modelEntity, Model3D& model, GlobalTransformation& global) {
        const Color previousColor = model.color;
        if (useRadTint && !shadedDraw) {
            model.color = fpTint;
        }

        std::vector<Shader> previousShaders;
        if (shadedDraw) {
            previousShaders.resize(static_cast<std::size_t>(model.model.materialCount));
            for (int i = 0; i < model.model.materialCount; ++i) {
                previousShaders[static_cast<std::size_t>(i)] = model.model.materials[i].shader;
                model.model.materials[i].shader = viewShader->shader;
            }
            if (viewShader->matModelLoc >= 0) {
                SetShaderValueMatrix(
                    viewShader->shader,
                    viewShader->matModelLoc,
                    global.matrix);
            }
        }

        renderWorldModel(modelEntity, model, global, lens, unlit);

        if (shadedDraw) {
            for (int i = 0; i < model.model.materialCount; ++i) {
                model.model.materials[i].shader =
                    previousShaders[static_cast<std::size_t>(i)];
            }
        }
        model.color = previousColor;
    });
    rlEnableBackfaceCulling();
    EndMode3D();
}

void drawViewSprites(flecs::world& world) {
    if (!world.has<AssetServices>() || world.get<AssetServices>().store == nullptr) {
        return;
    }
    AssetStore& viewAssets = *world.get_mut<AssetServices>().store;
    ViewCanvas viewCanvas{};
    if (world.has<ViewCanvas>()) {
        viewCanvas = world.get<ViewCanvas>();
    }
    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());
    const ViewCanvasFit viewFit = makeViewCanvasFit(
        viewCanvas.width, viewCanvas.height, screenW, screenH);

    BeginBlendMode(BLEND_ALPHA);
    BlendMode activeBlend = BLEND_ALPHA;
    auto drawViewSprite =
        [&](flecs::entity entity, ViewSprite& viewSprite, SpriteInstance& sprite) {
            const auto frame = resolveViewSpriteFrame(sprite, viewAssets);
            if (!frame) {
                return;
            }

            BlendMode want = BLEND_ALPHA;
            if (const SpriteAsset* asset = viewAssets.getSpriteAsset(sprite.sprite);
                asset != nullptr && asset->blend == SpriteBlendMode::Additive) {
                want = BLEND_ADD_COLORS;
            }
            if (want != activeBlend) {
                EndBlendMode();
                activeBlend = want;
                BeginBlendMode(activeBlend);
            }

            auto originFromFrame = [&](const ViewSpriteFrame& resolved) {
                if (resolved.hasOffset && resolved.pixelWidth > 0 && resolved.pixelHeight > 0) {
                    return Vector2{
                        static_cast<float>(resolved.offsetX) /
                            static_cast<float>(resolved.pixelWidth),
                        static_cast<float>(resolved.offsetY) /
                            static_cast<float>(resolved.pixelHeight),
                    };
                }
                return Vector2{viewSprite.originX, viewSprite.originY};
            };

            float originX = originFromFrame(*frame).x;
            float originY = originFromFrame(*frame).y;
            float rotationDeg = frame->rotationDeg + frame->animRotationDeg;
            float scaleX = frame->scaleX * frame->animScaleX;
            float scaleY = frame->scaleY * frame->animScaleY;
            float translateX = frame->translateX + frame->animTranslateX;
            float translateY = frame->translateY + frame->animTranslateY;

            if (entity.has<SpriteAnimator>()) {
                const SpriteAnimator& animator = entity.get<SpriteAnimator>();
                if (animator.hasTween() && !animator.nextFrame.empty()) {
                    SpriteInstance nextSprite = sprite;
                    nextSprite.frame = animator.nextFrame;
                    const auto nextFrame = resolveViewSpriteFrame(nextSprite, viewAssets);
                    if (nextFrame) {
                        const float blend = animator.transformBlend;
                        const float nextRotation =
                            nextFrame->rotationDeg + nextFrame->animRotationDeg;
                        const float nextScaleX = nextFrame->scaleX * nextFrame->animScaleX;
                        const float nextScaleY = nextFrame->scaleY * nextFrame->animScaleY;
                        const float nextTranslateX =
                            nextFrame->translateX + nextFrame->animTranslateX;
                        const float nextTranslateY =
                            nextFrame->translateY + nextFrame->animTranslateY;
                        if (animator.tweenRotation) {
                            rotationDeg = rotationDeg + (nextRotation - rotationDeg) * blend;
                        }
                        if (animator.tweenScale) {
                            scaleX = scaleX + (nextScaleX - scaleX) * blend;
                            scaleY = scaleY + (nextScaleY - scaleY) * blend;
                        }
                        if (animator.tweenTranslate) {
                            translateX = translateX + (nextTranslateX - translateX) * blend;
                            translateY = translateY + (nextTranslateY - translateY) * blend;
                        }
                    }
                }
            }

            const float destW = static_cast<float>(frame->pixelWidth) * viewFit.scale *
                                viewSprite.scaleX * scaleX;
            const float destH = static_cast<float>(frame->pixelHeight) * viewFit.scale *
                                viewSprite.scaleY * scaleY;
            const float screenX =
                viewFit.offsetX +
                (viewSprite.canvasX + viewSprite.offsetX + translateX) * viewFit.scale;
            const float screenY =
                viewFit.offsetY +
                (viewSprite.canvasY + viewSprite.offsetY + translateY) * viewFit.scale;
            const Rectangle dest{screenX, screenY, destW, destH};
            Color tint = WHITE;
            if (const SpriteAsset* asset = viewAssets.getSpriteAsset(sprite.sprite);
                asset != nullptr) {
                tint = asset->tint;
            }
            DrawTexturePro(
                *frame->texture,
                frame->source,
                dest,
                Vector2{destW * originX, destH * originY},
                viewSprite.rotationDeg + rotationDeg,
                tint);
        };

    struct ViewDrawItem {
        flecs::entity entity{};
        ViewSprite* viewSprite = nullptr;
        SpriteInstance* sprite = nullptr;
        int layer = 0;
    };
    std::vector<ViewDrawItem> viewDrawList;
    world.each([&](flecs::entity entity, ViewSprite& viewSprite, SpriteInstance& sprite) {
        viewDrawList.push_back(ViewDrawItem{
            entity,
            &viewSprite,
            &sprite,
            entity.has<SpriteOverlay>() ? entity.get<SpriteOverlay>().layer : 0,
        });
    });
    std::sort(
        viewDrawList.begin(),
        viewDrawList.end(),
        [](const ViewDrawItem& a, const ViewDrawItem& b) {
            return a.layer < b.layer;
        });
    for (const ViewDrawItem& item : viewDrawList) {
        drawViewSprite(item.entity, *item.viewSprite, *item.sprite);
    }
    EndBlendMode();
}

void drawHud(flecs::world& world) {
    if (!world.has<AssetServices>() || world.get<AssetServices>().store == nullptr) {
        return;
    }
    AssetStore& viewAssets = *world.get_mut<AssetServices>().store;
    HudCanvas hudCanvas{};
    if (world.has<HudCanvas>()) {
        hudCanvas = world.get<HudCanvas>();
    }
    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());
    const ViewCanvasFit hudFit = makeViewCanvasFit(
        hudCanvas.width, hudCanvas.height, screenW, screenH);

    if (!world.has<HudDrawList>()) {
        world.set<HudDrawList>({});
    }
    if (!world.has<HudFontCache>()) {
        world.set<HudFontCache>({});
    }
    HudDrawList& hud = world.get_mut<HudDrawList>();
    hud.clear();
    if (world.has<ScriptContext>() && world.get<ScriptContext>().scheme != nullptr) {
        callHook(world.get<ScriptContext>().scheme, "draw-hud", ScriptScope::Hud);
    }
    flushHudDrawList(hud, viewAssets, world.get_mut<HudFontCache>(), hudFit);
}

void drawSpriteAimHudText(const std::string& spriteAimStatus) {
    if (spriteAimStatus.empty()) {
        return;
    }
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

}
