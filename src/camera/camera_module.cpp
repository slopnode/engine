#include "camera/camera_module.hpp"

#include "camera/camera_path_player.hpp"
#include "camera/camera_math.hpp"
#include "camera/components.hpp"
#include "camera/view_recoil.hpp"
#include "input/actions.hpp"
#include "input/input_command.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "physics/components.hpp"
#include "render/components.hpp"
#include "ui/ui_state.hpp"
#include <algorithm>
#include <cmath>

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

namespace {

void registerComponents(flecs::world& world) {
    world.component<PlayerCamera>();
    world.component<FirstPersonScene>();
    world.component<FpLightControl>();
    world.component<FirstPersonController>();
    world.component<ViewEyeOffset>();
    world.component<ViewModelOffset>();
    world.component<ViewRecoil>();
    world.component<FreeFlyCamera>();
    registerCameraPathPlayer(world);
}

constexpr float kMaxPitch = 1.4f;

void registerSystems(flecs::world& world) {
    world.system("ViewRecoil")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!world.get<InputContextStack>().allowsGameplay()) {
                return;
            }
            const flecs::entity player = world.lookup("Player");
            if (!player.is_valid() || !player.has<ViewRecoil>() ||
                !player.has<FirstPersonController>()) {
                return;
            }
            ViewRecoil& recoil = player.get_mut<ViewRecoil>();
            const ViewRecoilLookDelta delta = stepViewRecoil(recoil, GetFrameTime());
            FirstPersonController& controller = player.get_mut<FirstPersonController>();
            if (controller.allowLook) {
                controller.yaw -= delta.right;
                if (controller.allowPitch) {
                    controller.pitch = std::clamp(controller.pitch + delta.up, -kMaxPitch, kMaxPitch);
                }
            }
            if (!player.has<ViewEyeOffset>()) {
                player.set<ViewEyeOffset>({});
            }
            const Vector3 shake = viewRecoilShake(recoil);
            ViewEyeOffset& eye = player.get_mut<ViewEyeOffset>();
            eye.shakeUp = shake.x;
            eye.shakeRight = shake.y;
            eye.shakeRoll = shake.z;
        });

    world.system("FirstPersonController")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            const InputCommand& command = world.get<InputCommand>();
            InputContextStack& contexts = world.get_mut<InputContextStack>();

            if (!contexts.allowsGameplay()) {
                return;
            }

            if (world.has<DebugUiState>() && world.get<DebugUiState>().freeCamera) {
                return;
            }

            const flecs::entity camera = world.lookup("Player");
            if (!camera.is_valid() || !camera.has<PlayerCamera>() || !camera.has<Lens>() ||
                !camera.has<FirstPersonController>()) {
                return;
            }

            Lens& lens = camera.get_mut<Lens>();
            FirstPersonController& controller = camera.get_mut<FirstPersonController>();
            const bool physicsDriven = camera.has<CharacterMotor>();

            const float dt = GetFrameTime();

            if (!physicsDriven) {
                if (controller.allowMove) {
                    const Vector3 forwardFlat =
                        Vector3Normalize({std::sin(controller.yaw), 0.0f, std::cos(controller.yaw)});
                    const Vector3 right = Vector3CrossProduct(forwardFlat, {0.0f, 1.0f, 0.0f});
                    Vector3 movement = Vector3Add(
                        Vector3Scale(forwardFlat, command.moveForward),
                        Vector3Scale(right, command.moveStrafe));

                    if (Vector3LengthSqr(movement) > 0.0f) {
                        movement = Vector3Scale(Vector3Normalize(movement), controller.moveSpeed * dt);
                        lens.camera.position = Vector3Add(lens.camera.position, movement);
                    }
                }

                lens.camera.position.y = controller.eyeHeight;
            }

            if (controller.allowLook) {
                float sensitivity = controller.lookSensitivity;
                if (camera.has<ExtraEye>()) {
                    const ExtraEye& extraEye = camera.get<ExtraEye>();
                    if (extraEye.active && lens.camera.fovy > 0.0f) {
                        const float zoomedTan = std::tan(extraEye.fovy * DEG2RAD * 0.5f);
                        const float baseTan = std::tan(lens.camera.fovy * DEG2RAD * 0.5f);
                        sensitivity *= zoomedTan / baseTan;
                    }
                }

                controller.yaw -= command.look.x * sensitivity;

                if (controller.allowPitch) {
                    controller.pitch -= command.look.y * sensitivity;

                    if (controller.pitch > kMaxPitch) {
                        controller.pitch = kMaxPitch;
                    }
                    if (controller.pitch < -kMaxPitch) {
                        controller.pitch = -kMaxPitch;
                    }
                } else {
                    controller.pitch = 0.0f;
                }
            }

            if (!physicsDriven) {
                const Vector3 forward = forwardFromYawPitch(controller.yaw, controller.pitch);
                lens.camera.target = Vector3Add(lens.camera.position, forward);
                lens.camera.up = {0.0f, 1.0f, 0.0f};
            }
        });

    world.system("FreeFlyCameraController")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!world.has<DebugUiState>() || !world.has<FreeFlyCamera>()) {
                return;
            }

            const bool wantActive = world.get<DebugUiState>().freeCamera;
            FreeFlyCamera& fly = world.get_mut<FreeFlyCamera>();

            if (!wantActive) {
                fly.active = false;
                return;
            }

            if (!fly.active) {
                const flecs::entity player = world.lookup("Player");
                if (player.is_valid() && player.has<Lens>() && player.has<FirstPersonController>()) {
                    fly.position = player.get<Lens>().camera.position;
                    const FirstPersonController& controller = player.get<FirstPersonController>();
                    fly.yaw = controller.yaw;
                    fly.pitch = controller.pitch;
                }
                fly.active = true;
            }

            InputContextStack& contexts = world.get_mut<InputContextStack>();
            if (!contexts.allowsGameplay()) {
                return;
            }

            InputState& input = world.get_mut<InputState>();
            fly.yaw -= input.mouseDelta.x * fly.lookSensitivity;
            fly.pitch -= input.mouseDelta.y * fly.lookSensitivity;
            constexpr float kMaxFlyPitch = 1.53f;
            fly.pitch = std::clamp(fly.pitch, -kMaxFlyPitch, kMaxFlyPitch);

            const float cosPitch = std::cos(fly.pitch);
            const Vector3 forward = Vector3Normalize({
                std::sin(fly.yaw) * cosPitch,
                std::sin(fly.pitch),
                std::cos(fly.yaw) * cosPitch,
            });
            const Vector3 forwardFlat = Vector3Normalize({std::sin(fly.yaw), 0.0f, std::cos(fly.yaw)});
            const Vector3 right = Vector3CrossProduct(forwardFlat, {0.0f, 1.0f, 0.0f});

            Vector3 move{};
            if (input.down(Action::MoveForward)) {
                move = Vector3Add(move, forward);
            }
            if (input.down(Action::MoveBackward)) {
                move = Vector3Subtract(move, forward);
            }
            if (input.down(Action::MoveRight)) {
                move = Vector3Add(move, right);
            }
            if (input.down(Action::MoveLeft)) {
                move = Vector3Subtract(move, right);
            }
            if (input.down(Action::Jump)) {
                move.y += 1.0f;
            }
            if (IsKeyDown(KEY_LEFT_CONTROL)) {
                move.y -= 1.0f;
            }

            float speed = fly.moveSpeed;
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                speed *= fly.fastMultiplier;
            }

            if (Vector3LengthSqr(move) > 1e-8f) {
                const float dt = GetFrameTime();
                fly.position =
                    Vector3Add(fly.position, Vector3Scale(Vector3Normalize(move), speed * dt));
            }
        });
}

}

void registerCameraModule(flecs::world& world) {
    registerComponents(world);
    world.set<FreeFlyCamera>({});
    registerSystems(world);
}

}
