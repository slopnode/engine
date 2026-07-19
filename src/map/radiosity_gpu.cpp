#include "map/radiosity_gpu.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace slopengine {

namespace {

constexpr unsigned int kShaderStorageBarrierBit = 0x00002000u;
constexpr unsigned int kBufferUpdateBarrierBit = 0x00000200u;
constexpr int kLuxelBatchSize = 1024;
constexpr int kEmitterBatchSize = 512;
constexpr int kDispatchesPerSync = 4;

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
    float pad = 0.0f;
};

struct GpuEmitterSSBO {
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    float area = 0.0f;
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
    std::int32_t faceIndex = -1;
    float rr = 0.0f;
    float rg = 0.0f;
    float rb = 0.0f;
    float pad = 0.0f;
};

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

struct GpuParamsSSBO {
    std::int32_t luxelCount = 0;
    std::int32_t emitterCount = 0;
    std::int32_t bvhRoot = -1;
    std::int32_t luxelOffset = 0;
    std::int32_t luxelBatch = 0;
    std::int32_t emitterOffset = 0;
    std::int32_t emitterBatch = 0;
    std::int32_t pad0 = 0;
    float directWrap = 0.35f;
    float coplanarFill = 0.15f;
    float coplanarSoft = 0.25f;
    float minDist2 = 0.0025f;
};

static_assert(sizeof(GpuLuxelSSBO) == 48);
static_assert(sizeof(GpuEmitterSSBO) == 48);
static_assert(sizeof(GpuBvhNodeSSBO) == 48);
static_assert(sizeof(GpuBvhPrimSSBO) == 64);
static_assert(sizeof(GpuParamsSSBO) == 48);

using MemoryBarrierFn = void (*)(unsigned int);
using FinishFn = void (*)();
using GetErrorFn = unsigned int (*)();

MemoryBarrierFn memoryBarrierFn() {
    static MemoryBarrierFn fn =
        reinterpret_cast<MemoryBarrierFn>(rlGetProcAddress("glMemoryBarrier"));
    return fn;
}

FinishFn finishFn() {
    static FinishFn fn = reinterpret_cast<FinishFn>(rlGetProcAddress("glFinish"));
    return fn;
}

GetErrorFn getErrorFn() {
    static GetErrorFn fn = reinterpret_cast<GetErrorFn>(rlGetProcAddress("glGetError"));
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

unsigned int loadComputeProgram(std::string_view source) {
    if (source.empty()) {
        return 0;
    }
    std::string code(source);
    const unsigned int shader = rlLoadShader(code.c_str(), RL_COMPUTE_SHADER);
    if (shader == 0) {
        TraceLog(LOG_WARNING, "sloprad: failed to compile direct compute shader");
        return 0;
    }
    const unsigned int program = rlLoadShaderProgramCompute(shader);
    if (program == 0) {
        TraceLog(LOG_WARNING, "sloprad: failed to link direct compute program");
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
    const std::vector<GpuLuxelSSBO>& after) {
    std::size_t nonFinite = 0;
    std::size_t improved = 0;
    float maxDelta = 0.0f;
    float maxAfterSum = 0.0f;

    for (std::size_t i = 0; i < after.size(); ++i) {
        const GpuLuxelSSBO& a = after[i];
        const GpuLuxelSSBO& b = before[i];
        if (!std::isfinite(a.ir) || !std::isfinite(a.ig) || !std::isfinite(a.ib)) {
            ++nonFinite;
            continue;
        }
        const float afterSum = a.ir + a.ig + a.ib;
        maxAfterSum = std::max(maxAfterSum, afterSum);
        const float beforeSum = b.ir + b.ig + b.ib;
        const float delta = afterSum - beforeSum;
        maxDelta = std::max(maxDelta, delta);
        if (delta > 1e-5f) {
            ++improved;
        }
    }

    TraceLog(
        LOG_INFO,
        "sloprad: GPU direct diagnostics luxels=%zu improved=%zu maxDelta=%.6f maxSum=%.6f",
        after.size(),
        improved,
        maxDelta,
        maxAfterSum);
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
    TraceLog(
        LOG_INFO,
        "sloprad: GPU direct lighting validated improved=%zu/%zu",
        improved,
        after.size());
    return true;
}

void unloadDirectResources(
    unsigned int luxelSsbo,
    unsigned int emitterSsbo,
    unsigned int nodeSsbo,
    unsigned int primSsbo,
    unsigned int paramsSsbo,
    unsigned int program) {
    unloadSsbo(luxelSsbo);
    unloadSsbo(emitterSsbo);
    unloadSsbo(nodeSsbo);
    unloadSsbo(primSsbo);
    unloadSsbo(paramsSsbo);
    rlUnloadShaderProgram(program);
}

} // namespace

bool radiosityGpuContextReady() {
    if (!IsWindowReady()) {
        return false;
    }
    if (rlGetVersion() != RL_OPENGL_43) {
        return false;
    }
    return true;
}

bool accumulateDirectLightingGpu(
    std::vector<RadGpuLuxel>& luxels,
    const std::vector<RadGpuEmitter>& emitters,
    const QuadBvh& occlusionBvh,
    std::string_view computeShaderSource,
    const RadGpuDirectParams& directParams) {
    if (!radiosityGpuContextReady()) {
        TraceLog(LOG_WARNING, "sloprad: GPU direct lighting unavailable (no GL 4.3 context)");
        return false;
    }
    if (luxels.empty()) {
        return true;
    }
    if (emitters.empty()) {
        TraceLog(LOG_INFO, "sloprad: GPU direct lighting skipped (no emitters)");
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
        dst.covered = 0;
        dst.nx = src.normal.x;
        dst.ny = src.normal.y;
        dst.nz = src.normal.z;
        dst.faceIndex = src.faceIndex;
        dst.ir = src.irradianceR;
        dst.ig = src.irradianceG;
        dst.ib = src.irradianceB;
    }
    const std::vector<GpuLuxelSSBO> gpuLuxelsBefore = gpuLuxels;

    std::vector<GpuEmitterSSBO> gpuEmitters(emitters.size());
    for (std::size_t i = 0; i < emitters.size(); ++i) {
        const RadGpuEmitter& src = emitters[i];
        GpuEmitterSSBO& dst = gpuEmitters[i];
        dst.px = src.position.x;
        dst.py = src.position.y;
        dst.pz = src.position.z;
        dst.area = src.area;
        dst.nx = src.normal.x;
        dst.ny = src.normal.y;
        dst.nz = src.normal.z;
        dst.faceIndex = src.faceIndex;
        dst.rr = src.radianceR;
        dst.rg = src.radianceG;
        dst.rb = src.radianceB;
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

    const unsigned int luxelSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuLuxels.size() * sizeof(GpuLuxelSSBO)),
        gpuLuxels.data(),
        RL_DYNAMIC_COPY);
    const unsigned int emitterSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuEmitters.size() * sizeof(GpuEmitterSSBO)),
        gpuEmitters.data(),
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
    const int emitterCount = static_cast<int>(gpuEmitters.size());

    GpuParamsSSBO initialParams{};
    initialParams.luxelCount = luxelCount;
    initialParams.emitterCount = emitterCount;
    initialParams.bvhRoot = bvhRoot;
    initialParams.luxelOffset = 0;
    initialParams.luxelBatch = std::min(kLuxelBatchSize, luxelCount);
    initialParams.emitterOffset = 0;
    initialParams.emitterBatch = std::min(kEmitterBatchSize, emitterCount);
    initialParams.directWrap = directParams.directWrap;
    initialParams.coplanarFill = directParams.coplanarFill;
    initialParams.coplanarSoft = directParams.coplanarSoft;
    initialParams.minDist2 = directParams.minDist2;
    const unsigned int paramsSsbo =
        rlLoadShaderBuffer(sizeof(GpuParamsSSBO), &initialParams, RL_DYNAMIC_COPY);

    if (luxelSsbo == 0 || emitterSsbo == 0 || nodeSsbo == 0 || primSsbo == 0 || paramsSsbo == 0) {
        TraceLog(LOG_WARNING, "sloprad: failed to allocate GPU SSBOs for direct lighting");
        unloadDirectResources(luxelSsbo, emitterSsbo, nodeSsbo, primSsbo, paramsSsbo, program);
        return false;
    }
    if (!checkGlError("ssbo allocate")) {
        unloadDirectResources(luxelSsbo, emitterSsbo, nodeSsbo, primSsbo, paramsSsbo, program);
        return false;
    }

    TraceLog(
        LOG_INFO,
        "sloprad: GPU direct lighting luxels=%d emitters=%d luxelBatch=%d emitterBatch=%d syncEvery=%d bvhRoot=%d",
        luxelCount,
        emitterCount,
        kLuxelBatchSize,
        kEmitterBatchSize,
        kDispatchesPerSync,
        bvhRoot);
    std::fflush(stdout);

    rlEnableShader(program);
    rlBindShaderBuffer(luxelSsbo, 0);
    rlBindShaderBuffer(emitterSsbo, 1);
    rlBindShaderBuffer(nodeSsbo, 2);
    rlBindShaderBuffer(primSsbo, 3);
    rlBindShaderBuffer(paramsSsbo, 4);

    bool dispatchFailed = false;
    int dispatchesSinceSync = 0;
    int lastLoggedLuxels = -1;
    for (int luxelOffset = 0; luxelOffset < luxelCount && !dispatchFailed; luxelOffset += kLuxelBatchSize) {
        const int luxelBatch = std::min(kLuxelBatchSize, luxelCount - luxelOffset);
        for (int emitterOffset = 0; emitterOffset < emitterCount; emitterOffset += kEmitterBatchSize) {
            GpuParamsSSBO params{};
            params.luxelCount = luxelCount;
            params.emitterCount = emitterCount;
            params.bvhRoot = bvhRoot;
            params.luxelOffset = luxelOffset;
            params.luxelBatch = luxelBatch;
            params.emitterOffset = emitterOffset;
            params.emitterBatch = std::min(kEmitterBatchSize, emitterCount - emitterOffset);
            params.directWrap = directParams.directWrap;
            params.coplanarFill = directParams.coplanarFill;
            params.coplanarSoft = directParams.coplanarSoft;
            params.minDist2 = directParams.minDist2;

            rlUpdateShaderBuffer(paramsSsbo, &params, sizeof(params), 0);
            memoryBarrierBits(kBufferUpdateBarrierBit | kShaderStorageBarrierBit);
            const unsigned int groups =
                static_cast<unsigned int>((params.luxelBatch + 63) / 64);
            rlComputeShaderDispatch(groups, 1, 1);
            memoryBarrierBits(kShaderStorageBarrierBit);

            ++dispatchesSinceSync;
            if (dispatchesSinceSync >= kDispatchesPerSync) {
                finishGpu();
                dispatchesSinceSync = 0;
                if (!checkGlError("compute dispatch")) {
                    dispatchFailed = true;
                    break;
                }
            }
        }

        const int luxelsDone = std::min(luxelOffset + luxelBatch, luxelCount);
        if (!dispatchFailed
            && (luxelsDone == luxelCount || luxelsDone / (kLuxelBatchSize * 4) != lastLoggedLuxels)) {
            lastLoggedLuxels = luxelsDone / (kLuxelBatchSize * 4);
            TraceLog(
                LOG_INFO,
                "sloprad: GPU direct %d/%d (%.0f%%)",
                luxelsDone,
                luxelCount,
                100.0 * static_cast<double>(luxelsDone) / static_cast<double>(luxelCount));
            std::fflush(stdout);
        }
    }

    if (!dispatchFailed && dispatchesSinceSync > 0) {
        finishGpu();
        if (!checkGlError("compute dispatch")) {
            dispatchFailed = true;
        }
    }

    rlDisableShader();
    if (dispatchFailed) {
        unloadDirectResources(luxelSsbo, emitterSsbo, nodeSsbo, primSsbo, paramsSsbo, program);
        return false;
    }

    rlReadShaderBuffer(
        luxelSsbo,
        gpuLuxels.data(),
        static_cast<unsigned int>(gpuLuxels.size() * sizeof(GpuLuxelSSBO)),
        0);
    if (!checkGlError("ssbo readback")) {
        unloadDirectResources(luxelSsbo, emitterSsbo, nodeSsbo, primSsbo, paramsSsbo, program);
        return false;
    }

    unloadDirectResources(luxelSsbo, emitterSsbo, nodeSsbo, primSsbo, paramsSsbo, program);

    if (!validateGpuResults(gpuLuxelsBefore, gpuLuxels)) {
        return false;
    }

    for (std::size_t i = 0; i < luxels.size(); ++i) {
        luxels[i].irradianceR = gpuLuxels[i].ir;
        luxels[i].irradianceG = gpuLuxels[i].ig;
        luxels[i].irradianceB = gpuLuxels[i].ib;
    }

    TraceLog(LOG_INFO, "sloprad: GPU direct lighting complete");
    std::fflush(stdout);
    return true;
}

}
