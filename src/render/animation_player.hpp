#pragma once

#include <string>

namespace slopengine {

/** Runtime state for playing a clip from a loaded animation bank.
 *  @ingroup render_components
 */
struct AnimationPlayer {
    std::string animBankPath;
    std::string clipName;
    float time = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool playing = false;
    bool justStarted = false;
    bool justFinished = false;

    /** Starts playback of @p clip at @p playbackSpeed, optionally looping. */
    void play(std::string_view clip, bool shouldLoop = true, float playbackSpeed = 1.0f);

    /** Stops playback and clears transition flags. */
    void stop();
};

}
