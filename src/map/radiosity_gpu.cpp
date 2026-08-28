#include "map/radiosity_gpu.hpp"

#include "map/emitter_bvh.hpp"

#include <raylib.h>
#include <rlgl.h>
#include "external/glad.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

namespace slopengine {

namespace {

constexpr unsigned int kShaderStorageBarrierBit = 0x00002000u;
constexpr unsigned int kBufferUpdateBarrierBit = 0x00000200u;
constexpr int kLuxelBatchSize = 1024;
constexpr int kLightBatchSize = 512;
constexpr int kLargeLuxelThreshold = 8192;

// Target wall-clock duration for one un-synced GPU submission. Windows' GPU watchdog
// (TDR) resets the driver at ~2s and kills the process (STATUS_STACK_BUFFER_OVERRUN /
// 0xC0000409 out of the vendor GL driver) before any GL error can be observed; Linux
// desktop drivers have no equivalent limit. 250ms leaves ~8x margin under the 2s
// Windows default. GpuBatchPacer measures each group with a GPU timer query and sizes
// the next luxel batch to hit this budget.
constexpr double kGpuSyncBudgetSecondsDefault = 0.25;
// If any group's measured GPU time exceeds this, the pacer aborts the GPU pass (the
// caller then finishes on the CPU). Comfortably under the 2s Windows TDR default, so a
// somewhat-more-expensive next group of the same size still would not reset the driver.
constexpr double kGpuGroupDangerSeconds = 1.2;
// Floor on adaptive luxels-per-dispatch. Below this the per-dispatch overhead dominates;
// if even this batch cannot fit the time budget the workload is a poor GPU fit.
constexpr int kMinAdaptiveLuxelBatch = 8;
// Consecutive groups pinned at the floor and still over budget before the pacer gives
// up and aborts to the CPU path. Keeps a pathological map (dense receivers against a
// high-sample precise-emission face) from crawling on the GPU for hours.
constexpr int kMaxFloorGroupsBeforeAbort = 24;
// Fixed luxel batch when GPU timer queries are unavailable (no adaptation possible):
// conservative, since a dense cluster could still be expensive.
constexpr int kUntimedLuxelBatch = 128;
// Maximum luxels per dispatch in --gpu-safe mode (the adaptive path still shrinks below
// this and still aborts to the CPU on a dangerous group).
constexpr int kSafeModeLuxelBatch = 256;

constexpr unsigned int kGlTimeElapsed = 0x88BFu;
constexpr unsigned int kGlQueryResult = 0x8866u;

double envSeconds(const char* name, double fallback, double lo, double hi) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }
    const double ms = std::atof(raw);
    if (!(ms > 0.0) || !std::isfinite(ms)) {
        return fallback;
    }
    return std::clamp(ms / 1000.0, lo, hi);
}

int envInt(const char* name, int fallback, int lo, int hi) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }
    const long parsed = std::strtol(raw, nullptr, 10);
    if (parsed <= 0) {
        return fallback;
    }
    return std::clamp(static_cast<int>(parsed), lo, hi);
}

/** Per-dispatch work sizing. How many dispatches are batched between glFinish() calls
 *  is no longer decided here: GpuDispatchPacer measures real GPU time and adapts it so
 *  no un-synced submission runs long enough to trip a GPU watchdog on any platform. */
struct DirectDispatchConfig {
    int luxelBatch = kLuxelBatchSize;
    int lightBatch = kLightBatchSize;
};

DirectDispatchConfig directDispatchConfig(int luxelCount, int lightCount, bool gpuSafeMode) {
    (void)lightCount;
    DirectDispatchConfig config;
    if (gpuSafeMode) {
        config.luxelBatch = 256;
        config.lightBatch = 128;
        return config;
    }
    if (luxelCount > 2048) {
        config.luxelBatch = 2048;
    }
    return config;
}

struct BounceDispatchConfig {
    int luxelBatch = kLuxelBatchSize;
};

BounceDispatchConfig bounceDispatchConfig(int luxelCount, bool gpuSafeMode) {
    BounceDispatchConfig config;
    if (gpuSafeMode) {
        config.luxelBatch = 256;
        return config;
    }
    config.luxelBatch = std::min(kLuxelBatchSize, luxelCount);
    if (luxelCount > kLargeLuxelThreshold) {
        config.luxelBatch = 2048;
    }
    return config;
}

struct GpuLuxelSSBO {
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    std::int32_t covered = 0;
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
    std::int32_t faceIndex = -1;
    float ir = 0.0f;
    float ig = 0.0f;
    float ib = 0.0f;
    std::int32_t leafIndex = -1;
    float sunIr = 0.0f;
    float sunIg = 0.0f;
    float sunIb = 0.0f;
    std::int32_t pad0 = 0;
};

struct GpuEmissiveFaceSSBO {
    float uAxisX = 0.0f;
    float uAxisY = 0.0f;
    float uAxisZ = 0.0f;
    float planeD = 0.0f;
    float vAxisX = 0.0f;
    float vAxisY = 0.0f;
    float vAxisZ = 0.0f;
    float uMin = 0.0f;
    float uMax = 0.0f;
    float vMin = 0.0f;
    float vMax = 0.0f;
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
    float area = 0.0f;
    std::int32_t faceIndex = -1;
    std::int32_t interiorLeaf = -1;
    std::int32_t gridWidth = 0;
    std::int32_t gridHeight = 0;
    std::int32_t gridOffset = 0;
    std::int32_t directSampleOffset = -1;
    float peakR = 0.0f;
    float peakG = 0.0f;
    float peakB = 0.0f;
    float castRange = 0.0f;
    float aabbMinX = 0.0f;
    float aabbMinY = 0.0f;
    float aabbMinZ = 0.0f;
    std::int32_t directSampleCount = 0;
    float aabbMaxX = 0.0f;
    float aabbMaxY = 0.0f;
    float aabbMaxZ = 0.0f;
    std::int32_t directSampleAxis = 0;
};

struct GpuGridSampleSSBO {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float pad = 0.0f;
};

struct GpuDirectSampleSSBO {
    float u = 0.0f;
    float v = 0.0f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
    float pad2 = 0.0f;
};

struct GpuLightSSBO {
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    std::int32_t kind = 0;
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 1.0f;
    float intensity = 1.0f;
    float cr = 1.0f;
    float cg = 1.0f;
    float cb = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f;
    std::int32_t leafIndex = -1;
    float pad1 = 0.0f;
    float pad2 = 0.0f;
};

struct GpuMaterialRectSSBO {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    float baseColorAlpha = 1.0f;
    float textureWidth = 1.0f;
    float textureHeight = 1.0f;
    std::int32_t yPixelOffset = 0;
};

struct GpuFaceOcclusionSSBO {
    float uvUAxisX = 0.0f;
    float uvUAxisY = 0.0f;
    float uvUAxisZ = 0.0f;
    float isTransparent = 0.0f;
    float uvVAxisX = 0.0f;
    float uvVAxisY = 0.0f;
    float uvVAxisZ = 0.0f;
    float materialIndex = 0.0f;
    float uvShiftX = 0.0f;
    float uvShiftY = 0.0f;
    float uvScaleX = 1.0f;
    float uvScaleY = 1.0f;
    float pixelsPerMeter = 64.0f;
    float baseColorAlpha = 1.0f;
    float baseColorR = 1.0f;
    float baseColorG = 1.0f;
    float baseColorB = 1.0f;
    float ior = 1.0f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
};

static_assert(sizeof(GpuMaterialRectSSBO) == 32);
static_assert(sizeof(GpuFaceOcclusionSSBO) == 80);

struct GpuBvhNodeSSBO {
    float minx = 0.0f;
    float miny = 0.0f;
    float minz = 0.0f;
    std::int32_t left = -1;
    float maxx = 0.0f;
    float maxy = 0.0f;
    float maxz = 0.0f;
    std::int32_t right = -1;
    std::int32_t firstPrim = -1;
    std::int32_t primCount = 0;
    std::int32_t pad0 = 0;
    std::int32_t pad1 = 0;
};

struct GpuBvhPrimSSBO {
    float v0x = 0.0f;
    float v0y = 0.0f;
    float v0z = 0.0f;
    float pad0 = 0.0f;
    float v1x = 0.0f;
    float v1y = 0.0f;
    float v1z = 0.0f;
    float pad1 = 0.0f;
    float v2x = 0.0f;
    float v2y = 0.0f;
    float v2z = 0.0f;
    float pad2 = 0.0f;
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
    std::int32_t faceIndex = -1;
};

struct GpuEmitterBvhPrimSSBO {
    float minx = 0.0f;
    float miny = 0.0f;
    float minz = 0.0f;
    float pad0 = 0.0f;
    float maxx = 0.0f;
    float maxy = 0.0f;
    float maxz = 0.0f;
    std::int32_t emitterIndex = -1;
    std::int32_t pad1 = 0;
    std::int32_t pad2 = 0;
    std::int32_t pad3 = 0;
    float pad4 = 0.0f;
};

struct GpuParamsSSBO {
    std::int32_t luxelCount = 0;
    std::int32_t emitterCount = 0;
    std::int32_t lightCount = 0;
    std::int32_t bvhRoot = -1;
    std::int32_t luxelOffset = 0;
    std::int32_t luxelBatch = 0;
    std::int32_t emitterBvhRoot = -1;
    float emitterQueryRadius = 0.0f;
    std::int32_t lightOffset = 0;
    std::int32_t lightBatch = 0;
    std::int32_t leafCount = 0;
    std::int32_t wordsPerRow = 0;
    float directWrap = 0.35f;
    float coplanarFill = 0.15f;
    float coplanarSoft = 0.25f;
    float minDist2 = 0.0025f;
    std::int32_t emitterDirectSamples = 4;
    std::int32_t emissionGridFloats = 0;
    std::int32_t sunRayCount = 1;
    float sunAngularSpread = 0.0f;
    float sunLeakThreshold = 0.0f;
    std::int32_t faceCount = 0;
    std::int32_t materialRectCount = 0;
    std::int32_t alphaAtlasWidth = 1;
    std::int32_t alphaAtlasHeight = 1;
    float sunRayMaxDistance = 1000.0f;
};

static_assert(sizeof(GpuLuxelSSBO) == 64);
static_assert(sizeof(GpuEmissiveFaceSSBO) == 132);
static_assert(sizeof(GpuGridSampleSSBO) == 16);
static_assert(sizeof(GpuDirectSampleSSBO) == 32);
static_assert(sizeof(GpuLightSSBO) == 64);
static_assert(sizeof(GpuBvhNodeSSBO) == 48);
static_assert(sizeof(GpuBvhPrimSSBO) == 64);
static_assert(sizeof(GpuEmitterBvhPrimSSBO) == 48);
static_assert(sizeof(GpuParamsSSBO) == 104);

using MemoryBarrierFn = void (*)(unsigned int);
using FinishFn = void (*)();
using FlushFn = void (*)();
using GetErrorFn = unsigned int (*)();
using GenQueriesFn = void (*)(int, unsigned int*);
using DeleteQueriesFn = void (*)(int, const unsigned int*);
using BeginQueryFn = void (*)(unsigned int, unsigned int);
using EndQueryFn = void (*)(unsigned int);
using GetQueryObjectUi64vFn = void (*)(unsigned int, unsigned int, std::uint64_t*);

MemoryBarrierFn memoryBarrierFn() {
    static MemoryBarrierFn fn =
        reinterpret_cast<MemoryBarrierFn>(rlGetProcAddress("glMemoryBarrier"));
    return fn;
}

FinishFn finishFn() {
    static FinishFn fn = reinterpret_cast<FinishFn>(rlGetProcAddress("glFinish"));
    return fn;
}

FlushFn flushFn() {
    static FlushFn fn = reinterpret_cast<FlushFn>(rlGetProcAddress("glFlush"));
    return fn;
}

GetErrorFn getErrorFn() {
    static GetErrorFn fn = reinterpret_cast<GetErrorFn>(rlGetProcAddress("glGetError"));
    return fn;
}

GenQueriesFn genQueriesFn() {
    static GenQueriesFn fn = reinterpret_cast<GenQueriesFn>(rlGetProcAddress("glGenQueries"));
    return fn;
}

DeleteQueriesFn deleteQueriesFn() {
    static DeleteQueriesFn fn =
        reinterpret_cast<DeleteQueriesFn>(rlGetProcAddress("glDeleteQueries"));
    return fn;
}

BeginQueryFn beginQueryFn() {
    static BeginQueryFn fn = reinterpret_cast<BeginQueryFn>(rlGetProcAddress("glBeginQuery"));
    return fn;
}

EndQueryFn endQueryFn() {
    static EndQueryFn fn = reinterpret_cast<EndQueryFn>(rlGetProcAddress("glEndQuery"));
    return fn;
}

GetQueryObjectUi64vFn getQueryObjectUi64vFn() {
    static GetQueryObjectUi64vFn fn =
        reinterpret_cast<GetQueryObjectUi64vFn>(rlGetProcAddress("glGetQueryObjectui64v"));
    return fn;
}

void memoryBarrierBits(unsigned int bits) {
    MemoryBarrierFn fn = memoryBarrierFn();
    if (fn != nullptr) {
        fn(bits);
    }
}

void finishGpu() {
    FinishFn fn = finishFn();
    if (fn != nullptr) {
        fn();
    }
}

void flushGpu() {
    FlushFn fn = flushFn();
    if (fn != nullptr) {
        fn();
    }
}

bool checkGlError(const char* stage) {
    GetErrorFn fn = getErrorFn();
    if (fn == nullptr) {
        return true;
    }
    unsigned int err = fn();
    if (err == 0) {
        return true;
    }
    TraceLog(LOG_WARNING, "sloprad: GL error 0x%x at %s", err, stage);
    while (fn() != 0) {
    }
    return false;
}

/** Chooses how many luxels to process per compute dispatch so that no single GPU
 *  submission runs long enough to trip a platform GPU watchdog. Windows TDR resets the
 *  driver at ~2s and kills the process (STATUS_STACK_BUFFER_OVERRUN / 0xC0000409 out of
 *  the vendor GL driver) before any GL error can be seen; Linux desktop drivers have no
 *  such limit, which is why an un-throttled bake that dies on a fast discrete GPU under
 *  Windows can grind to completion on a slow integrated GPU under Linux.
 *
 *  Per-luxel cost in this bake is wildly non-uniform: a luxel far from every emitter is
 *  nearly free, while one sitting next to a high-sample precise-emission face runs
 *  thousands of occlusion raycasts (see accumulateEmissiveFace in rad_direct_comp.glsl).
 *  A fixed luxel batch that is fine for open areas becomes a multi-second submission in a
 *  dense cluster. So each group is wrapped in a GL_TIME_ELAPSED query and followed by a
 *  glFinish(); the measured per-luxel GPU time feeds an EMA, and the next group's luxel
 *  count is chosen so its predicted time hits a wall-time budget (default 250ms, ~8x
 *  under the 2s Windows watchdog) while never being predicted to approach the danger
 *  limit. The first group is a single floor-sized batch so a safe estimate exists before
 *  any large group goes out; growth is capped per step so a cheap patch cannot overshoot
 *  into a dense region; shrink is immediate.
 *
 *  If the batch is at its floor and still over budget, or a group's measured time crosses
 *  the danger limit anyway (a very sharp cost gradient between groups), the pass aborts
 *  and the caller finishes on the CPU - slower, but it always terminates and never
 *  crashes. --gpu-safe just lowers the maximum batch; it adapts and aborts the same way.
 *  Without GL timer queries (should not happen on a 4.3 context) there is no measurement,
 *  so it falls back to a small fixed batch with a glFinish() per group. */
class GpuBatchPacer {
public:
    GpuBatchPacer(int hardMaxBatch, bool safeMode, const char* label)
        : label_(label),
          budgetNs_(static_cast<std::uint64_t>(
              envSeconds("SLOPRAD_GPU_SYNC_BUDGET_MS", kGpuSyncBudgetSecondsDefault, 0.01, 5.0)
              * 1.0e9)),
          minBatch_(std::clamp(
              envInt("SLOPRAD_GPU_MIN_LUXEL_BATCH", kMinAdaptiveLuxelBatch, 1, 4096),
              1,
              std::max(1, hardMaxBatch))) {
        int cap = std::min(hardMaxBatch, safeMode ? kSafeModeLuxelBatch : hardMaxBatch);
        cap = envInt("SLOPRAD_GPU_MAX_LUXEL_BATCH", cap, 1, 1 << 20);
        maxBatch_ = std::max(minBatch_, std::min(hardMaxBatch, cap));
        timed_ = initTimer();
        batch_ = timed_ ? minBatch_ : std::clamp(kUntimedLuxelBatch, minBatch_, maxBatch_);
    }

    ~GpuBatchPacer() {
        if (query_ != 0) {
            DeleteQueriesFn del = deleteQueriesFn();
            if (del != nullptr) {
                del(1, &query_);
            }
        }
    }

    GpuBatchPacer(const GpuBatchPacer&) = delete;
    GpuBatchPacer& operator=(const GpuBatchPacer&) = delete;

    /** Luxels to cover in the next group of dispatches. */
    int batch() const { return batch_; }
    const char* mode() const { return timed_ ? "adaptive" : "fixed-untimed"; }
    double budgetMs() const { return static_cast<double>(budgetNs_) / 1.0e6; }

    /** Call once before issuing the group's dispatch(es) for `luxelsInGroup` luxels. */
    void beginGroup(int luxelsInGroup) {
        groupLuxels_ = std::max(luxelsInGroup, 1);
        if (timed_) {
            beginQueryFn()(kGlTimeElapsed, query_);
            queryOpen_ = true;
        }
    }

    /** Call once after the group's dispatch(es) + final memory barrier. Runs glFinish(),
     *  measures GPU time, resizes the next batch, and checks for a GL error. Returns false
     *  and sets `failed` when the pass must not continue on the GPU (GL error, a group
     *  over the danger limit, or the batch stuck at the floor and over budget). The caller
     *  then finishes the pass on the CPU. */
    bool endGroup(const char* stage, bool& failed) {
        if (queryOpen_) {
            endQueryFn()(kGlTimeElapsed);
            queryOpen_ = false;
        }
        finishGpu();

        if (timed_ && groupLuxels_ > 0) {
            std::uint64_t elapsedNs = 0;
            getQueryObjectUi64vFn()(query_, kGlQueryResult, &elapsedNs);
            if (elapsedNs > 0) {
                const double elapsedSec = static_cast<double>(elapsedNs) / 1.0e9;
                if (elapsedSec > kGpuGroupDangerSeconds) {
                    TraceLog(
                        LOG_WARNING,
                        "sloprad: GPU %s group of %d luxels took %.2fs (>%.1fs danger limit); "
                        "aborting GPU lighting to avoid a driver watchdog reset - finishing on "
                        "the CPU. Lower --luxels-per-meter / --exact-emission-max-samples, or "
                        "pass --gpu-safe, to keep this on the GPU.",
                        label_,
                        groupLuxels_,
                        elapsedSec,
                        kGpuGroupDangerSeconds);
                    std::fflush(stdout);
                    failed = true;
                    return false;
                }

                const double perLuxelNs =
                    static_cast<double>(elapsedNs) / static_cast<double>(groupLuxels_);
                emaPerLuxelNs_ = emaPerLuxelNs_ > 0.0
                    ? 0.5 * emaPerLuxelNs_ + 0.5 * perLuxelNs
                    : perLuxelNs;

                const double safePerLuxel = std::max(emaPerLuxelNs_, 1.0e-3);
                // Size the next group to the budget, but never to a predicted time that
                // approaches the danger limit even if the estimate is optimistic.
                const double budgetTarget = static_cast<double>(budgetNs_) / safePerLuxel;
                const double dangerCap =
                    kGpuGroupDangerSeconds * 0.5 * 1.0e9 / safePerLuxel;
                double targetD = std::min(budgetTarget, dangerCap);
                int next = targetD >= 1.0 ? static_cast<int>(targetD) : 1;
                next = std::min(next, std::max(batch_ * 2, minBatch_));  // capped growth
                const int adapted = std::clamp(next, minBatch_, maxBatch_);
                if (adapted != batch_) {
                    TraceLog(
                        LOG_DEBUG,
                        "sloprad: GPU %s luxel batch %d -> %d (%.3fms/1k luxels, budget %.0fms)",
                        label_,
                        batch_,
                        adapted,
                        emaPerLuxelNs_ * 1.0e-6 * 1000.0,
                        static_cast<double>(budgetNs_) / 1.0e6);
                    batch_ = adapted;
                }

                const bool floorOverBudget = batch_ <= minBatch_
                    && emaPerLuxelNs_ * static_cast<double>(minBatch_)
                        > static_cast<double>(budgetNs_);
                floorGroups_ = floorOverBudget ? floorGroups_ + 1 : 0;
                if (floorOverBudget && !warnedFloor_) {
                    warnedFloor_ = true;
                    TraceLog(
                        LOG_WARNING,
                        "sloprad: GPU %s pinned at the minimum luxel batch (%d) and still over "
                        "the %.0fms budget - a dense emitter cluster. Will fall back to the CPU "
                        "if this persists; lower --luxels-per-meter / --exact-emission-max-samples "
                        "or pass --gpu-safe.",
                        label_,
                        minBatch_,
                        static_cast<double>(budgetNs_) / 1.0e6);
                    std::fflush(stdout);
                }
                if (floorGroups_ >= kMaxFloorGroupsBeforeAbort) {
                    TraceLog(
                        LOG_WARNING,
                        "sloprad: GPU %s cannot fit the time budget even at the minimum luxel "
                        "batch after %d groups; aborting GPU lighting and finishing on the CPU.",
                        label_,
                        floorGroups_);
                    std::fflush(stdout);
                    failed = true;
                    return false;
                }
            }
        }

        if (!checkGlError(stage)) {
            failed = true;
            return false;
        }
        return true;
    }

private:
    bool initTimer() {
        if (genQueriesFn() == nullptr || deleteQueriesFn() == nullptr || beginQueryFn() == nullptr
            || endQueryFn() == nullptr || getQueryObjectUi64vFn() == nullptr) {
            return false;
        }
        genQueriesFn()(1, &query_);
        return query_ != 0;
    }

    const char* label_ = "";
    std::uint64_t budgetNs_ = 0;
    int minBatch_ = 1;
    int maxBatch_ = 1;
    int batch_ = 1;
    int groupLuxels_ = 0;
    int floorGroups_ = 0;
    bool timed_ = false;
    bool queryOpen_ = false;
    bool warnedFloor_ = false;
    unsigned int query_ = 0;
    double emaPerLuxelNs_ = 0.0;
};

unsigned int loadComputeProgram(std::string_view source) {
    if (source.empty()) {
        return 0;
    }
    std::string code(source);
    const unsigned int shader = rlLoadShader(code.c_str(), RL_COMPUTE_SHADER);
    if (shader == 0) {
        TraceLog(LOG_WARNING, "sloprad: failed to compile compute shader");
        return 0;
    }
    const unsigned int program = rlLoadShaderProgramCompute(shader);
    if (program == 0) {
        TraceLog(LOG_WARNING, "sloprad: failed to link compute program");
    }
    return program;
}

void unloadSsbo(unsigned int id) {
    if (id != 0) {
        rlUnloadShaderBuffer(id);
    }
}

bool validateGpuResults(
    const std::vector<GpuLuxelSSBO>& before,
    const std::vector<GpuLuxelSSBO>& after,
    bool requireSunContribution) {
    std::size_t nonFinite = 0;
    std::size_t improved = 0;
    float maxDelta = 0.0f;
    float maxAfterSum = 0.0f;
    float maxSunSum = 0.0f;

    for (std::size_t i = 0; i < after.size(); ++i) {
        const GpuLuxelSSBO& a = after[i];
        const GpuLuxelSSBO& b = before[i];
        if (!std::isfinite(a.ir) || !std::isfinite(a.ig) || !std::isfinite(a.ib)) {
            ++nonFinite;
            continue;
        }
        const float afterSum = a.ir + a.ig + a.ib;
        maxAfterSum = std::max(maxAfterSum, afterSum);
        maxSunSum = std::max(maxSunSum, a.sunIr + a.sunIg + a.sunIb);
        const float beforeSum = b.ir + b.ig + b.ib;
        const float delta = afterSum - beforeSum;
        maxDelta = std::max(maxDelta, delta);
        if (delta > 1e-5f) {
            ++improved;
        }
    }

    TraceLog(
        LOG_INFO,
        "sloprad: GPU direct diagnostics luxels=%zu improved=%zu maxDelta=%.6f maxSum=%.6f maxSun=%.6f",
        after.size(),
        improved,
        maxDelta,
        maxAfterSum,
        maxSunSum);
    std::fflush(stdout);

    if (nonFinite > 0) {
        TraceLog(
            LOG_WARNING,
            "sloprad: GPU direct lighting produced %zu non-finite luxels",
            nonFinite);
        return false;
    }
    if (!after.empty() && improved == 0) {
        TraceLog(
            LOG_WARNING,
            "sloprad: GPU direct lighting made no irradiance progress (%zu luxels)",
            after.size());
        return false;
    }
    if (requireSunContribution && maxSunSum < 1e-3f) {
        TraceLog(
            LOG_WARNING,
            "sloprad: GPU direct lighting produced no sun irradiance (maxSun=%.6f)",
            maxSunSum);
        return false;
    }
    TraceLog(
        LOG_INFO,
        "sloprad: GPU direct lighting validated improved=%zu/%zu",
        improved,
        after.size());
    return true;
}

constexpr unsigned int kAlphaAtlasTextureUnit = 0;

void bindAlphaAtlasForCompute(unsigned int program, const RadGpuOcclusionResources& resources) {
    if (!resources.valid || resources.alphaAtlas.id == 0) {
        return;
    }
    const int loc = glGetUniformLocation(program, "materialAlphaAtlas");
    if (loc >= 0) {
        const int unit = static_cast<int>(kAlphaAtlasTextureUnit);
        glUniform1i(loc, unit);
    }
    glActiveTexture(GL_TEXTURE0 + kAlphaAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, resources.alphaAtlas.id);
}

void rebindAlphaAtlasTexture(const RadGpuOcclusionResources& resources) {
    if (!resources.valid || resources.alphaAtlas.id == 0) {
        return;
    }
    glActiveTexture(GL_TEXTURE0 + kAlphaAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, resources.alphaAtlas.id);
}

void unbindAlphaAtlas() {
    glActiveTexture(GL_TEXTURE0 + kAlphaAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
}

float wrapCosine(float cosine, float wrap) {
    const float w = std::max(wrap, 0.0f);
    return std::max(0.0f, (cosine + w) / (1.0f + w));
}

Vector3 normalize3(Vector3 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-8f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
}

Vector3 cross3(Vector3 a, Vector3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

void buildSunBasis(Vector3 toLight, Vector3& tangentOut, Vector3& bitangentOut) {
    const Vector3 helper =
        std::abs(toLight.y) < 0.999f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
    tangentOut = normalize3(cross3(helper, toLight));
    bitangentOut = normalize3(cross3(toLight, tangentOut));
}

bool validateGpuSunParity(
    const std::vector<GpuLuxelSSBO>& gpuLuxels,
    const std::vector<RadGpuLight>& lights,
    const QuadBvh& occlusionBvh,
    const RadGpuDirectCpuReference& cpuReference) {
    const RadGpuLight* sunLight = nullptr;
    for (const RadGpuLight& light : lights) {
        if (light.kind == 2) {
            sunLight = &light;
            break;
        }
    }
    if (sunLight == nullptr) {
        return true;
    }

    const float forwardLen = std::sqrt(
        sunLight->direction.x * sunLight->direction.x
        + sunLight->direction.y * sunLight->direction.y
        + sunLight->direction.z * sunLight->direction.z);
    if (forwardLen < 1e-6f) {
        return true;
    }
    const Vector3 toLight{
        -sunLight->direction.x / forwardLen,
        -sunLight->direction.y / forwardLen,
        -sunLight->direction.z / forwardLen,
    };
    const Vector3 intensity{
        sunLight->color.x * sunLight->intensity,
        sunLight->color.y * sunLight->intensity,
        sunLight->color.z * sunLight->intensity,
    };

    Vector3 tangent{};
    Vector3 bitangent{};
    buildSunBasis(toLight, tangent, bitangent);

    constexpr std::size_t kMaxProbes = 512;
    constexpr float kRelTol = 0.05f;
    constexpr float kAbsTol = 1e-3f;
    const float raySlop = 1.5f / static_cast<float>(std::max(cpuReference.sunParams.rayCount, 1));
    const std::size_t stride = std::max(gpuLuxels.size() / kMaxProbes, std::size_t{1});

    std::size_t probes = 0;
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < gpuLuxels.size() && probes < kMaxProbes; i += stride) {
        const GpuLuxelSSBO& luxel = gpuLuxels[i];
        if (luxel.covered != 0) {
            continue;
        }
        const Vector3 normal{luxel.nx, luxel.ny, luxel.nz};
        const Vector3 position{luxel.px, luxel.py, luxel.pz};
        const float nDotL = wrapCosine(
            normal.x * toLight.x + normal.y * toLight.y + normal.z * toLight.z,
            cpuReference.directWrap);
        if (nDotL <= 0.0f) {
            continue;
        }

        Vector3 cpuTint{1.0f, 1.0f, 1.0f};
        const float cpuVisibility = sunSkyVisibilityWithAlphaOcclusion(
            position,
            luxel.faceIndex,
            toLight,
            tangent,
            bitangent,
            cpuReference.sunParams,
            occlusionBvh,
            cpuReference.faceSky,
            cpuReference.faceTransparent,
            cpuReference.faces,
            cpuReference.materialCache,
            &cpuTint);
        const float cpuSunR = intensity.x * nDotL * cpuVisibility * cpuTint.x;
        const float cpuSunG = intensity.y * nDotL * cpuVisibility * cpuTint.y;
        const float cpuSunB = intensity.z * nDotL * cpuVisibility * cpuTint.z;
        const float cpuSun = cpuSunR + cpuSunG + cpuSunB;
        const float gpuSun = luxel.sunIr + luxel.sunIg + luxel.sunIb;
        const float ref = std::max({cpuSun, gpuSun, 1e-4f});
        const float sumSlop = (intensity.x + intensity.y + intensity.z) * nDotL * raySlop;
        const float rSlop = intensity.x * nDotL * raySlop;
        const float gSlop = intensity.y * nDotL * raySlop;
        const float bSlop = intensity.z * nDotL * raySlop;
        ++probes;
        if (std::fabs(cpuSun - gpuSun) > kAbsTol + kRelTol * ref + sumSlop
            || std::fabs(cpuSunR - luxel.sunIr)
                > kAbsTol + kRelTol * std::max({cpuSunR, luxel.sunIr, 1e-4f}) + rSlop
            || std::fabs(cpuSunG - luxel.sunIg)
                > kAbsTol + kRelTol * std::max({cpuSunG, luxel.sunIg, 1e-4f}) + gSlop
            || std::fabs(cpuSunB - luxel.sunIb)
                > kAbsTol + kRelTol * std::max({cpuSunB, luxel.sunIb, 1e-4f}) + bSlop) {
            ++mismatches;
        }
    }

    if (probes == 0) {
        return true;
    }
    TraceLog(
        LOG_INFO,
        "sloprad: GPU sun parity probes=%zu mismatches=%zu",
        probes,
        mismatches);
    std::fflush(stdout);
    return mismatches == 0;
}

void unloadDirectResources(
    unsigned int luxelSsbo,
    unsigned int emissiveFaceSsbo,
    unsigned int emissionGridSsbo,
    unsigned int directSampleSsbo,
    unsigned int lightSsbo,
    unsigned int nodeSsbo,
    unsigned int primSsbo,
    unsigned int emitterNodeSsbo,
    unsigned int emitterPrimSsbo,
    unsigned int paramsSsbo,
    unsigned int reachSsbo,
    unsigned int faceSkySsbo,
    unsigned int faceTransparentSsbo,
    unsigned int faceOcclusionSsbo,
    unsigned int materialRectSsbo,
    unsigned int program) {
    unloadSsbo(luxelSsbo);
    unloadSsbo(emissiveFaceSsbo);
    unloadSsbo(emissionGridSsbo);
    unloadSsbo(directSampleSsbo);
    unloadSsbo(lightSsbo);
    unloadSsbo(nodeSsbo);
    unloadSsbo(primSsbo);
    unloadSsbo(emitterNodeSsbo);
    unloadSsbo(emitterPrimSsbo);
    unloadSsbo(paramsSsbo);
    unloadSsbo(reachSsbo);
    unloadSsbo(faceSkySsbo);
    unloadSsbo(faceTransparentSsbo);
    unloadSsbo(faceOcclusionSsbo);
    unloadSsbo(materialRectSsbo);
    rlUnloadShaderProgram(program);
}

void fillBaseParams(
    GpuParamsSSBO& params,
    int luxelCount,
    int emitterCount,
    int lightCount,
    std::int32_t bvhRoot,
    std::int32_t emitterBvhRoot,
    int luxelOffset,
    int luxelBatch,
    int faceCount,
    const RadGpuOcclusionResources& occlusionResources,
    const RadGpuDirectParams& directParams,
    const RadGpuReachability& reachability) {
    params.luxelCount = luxelCount;
    params.emitterCount = emitterCount;
    params.lightCount = lightCount;
    params.bvhRoot = bvhRoot;
    params.luxelOffset = luxelOffset;
    params.luxelBatch = luxelBatch;
    params.emitterBvhRoot = emitterBvhRoot;
    params.emitterQueryRadius = directParams.emitterQueryRadius;
    params.lightOffset = 0;
    params.lightBatch = 0;
    params.leafCount = reachability.leafCount;
    params.wordsPerRow = reachability.wordsPerRow;
    params.directWrap = directParams.directWrap;
    params.coplanarFill = directParams.coplanarFill;
    params.coplanarSoft = directParams.coplanarSoft;
    params.minDist2 = directParams.minDist2;
    params.emitterDirectSamples = directParams.emitterDirectSamples;
    params.emissionGridFloats = directParams.emissionGridFloats;
    params.sunRayCount = directParams.sunRayCount;
    params.sunAngularSpread = directParams.sunAngularSpread;
    params.sunLeakThreshold = directParams.sunLeakThreshold;
    params.sunRayMaxDistance = directParams.sunRayMaxDistance;
    params.faceCount = faceCount;
    params.materialRectCount =
        static_cast<std::int32_t>(occlusionResources.materialRects.size());
    params.alphaAtlasWidth = occlusionResources.atlasWidth;
    params.alphaAtlasHeight = occlusionResources.atlasHeight;
}

// Issues one direct-lighting dispatch. The enclosing GpuBatchPacer group (beginGroup /
// endGroup around the whole luxel batch, including every light sub-dispatch) owns the
// glFinish, GPU timing, and error check.
void issueDirectDispatch(
    unsigned int program,
    unsigned int paramsSsbo,
    const GpuParamsSSBO& params,
    const RadGpuOcclusionResources& occlusionResources) {
    rlEnableShader(program);
    rebindAlphaAtlasTexture(occlusionResources);
    rlUpdateShaderBuffer(paramsSsbo, &params, sizeof(params), 0);
    memoryBarrierBits(kBufferUpdateBarrierBit | kShaderStorageBarrierBit);
    const unsigned int groups = static_cast<unsigned int>((params.luxelBatch + 63) / 64);
    rlComputeShaderDispatch(groups, 1, 1);
    memoryBarrierBits(kShaderStorageBarrierBit);
}

} // namespace

namespace {

constexpr unsigned int kGlRenderer = 0x1F01;

using GetStringFn = const unsigned char* (*)(unsigned int);

GetStringFn glGetStringFn() {
    static GetStringFn fn = reinterpret_cast<GetStringFn>(rlGetProcAddress("glGetString"));
    return fn;
}

bool stringContainsInsensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            const char a = haystack[i + j];
            const char b = needle[j];
            if (std::tolower(static_cast<unsigned char>(a))
                != std::tolower(static_cast<unsigned char>(b))) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

} // namespace

const char* radiosityGpuRenderer() {
    GetStringFn fn = glGetStringFn();
    if (fn == nullptr) {
        return "";
    }
    const char* renderer = reinterpret_cast<const char*>(fn(kGlRenderer));
    return renderer != nullptr ? renderer : "";
}

bool radiosityGpuIsIntegrated() {
    const char* renderer = radiosityGpuRenderer();
    if (renderer[0] == '\0') {
        return false;
    }
    const std::string_view r(renderer);

    if (stringContainsInsensitive(r, "llvmpipe") || stringContainsInsensitive(r, "softpipe")
        || stringContainsInsensitive(r, "svga3d") || stringContainsInsensitive(r, "virgl")
        || stringContainsInsensitive(r, "Microsoft Basic Render Driver")) {
        return true;
    }
    if (stringContainsInsensitive(r, "Intel")) {
        return true;
    }
    if (stringContainsInsensitive(r, "Apple M")) {
        return true;
    }

    const bool amd =
        stringContainsInsensitive(r, "AMD") || stringContainsInsensitive(r, "Radeon");
    if (amd) {
        if (stringContainsInsensitive(r, " RX ") || stringContainsInsensitive(r, " RX")
            || stringContainsInsensitive(r, "Radeon Pro") || stringContainsInsensitive(r, "FirePro")
            || stringContainsInsensitive(r, "Radeon VII")) {
            return false;
        }
        if (stringContainsInsensitive(r, "Graphics") || stringContainsInsensitive(r, "Raven")
            || stringContainsInsensitive(r, "Renoir") || stringContainsInsensitive(r, "Phoenix")
            || stringContainsInsensitive(r, "Cezanne") || stringContainsInsensitive(r, "Rembrandt")
            || stringContainsInsensitive(r, "Van Gogh") || stringContainsInsensitive(r, "Barcelo")) {
            return true;
        }
    }

    return false;
}

bool radiosityGpuContextReady() {
    if (rlGetVersion() != RL_OPENGL_43) {
        return false;
    }
    return true;
}

bool accumulateDirectLightingGpu(
    std::vector<RadGpuLuxel>& luxels,
    const std::vector<RadGpuEmissiveFace>& emissiveFaces,
    std::span<const Vector3> emissionGrid,
    std::span<const EmitterDirectSample> directSampleData,
    const std::vector<RadGpuLight>& lights,
    const QuadBvh& occlusionBvh,
    const EmitterBvh& emitterBvh,
    std::string_view computeShaderSource,
    const RadGpuDirectParams& directParams,
    const RadGpuReachability& reachability,
    const std::vector<std::int32_t>& faceIsSky,
    const std::vector<std::int32_t>& faceIsTransparent,
    const RadGpuOcclusionResources& occlusionResources,
    const RadGpuDirectCpuReference* cpuReference) {
    if (!occlusionResources.valid) {
        TraceLog(LOG_WARNING, "sloprad: GPU direct lighting requires alpha occlusion resources");
        return false;
    }
    if (cpuReference != nullptr
        && !verifyRadGpuOcclusionAtlas(occlusionResources, cpuReference->materialCache)) {
        TraceLog(LOG_WARNING, "sloprad: GPU alpha atlas verification failed; using CPU direct lighting");
        return false;
    }
    if (!radiosityGpuContextReady()) {
        TraceLog(LOG_WARNING, "sloprad: GPU direct lighting unavailable (no GL 4.3 context)");
        return false;
    }
    if (luxels.empty()) {
        return true;
    }
    if (emissiveFaces.empty() && lights.empty()) {
        TraceLog(LOG_INFO, "sloprad: GPU direct lighting skipped (no emissive faces or lights)");
        return true;
    }

    const unsigned int program = loadComputeProgram(computeShaderSource);
    if (program == 0) {
        return false;
    }

    std::vector<GpuLuxelSSBO> gpuLuxels(luxels.size());
    for (std::size_t i = 0; i < luxels.size(); ++i) {
        const RadGpuLuxel& src = luxels[i];
        GpuLuxelSSBO& dst = gpuLuxels[i];
        dst.px = src.position.x;
        dst.py = src.position.y;
        dst.pz = src.position.z;
        dst.covered = src.covered;
        dst.nx = src.normal.x;
        dst.ny = src.normal.y;
        dst.nz = src.normal.z;
        dst.faceIndex = src.faceIndex;
        dst.ir = src.irradianceR;
        dst.ig = src.irradianceG;
        dst.ib = src.irradianceB;
        dst.leafIndex = src.interiorLeaf;
    }
    const std::vector<GpuLuxelSSBO> gpuLuxelsBefore = gpuLuxels;

    std::vector<GpuEmissiveFaceSSBO> gpuEmissiveFaces(std::max<std::size_t>(emissiveFaces.size(), 1));
    for (std::size_t i = 0; i < emissiveFaces.size(); ++i) {
        const RadGpuEmissiveFace& src = emissiveFaces[i];
        GpuEmissiveFaceSSBO& dst = gpuEmissiveFaces[i];
        dst.uAxisX = src.uAxis.x;
        dst.uAxisY = src.uAxis.y;
        dst.uAxisZ = src.uAxis.z;
        dst.planeD = src.planeD;
        dst.vAxisX = src.vAxis.x;
        dst.vAxisY = src.vAxis.y;
        dst.vAxisZ = src.vAxis.z;
        dst.uMin = src.uMin;
        dst.uMax = src.uMax;
        dst.vMin = src.vMin;
        dst.vMax = src.vMax;
        dst.nx = src.normal.x;
        dst.ny = src.normal.y;
        dst.nz = src.normal.z;
        dst.area = src.area;
        dst.faceIndex = src.faceIndex;
        dst.interiorLeaf = src.interiorLeaf;
        dst.gridWidth = src.gridWidth;
        dst.gridHeight = src.gridHeight;
        dst.gridOffset = src.gridOffset;
        dst.directSampleOffset = src.directSampleOffset;
        dst.peakR = src.peakRadiance.x;
        dst.peakG = src.peakRadiance.y;
        dst.peakB = src.peakRadiance.z;
        dst.castRange = src.castRange;
        dst.aabbMinX = src.aabbMins.x;
        dst.aabbMinY = src.aabbMins.y;
        dst.aabbMinZ = src.aabbMins.z;
        dst.aabbMaxX = src.aabbMaxs.x;
        dst.aabbMaxY = src.aabbMaxs.y;
        dst.aabbMaxZ = src.aabbMaxs.z;
        dst.directSampleAxis = src.directSampleAxis;
        dst.directSampleCount = src.directSampleCount;
    }

    std::vector<GpuGridSampleSSBO> gpuGridSamples(std::max<std::size_t>(emissionGrid.size(), 1));
    for (std::size_t i = 0; i < emissionGrid.size(); ++i) {
        gpuGridSamples[i].r = emissionGrid[i].x;
        gpuGridSamples[i].g = emissionGrid[i].y;
        gpuGridSamples[i].b = emissionGrid[i].z;
    }

    std::vector<GpuDirectSampleSSBO> gpuDirectSamples(std::max<std::size_t>(directSampleData.size(), 1));
    for (std::size_t i = 0; i < directSampleData.size(); ++i) {
        gpuDirectSamples[i].u = directSampleData[i].u;
        gpuDirectSamples[i].v = directSampleData[i].v;
        gpuDirectSamples[i].r = directSampleData[i].radiance.x;
        gpuDirectSamples[i].g = directSampleData[i].radiance.y;
        gpuDirectSamples[i].b = directSampleData[i].radiance.z;
    }

    std::vector<GpuLightSSBO> gpuLights(std::max<std::size_t>(lights.size(), 1));
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const RadGpuLight& src = lights[i];
        GpuLightSSBO& dst = gpuLights[i];
        dst.px = src.position.x;
        dst.py = src.position.y;
        dst.pz = src.position.z;
        dst.kind = src.kind;
        dst.dx = src.direction.x;
        dst.dy = src.direction.y;
        dst.dz = src.direction.z;
        dst.intensity = src.intensity;
        dst.cr = src.color.x;
        dst.cg = src.color.y;
        dst.cb = src.color.z;
        dst.range = src.range;
        dst.coneAngle = src.coneAngle;
        dst.leafIndex = src.interiorLeaf;
    }

    std::vector<GpuBvhNodeSSBO> gpuNodes;
    std::vector<GpuBvhPrimSSBO> gpuPrims;
    std::int32_t bvhRoot = -1;
    if (!occlusionBvh.empty()) {
        bvhRoot = occlusionBvh.root;
        gpuNodes.resize(occlusionBvh.nodes.size());
        for (std::size_t i = 0; i < occlusionBvh.nodes.size(); ++i) {
            const QuadBvh::Node& src = occlusionBvh.nodes[i];
            GpuBvhNodeSSBO& dst = gpuNodes[i];
            dst.minx = src.mins.x;
            dst.miny = src.mins.y;
            dst.minz = src.mins.z;
            dst.left = src.left;
            dst.maxx = src.maxs.x;
            dst.maxy = src.maxs.y;
            dst.maxz = src.maxs.z;
            dst.right = src.right;
            dst.firstPrim = src.firstPrim;
            dst.primCount = src.primCount;
        }
        gpuPrims.resize(occlusionBvh.prims.size());
        for (std::size_t i = 0; i < occlusionBvh.prims.size(); ++i) {
            const QuadBvh::Prim& src = occlusionBvh.prims[i];
            GpuBvhPrimSSBO& dst = gpuPrims[i];
            dst.v0x = src.tri[0].x;
            dst.v0y = src.tri[0].y;
            dst.v0z = src.tri[0].z;
            dst.v1x = src.tri[1].x;
            dst.v1y = src.tri[1].y;
            dst.v1z = src.tri[1].z;
            dst.v2x = src.tri[2].x;
            dst.v2y = src.tri[2].y;
            dst.v2z = src.tri[2].z;
            dst.nx = src.normal.x;
            dst.ny = src.normal.y;
            dst.nz = src.normal.z;
            dst.faceIndex = src.faceIndex;
        }
    } else {
        gpuNodes.push_back({});
        gpuPrims.push_back({});
    }

    std::vector<GpuBvhNodeSSBO> gpuEmitterNodes;
    std::vector<GpuEmitterBvhPrimSSBO> gpuEmitterPrims;
    std::int32_t emitterBvhRoot = -1;
    if (!emitterBvh.empty()) {
        emitterBvhRoot = emitterBvh.root;
        gpuEmitterNodes.resize(emitterBvh.nodes.size());
        for (std::size_t i = 0; i < emitterBvh.nodes.size(); ++i) {
            const EmitterBvh::Node& src = emitterBvh.nodes[i];
            GpuBvhNodeSSBO& dst = gpuEmitterNodes[i];
            dst.minx = src.mins.x;
            dst.miny = src.mins.y;
            dst.minz = src.mins.z;
            dst.left = src.left;
            dst.maxx = src.maxs.x;
            dst.maxy = src.maxs.y;
            dst.maxz = src.maxs.z;
            dst.right = src.right;
            dst.firstPrim = src.firstPrim;
            dst.primCount = src.primCount;
        }
        gpuEmitterPrims.resize(emitterBvh.prims.size());
        for (std::size_t i = 0; i < emitterBvh.prims.size(); ++i) {
            const EmitterBvh::Prim& src = emitterBvh.prims[i];
            GpuEmitterBvhPrimSSBO& dst = gpuEmitterPrims[i];
            dst.minx = src.mins.x;
            dst.miny = src.mins.y;
            dst.minz = src.mins.z;
            dst.maxx = src.maxs.x;
            dst.maxy = src.maxs.y;
            dst.maxz = src.maxs.z;
            dst.emitterIndex = src.emitterIndex;
        }
    } else {
        gpuEmitterNodes.push_back({});
        gpuEmitterPrims.push_back({});
    }

    std::vector<std::uint32_t> reachBits = reachability.bits;
    if (reachBits.empty()) {
        reachBits.push_back(0);
    }
    std::vector<std::int32_t> faceSkyBits = faceIsSky;
    if (faceSkyBits.empty()) {
        faceSkyBits.push_back(0);
    }
    std::vector<std::int32_t> faceTransparentBits = faceIsTransparent;
    if (faceTransparentBits.empty()) {
        faceTransparentBits.push_back(0);
    }

    const unsigned int luxelSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuLuxels.size() * sizeof(GpuLuxelSSBO)),
        gpuLuxels.data(),
        RL_DYNAMIC_COPY);
    const unsigned int emissiveFaceSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuEmissiveFaces.size() * sizeof(GpuEmissiveFaceSSBO)),
        gpuEmissiveFaces.data(),
        RL_DYNAMIC_COPY);
    const unsigned int emissionGridSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuGridSamples.size() * sizeof(GpuGridSampleSSBO)),
        gpuGridSamples.data(),
        RL_DYNAMIC_COPY);
    const unsigned int directSampleSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuDirectSamples.size() * sizeof(GpuDirectSampleSSBO)),
        gpuDirectSamples.data(),
        RL_DYNAMIC_COPY);
    const unsigned int lightSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuLights.size() * sizeof(GpuLightSSBO)),
        gpuLights.data(),
        RL_DYNAMIC_COPY);
    const unsigned int nodeSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuNodes.size() * sizeof(GpuBvhNodeSSBO)),
        gpuNodes.data(),
        RL_DYNAMIC_COPY);
    const unsigned int primSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuPrims.size() * sizeof(GpuBvhPrimSSBO)),
        gpuPrims.data(),
        RL_DYNAMIC_COPY);
    const unsigned int emitterNodeSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuEmitterNodes.size() * sizeof(GpuBvhNodeSSBO)),
        gpuEmitterNodes.data(),
        RL_DYNAMIC_COPY);
    const unsigned int emitterPrimSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuEmitterPrims.size() * sizeof(GpuEmitterBvhPrimSSBO)),
        gpuEmitterPrims.data(),
        RL_DYNAMIC_COPY);
    const unsigned int reachSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(reachBits.size() * sizeof(std::uint32_t)),
        reachBits.data(),
        RL_DYNAMIC_COPY);
    const unsigned int faceSkySsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(faceSkyBits.size() * sizeof(std::int32_t)),
        faceSkyBits.data(),
        RL_DYNAMIC_COPY);
    const unsigned int faceTransparentSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(faceTransparentBits.size() * sizeof(std::int32_t)),
        faceTransparentBits.data(),
        RL_DYNAMIC_COPY);

    std::vector<GpuFaceOcclusionSSBO> gpuFaceOcclusion(occlusionResources.faceOcclusion.size());
    for (std::size_t i = 0; i < occlusionResources.faceOcclusion.size(); ++i) {
        const RadGpuFaceOcclusion& src = occlusionResources.faceOcclusion[i];
        GpuFaceOcclusionSSBO& dst = gpuFaceOcclusion[i];
        dst.uvUAxisX = src.uvUAxisX;
        dst.uvUAxisY = src.uvUAxisY;
        dst.uvUAxisZ = src.uvUAxisZ;
        dst.isTransparent = src.isTransparent;
        dst.uvVAxisX = src.uvVAxisX;
        dst.uvVAxisY = src.uvVAxisY;
        dst.uvVAxisZ = src.uvVAxisZ;
        dst.materialIndex = src.materialIndex;
        dst.uvShiftX = src.uvShiftX;
        dst.uvShiftY = src.uvShiftY;
        dst.uvScaleX = src.uvScaleX;
        dst.uvScaleY = src.uvScaleY;
        dst.pixelsPerMeter = src.pixelsPerMeter;
        dst.baseColorAlpha = src.baseColorAlpha;
        dst.baseColorR = src.baseColorR;
        dst.baseColorG = src.baseColorG;
        dst.baseColorB = src.baseColorB;
        dst.ior = src.ior;
    }
    if (gpuFaceOcclusion.empty()) {
        gpuFaceOcclusion.push_back({});
    }

    std::vector<GpuMaterialRectSSBO> gpuMaterialRects(occlusionResources.materialRects.size());
    for (std::size_t i = 0; i < occlusionResources.materialRects.size(); ++i) {
        const RadGpuMaterialRect& src = occlusionResources.materialRects[i];
        GpuMaterialRectSSBO& dst = gpuMaterialRects[i];
        dst.u0 = src.u0;
        dst.v0 = src.v0;
        dst.u1 = src.u1;
        dst.v1 = src.v1;
        dst.baseColorAlpha = src.baseColorAlpha;
        dst.textureWidth = src.textureWidth;
        dst.textureHeight = src.textureHeight;
        dst.yPixelOffset = src.yPixelOffset;
    }
    if (gpuMaterialRects.empty()) {
        gpuMaterialRects.push_back({});
    }

    const unsigned int faceOcclusionSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuFaceOcclusion.size() * sizeof(GpuFaceOcclusionSSBO)),
        gpuFaceOcclusion.data(),
        RL_DYNAMIC_COPY);
    const unsigned int materialRectSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuMaterialRects.size() * sizeof(GpuMaterialRectSSBO)),
        gpuMaterialRects.data(),
        RL_DYNAMIC_COPY);

    const int luxelCount = static_cast<int>(gpuLuxels.size());
    const int emitterCount = static_cast<int>(emissiveFaces.size());
    const int lightCount = static_cast<int>(lights.size());
    const int faceCount = static_cast<int>(faceIsSky.size());
    bool requireSunContribution = false;
    for (const RadGpuLight& light : lights) {
        if (light.kind == 2) {
            requireSunContribution = true;
            break;
        }
    }
    const DirectDispatchConfig dispatchConfig =
        directDispatchConfig(luxelCount, lightCount, directParams.gpuSafeMode);

    GpuParamsSSBO initialParams{};
    fillBaseParams(
        initialParams,
        luxelCount,
        emitterCount,
        lightCount,
        bvhRoot,
        emitterBvhRoot,
        0,
        std::min(dispatchConfig.luxelBatch, luxelCount),
        faceCount,
        occlusionResources,
        directParams,
        reachability);
    const unsigned int paramsSsbo =
        rlLoadShaderBuffer(sizeof(GpuParamsSSBO), &initialParams, RL_DYNAMIC_COPY);

    if (luxelSsbo == 0 || emissiveFaceSsbo == 0 || emissionGridSsbo == 0 || directSampleSsbo == 0
        || lightSsbo == 0
        || nodeSsbo == 0 || primSsbo == 0 || emitterNodeSsbo == 0 || emitterPrimSsbo == 0
        || paramsSsbo == 0 || reachSsbo == 0 || faceSkySsbo == 0 || faceTransparentSsbo == 0
        || faceOcclusionSsbo == 0 || materialRectSsbo == 0) {
        TraceLog(LOG_WARNING, "sloprad: failed to allocate GPU SSBOs for direct lighting");
        unloadDirectResources(
            luxelSsbo,
            emissiveFaceSsbo,
            emissionGridSsbo,
            directSampleSsbo,
            lightSsbo,
            nodeSsbo,
            primSsbo,
            emitterNodeSsbo,
            emitterPrimSsbo,
            paramsSsbo,
            reachSsbo,
            faceSkySsbo,
            faceTransparentSsbo,
            faceOcclusionSsbo,
            materialRectSsbo,
            program);
        return false;
    }
    if (!checkGlError("ssbo allocate")) {
        unloadDirectResources(
            luxelSsbo,
            emissiveFaceSsbo,
            emissionGridSsbo,
            directSampleSsbo,
            lightSsbo,
            nodeSsbo,
            primSsbo,
            emitterNodeSsbo,
            emitterPrimSsbo,
            paramsSsbo,
            reachSsbo,
            faceSkySsbo,
            faceTransparentSsbo,
            faceOcclusionSsbo,
            materialRectSsbo,
            program);
        return false;
    }

    GpuBatchPacer pacer(dispatchConfig.luxelBatch, directParams.gpuSafeMode, "direct");

    TraceLog(
        LOG_INFO,
        "sloprad: GPU direct lighting luxels=%d emissiveFaces=%d lights=%d luxelBatch<=%d lightBatch=%d sync=%s(budget=%.0fms) bvhRoot=%d emitterBvhRoot=%d leafCull=%d safeMode=%d",
        luxelCount,
        emitterCount,
        lightCount,
        dispatchConfig.luxelBatch,
        dispatchConfig.lightBatch,
        pacer.mode(),
        pacer.budgetMs(),
        bvhRoot,
        emitterBvhRoot,
        reachability.leafCount > 0 ? 1 : 0,
        directParams.gpuSafeMode ? 1 : 0);
    std::fflush(stdout);

    rlEnableShader(program);
    rlBindShaderBuffer(luxelSsbo, 0);
    rlBindShaderBuffer(emissiveFaceSsbo, 1);
    rlBindShaderBuffer(nodeSsbo, 2);
    rlBindShaderBuffer(primSsbo, 3);
    rlBindShaderBuffer(paramsSsbo, 4);
    rlBindShaderBuffer(lightSsbo, 5);
    rlBindShaderBuffer(reachSsbo, 6);
    rlBindShaderBuffer(faceSkySsbo, 7);
    rlBindShaderBuffer(faceTransparentSsbo, 8);
    rlBindShaderBuffer(emitterNodeSsbo, 9);
    rlBindShaderBuffer(emitterPrimSsbo, 10);
    rlBindShaderBuffer(emissionGridSsbo, 11);
    rlBindShaderBuffer(faceOcclusionSsbo, 12);
    rlBindShaderBuffer(materialRectSsbo, 13);
    rlBindShaderBuffer(directSampleSsbo, 14);
    bindAlphaAtlasForCompute(program, occlusionResources);

    bool dispatchFailed = false;
    int lastLoggedLuxels = -1;
    const int logLuxelStep = std::max(luxelCount / 50, 1);
    for (int luxelOffset = 0; luxelOffset < luxelCount && !dispatchFailed;) {
        const int luxelBatch = std::min(pacer.batch(), luxelCount - luxelOffset);
        pacer.beginGroup(luxelBatch);

        if (emitterBvhRoot >= 0 && emitterCount > 0) {
            GpuParamsSSBO params{};
            fillBaseParams(
                params,
                luxelCount,
                emitterCount,
                lightCount,
                bvhRoot,
                emitterBvhRoot,
                luxelOffset,
                luxelBatch,
                faceCount,
                occlusionResources,
                directParams,
                reachability);
            issueDirectDispatch(program, paramsSsbo, params, occlusionResources);
        }

        for (int lightOffset = 0; lightOffset < lightCount;
             lightOffset += dispatchConfig.lightBatch) {
            GpuParamsSSBO params{};
            fillBaseParams(
                params,
                luxelCount,
                emitterCount,
                lightCount,
                bvhRoot,
                -1,
                luxelOffset,
                luxelBatch,
                faceCount,
                occlusionResources,
                directParams,
                reachability);
            params.lightOffset = lightOffset;
            params.lightBatch = std::min(dispatchConfig.lightBatch, lightCount - lightOffset);
            issueDirectDispatch(program, paramsSsbo, params, occlusionResources);
        }

        if (!pacer.endGroup("compute dispatch", dispatchFailed)) {
            break;
        }

        luxelOffset += luxelBatch;
        if (luxelOffset == luxelCount || luxelOffset / logLuxelStep != lastLoggedLuxels) {
            lastLoggedLuxels = luxelOffset / logLuxelStep;
            TraceLog(
                LOG_INFO,
                "sloprad: GPU direct %d/%d (%.0f%%) batch=%d",
                luxelOffset,
                luxelCount,
                100.0 * static_cast<double>(luxelOffset) / static_cast<double>(luxelCount),
                pacer.batch());
            std::fflush(stdout);
        }
    }

    rlDisableShader();
    unbindAlphaAtlas();
    if (dispatchFailed) {
        unloadDirectResources(
            luxelSsbo,
            emissiveFaceSsbo,
            emissionGridSsbo,
            directSampleSsbo,
            lightSsbo,
            nodeSsbo,
            primSsbo,
            emitterNodeSsbo,
            emitterPrimSsbo,
            paramsSsbo,
            reachSsbo,
            faceSkySsbo,
            faceTransparentSsbo,
            faceOcclusionSsbo,
            materialRectSsbo,
            program);
        return false;
    }

    rlReadShaderBuffer(
        luxelSsbo,
        gpuLuxels.data(),
        static_cast<unsigned int>(gpuLuxels.size() * sizeof(GpuLuxelSSBO)),
        0);
    if (!checkGlError("ssbo readback")) {
        unloadDirectResources(
            luxelSsbo,
            emissiveFaceSsbo,
            emissionGridSsbo,
            directSampleSsbo,
            lightSsbo,
            nodeSsbo,
            primSsbo,
            emitterNodeSsbo,
            emitterPrimSsbo,
            paramsSsbo,
            reachSsbo,
            faceSkySsbo,
            faceTransparentSsbo,
            faceOcclusionSsbo,
            materialRectSsbo,
            program);
        return false;
    }

    unloadDirectResources(
        luxelSsbo,
        emissiveFaceSsbo,
        emissionGridSsbo,
        directSampleSsbo,
        lightSsbo,
        nodeSsbo,
        primSsbo,
        emitterNodeSsbo,
        emitterPrimSsbo,
        paramsSsbo,
        reachSsbo,
        faceSkySsbo,
        faceTransparentSsbo,
        faceOcclusionSsbo,
        materialRectSsbo,
        program);

    if (!validateGpuResults(gpuLuxelsBefore, gpuLuxels, requireSunContribution)) {
        return false;
    }
    if (cpuReference != nullptr
        && !validateGpuSunParity(gpuLuxels, lights, occlusionBvh, *cpuReference)) {
        TraceLog(
            LOG_WARNING,
            "sloprad: GPU sun results diverged from CPU; using CPU direct lighting");
        std::fflush(stdout);
        return false;
    }

    for (std::size_t i = 0; i < luxels.size(); ++i) {
        luxels[i].irradianceR = gpuLuxels[i].ir;
        luxels[i].irradianceG = gpuLuxels[i].ig;
        luxels[i].irradianceB = gpuLuxels[i].ib;
        luxels[i].sunIrradianceR = gpuLuxels[i].sunIr;
        luxels[i].sunIrradianceG = gpuLuxels[i].sunIg;
        luxels[i].sunIrradianceB = gpuLuxels[i].sunIb;
    }

    TraceLog(LOG_INFO, "sloprad: GPU direct lighting complete");
    std::fflush(stdout);
    return true;
}

namespace {

struct GpuBounceLuxelSSBO {
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    std::int32_t covered = 0;
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
    std::int32_t faceIndex = -1;
    std::int32_t localX = 0;
    std::int32_t localY = 0;
    std::int32_t pad0 = 0;
    std::int32_t pad1 = 0;
};

struct GpuRgbSSBO {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float pad = 0.0f;
};

struct GpuFaceGridSSBO {
    std::int32_t luxelBase = 0;
    std::int32_t luxelWidth = 0;
    std::int32_t luxelHeight = 0;
    std::int32_t valid = 0;
    float ux = 0.0f;
    float uy = 0.0f;
    float uz = 0.0f;
    float uMin = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    float vMin = 0.0f;
    float uMax = 0.0f;
    float vMax = 0.0f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
};

struct GpuBounceParamsSSBO {
    std::int32_t luxelCount = 0;
    std::int32_t faceCount = 0;
    std::int32_t sampleCount = 0;
    std::int32_t bvhRoot = -1;
    std::int32_t luxelOffset = 0;
    std::int32_t luxelBatch = 0;
    std::uint32_t seed = 1;
    float rayMaxDistance = 1000.0f;
    float ambientR = 0.0f;
    float ambientG = 0.0f;
    float ambientB = 0.0f;
    std::int32_t materialRectCount = 0;
    std::int32_t alphaAtlasWidth = 1;
    std::int32_t alphaAtlasHeight = 1;
};

static_assert(sizeof(GpuBounceLuxelSSBO) == 48);
static_assert(sizeof(GpuRgbSSBO) == 16);
static_assert(sizeof(GpuFaceGridSSBO) == 64);
static_assert(sizeof(GpuBounceParamsSSBO) == 56);

void unloadBounceResources(
    unsigned int luxelSsbo,
    unsigned int gatherSsbo,
    unsigned int shootSsbo,
    unsigned int faceSsbo,
    unsigned int nodeSsbo,
    unsigned int primSsbo,
    unsigned int paramsSsbo,
    unsigned int faceTransparentSsbo,
    unsigned int faceOcclusionSsbo,
    unsigned int materialRectSsbo,
    unsigned int program) {
    unloadSsbo(luxelSsbo);
    unloadSsbo(gatherSsbo);
    unloadSsbo(shootSsbo);
    unloadSsbo(faceSsbo);
    unloadSsbo(nodeSsbo);
    unloadSsbo(primSsbo);
    unloadSsbo(paramsSsbo);
    unloadSsbo(faceTransparentSsbo);
    unloadSsbo(faceOcclusionSsbo);
    unloadSsbo(materialRectSsbo);
    if (program != 0) {
        rlUnloadShaderProgram(program);
    }
}

} // namespace

std::optional<RadGpuBounceSession> createBounceGpuSession(
    const std::vector<RadGpuBounceLuxel>& luxels,
    const std::vector<RadGpuFaceGrid>& faceGrids,
    const QuadBvh& sceneBvh,
    std::string_view computeShaderSource,
    const std::vector<char>& faceTransparent,
    const RadGpuOcclusionResources& occlusionResources,
    bool gpuSafeMode) {
    if (!occlusionResources.valid) {
        TraceLog(LOG_WARNING, "sloprad: GPU bounce lighting requires alpha occlusion resources");
        return std::nullopt;
    }
    if (!radiosityGpuContextReady()) {
        TraceLog(LOG_WARNING, "sloprad: GPU bounce lighting unavailable (no GL 4.3 context)");
        return std::nullopt;
    }

    RadGpuBounceSession session;
    session.occlusionResources = &occlusionResources;
    session.luxelCount = static_cast<int>(luxels.size());
    if (luxels.empty()) {
        return session;
    }

    const unsigned int program = loadComputeProgram(computeShaderSource);
    if (program == 0) {
        return std::nullopt;
    }

    std::vector<GpuBounceLuxelSSBO> gpuLuxels(luxels.size());
    std::vector<GpuRgbSSBO> gpuGather(luxels.size());
    for (std::size_t i = 0; i < luxels.size(); ++i) {
        const RadGpuBounceLuxel& src = luxels[i];
        GpuBounceLuxelSSBO& dst = gpuLuxels[i];
        dst.px = src.position.x;
        dst.py = src.position.y;
        dst.pz = src.position.z;
        dst.covered = src.covered;
        dst.nx = src.normal.x;
        dst.ny = src.normal.y;
        dst.nz = src.normal.z;
        dst.faceIndex = src.faceIndex;
        dst.localX = src.localX;
        dst.localY = src.localY;
    }

    std::vector<GpuFaceGridSSBO> gpuFaces(std::max<std::size_t>(faceGrids.size(), 1));
    for (std::size_t i = 0; i < faceGrids.size(); ++i) {
        const RadGpuFaceGrid& src = faceGrids[i];
        GpuFaceGridSSBO& dst = gpuFaces[i];
        dst.luxelBase = src.luxelBase;
        dst.luxelWidth = src.luxelWidth;
        dst.luxelHeight = src.luxelHeight;
        dst.valid = src.valid;
        dst.ux = src.uAxis.x;
        dst.uy = src.uAxis.y;
        dst.uz = src.uAxis.z;
        dst.uMin = src.uMin;
        dst.vx = src.vAxis.x;
        dst.vy = src.vAxis.y;
        dst.vz = src.vAxis.z;
        dst.vMin = src.vMin;
        dst.uMax = src.uMax;
        dst.vMax = src.vMax;
    }

    std::vector<GpuBvhNodeSSBO> gpuNodes;
    std::vector<GpuBvhPrimSSBO> gpuPrims;
    std::int32_t bvhRoot = -1;
    if (!sceneBvh.empty()) {
        bvhRoot = sceneBvh.root;
        gpuNodes.resize(sceneBvh.nodes.size());
        for (std::size_t i = 0; i < sceneBvh.nodes.size(); ++i) {
            const QuadBvh::Node& src = sceneBvh.nodes[i];
            GpuBvhNodeSSBO& dst = gpuNodes[i];
            dst.minx = src.mins.x;
            dst.miny = src.mins.y;
            dst.minz = src.mins.z;
            dst.left = src.left;
            dst.maxx = src.maxs.x;
            dst.maxy = src.maxs.y;
            dst.maxz = src.maxs.z;
            dst.right = src.right;
            dst.firstPrim = src.firstPrim;
            dst.primCount = src.primCount;
        }
        gpuPrims.resize(sceneBvh.prims.size());
        for (std::size_t i = 0; i < sceneBvh.prims.size(); ++i) {
            const QuadBvh::Prim& src = sceneBvh.prims[i];
            GpuBvhPrimSSBO& dst = gpuPrims[i];
            dst.v0x = src.tri[0].x;
            dst.v0y = src.tri[0].y;
            dst.v0z = src.tri[0].z;
            dst.v1x = src.tri[1].x;
            dst.v1y = src.tri[1].y;
            dst.v1z = src.tri[1].z;
            dst.v2x = src.tri[2].x;
            dst.v2y = src.tri[2].y;
            dst.v2z = src.tri[2].z;
            dst.nx = src.normal.x;
            dst.ny = src.normal.y;
            dst.nz = src.normal.z;
            dst.faceIndex = src.faceIndex;
        }
    } else {
        gpuNodes.push_back({});
        gpuPrims.push_back({});
    }

    const unsigned int luxelSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuLuxels.size() * sizeof(GpuBounceLuxelSSBO)),
        gpuLuxels.data(),
        RL_DYNAMIC_COPY);
    const unsigned int gatherSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuGather.size() * sizeof(GpuRgbSSBO)),
        gpuGather.data(),
        RL_DYNAMIC_COPY);
    // shootSsbo starts empty; the first runBounceGpuPass() call fills it via
    // rlUpdateShaderBuffer before any dispatch reads it.
    const unsigned int shootSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuGather.size() * sizeof(GpuRgbSSBO)),
        nullptr,
        RL_DYNAMIC_COPY);
    const unsigned int faceSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuFaces.size() * sizeof(GpuFaceGridSSBO)),
        gpuFaces.data(),
        RL_DYNAMIC_COPY);
    const unsigned int nodeSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuNodes.size() * sizeof(GpuBvhNodeSSBO)),
        gpuNodes.data(),
        RL_DYNAMIC_COPY);
    const unsigned int primSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuPrims.size() * sizeof(GpuBvhPrimSSBO)),
        gpuPrims.data(),
        RL_DYNAMIC_COPY);

    const int luxelCount = static_cast<int>(gpuLuxels.size());
    const BounceDispatchConfig dispatchConfig = bounceDispatchConfig(luxelCount, gpuSafeMode);

    // Placeholder content; every dispatch overwrites this buffer in full before use
    // (see runBounceGpuPass), so the initial contents here are never read.
    const GpuBounceParamsSSBO placeholderParams{};
    const unsigned int paramsSsbo =
        rlLoadShaderBuffer(sizeof(GpuBounceParamsSSBO), &placeholderParams, RL_DYNAMIC_COPY);

    std::vector<std::int32_t> faceTransparentBits(faceTransparent.size(), 0);
    for (std::size_t i = 0; i < faceTransparent.size(); ++i) {
        faceTransparentBits[i] = faceTransparent[i] != 0 ? 1 : 0;
    }
    if (faceTransparentBits.empty()) {
        faceTransparentBits.push_back(0);
    }
    const unsigned int faceTransparentSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(faceTransparentBits.size() * sizeof(std::int32_t)),
        faceTransparentBits.data(),
        RL_DYNAMIC_COPY);

    std::vector<GpuFaceOcclusionSSBO> gpuFaceOcclusion(occlusionResources.faceOcclusion.size());
    for (std::size_t i = 0; i < occlusionResources.faceOcclusion.size(); ++i) {
        const RadGpuFaceOcclusion& src = occlusionResources.faceOcclusion[i];
        GpuFaceOcclusionSSBO& dst = gpuFaceOcclusion[i];
        dst.uvUAxisX = src.uvUAxisX;
        dst.uvUAxisY = src.uvUAxisY;
        dst.uvUAxisZ = src.uvUAxisZ;
        dst.isTransparent = src.isTransparent;
        dst.uvVAxisX = src.uvVAxisX;
        dst.uvVAxisY = src.uvVAxisY;
        dst.uvVAxisZ = src.uvVAxisZ;
        dst.materialIndex = src.materialIndex;
        dst.uvShiftX = src.uvShiftX;
        dst.uvShiftY = src.uvShiftY;
        dst.uvScaleX = src.uvScaleX;
        dst.uvScaleY = src.uvScaleY;
        dst.pixelsPerMeter = src.pixelsPerMeter;
        dst.baseColorAlpha = src.baseColorAlpha;
        dst.baseColorR = src.baseColorR;
        dst.baseColorG = src.baseColorG;
        dst.baseColorB = src.baseColorB;
        dst.ior = src.ior;
    }
    if (gpuFaceOcclusion.empty()) {
        gpuFaceOcclusion.push_back({});
    }

    std::vector<GpuMaterialRectSSBO> gpuMaterialRects(occlusionResources.materialRects.size());
    for (std::size_t i = 0; i < occlusionResources.materialRects.size(); ++i) {
        const RadGpuMaterialRect& src = occlusionResources.materialRects[i];
        GpuMaterialRectSSBO& dst = gpuMaterialRects[i];
        dst.u0 = src.u0;
        dst.v0 = src.v0;
        dst.u1 = src.u1;
        dst.v1 = src.v1;
        dst.baseColorAlpha = src.baseColorAlpha;
        dst.textureWidth = src.textureWidth;
        dst.textureHeight = src.textureHeight;
        dst.yPixelOffset = src.yPixelOffset;
    }
    if (gpuMaterialRects.empty()) {
        gpuMaterialRects.push_back({});
    }

    const unsigned int faceOcclusionSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuFaceOcclusion.size() * sizeof(GpuFaceOcclusionSSBO)),
        gpuFaceOcclusion.data(),
        RL_DYNAMIC_COPY);
    const unsigned int materialRectSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuMaterialRects.size() * sizeof(GpuMaterialRectSSBO)),
        gpuMaterialRects.data(),
        RL_DYNAMIC_COPY);

    if (luxelSsbo == 0 || gatherSsbo == 0 || shootSsbo == 0 || faceSsbo == 0 || nodeSsbo == 0
        || primSsbo == 0 || paramsSsbo == 0 || faceTransparentSsbo == 0 || faceOcclusionSsbo == 0
        || materialRectSsbo == 0) {
        TraceLog(LOG_WARNING, "sloprad: failed to allocate GPU SSBOs for bounce lighting");
        unloadBounceResources(
            luxelSsbo,
            gatherSsbo,
            shootSsbo,
            faceSsbo,
            nodeSsbo,
            primSsbo,
            paramsSsbo,
            faceTransparentSsbo,
            faceOcclusionSsbo,
            materialRectSsbo,
            program);
        return std::nullopt;
    }
    if (!checkGlError("bounce ssbo allocate")) {
        unloadBounceResources(
            luxelSsbo,
            gatherSsbo,
            shootSsbo,
            faceSsbo,
            nodeSsbo,
            primSsbo,
            paramsSsbo,
            faceTransparentSsbo,
            faceOcclusionSsbo,
            materialRectSsbo,
            program);
        return std::nullopt;
    }

    TraceLog(
        LOG_INFO,
        "sloprad: GPU bounce session created luxels=%d faces=%d bvhRoot=%d luxelBatch=%d safeMode=%d",
        luxelCount,
        static_cast<int>(faceGrids.size()),
        bvhRoot,
        dispatchConfig.luxelBatch,
        gpuSafeMode ? 1 : 0);
    std::fflush(stdout);

    session.program = program;
    session.luxelSsbo = luxelSsbo;
    session.gatherSsbo = gatherSsbo;
    session.shootSsbo = shootSsbo;
    session.faceSsbo = faceSsbo;
    session.nodeSsbo = nodeSsbo;
    session.primSsbo = primSsbo;
    session.paramsSsbo = paramsSsbo;
    session.faceTransparentSsbo = faceTransparentSsbo;
    session.faceOcclusionSsbo = faceOcclusionSsbo;
    session.materialRectSsbo = materialRectSsbo;
    session.faceCount = static_cast<int>(faceGrids.size());
    session.bvhRoot = bvhRoot;
    session.luxelBatch = dispatchConfig.luxelBatch;
    return session;
}

bool runBounceGpuPass(
    RadGpuBounceSession& session,
    std::vector<Vector3>& gatheredRgb,
    const std::vector<Vector3>& shootRgb,
    const RadGpuBounceParams& bounceParams) {
    if (session.luxelCount == 0) {
        gatheredRgb.clear();
        return true;
    }
    if (static_cast<int>(shootRgb.size()) != session.luxelCount) {
        TraceLog(LOG_WARNING, "sloprad: GPU bounce shoot buffer size mismatch");
        return false;
    }

    std::vector<GpuRgbSSBO> gpuShoot(shootRgb.size());
    for (std::size_t i = 0; i < shootRgb.size(); ++i) {
        gpuShoot[i].r = shootRgb[i].x;
        gpuShoot[i].g = shootRgb[i].y;
        gpuShoot[i].b = shootRgb[i].z;
    }
    // glBufferSubData onto an SSBO that was allocated empty (glBufferData with NULL data,
    // then glClearBufferData) silently loses the write on at least some drivers - the shoot
    // buffer would read back all-zero every bounce pass, forever, even though the upload
    // reported success. Recreating the buffer with the actual data each pass (mirroring how
    // every other SSBO here is allocated) avoids that path entirely.
    rlUnloadShaderBuffer(session.shootSsbo);
    session.shootSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuShoot.size() * sizeof(GpuRgbSSBO)),
        gpuShoot.data(),
        RL_DYNAMIC_COPY);
    memoryBarrierBits(kBufferUpdateBarrierBit | kShaderStorageBarrierBit);

    GpuBounceParamsSSBO initialParams{};
    initialParams.luxelCount = session.luxelCount;
    initialParams.faceCount = session.faceCount;
    initialParams.sampleCount = std::max(1, bounceParams.sampleCount);
    initialParams.bvhRoot = session.bvhRoot;
    initialParams.luxelOffset = 0;
    initialParams.luxelBatch = std::min(session.luxelBatch, session.luxelCount);
    initialParams.seed = bounceParams.seed;
    initialParams.rayMaxDistance = bounceParams.rayMaxDistance;
    initialParams.ambientR = bounceParams.ambientR;
    initialParams.ambientG = bounceParams.ambientG;
    initialParams.ambientB = bounceParams.ambientB;
    initialParams.materialRectCount =
        static_cast<std::int32_t>(session.occlusionResources->materialRects.size());
    initialParams.alphaAtlasWidth = session.occlusionResources->atlasWidth;
    initialParams.alphaAtlasHeight = session.occlusionResources->atlasHeight;

    GpuBatchPacer pacer(session.luxelBatch, bounceParams.gpuSafeMode, "bounce");

    TraceLog(
        LOG_INFO,
        "sloprad: GPU bounce lighting luxels=%d samples=%d bvhRoot=%d luxelBatch<=%d sync=%s(budget=%.0fms)",
        session.luxelCount,
        initialParams.sampleCount,
        session.bvhRoot,
        session.luxelBatch,
        pacer.mode(),
        pacer.budgetMs());
    std::fflush(stdout);

    rlEnableShader(session.program);
    rlBindShaderBuffer(session.luxelSsbo, 0);
    rlBindShaderBuffer(session.gatherSsbo, 1);
    rlBindShaderBuffer(session.shootSsbo, 2);
    rlBindShaderBuffer(session.faceSsbo, 3);
    rlBindShaderBuffer(session.nodeSsbo, 4);
    rlBindShaderBuffer(session.primSsbo, 5);
    rlBindShaderBuffer(session.paramsSsbo, 6);
    rlBindShaderBuffer(session.faceTransparentSsbo, 7);
    rlBindShaderBuffer(session.faceOcclusionSsbo, 8);
    rlBindShaderBuffer(session.materialRectSsbo, 9);
    bindAlphaAtlasForCompute(session.program, *session.occlusionResources);

    bool dispatchFailed = false;
    for (int luxelOffset = 0; luxelOffset < session.luxelCount && !dispatchFailed;) {
        const int luxelBatch = std::min(pacer.batch(), session.luxelCount - luxelOffset);
        GpuBounceParamsSSBO params = initialParams;
        params.luxelOffset = luxelOffset;
        params.luxelBatch = luxelBatch;
        rlEnableShader(session.program);
        rebindAlphaAtlasTexture(*session.occlusionResources);
        rlUpdateShaderBuffer(session.paramsSsbo, &params, sizeof(params), 0);
        memoryBarrierBits(kBufferUpdateBarrierBit | kShaderStorageBarrierBit);
        const unsigned int groups = static_cast<unsigned int>((params.luxelBatch + 63) / 64);
        pacer.beginGroup(luxelBatch);
        rlComputeShaderDispatch(groups, 1, 1);
        memoryBarrierBits(kShaderStorageBarrierBit);
        if (!pacer.endGroup("bounce compute dispatch", dispatchFailed)) {
            break;
        }
        luxelOffset += luxelBatch;
    }

    rlDisableShader();
    unbindAlphaAtlas();
    if (dispatchFailed) {
        return false;
    }

    std::vector<GpuRgbSSBO> gpuGather(static_cast<std::size_t>(session.luxelCount));
    rlReadShaderBuffer(
        session.gatherSsbo,
        gpuGather.data(),
        static_cast<unsigned int>(gpuGather.size() * sizeof(GpuRgbSSBO)),
        0);
    if (!checkGlError("bounce ssbo readback")) {
        return false;
    }

    gatheredRgb.resize(gpuGather.size());
    for (std::size_t i = 0; i < gpuGather.size(); ++i) {
        if (!std::isfinite(gpuGather[i].r) || !std::isfinite(gpuGather[i].g)
            || !std::isfinite(gpuGather[i].b)) {
            TraceLog(LOG_WARNING, "sloprad: GPU bounce produced non-finite luxel %zu", i);
            return false;
        }
        gatheredRgb[i] = {gpuGather[i].r, gpuGather[i].g, gpuGather[i].b};
    }

    TraceLog(LOG_INFO, "sloprad: GPU bounce lighting complete");
    std::fflush(stdout);
    return true;
}

void destroyBounceGpuSession(RadGpuBounceSession& session) {
    unloadBounceResources(
        session.luxelSsbo,
        session.gatherSsbo,
        session.shootSsbo,
        session.faceSsbo,
        session.nodeSsbo,
        session.primSsbo,
        session.paramsSsbo,
        session.faceTransparentSsbo,
        session.faceOcclusionSsbo,
        session.materialRectSsbo,
        session.program);
    session = RadGpuBounceSession{};
}

}
