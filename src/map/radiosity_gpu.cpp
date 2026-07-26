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
constexpr int kLightBatchSize = 512;
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
    std::int32_t leafIndex = -1;
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
    std::int32_t leafIndex = -1;
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
    std::int32_t lightCount = 0;
    std::int32_t bvhRoot = -1;
    std::int32_t luxelOffset = 0;
    std::int32_t luxelBatch = 0;
    std::int32_t emitterOffset = 0;
    std::int32_t emitterBatch = 0;
    std::int32_t lightOffset = 0;
    std::int32_t lightBatch = 0;
    std::int32_t leafCount = 0;
    std::int32_t wordsPerRow = 0;
    float directWrap = 0.35f;
    float coplanarFill = 0.15f;
    float coplanarSoft = 0.25f;
    float minDist2 = 0.0025f;
};

static_assert(sizeof(GpuLuxelSSBO) == 48);
static_assert(sizeof(GpuEmitterSSBO) == 48);
static_assert(sizeof(GpuLightSSBO) == 64);
static_assert(sizeof(GpuBvhNodeSSBO) == 48);
static_assert(sizeof(GpuBvhPrimSSBO) == 64);
static_assert(sizeof(GpuParamsSSBO) == 64);

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
    unsigned int lightSsbo,
    unsigned int nodeSsbo,
    unsigned int primSsbo,
    unsigned int paramsSsbo,
    unsigned int reachSsbo,
    unsigned int faceSkySsbo,
    unsigned int program) {
    unloadSsbo(luxelSsbo);
    unloadSsbo(emitterSsbo);
    unloadSsbo(lightSsbo);
    unloadSsbo(nodeSsbo);
    unloadSsbo(primSsbo);
    unloadSsbo(paramsSsbo);
    unloadSsbo(reachSsbo);
    unloadSsbo(faceSkySsbo);
    rlUnloadShaderProgram(program);
}

void fillBaseParams(
    GpuParamsSSBO& params,
    int luxelCount,
    int emitterCount,
    int lightCount,
    std::int32_t bvhRoot,
    int luxelOffset,
    int luxelBatch,
    const RadGpuDirectParams& directParams,
    const RadGpuReachability& reachability) {
    params.luxelCount = luxelCount;
    params.emitterCount = emitterCount;
    params.lightCount = lightCount;
    params.bvhRoot = bvhRoot;
    params.luxelOffset = luxelOffset;
    params.luxelBatch = luxelBatch;
    params.emitterOffset = 0;
    params.emitterBatch = 0;
    params.lightOffset = 0;
    params.lightBatch = 0;
    params.leafCount = reachability.leafCount;
    params.wordsPerRow = reachability.wordsPerRow;
    params.directWrap = directParams.directWrap;
    params.coplanarFill = directParams.coplanarFill;
    params.coplanarSoft = directParams.coplanarSoft;
    params.minDist2 = directParams.minDist2;
}

bool dispatchBatch(
    unsigned int paramsSsbo,
    const GpuParamsSSBO& params,
    int& dispatchesSinceSync,
    bool& dispatchFailed) {
    rlUpdateShaderBuffer(paramsSsbo, &params, sizeof(params), 0);
    memoryBarrierBits(kBufferUpdateBarrierBit | kShaderStorageBarrierBit);
    const unsigned int groups = static_cast<unsigned int>((params.luxelBatch + 63) / 64);
    rlComputeShaderDispatch(groups, 1, 1);
    memoryBarrierBits(kShaderStorageBarrierBit);

    ++dispatchesSinceSync;
    if (dispatchesSinceSync >= kDispatchesPerSync) {
        finishGpu();
        dispatchesSinceSync = 0;
        if (!checkGlError("compute dispatch")) {
            dispatchFailed = true;
            return false;
        }
    }
    return true;
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
    const std::vector<RadGpuLight>& lights,
    const QuadBvh& occlusionBvh,
    std::string_view computeShaderSource,
    const RadGpuDirectParams& directParams,
    const RadGpuReachability& reachability,
    const std::vector<std::int32_t>& faceIsSky) {
    if (!radiosityGpuContextReady()) {
        TraceLog(LOG_WARNING, "sloprad: GPU direct lighting unavailable (no GL 4.3 context)");
        return false;
    }
    if (luxels.empty()) {
        return true;
    }
    if (emitters.empty() && lights.empty()) {
        TraceLog(LOG_INFO, "sloprad: GPU direct lighting skipped (no emitters or lights)");
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

    std::vector<GpuEmitterSSBO> gpuEmitters(std::max<std::size_t>(emitters.size(), 1));
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
        dst.leafIndex = src.interiorLeaf;
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

    std::vector<std::uint32_t> reachBits = reachability.bits;
    if (reachBits.empty()) {
        reachBits.push_back(0);
    }
    std::vector<std::int32_t> faceSkyBits = faceIsSky;
    if (faceSkyBits.empty()) {
        faceSkyBits.push_back(0);
    }

    const unsigned int luxelSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuLuxels.size() * sizeof(GpuLuxelSSBO)),
        gpuLuxels.data(),
        RL_DYNAMIC_COPY);
    const unsigned int emitterSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuEmitters.size() * sizeof(GpuEmitterSSBO)),
        gpuEmitters.data(),
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
    const unsigned int reachSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(reachBits.size() * sizeof(std::uint32_t)),
        reachBits.data(),
        RL_DYNAMIC_COPY);
    const unsigned int faceSkySsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(faceSkyBits.size() * sizeof(std::int32_t)),
        faceSkyBits.data(),
        RL_DYNAMIC_COPY);
    const int luxelCount = static_cast<int>(gpuLuxels.size());
    const int emitterCount = static_cast<int>(emitters.size());
    const int lightCount = static_cast<int>(lights.size());

    GpuParamsSSBO initialParams{};
    fillBaseParams(
        initialParams,
        luxelCount,
        emitterCount,
        lightCount,
        bvhRoot,
        0,
        std::min(kLuxelBatchSize, luxelCount),
        directParams,
        reachability);
    const unsigned int paramsSsbo =
        rlLoadShaderBuffer(sizeof(GpuParamsSSBO), &initialParams, RL_DYNAMIC_COPY);

    if (luxelSsbo == 0 || emitterSsbo == 0 || lightSsbo == 0 || nodeSsbo == 0 || primSsbo == 0
        || paramsSsbo == 0 || reachSsbo == 0 || faceSkySsbo == 0) {
        TraceLog(LOG_WARNING, "sloprad: failed to allocate GPU SSBOs for direct lighting");
        unloadDirectResources(
            luxelSsbo,
            emitterSsbo,
            lightSsbo,
            nodeSsbo,
            primSsbo,
            paramsSsbo,
            reachSsbo,
            faceSkySsbo,
            program);
        return false;
    }
    if (!checkGlError("ssbo allocate")) {
        unloadDirectResources(
            luxelSsbo,
            emitterSsbo,
            lightSsbo,
            nodeSsbo,
            primSsbo,
            paramsSsbo,
            reachSsbo,
            faceSkySsbo,
            program);
        return false;
    }

    TraceLog(
        LOG_INFO,
        "sloprad: GPU direct lighting luxels=%d emitters=%d lights=%d luxelBatch=%d emitterBatch=%d lightBatch=%d syncEvery=%d bvhRoot=%d leafCull=%d",
        luxelCount,
        emitterCount,
        lightCount,
        kLuxelBatchSize,
        kEmitterBatchSize,
        kLightBatchSize,
        kDispatchesPerSync,
        bvhRoot,
        reachability.leafCount > 0 ? 1 : 0);
    std::fflush(stdout);

    rlEnableShader(program);
    rlBindShaderBuffer(luxelSsbo, 0);
    rlBindShaderBuffer(emitterSsbo, 1);
    rlBindShaderBuffer(nodeSsbo, 2);
    rlBindShaderBuffer(primSsbo, 3);
    rlBindShaderBuffer(paramsSsbo, 4);
    rlBindShaderBuffer(lightSsbo, 5);
    rlBindShaderBuffer(reachSsbo, 6);
    rlBindShaderBuffer(faceSkySsbo, 7);

    bool dispatchFailed = false;
    int dispatchesSinceSync = 0;
    int lastLoggedLuxels = -1;
    for (int luxelOffset = 0; luxelOffset < luxelCount && !dispatchFailed; luxelOffset += kLuxelBatchSize) {
        const int luxelBatch = std::min(kLuxelBatchSize, luxelCount - luxelOffset);

        for (int emitterOffset = 0; emitterOffset < emitterCount && !dispatchFailed;
             emitterOffset += kEmitterBatchSize) {
            GpuParamsSSBO params{};
            fillBaseParams(
                params,
                luxelCount,
                emitterCount,
                lightCount,
                bvhRoot,
                luxelOffset,
                luxelBatch,
                directParams,
                reachability);
            params.emitterOffset = emitterOffset;
            params.emitterBatch = std::min(kEmitterBatchSize, emitterCount - emitterOffset);
            if (!dispatchBatch(paramsSsbo, params, dispatchesSinceSync, dispatchFailed)) {
                break;
            }
        }

        for (int lightOffset = 0; lightOffset < lightCount && !dispatchFailed;
             lightOffset += kLightBatchSize) {
            GpuParamsSSBO params{};
            fillBaseParams(
                params,
                luxelCount,
                emitterCount,
                lightCount,
                bvhRoot,
                luxelOffset,
                luxelBatch,
                directParams,
                reachability);
            params.lightOffset = lightOffset;
            params.lightBatch = std::min(kLightBatchSize, lightCount - lightOffset);
            if (!dispatchBatch(paramsSsbo, params, dispatchesSinceSync, dispatchFailed)) {
                break;
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
        unloadDirectResources(
            luxelSsbo,
            emitterSsbo,
            lightSsbo,
            nodeSsbo,
            primSsbo,
            paramsSsbo,
            reachSsbo,
            faceSkySsbo,
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
            emitterSsbo,
            lightSsbo,
            nodeSsbo,
            primSsbo,
            paramsSsbo,
            reachSsbo,
            faceSkySsbo,
            program);
        return false;
    }

    unloadDirectResources(
        luxelSsbo,
        emitterSsbo,
        lightSsbo,
        nodeSsbo,
        primSsbo,
        paramsSsbo,
        reachSsbo,
        faceSkySsbo,
        program);

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
    float pad = 0.0f;
};

static_assert(sizeof(GpuBounceLuxelSSBO) == 48);
static_assert(sizeof(GpuRgbSSBO) == 16);
static_assert(sizeof(GpuFaceGridSSBO) == 64);
static_assert(sizeof(GpuBounceParamsSSBO) == 48);

void unloadBounceResources(
    unsigned int luxelSsbo,
    unsigned int gatherSsbo,
    unsigned int shootSsbo,
    unsigned int faceSsbo,
    unsigned int nodeSsbo,
    unsigned int primSsbo,
    unsigned int paramsSsbo,
    unsigned int program) {
    unloadSsbo(luxelSsbo);
    unloadSsbo(gatherSsbo);
    unloadSsbo(shootSsbo);
    unloadSsbo(faceSsbo);
    unloadSsbo(nodeSsbo);
    unloadSsbo(primSsbo);
    unloadSsbo(paramsSsbo);
    if (program != 0) {
        rlUnloadShaderProgram(program);
    }
}

} // namespace

bool accumulateBounceLightingGpu(
    std::vector<RadGpuBounceLuxel>& luxels,
    std::vector<Vector3>& gatheredRgb,
    const std::vector<Vector3>& shootRgb,
    const std::vector<RadGpuFaceGrid>& faceGrids,
    const QuadBvh& sceneBvh,
    std::string_view computeShaderSource,
    const RadGpuBounceParams& bounceParams) {
    if (!radiosityGpuContextReady()) {
        TraceLog(LOG_WARNING, "sloprad: GPU bounce lighting unavailable (no GL 4.3 context)");
        return false;
    }
    if (luxels.empty()) {
        return true;
    }
    if (shootRgb.size() != luxels.size()) {
        TraceLog(LOG_WARNING, "sloprad: GPU bounce shoot buffer size mismatch");
        return false;
    }

    const unsigned int program = loadComputeProgram(computeShaderSource);
    if (program == 0) {
        return false;
    }

    std::vector<GpuBounceLuxelSSBO> gpuLuxels(luxels.size());
    std::vector<GpuRgbSSBO> gpuGather(luxels.size());
    std::vector<GpuRgbSSBO> gpuShoot(shootRgb.size());
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
        gpuShoot[i].r = shootRgb[i].x;
        gpuShoot[i].g = shootRgb[i].y;
        gpuShoot[i].b = shootRgb[i].z;
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
    const unsigned int shootSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(gpuShoot.size() * sizeof(GpuRgbSSBO)),
        gpuShoot.data(),
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
    GpuBounceParamsSSBO initialParams{};
    initialParams.luxelCount = luxelCount;
    initialParams.faceCount = static_cast<std::int32_t>(faceGrids.size());
    initialParams.sampleCount = std::max(1, bounceParams.sampleCount);
    initialParams.bvhRoot = bvhRoot;
    initialParams.luxelOffset = 0;
    initialParams.luxelBatch = std::min(kLuxelBatchSize, luxelCount);
    initialParams.seed = bounceParams.seed;
    initialParams.rayMaxDistance = bounceParams.rayMaxDistance;
    initialParams.ambientR = bounceParams.ambientR;
    initialParams.ambientG = bounceParams.ambientG;
    initialParams.ambientB = bounceParams.ambientB;
    const unsigned int paramsSsbo =
        rlLoadShaderBuffer(sizeof(GpuBounceParamsSSBO), &initialParams, RL_DYNAMIC_COPY);

    if (luxelSsbo == 0 || gatherSsbo == 0 || shootSsbo == 0 || faceSsbo == 0 || nodeSsbo == 0
        || primSsbo == 0 || paramsSsbo == 0) {
        TraceLog(LOG_WARNING, "sloprad: failed to allocate GPU SSBOs for bounce lighting");
        unloadBounceResources(
            luxelSsbo, gatherSsbo, shootSsbo, faceSsbo, nodeSsbo, primSsbo, paramsSsbo, program);
        return false;
    }
    if (!checkGlError("bounce ssbo allocate")) {
        unloadBounceResources(
            luxelSsbo, gatherSsbo, shootSsbo, faceSsbo, nodeSsbo, primSsbo, paramsSsbo, program);
        return false;
    }

    TraceLog(
        LOG_INFO,
        "sloprad: GPU bounce lighting luxels=%d samples=%d bvhRoot=%d",
        luxelCount,
        initialParams.sampleCount,
        bvhRoot);
    std::fflush(stdout);

    rlEnableShader(program);
    rlBindShaderBuffer(luxelSsbo, 0);
    rlBindShaderBuffer(gatherSsbo, 1);
    rlBindShaderBuffer(shootSsbo, 2);
    rlBindShaderBuffer(faceSsbo, 3);
    rlBindShaderBuffer(nodeSsbo, 4);
    rlBindShaderBuffer(primSsbo, 5);
    rlBindShaderBuffer(paramsSsbo, 6);

    bool dispatchFailed = false;
    int dispatchesSinceSync = 0;
    for (int luxelOffset = 0; luxelOffset < luxelCount && !dispatchFailed;
         luxelOffset += kLuxelBatchSize) {
        GpuBounceParamsSSBO params = initialParams;
        params.luxelOffset = luxelOffset;
        params.luxelBatch = std::min(kLuxelBatchSize, luxelCount - luxelOffset);
        rlUpdateShaderBuffer(paramsSsbo, &params, sizeof(params), 0);
        memoryBarrierBits(kBufferUpdateBarrierBit | kShaderStorageBarrierBit);
        const unsigned int groups = static_cast<unsigned int>((params.luxelBatch + 63) / 64);
        rlComputeShaderDispatch(groups, 1, 1);
        memoryBarrierBits(kShaderStorageBarrierBit);
        ++dispatchesSinceSync;
        if (dispatchesSinceSync >= kDispatchesPerSync) {
            finishGpu();
            dispatchesSinceSync = 0;
            if (!checkGlError("bounce compute dispatch")) {
                dispatchFailed = true;
            }
        }
    }

    if (!dispatchFailed && dispatchesSinceSync > 0) {
        finishGpu();
        if (!checkGlError("bounce compute dispatch")) {
            dispatchFailed = true;
        }
    }

    rlDisableShader();
    if (dispatchFailed) {
        unloadBounceResources(
            luxelSsbo, gatherSsbo, shootSsbo, faceSsbo, nodeSsbo, primSsbo, paramsSsbo, program);
        return false;
    }

    rlReadShaderBuffer(
        gatherSsbo,
        gpuGather.data(),
        static_cast<unsigned int>(gpuGather.size() * sizeof(GpuRgbSSBO)),
        0);
    if (!checkGlError("bounce ssbo readback")) {
        unloadBounceResources(
            luxelSsbo, gatherSsbo, shootSsbo, faceSsbo, nodeSsbo, primSsbo, paramsSsbo, program);
        return false;
    }

    unloadBounceResources(
        luxelSsbo, gatherSsbo, shootSsbo, faceSsbo, nodeSsbo, primSsbo, paramsSsbo, program);

    gatheredRgb.resize(luxels.size());
    for (std::size_t i = 0; i < luxels.size(); ++i) {
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

}
