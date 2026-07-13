#include "interact/interact_module.hpp"

#include "camera/components.hpp"
#include "input/actions.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "interact/components.hpp"
#include "render/components.hpp"

#include <limits>

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

namespace {

Ray cameraRay(const Lens& lens) {
    Ray ray{};
    ray.position = lens.camera.position;
    ray.direction = Vector3Normalize(Vector3Subtract(lens.camera.target, lens.camera.position));
    return ray;
}

bool tryGetPlayerLens(flecs::world world, Lens& outLens) {
    const flecs::entity camera = world.lookup("MainCamera");
    if (!camera.is_valid() || !camera.has<PlayerCamera>() || !camera.has<Lens>()) {
        return false;
    }

    outLens = camera.get<Lens>();
    return true;
}

bool rayHitsModel(const Ray& ray, const Model& model, const Matrix& transform, float maxDistance, float& hitDistance) {
    bool hit = false;
    float closest = maxDistance;

    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        const RayCollision collision = GetRayCollisionMesh(ray, model.meshes[meshIndex], transform);
        if (collision.hit && collision.distance < closest) {
            closest = collision.distance;
            hit = true;
        }
    }

    if (hit) {
        hitDistance = closest;
    }
    return hit;
}

void registerComponents(flecs::world& world) {
    world.component<Interactable>();
    world.component<InteractionTarget>();
}

void registerSystems(flecs::world& world) {
    world.system("UpdateInteractionTarget")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            InteractionTarget& target = it.world().get_mut<InteractionTarget>();
            InputContextStack& contexts = it.world().get_mut<InputContextStack>();

            target.entity = {};
            target.distance = 0.0f;
            target.prompt.clear();
            target.eventName.clear();

            if (!contexts.allowsGameplay()) {
                return;
            }

            Lens lens{};
            if (!tryGetPlayerLens(it.world(), lens)) {
                return;
            }

            const Ray ray = cameraRay(lens);
            float bestDistance = std::numeric_limits<float>::max();
            flecs::entity bestEntity{};

            it.world()
                .query<Interactable, Model3D, GlobalTransformation>()
                .each([&](flecs::entity entity, Interactable& interactable, Model3D& model3d, GlobalTransformation& global) {
                    if (!entity.has<WorldSpace>()) {
                        return;
                    }

                    float hitDistance = 0.0f;
                    if (!rayHitsModel(ray, model3d.model, global.matrix, interactable.maxDistance, hitDistance)) {
                        return;
                    }

                    if (hitDistance < bestDistance) {
                        bestDistance = hitDistance;
                        bestEntity = entity;
                        target.prompt = interactable.prompt;
                        target.eventName = interactable.eventName;
                    }
                });

            if (bestEntity.is_valid()) {
                target.entity = bestEntity;
                target.distance = bestDistance;
            }
        });

    world.system("HandleInteractAction")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            InteractionTarget& target = it.world().get_mut<InteractionTarget>();
            InputState& input = it.world().get_mut<InputState>();
            InputContextStack& contexts = it.world().get_mut<InputContextStack>();

            if (!contexts.allowsGameplay() || !target.entity.is_valid() || !input.pressed(Action::Interact)) {
                return;
            }

            contexts.push(InputContext::InteractUI);
            input.mouseDelta = {};
        });
}

}

void registerInteractModule(flecs::world& world) {
    registerComponents(world);
    world.set<InteractionTarget>({});
    registerSystems(world);
}

}