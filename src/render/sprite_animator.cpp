#include "render/sprite_animator.hpp"

#include "assets/sprite_anim_loader.hpp"
#include "render/components.hpp"

namespace slopengine {

void SpriteAnimator::play(std::string_view clip, bool shouldLoop, float playbackSpeed) {
    clipName = std::string{clip};
    loop = shouldLoop;
    speed = playbackSpeed;
    time = 0.0f;
    playing = true;
    justStarted = true;
    justFinished = false;
    tweenRotation = false;
    tweenScale = false;
    tweenTranslate = false;
    transformBlend = 0.0f;
    nextFrame.clear();
    lastEnteredHoldIndex = -1;
}

void SpriteAnimator::stop() {
    playing = false;
    justStarted = false;
    justFinished = false;
    tweenRotation = false;
    tweenScale = false;
    tweenTranslate = false;
    transformBlend = 0.0f;
    nextFrame.clear();
    lastEnteredHoldIndex = -1;
}

void playSpriteAnim(
    SpriteAnimator& animator,
    SpriteInstance& sprite,
    const SpriteAnimBank* bank,
    std::string_view clip,
    bool shouldLoop,
    float playbackSpeed) {
    animator.play(clip, shouldLoop, playbackSpeed);
    if (bank == nullptr || clip.empty()) {
        return;
    }
    const auto clipIt = bank->clipIndexByName.find(std::string{clip});
    if (clipIt == bank->clipIndexByName.end() || clipIt->second >= bank->clips.size()) {
        return;
    }
    const SpriteAnimClip& animClip = bank->clips[clipIt->second];
    if (animClip.frames.empty()) {
        return;
    }
    sprite.frame = animClip.frames.front().id;
}

}
