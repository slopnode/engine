#include "render/animation_player.hpp"

namespace slopengine {

void AnimationPlayer::play(std::string_view clip, bool shouldLoop, float playbackSpeed) {
    clipName = std::string{clip};
    loop = shouldLoop;
    speed = playbackSpeed;
    time = 0.0f;
    playing = true;
    justStarted = true;
    justFinished = false;
}

void AnimationPlayer::stop() {
    playing = false;
    justStarted = false;
    justFinished = false;
}

}
