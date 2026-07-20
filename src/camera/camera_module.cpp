#include "camera/camera_module.hpp"

#include "camera/components.hpp"
#include "input/actions.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "physics/components.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"

#include <cmath>

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

namespace {

constexpr float kFlashlightIntensity = 1.35f;
constexpr float kFlashlightRange = 11.0f;
constexpr float kFlashlightCone = 0.55f;
constexpr float kFlashlightForwardOffset = 0.12f;
constexpr const char* kFlashlightEntityName = "player_flashlight_light";

Vector3 forwardFromYawPitch(float yaw, float pitch) {
    const float cosPitch = std::cos(pitch);
    return Vector3Normalize({
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        std::cos(yaw) * cosPitch,
    });
}

Matrix makeTrsMatrix(Vector3 position, Quaternion rotation, Vector3 scale) {
    const Matrix s = MatrixScale(scale.x, scale.y, scale.z);
    const Matrix r = QuaternionToMatrix(rotation);
    const Matrix t = MatrixTranslate(position.x, position.y, position.z);
    return MatrixMultiply(t, MatrixMultiply(r, s));
}

flecs::entity ensurePlayerFlashlightLight(flecs::world& world, PlayerFlashlight& flashlight) {
    if (flashlight.light != 0) {
        flecs::entity existing = world.entity(flashlight.light);
        if (existing.is_valid() && existing.has<DynamicLight>()) {
            return existing;
        }
        flashlight.light = 0;
    }

    flecs::entity named = world.lookup(kFlashlightEntityName);
    if (named.is_valid() && named.has<DynamicLight>()) {
        flashlight.light = named.id();
        return named;
    }

    DynamicLight dyn{};
    dyn.kind = DynamicLightKind::Spot;
    dyn.intensity = 0.0f;
    dyn.range = kFlashlightRange;
    dyn.coneAngle = kFlashlightCone;
    dyn.castShadows = false;
    setDynamicLightRgb(dyn, {1.0f, 0.96f, 0.88f});
    flecs::entity light = spawnDynamicLight(
        world,
        kFlashlightEntityName,
        {0.0f, 0.0f, 0.0f},
        QuaternionIdentity(),
        dyn);
    flashlight.light = light.id();
    return light;
}

void syncPlayerFlashlight(flecs::world& world) {
    const flecs::entity player = world.lookup("Player");
    if (!player.is_valid() || !player.has<Lens>() || !player.has<PlayerFlashlight>()) {
        return;
    }

    PlayerFlashlight& flashlight = player.get_mut<PlayerFlashlight>();
    flecs::entity light = ensurePlayerFlashlightLight(world, flashlight);
    if (!light.is_valid() || !light.has<DynamicLight>() || !light.has<LocalTransformation>()) {
        return;
    }

    const Lens& lens = player.get<Lens>();
    Vector3 forward = Vector3Subtract(lens.camera.target, lens.camera.position);
    if (Vector3LengthSqr(forward) < 1e-8f) {
        forward = {0.0f, 0.0f, 1.0f};
    } else {
        forward = Vector3Normalize(forward);
    }

    LocalTransformation& local = light.get_mut<LocalTransformation>();
    local.position = Vector3Add(lens.camera.position, Vector3Scale(forward, kFlashlightForwardOffset));
    local.rotation = QuaternionFromVector3ToVector3({0.0f, 0.0f, 1.0f}, forward);
    local.scale = {1.0f, 1.0f, 1.0f};
    if (light.has<GlobalTransformation>()) {
        light.get_mut<GlobalTransformation>().matrix =
            makeTrsMatrix(local.position, local.rotation, local.scale);
    }

    DynamicLight& dyn = light.get_mut<DynamicLight>();
    dyn.kind = DynamicLightKind::Spot;
    dyn.range = kFlashlightRange;
    dyn.coneAngle = kFlashlightCone;
    dyn.castShadows = false;
    dyn.intensity = flashlight.enabled ? kFlashlightIntensity : 0.0f;
}

void registerComponents(flecs::world& world) {
    world.component<PlayerCamera>();
    world.component<PlayerFlashlight>();
    world.component<FirstPersonController>();
}

void registerSystems(flecs::world& world) {
    world.system("FirstPersonController")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            InputState& input = it.world().get_mut<InputState>();
            InputContextStack& contexts = it.world().get_mut<InputContextStack>();

            if (!contexts.allowsGameplay()) {
                return;
            }

            const flecs::entity camera = it.world().lookup("Player");
            if (!camera.is_valid() || !camera.has<PlayerCamera>() || !camera.has<Lens>() ||
                !camera.has<FirstPersonController>()) {
                return;
            }

            Lens& lens = camera.get_mut<Lens>();
            FirstPersonController& controller = camera.get_mut<FirstPersonController>();
            const bool physicsDriven = camera.has<CharacterMotor>();

            const float dt = GetFrameTime();

            if (!physicsDriven) {
                const Vector3 forwardFlat =
                    Vector3Normalize({std::sin(controller.yaw), 0.0f, std::cos(controller.yaw)});
                const Vector3 right = Vector3CrossProduct(forwardFlat, {0.0f, 1.0f, 0.0f});
                Vector3 movement{};

                if (input.down(Action::MoveForward)) {
                    movement = Vector3Add(movement, forwardFlat);
                }
                if (input.down(Action::MoveBackward)) {
                    movement = Vector3Subtract(movement, forwardFlat);
                }
                if (input.down(Action::MoveLeft)) {
                    movement = Vector3Subtract(movement, right);
                }
                if (input.down(Action::MoveRight)) {
                    movement = Vector3Add(movement, right);
                }

                if (Vector3LengthSqr(movement) > 0.0f) {
                    movement = Vector3Scale(Vector3Normalize(movement), controller.moveSpeed * dt);
                    lens.camera.position = Vector3Add(lens.camera.position, movement);
                }

                lens.camera.position.y = controller.eyeHeight;
            }

            controller.yaw -= input.mouseDelta.x * controller.lookSensitivity;
            controller.pitch -= input.mouseDelta.y * controller.lookSensitivity;

            constexpr float kMaxPitch = 1.4f;
            if (controller.pitch > kMaxPitch) {
                controller.pitch = kMaxPitch;
            }
            if (controller.pitch < -kMaxPitch) {
                controller.pitch = -kMaxPitch;
            }

            if (!physicsDriven) {
                const Vector3 forward = forwardFromYawPitch(controller.yaw, controller.pitch);
                lens.camera.target = Vector3Add(lens.camera.position, forward);
                lens.camera.up = {0.0f, 1.0f, 0.0f};
            }
        });

    world.system("PlayerFlashlightSystem")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            InputState& input = world.get_mut<InputState>();
            InputContextStack& contexts = world.get_mut<InputContextStack>();

            flecs::entity player = world.lookup("Player");
            if (!player.is_valid()) {
                return;
            }
            if (!player.has<PlayerFlashlight>()) {
                player.set<PlayerFlashlight>({});
            }

            if (contexts.allowsGameplay() && input.pressed(Action::Flashlight)) {
                PlayerFlashlight& flashlight = player.get_mut<PlayerFlashlight>();
                flashlight.enabled = !flashlight.enabled;
            }

            syncPlayerFlashlight(world);
        });
}

}

void registerCameraModule(flecs::world& world) {
    registerComponents(world);
    registerSystems(world);
}

}
