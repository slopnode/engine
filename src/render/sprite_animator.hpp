#pragma once

#include <string>
#include <string_view>

namespace slopengine {

/** Runtime state for playing a sprite clip from a .spanim bank. */
struct SpriteAnimator {
    std::string animPath;
    std::string clipName;
    float time = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool playing = false;
    bool justStarted = false;
    bool justFinished = false;

    /** Starts @p clip at @p playbackSpeed. */
    void play(std::string_view clip, bool shouldLoop = true, float playbackSpeed = 1.0f);
    /** Stops playback and clears transition flags. */
    void stop();
};

}
