#include "render/render_module.hpp"

#include "assets/asset_store.hpp"
#include "assets/skeleton_loader.hpp"
#include "camera/components.hpp"
#include "interact/components.hpp"
#include "map/csg_script.hpp"
#include "physics/components.hpp"
#include "physics/map_collision.hpp"
#include "physics/physics_module.hpp"
#include "render/animation_player.hpp"
#include "render/components.hpp"
#include "render/transform.hpp"
#include "ui/ui_module.hpp"

#include "rlImGui.h"

#include <cmath>
#include <string_view>

#include <rlgl.h>

namespace slopengine {

namespace {

struct AssetServices {
    AssetStore* store = nullptr;
};

struct RenderContext {
    flecs::query<Model3D, GlobalTransformation> worldModelQuery;
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

void renderWorldModel(
    flecs::entity entity,
    Model3D& model,
    GlobalTransformation& globalTransform,
    const Lens& lens) {
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloatV(globalTransform.matrix).v);

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

void registerComponents(flecs::world& world) {
    world.component<LocalTransformation>()
        .add(flecs::With, world.component<GlobalTransformation>());

    world.component<GlobalTransformation>();
    world.component<WorldSpace>();
    world.component<Lens>();
    world.component<Spin>();
    world.component<Model3D>();
    world.component<AnimationPlayer>();
    world.component<AnimationClipFlipTest>();
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
            RenderContext& context = it.world().get_mut<RenderContext>();
            BeginMode3D(lens.camera);
            DrawGrid(10, 1.0f);
            context.worldModelQuery.each([&](flecs::entity modelEntity, Model3D& model, GlobalTransformation& global) {
                if (!modelEntity.has<WorldSpace>()) {
                    return;
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
            EndMode3D();
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
        .run([](flecs::iter&) {
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

    world.entity("MainCamera")
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

    world.entity("MapStatic")
        .add<WorldSpace>()
        .set<LocalTransformation>({
            .position = {0.0f, 0.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        })
        .set<Model3D>({loaded->model, WHITE});

    CharacterMotor motor{};
    FirstPersonController controller{};
    controller.yaw = PI;
    controller.pitch = -0.05f;
    controller.eyeHeight = motor.eyeHeight;
    controller.moveSpeed = motor.moveSpeed;

    Lens lens{};
    lens.camera.position = {0.0f, motor.eyeHeight, 0.0f};
    lens.camera.target = {0.0f, motor.eyeHeight, -1.0f};
    lens.camera.up = {0.0f, 1.0f, 0.0f};
    lens.camera.fovy = 75.0f;
    lens.camera.projection = CAMERA_PERSPECTIVE;

    world.entity("MainCamera")
        .add<PlayerCamera>()
        .add<WorldSpace>()
        .set<Lens>(lens)
        .set<FirstPersonController>(controller)
        .set<CharacterMotor>(motor);

    if (world.has<PhysicsContext>()) {
        PhysicsWorld* physics = world.get_mut<PhysicsContext>().world;
        if (physics != nullptr) {
            addStaticBrushes(*physics, loaded->brushes);
            physics->createPlayerCharacter(0.0f, 0.1f, 0.0f, motor);
        }
    }

    constexpr const char* kHumanAsset = "human01/human01";
    if (assets.hasGeo(kHumanAsset) && assets.hasAnim(kHumanAsset)) {
        const AnimBank* animBank = assets.getAnimBank(kHumanAsset);
        if (animBank != nullptr &&
            animBank->clipIndexByName.find("wave") != animBank->clipIndexByName.end()) {
            const Model humanSource = assets.getGeoModel(kHumanAsset);
            Model humanModel = cloneGeoModelInstance(humanSource);
            if (humanModel.meshCount > 0) {
                if (humanModel.materials != nullptr) {
                    const Material male1591Material = assets.resolveMaterial("male1591");
                    const Material highPolyMaterial = assets.resolveMaterial("high-poly");
                    for (int meshIndex = 0; meshIndex < humanModel.meshCount; ++meshIndex) {
                        humanModel.materials[meshIndex] =
                            meshIndex == 0 ? male1591Material : highPolyMaterial;
                    }
                }

                AnimationPlayer humanAnimation{};
                humanAnimation.animBankPath = kHumanAsset;
                humanAnimation.play("wave", true);

                world.entity("MapHuman01")
                    .add<WorldSpace>()
                    .set<LocalTransformation>({
                        .position = {2.0f, 0.0f, -2.0f},
                        .scale = {0.1f, 0.1f, 0.1f},
                        .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
                    })
                    .set<Spin>({
                        .axis = {0.0f, 1.0f, 0.0f},
                        .speed = 0.4f,
                    })
                    .set<Model3D>({humanModel, WHITE})
                    .set<AnimationPlayer>(humanAnimation);
            }
        }
    }
}

void registerDemoScene(flecs::world& world, AssetStore& assets) {
    spawnMainCamera(world, {0.0f, 1.7f, 8.0f}, {0.0f, 1.7f, 7.0f}, PI);

    constexpr const char* kHumanAsset = "human01/human01";

    if (!assets.hasGeo(kHumanAsset)) {
        TraceLog(LOG_WARNING, "Demo geometry not found at geometry/%s", kHumanAsset);
        return;
    }

    if (!assets.hasAnim(kHumanAsset)) {
        TraceLog(LOG_WARNING, "Demo animation not found at animations/%s", kHumanAsset);
        return;
    }

    const AnimBank* animBank = assets.getAnimBank(kHumanAsset);
    if (animBank == nullptr) {
        TraceLog(LOG_WARNING, "Failed to load animation bank at animations/%s", kHumanAsset);
        return;
    }

    if (animBank->clipIndexByName.find("wave") == animBank->clipIndexByName.end()) {
        TraceLog(LOG_WARNING, "Animation bank '%s' has no 'wave' clip", kHumanAsset);
        return;
    }

    const Model humanSource = assets.getGeoModel(kHumanAsset);
    Model humanModel = cloneGeoModelInstance(humanSource);
    if (humanModel.meshCount == 0) {
        TraceLog(LOG_WARNING, "Failed to load demo geometry at geometry/%s", kHumanAsset);
        return;
    }

    if (humanModel.materials != nullptr) {
        const Material male1591Material = assets.resolveMaterial("male1591");
        const Material highPolyMaterial = assets.resolveMaterial("high-poly");
        for (int meshIndex = 0; meshIndex < humanModel.meshCount; ++meshIndex) {
            humanModel.materials[meshIndex] =
                meshIndex == 0 ? male1591Material : highPolyMaterial;
        }
    }

    AnimationPlayer humanAnimation{};
    humanAnimation.animBankPath = kHumanAsset;
    humanAnimation.play("wave", true);

    flecs::entity human = world.entity("Human01")
        .add<WorldSpace>()
        .set<LocalTransformation>({
            .position = {0.0f, 0.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        })
        .set<Spin>({
            .axis = {0.0f, 1.0f, 0.0f},
            .speed = 0.4f,
        })
        .set<Model3D>({humanModel, WHITE})
        .set<AnimationPlayer>(humanAnimation);

    // Add a ground plane for the world to move in
    Model groundModel = LoadModelFromMesh(GenMeshPlane(10.0f, 10.0f, 10, 10));
    if (groundModel.meshCount > 0) {
        // Apply ground material
        const Material groundMaterial = assets.resolveMaterial("default/ground");
        for (int i = 0; i < groundModel.materialCount; ++i) {
            groundModel.materials[i] = groundMaterial;
        }
        world.entity("GroundPlane")
            .add<WorldSpace>()
            .set<LocalTransformation>({
                .position = {0.0f, 0.0f, 0.0f},
                .scale = {1.0f, 1.0f, 1.0f},
                .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
            })
            .set<Model3D>({groundModel, WHITE});
    }

    // Add a simple obstacle for the world to move around
    Model obstacleModel = LoadModelFromMesh(GenMeshCube(2.0f, 2.0f, 2.0f));
    if (obstacleModel.meshCount > 0) {
        const Material defaultMaterial = assets.resolveMaterial("default/cube");
        for (int i = 0; i < obstacleModel.materialCount; ++i) {
            obstacleModel.materials[i] = defaultMaterial;
        }
        world.entity("WorldObstacle")
            .add<WorldSpace>()
            .set<LocalTransformation>({
                .position = {5.0f, 1.0f, 5.0f},
                .scale = {1.0f, 1.0f, 1.0f},
                .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
            })
            .set<Model3D>({obstacleModel, WHITE})
            .set<Interactable>({
                .prompt = "Inspect obstacle",
                .eventName = "obstacle_inspect",
                .maxDistance = 6.0f,
            });
    }

    constexpr const char* kCubeAsset = "default/cube";
    Model cubeModel = {};
    if (assets.hasGeo(kCubeAsset)) {
        const Model cubeSource = assets.getGeoModel(kCubeAsset);
        cubeModel = cloneGeoModelInstance(cubeSource);
    }
    if (cubeModel.meshCount == 0) {
        cubeModel = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    }

    flecs::entity parentCube = world.entity("Human01Cube")
        .child_of(human)
        .add<WorldSpace>()
        .set<LocalTransformation>({
            .position = {15.0f, 0.0f, 0.0f},
            .scale = {5.0f, 5.0f, 5.0f},
            .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        })
        .set<Spin>({
            .axis = {0.0f, 0.0f, 1.0f},
            .speed = 0.4f,
        })
        .set<Model3D>({cubeModel, RED});

    const Model childCubeModel = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));

    world.entity("Human01CubeChild")
        .child_of(parentCube)
        .add<WorldSpace>()
        .set<LocalTransformation>({
            .position = {3.0f, 0.0f, 0.0f},
            .scale = {3.0f, 3.0f, 3.0f},
            .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        })
        .set<Model3D>({childCubeModel, BLUE});
}

}

void registerRenderModule(
    flecs::world& world,
    AssetStore& assets,
    const AppConfig& config,
    s7_scheme* scheme) {
    registerComponents(world);

    world.set<AssetServices>(AssetServices{&assets});
    world.set<RenderContext>({
        world.query<Model3D, GlobalTransformation>(),
    });

    registerSpinSystem(world);
    registerAnimationSystems(world);
    registerAnimationClipFlipTestSystem(world);
    registerTransformSystems(world);
    registerRenderSystems(world);

    if (config.map) {
        registerMapScene(world, assets, scheme, *config.map);
    } else {
        registerDemoScene(world, assets);
    }
}

}
