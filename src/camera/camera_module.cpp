#include "camera/camera_module.hpp"

#include "camera/components.hpp"
#include "input/actions.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "physics/components.hpp"
#include "render/components.hpp"
#include <cmath>

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

namespace {

Vector3 forwardFromYawPitch(float yaw, float pitch) {
    const float cosPitch = std::cos(pitch);
    return Vector3Normalize({
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        std::cos(yaw) * cosPitch,
    });
}

void registerComponents(flecs::world& world) {
    world.component<PlayerCamera>();
    world.component<FirstPersonScene>();
    world.component<FpLightControl>();
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
}

}

void registerCameraModule(flecs::world& world) {
    registerComponents(world);
    registerSystems(world);
}

}
