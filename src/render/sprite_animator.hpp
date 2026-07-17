#pragma once

#include <string>
#include <string_view>

namespace slopengine {

struct SpriteAnimator {
    std::string animPath;
    std::string clipName;
    float time = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool playing = false;
    bool justStarted = false;
    bool justFinished = false;

    void play(std::string_view clip, bool shouldLoop = true, float playbackSpeed = 1.0f);
    void stop();
};

}
