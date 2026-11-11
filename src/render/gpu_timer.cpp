#include "render/gpu_timer.hpp"

#include <cstdint>

#include <rlgl.h>
#include "external/glad.h"

namespace slopengine {

namespace {

constexpr int kRingFrames = 4;
constexpr int kMaxMarks = 16;

struct TimerFrame {
    unsigned int startQuery = 0;
    unsigned int markQueries[kMaxMarks]{};
    GpuPass markPasses[kMaxMarks]{};
    int markCount = 0;
    bool issued = false;
};

struct TimerState {
    TimerFrame frames[kRingFrames]{};
    int current = -1;
    bool initialized = false;
    bool supported = false;
};

TimerState& timerState() {
    static TimerState state{};
    return state;
}

bool ensureQueries(TimerState& state) {
    if (state.initialized) {
        return state.supported;
    }
    state.initialized = true;
    state.supported = glQueryCounter != nullptr && glGetQueryObjectui64v != nullptr &&
        glGetQueryObjectuiv != nullptr && glGenQueries != nullptr;
    if (!state.supported) {
        return false;
    }
    for (TimerFrame& frame : state.frames) {
        glGenQueries(1, &frame.startQuery);
        glGenQueries(kMaxMarks, frame.markQueries);
    }
    return true;
}

bool resolveFrame(const TimerFrame& frame, FramePerfStats& stats) {
    if (!frame.issued || frame.markCount == 0) {
        return false;
    }
    unsigned int available = 0;
    glGetQueryObjectuiv(
        frame.markQueries[frame.markCount - 1], GL_QUERY_RESULT_AVAILABLE, &available);
    if (available == 0) {
        return false;
    }
    std::uint64_t start = 0;
    glGetQueryObjectui64v(frame.startQuery, GL_QUERY_RESULT, &start);
    for (float& ms : stats.gpuPassMs) {
        ms = 0.0f;
    }
    std::uint64_t previous = start;
    for (int i = 0; i < frame.markCount; ++i) {
        std::uint64_t stamp = 0;
        glGetQueryObjectui64v(frame.markQueries[i], GL_QUERY_RESULT, &stamp);
        if (stamp < previous) {
            stamp = previous;
        }
        stats.gpuPassMs[static_cast<int>(frame.markPasses[i])] +=
            static_cast<float>(static_cast<double>(stamp - previous) * 1.0e-6);
        previous = stamp;
    }
    stats.gpuFrameMs = static_cast<float>(static_cast<double>(previous - start) * 1.0e-6);
    return true;
}

}

void gpuTimerFrameBegin(FramePerfStats* stats) {
    TimerState& state = timerState();
    if (!ensureQueries(state)) {
        if (stats != nullptr) {
            stats->gpuTimingAvailable = false;
        }
        return;
    }
    state.current = (state.current + 1) % kRingFrames;
    TimerFrame& frame = state.frames[state.current];
    if (stats != nullptr && resolveFrame(frame, *stats)) {
        stats->gpuTimingAvailable = true;
    }
    frame.markCount = 0;
    frame.issued = true;
    rlDrawRenderBatchActive();
    glQueryCounter(frame.startQuery, GL_TIMESTAMP);
}

void gpuTimerMark(GpuPass pass) {
    TimerState& state = timerState();
    if (!state.supported || state.current < 0) {
        return;
    }
    TimerFrame& frame = state.frames[state.current];
    if (!frame.issued || frame.markCount >= kMaxMarks) {
        return;
    }
    rlDrawRenderBatchActive();
    glQueryCounter(frame.markQueries[frame.markCount], GL_TIMESTAMP);
    frame.markPasses[frame.markCount] = pass;
    ++frame.markCount;
}

}
