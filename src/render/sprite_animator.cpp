#include "render/sprite_animator.hpp"

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
    lastSoundFrameIndex = -1;
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
    lastSoundFrameIndex = -1;
}

}
