#include "audio/audio_module.hpp"

#include "audio/components.hpp"
#include "camera/components.hpp"
#include "render/components.hpp"
#include "script/audio_script.hpp"
#include "script/script_context.hpp"

#include <soloud.h>
#include <raymath.h>

#include <cmath>

namespace slopengine {

namespace {

Vector3 translationFromMatrix(const Matrix& matrix) {
    return {matrix.m12, matrix.m13, matrix.m14};
}

void listenerBasis(
    const FirstPersonController* controller,
    Vector3& at,
    Vector3& up) {
    if (controller == nullptr) {
        at = {0.0f, 0.0f, 1.0f};
        up = {0.0f, 1.0f, 0.0f};
        return;
    }

    const float cy = std::cos(controller->yaw);
    const float sy = std::sin(controller->yaw);
    const float cp = std::cos(controller->pitch);
    const float sp = std::sin(controller->pitch);
    at = Vector3Normalize({sy * cp, -sp, cy * cp});
    up = {0.0f, 1.0f, 0.0f};
}

void registerComponents(flecs::world& world) {
    world.component<AudioListener>();
    world.component<AudioSource>();
    world.component<AudioContext>();
}

void registerSystems(flecs::world& world) {
    world.system<AudioSource>("AudioSourceAutoplay")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity entity, AudioSource& source) {
            if (!source.autoplay || source.playing) {
                return;
            }
            if (source.audio.empty() && source.clip.empty()) {
                return;
            }
            if (!entity.world().has<AudioContext>()) {
                return;
            }
            AudioContext& ctx = entity.world().get_mut<AudioContext>();
            if (ctx.world == nullptr || ctx.assets == nullptr || !ctx.world->ready()) {
                return;
            }

            Vector3 pos{0.0f, 0.0f, 0.0f};
            if (entity.has<GlobalTransformation>()) {
                pos = translationFromMatrix(entity.get<GlobalTransformation>().matrix);
            }

            if (!source.audio.empty()) {
                s7_scheme* scheme = nullptr;
                if (entity.world().has<ScriptContext>()) {
                    scheme = entity.world().get<ScriptContext>().scheme;
                }
                const AudioDef* def = ctx.assets->getAudioDef(scheme, source.audio);
                if (def == nullptr) {
                    source.autoplay = false;
                    return;
                }
                if (def->spatial) {
                    source.voice = static_cast<std::uint32_t>(ctx.world->playAudioDef3d(
                        *ctx.assets,
                        source.audio,
                        *def,
                        pos.x,
                        pos.y,
                        pos.z));
                } else {
                    source.voice = static_cast<std::uint32_t>(ctx.world->playAudioDef(
                        *ctx.assets,
                        source.audio,
                        *def));
                }
            } else if (source.spatial) {
                source.voice = static_cast<std::uint32_t>(ctx.world->playSound3d(
                    *ctx.assets,
                    source.clip,
                    pos.x,
                    pos.y,
                    pos.z,
                    source.volume,
                    source.looping,
                    source.minDistance,
                    source.maxDistance));
            } else {
                source.voice = static_cast<std::uint32_t>(ctx.world->playSound(
                    *ctx.assets,
                    source.clip,
                    source.volume,
                    source.looping));
            }
            source.playing = source.voice != 0;
            source.autoplay = false;
        });

    world.system("Sync3dAudio")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!world.has<AudioContext>()) {
                return;
            }
            AudioContext& ctx = world.get_mut<AudioContext>();
            if (ctx.world == nullptr || !ctx.world->ready()) {
                return;
            }

            bool haveListener = false;
            Vector3 listenerPos{0.0f, 0.0f, 0.0f};
            Vector3 at{0.0f, 0.0f, 1.0f};
            Vector3 up{0.0f, 1.0f, 0.0f};

            world.each([&](flecs::entity entity, AudioListener& listener) {
                if (!listener.active || haveListener) {
                    return;
                }
                if (entity.has<GlobalTransformation>()) {
                    listenerPos = translationFromMatrix(entity.get<GlobalTransformation>().matrix);
                }
                const FirstPersonController* controller =
                    entity.has<FirstPersonController>() ? &entity.get<FirstPersonController>()
                                                        : nullptr;
                listenerBasis(controller, at, up);
                haveListener = true;
            });

            if (haveListener) {
                ctx.world->setListener(
                    listenerPos.x,
                    listenerPos.y,
                    listenerPos.z,
                    at.x,
                    at.y,
                    at.z,
                    up.x,
                    up.y,
                    up.z);
            }

            world.each([&](flecs::entity entity, AudioSource& source) {
                if (!source.spatial || source.voice == 0 || !source.playing) {
                    return;
                }
                if (!entity.has<GlobalTransformation>()) {
                    return;
                }
                const Vector3 pos = translationFromMatrix(entity.get<GlobalTransformation>().matrix);
                ctx.world->setSourcePosition(
                    static_cast<SoLoud::handle>(source.voice),
                    pos.x,
                    pos.y,
                    pos.z);
            });

            ctx.world->update3d();
        });

    world.observer<AudioSource>("AudioSourceOnRemove")
        .event(flecs::OnRemove)
        .each([](flecs::entity entity, AudioSource& source) {
            if (source.voice == 0 || !entity.world().has<AudioContext>()) {
                return;
            }
            AudioContext& ctx = entity.world().get_mut<AudioContext>();
            if (ctx.world != nullptr) {
                ctx.world->stop(static_cast<SoLoud::handle>(source.voice));
            }
            source.voice = 0;
            source.playing = false;
        });
}

} // namespace

void registerAudioModule(
    flecs::world& world,
    AudioWorld* audio,
    AssetStore& assets,
    s7_scheme* scheme) {
    registerComponents(world);
    world.set<AudioContext>(AudioContext{audio, &assets});
    registerSystems(world);
    bindAudioApi(world, scheme);
}

void unregisterAudioModule(flecs::world& world) {
    if (world.has<AudioContext>()) {
        world.remove<AudioContext>();
    }
}

}
