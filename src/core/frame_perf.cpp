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

void perfLogSample(PerfLogState& log, const FramePerfStats& stats) {
    if (!log.enabled) {
        return;
    }
    const double now = GetTime();
    if (log.lastLogTime == 0.0) {
        log.lastLogTime = now;
    }
    ++log.frames;
    log.sumFrameMs += stats.frameMs;
    log.sumPhysicsMs += stats.physicsMs;
    log.sumRenderMs += stats.renderMs;
    log.sumUiMs += stats.uiMs;
    log.sumSwapMs += stats.swapMs;
    log.sumGpuFrameMs += stats.gpuFrameMs;
    for (int i = 0; i < kGpuPassCount; ++i) {
        log.sumGpuPassMs[i] += stats.gpuPassMs[i];
        log.sumCpuPassMs[i] += stats.cpuPassMs[i];
    }
    constexpr int kCounterCount = static_cast<int>(sizeof(FrameCounters) / sizeof(int));
    const int* counters = reinterpret_cast<const int*>(&stats.counters);
    for (int i = 0; i < kCounterCount; ++i) {
        log.sumCounters[i] += counters[i];
    }
    if (now - log.lastLogTime < log.intervalSeconds || log.frames <= 0) {
        return;
    }
    const double n = static_cast<double>(log.frames);
    const double frameMs = log.sumFrameMs / n;
    TraceLog(
        LOG_INFO,
        "PERF: %d frames, frame %.2f ms (%.0f fps), cpu physics %.2f render %.2f ui %.2f swap %.2f other %.2f, gpu %.2f: "
        "shadows %.2f world %.2f transparent %.2f effects %.2f fp %.2f post %.2f extra %.2f ui %.2f%s",
        log.frames,
        frameMs,
        frameMs > 0.0 ? 1000.0 / frameMs : 0.0,
        log.sumPhysicsMs / n,
        log.sumRenderMs / n,
        log.sumUiMs / n,
        log.sumSwapMs / n,
        (log.sumFrameMs - log.sumPhysicsMs - log.sumRenderMs - log.sumUiMs - log.sumSwapMs) / n,
        log.sumGpuFrameMs / n,
        log.sumGpuPassMs[static_cast<int>(GpuPass::Shadow)] / n,
        log.sumGpuPassMs[static_cast<int>(GpuPass::World)] / n,
        log.sumGpuPassMs[static_cast<int>(GpuPass::Transparent)] / n,
        log.sumGpuPassMs[static_cast<int>(GpuPass::Effects)] / n,
        log.sumGpuPassMs[static_cast<int>(GpuPass::FirstPerson)] / n,
        log.sumGpuPassMs[static_cast<int>(GpuPass::Post)] / n,
        log.sumGpuPassMs[static_cast<int>(GpuPass::ExtraEye)] / n,
        log.sumGpuPassMs[static_cast<int>(GpuPass::Ui)] / n,
        stats.gpuTimingAvailable ? "" : " (no gpu timers)");
    TraceLog(
        LOG_INFO,
        "PERF: cpu passes: shadows %.2f world %.2f transparent %.2f effects %.2f fp %.2f post %.2f extra %.2f ui %.2f",
        log.sumCpuPassMs[static_cast<int>(GpuPass::Shadow)] / n,
        log.sumCpuPassMs[static_cast<int>(GpuPass::World)] / n,
        log.sumCpuPassMs[static_cast<int>(GpuPass::Transparent)] / n,
        log.sumCpuPassMs[static_cast<int>(GpuPass::Effects)] / n,
        log.sumCpuPassMs[static_cast<int>(GpuPass::FirstPerson)] / n,
        log.sumCpuPassMs[static_cast<int>(GpuPass::Post)] / n,
        log.sumCpuPassMs[static_cast<int>(GpuPass::ExtraEye)] / n,
        log.sumCpuPassMs[static_cast<int>(GpuPass::Ui)] / n);
    FrameCounters avg{};
    int* avgCounters = reinterpret_cast<int*>(&avg);
    for (int i = 0; i < kCounterCount; ++i) {
        avgCounters[i] = static_cast<int>(log.sumCounters[i] / log.frames);
    }
    TraceLog(
        LOG_INFO,
        "PERF: draws map %d models %d transparent %d sprites %d particles %d, shadow faces %d draws %d, "
        "culled models frustum %d pvs %d sprites frustum %d pvs %d transparent frustum %d pvs %d, "
        "lights active %d culled %d, "
        "pvs tests %d, rays bvh %d physics %d, probe samples %d, rss %.0f MB",
        avg.mapMeshDraws,
        avg.modelMeshDraws,
        avg.transparentMeshDraws,
        avg.spritesDrawn,
        avg.particlesDrawn,
        avg.shadowFaces,
        avg.shadowMeshDraws,
        avg.modelsFrustumCulled,
        avg.modelsPvsCulled,
        avg.spritesFrustumCulled,
        avg.spritesPvsCulled,
        avg.transparentFrustumCulled,
        avg.transparentPvsCulled,
        avg.lightsActive,
        avg.lightsCulled,
        avg.pvsTests,
        avg.bvhRays,
        avg.physicsRays,
        avg.probeSamples,
        static_cast<double>(processRssMb()));
    log.lastLogTime = now;
    log.frames = 0;
    log.sumFrameMs = 0.0;
    log.sumPhysicsMs = 0.0;
    log.sumRenderMs = 0.0;
    log.sumUiMs = 0.0;
    log.sumSwapMs = 0.0;
    log.sumGpuFrameMs = 0.0;
    for (double& ms : log.sumGpuPassMs) {
        ms = 0.0;
    }
    for (double& ms : log.sumCpuPassMs) {
        ms = 0.0;
    }
    for (long long& sum : log.sumCounters) {
        sum = 0;
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
