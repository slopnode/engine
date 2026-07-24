#pragma once

#include <flecs.h>

#include <string>
#include <string_view>

namespace slopengine {

struct SpriteAnimBank;
struct SpriteInstance;

/** Runtime state for playing a sprite clip from a .spanim bank.
 *  @ingroup render_components
 */
struct SpriteAnimator {
    std::string animPath;
    std::string clipName;
    float time = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool playing = false;
    bool justStarted = false;
    bool justFinished = false;
    bool tweenRotation = false;
    bool tweenScale = false;
    bool tweenTranslate = false;
    float transformBlend = 0.0f;
    std::string nextFrame;
    int lastEnteredHoldIndex = -1;

    bool hasTween() const {
        return tweenRotation || tweenScale || tweenTranslate;
    }

    /** Starts @p clip at @p playbackSpeed. */
    void play(std::string_view clip, bool shouldLoop = true, float playbackSpeed = 1.0f);
    /** Stops playback and clears transition flags. */
    void stop();
};

/** Starts @p clip and immediately applies its first hold to @p sprite when @p bank has it. */
void playSpriteAnim(
    SpriteAnimator& animator,
    SpriteInstance& sprite,
    const SpriteAnimBank* bank,
    std::string_view clip,
    bool shouldLoop = true,
    float playbackSpeed = 1.0f);

void registerSpriteAnimatorSystem(flecs::world& world);

}
