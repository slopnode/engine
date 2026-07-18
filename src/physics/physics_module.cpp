#include "physics/physics_module.hpp"

#include "camera/components.hpp"
#include "input/actions.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "physics/components.hpp"
#include "render/components.hpp"
#include "ui/ui_state.hpp"

#include <raylib.h>
#include <raymath.h>

#include <cmath>

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

} // namespace

void registerPhysicsModule(flecs::world& world, PhysicsWorld* physics) {
    world.component<CharacterMotor>();
    world.set<PhysicsContext>(PhysicsContext{physics});

    world.system("CharacterMotorInput")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            if (!it.world().has<PhysicsContext>() || !it.world().has<InputState>()) {
                return;
            }

            PhysicsContext& physics = it.world().get_mut<PhysicsContext>();
            if (physics.world == nullptr || !physics.world->hasPlayer()) {
                return;
            }

            InputContextStack& contexts = it.world().get_mut<InputContextStack>();
            if (!contexts.allowsGameplay()) {
                return;
            }

            const flecs::entity camera = it.world().lookup("Player");
            if (!camera.is_valid() || !camera.has<CharacterMotor>() || !camera.has<FirstPersonController>()) {
                return;
            }

            CharacterMotor& motor = camera.get_mut<CharacterMotor>();
            FirstPersonController& controller = camera.get_mut<FirstPersonController>();
            InputState& input = it.world().get_mut<InputState>();

            const Vector3 forwardFlat =
                Vector3Normalize({std::sin(controller.yaw), 0.0f, std::cos(controller.yaw)});
            const Vector3 right = Vector3CrossProduct(forwardFlat, {0.0f, 1.0f, 0.0f});
            Vector3 wish{};

            if (input.down(Action::MoveForward)) {
                wish = Vector3Add(wish, forwardFlat);
            }
            if (input.down(Action::MoveBackward)) {
                wish = Vector3Subtract(wish, forwardFlat);
            }
            if (input.down(Action::MoveLeft)) {
                wish = Vector3Subtract(wish, right);
            }
            if (input.down(Action::MoveRight)) {
                wish = Vector3Add(wish, right);
            }

            motor.wishX = wish.x;
            motor.wishZ = wish.z;
        });

    world.system("PhysicsStep")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            if (!it.world().has<PhysicsContext>()) {
                return;
            }

            PhysicsContext& physics = it.world().get_mut<PhysicsContext>();
            if (physics.world == nullptr || !physics.world->hasPlayer()) {
                return;
            }

            const flecs::entity camera = it.world().lookup("Player");
            if (!camera.is_valid() || !camera.has<CharacterMotor>() || !camera.has<Lens>() ||
                !camera.has<FirstPersonController>()) {
                return;
            }

            CharacterMotor& motor = camera.get_mut<CharacterMotor>();
            FirstPersonController& controller = camera.get_mut<FirstPersonController>();
            Lens& lens = camera.get_mut<Lens>();

            const bool noclip =
                it.world().has<DebugUiState>() && it.world().get<DebugUiState>().noclip;
            physics.world->update(GetFrameTime(), motor, noclip);

            const JPH::RVec3 feet = physics.world->playerPosition();
            lens.camera.position = {
                static_cast<float>(feet.GetX()),
                static_cast<float>(feet.GetY()) + motor.eyeHeight,
                static_cast<float>(feet.GetZ()),
            };

            const Vector3 forward = forwardFromYawPitch(controller.yaw, controller.pitch);
            lens.camera.target = Vector3Add(lens.camera.position, forward);
            lens.camera.up = {0.0f, 1.0f, 0.0f};
        });
}

void unregisterPhysicsModule(flecs::world& world) {
    if (world.has<PhysicsContext>()) {
        world.remove<PhysicsContext>();
    }
}

}
