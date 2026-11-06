#include "core/frame_perf.hpp"

#include <raylib.h>

#if defined(_WIN32)
#include "core/win32.hpp"
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace slopengine {

const char* gpuPassName(GpuPass pass) {
    switch (pass) {
        case GpuPass::Shadow: return "Shadows";
        case GpuPass::World: return "World";
        case GpuPass::Transparent: return "Transparent";
        case GpuPass::Effects: return "Effects";
        case GpuPass::FirstPerson: return "First person";
        case GpuPass::Post: return "Post + HUD";
        case GpuPass::ExtraEye: return "Extra eye";
        case GpuPass::Ui: return "UI";
        default: return "?";
    }
}

void FramePerfStats::pushSample() {
    frameHistory[historyOffset] = frameMs;
    physicsHistory[historyOffset] = physicsMs;
    renderHistory[historyOffset] = renderMs;
    uiHistory[historyOffset] = uiMs;
    gpuHistory[historyOffset] = gpuFrameMs;
    historyOffset = (historyOffset + 1) % kFramePerfHistorySize;
    if (historyCount < kFramePerfHistorySize) {
        ++historyCount;
    }
}

double perfNow() {
    return GetTime();
}

float perfElapsedMs(double start) {
    return static_cast<float>((GetTime() - start) * 1000.0);
}

float processRssMb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return 0.0f;
    }
    return static_cast<float>(counters.WorkingSetSize) / (1024.0f * 1024.0f);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0f;
    }
    return static_cast<float>(usage.ru_maxrss) / 1024.0f;
#endif
}

}
