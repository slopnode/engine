#include "render/animation_player.hpp"

namespace slopengine {

void AnimationPlayer::play(std::string_view clip, bool shouldLoop, float playbackSpeed) {
    clipName = std::string{clip};
    loop = shouldLoop;
    speed = playbackSpeed;
    time = 0.0f;
    prevTime = 0.0f;
    wrappedLastTick = false;
    poseSampledTick = 0;
    playing = true;
    justStarted = true;
    justFinished = false;
    lastFrame = -1.0f;
}

float blendAnimationTime(float prevTime, float time, float duration, bool wrapped, float alpha) {
    if (!wrapped || duration <= 0.0f) {
        return prevTime + (time - prevTime) * alpha;
    }
    float blended = prevTime + (time + duration - prevTime) * alpha;
    if (blended >= duration) {
        blended -= duration;
    }
    return blended;
}

void AnimationPlayer::stop() {
    playing = false;
    justStarted = false;
    justFinished = false;
}

}
