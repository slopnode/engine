#include "render/sprite_animator.hpp"

#include "assets/asset_services.hpp"
#include "assets/sprite_anim_loader.hpp"
#include "audio/audio_module.hpp"
#include "audio/components.hpp"
#include "camera/components.hpp"
#include "game/game_state.hpp"
#include "render/components.hpp"
#include "script/first_person_script.hpp"
#include "script/hook_registry.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"

#include <cmath>
#include <cstring>
#include <string>

#include <raylib.h>
#include <raymath.h>
#include <s7.h>

namespace slopengine {

void registerSpriteAnimatorSystem(flecs::world& world) {
    world.system<SpriteAnimator, SpriteInstance>("AdvanceSpriteAnimator")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity entity, SpriteAnimator& animator, SpriteInstance& sprite) {
            flecs::world world = entity.world();
            if (isSimulationPaused(world)) {
                return;
            }

            animator.justFinished = false;
            const bool startedThisFrame = animator.justStarted;
            animator.justStarted = false;

            if (!animator.playing || animator.clipName.empty() || animator.animPath.empty()) {
                return;
            }

            AssetServices& services = world.get_mut<AssetServices>();
            if (services.store == nullptr) {
                return;
            }

            const SpriteAnimBank* bank = services.store->getSpriteAnimBank(animator.animPath);
            if (bank == nullptr) {
                return;
            }

            const auto clipIt = bank->clipIndexByName.find(animator.clipName);
            if (clipIt == bank->clipIndexByName.end() || clipIt->second >= bank->clips.size()) {
                return;
            }

            const SpriteAnimClip& clip = bank->clips[clipIt->second];
            if (clip.frames.empty()) {
                return;
            }

            float clipDuration = 0.0f;
            for (const SpriteAnimFrame& frame : clip.frames) {
                clipDuration += frame.duration;
            }
            if (clipDuration <= 0.0f) {
                return;
            }

            const bool useLoop = animator.loop;
            if (!startedThisFrame) {
                animator.time += GetFrameTime() * animator.speed;
            }

            auto clearTween = [&]() {
                animator.tweenRotation = false;
                animator.tweenScale = false;
                animator.tweenTranslate = false;
                animator.transformBlend = 0.0f;
                animator.nextFrame.clear();
            };

            auto applyTween = [&](std::size_t frameIndex, float holdTime, float holdDuration) {
                const SpriteAnimFrame& hold = clip.frames[frameIndex];
                if (!hold.hasTween() || holdDuration <= 0.0f) {
                    clearTween();
                    return;
                }
                std::size_t nextIndex = frameIndex + 1;
                if (nextIndex >= clip.frames.size()) {
                    if (!useLoop) {
                        clearTween();
                        return;
                    }
                    nextIndex = 0;
                }
                animator.tweenRotation = hold.tweenRotation;
                animator.tweenScale = hold.tweenScale;
                animator.tweenTranslate = hold.tweenTranslate;
                animator.transformBlend = holdTime / holdDuration;
                animator.nextFrame = clip.frames[nextIndex].id;
            };

            auto resolveHintSource = [&]() -> std::string {
                if (entity.name() != nullptr && entity.name()[0] != '\0') {
                    return std::string{entity.name()};
                }

                flecs::entity parent = entity.parent();
                if (parent.is_valid()) {
                    const char* parentName = parent.name();
                    if (parentName != nullptr &&
                        (std::strcmp(parentName, "weapon") == 0 ||
                         std::strcmp(parentName, "emission") == 0)) {
                        return std::string{parentName};
                    }

                    const flecs::entity_t parentId = parent.id();
                    std::string socketSource;
                    world.each([&](flecs::entity, FirstPersonScene& scene) {
                        if (!socketSource.empty()) {
                            return;
                        }
                        if (scene.weaponSocket != 0 && parentId == scene.weaponSocket) {
                            socketSource = "weapon";
                        } else if (
                            scene.emissionSocket != 0 && parentId == scene.emissionSocket) {
                            socketSource = "emission";
                        }
                    });
                    if (!socketSource.empty()) {
                        return socketSource;
                    }

                    flecs::entity cursor = parent.parent();
                    while (cursor.is_valid()) {
                        const char* name = cursor.name();
                        if (name != nullptr &&
                            (std::strcmp(name, "weapon") == 0 ||
                             std::strcmp(name, "emission") == 0)) {
                            return std::string{name};
                        }
                        cursor = cursor.parent();
                    }
                }
                return {};
            };

            auto fireSound = [&](const SpriteAnimFrame& frame) {
                if (!frame.hasSound() || !world.has<AudioContext>()) {
                    return;
                }
                AudioContext& ctx = world.get_mut<AudioContext>();
                if (ctx.world == nullptr || ctx.assets == nullptr || !ctx.world->ready()) {
                    return;
                }

                auto playAtWorld = [&](float x, float y, float z) {
                    ctx.world->playSound3d(
                        *ctx.assets,
                        frame.sound,
                        x,
                        y,
                        z,
                        frame.soundVolume);
                };

                if (entity.has<ViewSpace>() || entity.has<ViewSprite>()) {
                    bool haveListener = false;
                    Vector3 listenerPos{0.0f, 0.0f, 0.0f};
                    Vector3 ahead{0.0f, 0.0f, 1.0f};
                    world.each([&](flecs::entity listenerEntity, AudioListener& listener) {
                        if (!listener.active || haveListener) {
                            return;
                        }
                        if (listenerEntity.has<Lens>()) {
                            const Lens& lens = listenerEntity.get<Lens>();
                            listenerPos = lens.camera.position;
                            ahead = Vector3Normalize(
                                Vector3Subtract(lens.camera.target, lens.camera.position));
                            haveListener = true;
                            return;
                        }
                        if (listenerEntity.has<GlobalTransformation>()) {
                            const Matrix& matrix =
                                listenerEntity.get<GlobalTransformation>().matrix;
                            listenerPos = {matrix.m12, matrix.m13, matrix.m14};
                            if (listenerEntity.has<FirstPersonController>()) {
                                const FirstPersonController& controller =
                                    listenerEntity.get<FirstPersonController>();
                                const float cy = std::cos(controller.yaw);
                                const float sy = std::sin(controller.yaw);
                                const float cp = std::cos(controller.pitch);
                                const float sp = std::sin(controller.pitch);
                                ahead = Vector3Normalize({sy * cp, -sp, cy * cp});
                            }
                            haveListener = true;
                        }
                    });
                    if (haveListener) {
                        constexpr float kMuzzleForward = 0.35f;
                        playAtWorld(
                            listenerPos.x + ahead.x * kMuzzleForward,
                            listenerPos.y + ahead.y * kMuzzleForward,
                            listenerPos.z + ahead.z * kMuzzleForward);
                    } else {
                        ctx.world->playSound(*ctx.assets, frame.sound, frame.soundVolume);
                    }
                    return;
                }

                if (entity.has<GlobalTransformation>()) {
                    const Matrix& matrix = entity.get<GlobalTransformation>().matrix;
                    playAtWorld(matrix.m12, matrix.m13, matrix.m14);
                } else {
                    ctx.world->playSound(*ctx.assets, frame.sound, frame.soundVolume);
                }
            };

            auto fireHints = [&](const SpriteAnimFrame& frame) {
                if (!frame.hasHints()) {
                    return;
                }
                if (!world.has<ScriptContext>() || world.get<ScriptContext>().scheme == nullptr) {
                    return;
                }
                const std::string source = resolveHintSource();
                if (source.empty()) {
                    static bool warnedAnonymousHint = false;
                    if (!warnedAnonymousHint) {
                        TraceLog(
                            LOG_WARNING,
                            "Sprite hint fired on unnamed entity (anim '%s'); "
                            "(on-sprite-hint) skipped",
                            animator.animPath.c_str());
                        warnedAnonymousHint = true;
                    }
                    return;
                }
                s7_scheme* scheme = world.get<ScriptContext>().scheme;
                for (const std::string& hint : frame.hints) {
                    callHook2String(scheme, "on-sprite-hint", source, hint, ScriptScope::World);
                }
            };

            auto fireHoldEnter = [&](const SpriteAnimFrame& frame) {
                fireSound(frame);
                fireHints(frame);
            };

            auto fireEnteredHolds = [&](int previousIndex, int currentIndex) {
                if (currentIndex < 0 || clip.frames.empty()) {
                    return;
                }
                const int frameCount = static_cast<int>(clip.frames.size());
                if (previousIndex < 0) {
                    fireHoldEnter(clip.frames[static_cast<std::size_t>(currentIndex)]);
                    return;
                }
                if (previousIndex == currentIndex) {
                    return;
                }
                if (useLoop && currentIndex < previousIndex) {
                    for (int i = previousIndex + 1; i < frameCount; ++i) {
                        fireHoldEnter(clip.frames[static_cast<std::size_t>(i)]);
                        if (animator.justStarted) {
                            return;
                        }
                    }
                    for (int i = 0; i <= currentIndex; ++i) {
                        fireHoldEnter(clip.frames[static_cast<std::size_t>(i)]);
                        if (animator.justStarted) {
                            return;
                        }
                    }
                    return;
                }
                const int begin = previousIndex + 1;
                const int end = std::min(currentIndex, frameCount - 1);
                for (int i = begin; i <= end; ++i) {
                    fireHoldEnter(clip.frames[static_cast<std::size_t>(i)]);
                    if (animator.justStarted) {
                        return;
                    }
                }
            };

            float localTime = animator.time;
            if (useLoop) {
                localTime = std::fmod(localTime, clipDuration);
                if (localTime < 0.0f) {
                    localTime += clipDuration;
                }
            } else if (localTime >= clipDuration) {
                const int lastIndex = static_cast<int>(clip.frames.size()) - 1;
                fireEnteredHolds(animator.lastEnteredHoldIndex, lastIndex);
                if (animator.justStarted) {
                    return;
                }
                animator.lastEnteredHoldIndex = lastIndex;
                animator.playing = false;
                animator.justFinished = true;
                animator.time = clipDuration;
                sprite.frame = clip.frames.back().id;
                clearTween();
                return;
            }

            for (std::size_t frameIndex = 0; frameIndex < clip.frames.size(); ++frameIndex) {
                const SpriteAnimFrame& frame = clip.frames[frameIndex];
                if (localTime < frame.duration) {
                    const int currentIndex = static_cast<int>(frameIndex);
                    fireEnteredHolds(animator.lastEnteredHoldIndex, currentIndex);
                    if (animator.justStarted) {
                        return;
                    }
                    animator.lastEnteredHoldIndex = currentIndex;
                    sprite.frame = frame.id;
                    applyTween(frameIndex, localTime, frame.duration);
                    return;
                }
                localTime -= frame.duration;
            }

            const int lastIndex = static_cast<int>(clip.frames.size()) - 1;
            fireEnteredHolds(animator.lastEnteredHoldIndex, lastIndex);
            if (animator.justStarted) {
                return;
            }
            animator.lastEnteredHoldIndex = lastIndex;
            sprite.frame = clip.frames.back().id;
            clearTween();
        });
}

}
