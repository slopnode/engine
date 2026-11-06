#pragma once

#include "core/frame_counters.hpp"

namespace slopengine {

inline constexpr int kFramePerfHistorySize = 240;

enum class GpuPass : int {
    Shadow,
    World,
    Transparent,
    Effects,
    FirstPerson,
    Post,
    ExtraEye,
    Ui,
    Count,
};

inline constexpr int kGpuPassCount = static_cast<int>(GpuPass::Count);

const char* gpuPassName(GpuPass pass);

struct FramePerfStats {
    float frameMs = 0.0f;
    float physicsMs = 0.0f;
    float renderMs = 0.0f;
    float uiMs = 0.0f;
    float gpuFrameMs = 0.0f;
    float gpuPassMs[kGpuPassCount]{};
    bool gpuTimingAvailable = false;
    FrameCounters counters{};

    float frameHistory[kFramePerfHistorySize]{};
    float physicsHistory[kFramePerfHistorySize]{};
    float renderHistory[kFramePerfHistorySize]{};
    float uiHistory[kFramePerfHistorySize]{};
    float gpuHistory[kFramePerfHistorySize]{};
    int historyOffset = 0;
    int historyCount = 0;

    void pushSample();
};

double perfNow();
float perfElapsedMs(double start);
float processRssMb();

}
