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
    float swapMs = 0.0f;
    float gpuFrameMs = 0.0f;
    float gpuPassMs[kGpuPassCount]{};
    float cpuPassMs[kGpuPassCount]{};
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

struct PerfLogState {
    bool enabled = false;
    bool viewLock = false;
    float viewPose[5]{};
    double intervalSeconds = 2.0;
    double lastLogTime = 0.0;
    int frames = 0;
    double sumFrameMs = 0.0;
    double sumPhysicsMs = 0.0;
    double sumRenderMs = 0.0;
    double sumUiMs = 0.0;
    double sumSwapMs = 0.0;
    double sumGpuFrameMs = 0.0;
    double sumGpuPassMs[kGpuPassCount]{};
    double sumCpuPassMs[kGpuPassCount]{};
    long long sumCounters[sizeof(FrameCounters) / sizeof(int)]{};
};

void perfLogSample(PerfLogState& log, const FramePerfStats& stats);

double perfNow();
float perfElapsedMs(double start);
float processRssMb();

}
