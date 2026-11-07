#include "script/player_target.hpp"

#include "net/net_state.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"

namespace slopengine {

flecs::entity scriptPlayer(const flecs::world& world, s7_pointer args) {
    const char* id = kLocalNetPlayerId;
    if (s7_is_pair(args) && s7_is_string(s7_car(args))) {
        id = s7_string(s7_car(args));
    }
    const flecs::entity player = world.lookup(id);
    if (!player.is_valid() || (!player.has<PlayerBody>() && std::string_view(id) != kLocalNetPlayerId)) {
        return {};
    }
    return player;
}

bool scriptPlayerFeet(const flecs::world& world, flecs::entity player, float& x, float& y, float& z) {
    if (world.has<PhysicsContext>()) {
        const PhysicsWorld* physics = world.get<PhysicsContext>().world;
        const std::uint64_t id = static_cast<std::uint64_t>(player.id());
        if (physics != nullptr && physics->hasCharacter(id)) {
            const JPH::RVec3 feet = physics->characterPosition(id);
            x = static_cast<float>(feet.GetX());
            y = static_cast<float>(feet.GetY());
            z = static_cast<float>(feet.GetZ());
            return true;
        }
    }
    if (!player.has<Lens>()) {
        return false;
    }
    const Lens& lens = player.get<Lens>();
    const float eye = player.has<CharacterMotor>() ? player.get<CharacterMotor>().eyeHeight : 1.7f;
    x = lens.camera.position.x;
    y = lens.camera.position.y - eye;
    z = lens.camera.position.z;
    return true;
}

}
