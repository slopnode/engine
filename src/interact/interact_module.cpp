#include "interact/interact_module.hpp"

#include "assets/asset_services.hpp"
#include "camera/components.hpp"
#include "input/actions.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "interact/components.hpp"
#include "render/components.hpp"
#include "render/sprite_billboard.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"

#include <limits>
#include <string>

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
    const flecs::entity camera = world.lookup("Player");
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

bool tryCallUseHandler(s7_scheme* scheme, const std::string& handlerName, const std::string& entityId) {
    return tryCallSchemeProc1String(scheme, handlerName, entityId, ScriptScope::World);
}

bool rayHitsFaceUseSurface(
    const Ray& ray,
    const FaceUseSurface& surface,
    float maxDistance,
    float& hitDistance) {
    if (surface.vertices.size() < 3) {
        return false;
    }
    bool hit = false;
    float closest = maxDistance;
    const Vector3& v0 = surface.vertices[0];
    for (std::size_t i = 1; i + 1 < surface.vertices.size(); ++i) {
        const RayCollision collision =
            GetRayCollisionTriangle(ray, v0, surface.vertices[i], surface.vertices[i + 1]);
        if (collision.hit && collision.distance >= 0.0f && collision.distance < closest) {
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
    world.component<FaceUseSurface>();
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
            std::string bestPrompt;
            std::string bestEventName;

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
                        bestPrompt = interactable.prompt;
                        bestEventName = interactable.eventName;
                    }
                });

            if (it.world().has<AssetServices>()) {
                AssetStore* store = it.world().get<AssetServices>().store;
                if (store != nullptr) {
                    it.world()
                        .query<Interactable, SpriteInstance, GlobalTransformation>()
                        .each([&](flecs::entity entity,
                                  Interactable& interactable,
                                  SpriteInstance& sprite,
                                  GlobalTransformation& global) {
                            if (!entity.has<WorldSpace>()) {
                                return;
                            }

                            const auto billboard = resolveSpriteBillboard(sprite, global, lens, *store);
                            if (!billboard) {
                                return;
                            }

                            const auto hit = raycastSpriteBillboard(ray, *billboard, interactable.maxDistance);
                            if (!hit || hit->distance >= bestDistance) {
                                return;
                            }

                            bestDistance = hit->distance;
                            bestEntity = entity;
                            bestPrompt = interactable.prompt;
                            bestEventName = interactable.eventName;
                        });
                }
            }

            it.world()
                .query<Interactable, FaceUseSurface>()
                .each([&](flecs::entity entity, Interactable& interactable, FaceUseSurface& surface) {
                    if (!entity.has<WorldSpace>()) {
                        return;
                    }

                    float hitDistance = 0.0f;
                    if (!rayHitsFaceUseSurface(ray, surface, interactable.maxDistance, hitDistance)) {
                        return;
                    }

                    if (hitDistance < bestDistance) {
                        bestDistance = hitDistance;
                        bestEntity = entity;
                        bestPrompt = interactable.prompt;
                        bestEventName = interactable.eventName;
                    }
                });

            if (bestEntity.is_valid()) {
                target.entity = bestEntity;
                target.distance = bestDistance;
                target.prompt = std::move(bestPrompt);
                target.eventName = std::move(bestEventName);
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

            s7_scheme* scheme = nullptr;
            if (it.world().has<ScriptContext>()) {
                scheme = it.world().get<ScriptContext>().scheme;
            }

            std::string entityId;
            if (target.entity.name()) {
                entityId = target.entity.name();
            }

            if (tryCallUseHandler(scheme, target.eventName, entityId)) {
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
