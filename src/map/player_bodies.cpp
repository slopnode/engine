#include "map/player_bodies.hpp"

#include "camera/camera_math.hpp"
#include "camera/components.hpp"
#include "input/player_command.hpp"
#include "map/bsp.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "physics/trigger_components.hpp"
#include "render/components.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

constexpr float kSpawnRing = 1.2f;

}

flecs::entity spawnPlayerBody(flecs::world& world, std::string_view name, int index) {
    MapPlayerStart start = world.has<MapPlayerStart>() ? world.get<MapPlayerStart>() : MapPlayerStart{};
    const flecs::entity mapPlayer = world.lookup("Player");
    if (mapPlayer.is_valid() && mapPlayer.has<CharacterMotor>()) {
        start.motor = mapPlayer.get<CharacterMotor>();
        start.motor.wishX = 0.0f;
        start.motor.wishY = 0.0f;
        start.motor.wishZ = 0.0f;
    }
    const float angle = static_cast<float>(index) * 2.39996323f;
    const Vector3 position{
        start.position.x + kSpawnRing * std::cos(angle),
        start.position.y,
        start.position.z + kSpawnRing * std::sin(angle),
    };
    FirstPersonController controller{};
    controller.yaw = start.yaw;
    controller.pitch = start.pitch;
    controller.eyeHeight = start.motor.eyeHeight;
    controller.moveSpeed = start.motor.moveSpeed;
    Lens lens{};
    lens.camera.position = {position.x, position.y + start.motor.eyeHeight, position.z};
    lens.camera.target = Vector3Add(lens.camera.position, forwardFromYawPitch(start.yaw, start.pitch));
    lens.camera.up = {0.0f, 1.0f, 0.0f};
    lens.camera.fovy = 75.0f;
    lens.camera.projection = CAMERA_PERSPECTIVE;

    flecs::entity body = world.entity(std::string(name).c_str())
        .add<MapOwned>()
        .add<PlayerBody>()
        .set<FirstPersonController>(controller)
        .set<Lens>(lens)
        .set<CharacterMotor>(start.motor)
        .set<CollisionTags>(CollisionTags{{"player"}})
        .set<PlayerCommand>({})
        .set<LocalTransformation>(
            LocalTransformation{position, {1.0f, 1.0f, 1.0f}, QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, start.yaw)});
    if (world.has<PhysicsContext>()) {
        if (PhysicsWorld* physics = world.get_mut<PhysicsContext>().world) {
            physics->createCharacter(static_cast<std::uint64_t>(body.id()), position.x, position.y, position.z, start.motor);
            physics->markPlayerCharacter(static_cast<std::uint64_t>(body.id()));
        }
    }
    return body;
}

void despawnPlayerBody(flecs::world& world, flecs::entity body) {
    if (!body.is_alive()) {
        return;
    }
    if (world.has<PhysicsContext>()) {
        if (PhysicsWorld* physics = world.get_mut<PhysicsContext>().world) {
            physics->destroyCharacter(static_cast<std::uint64_t>(body.id()));
        }
    }
    body.destruct();
}

std::vector<std::string> playerBodyIds(const flecs::world& world) {
    std::vector<std::string> ids;
    world.each([&](flecs::entity entity, PlayerBody) {
        if (entity.name() != nullptr) {
            ids.emplace_back(entity.name().c_str());
        }
    });
    std::sort(ids.begin(), ids.end());
    return ids;
}

}
