#ifdef SLOPENGINE_HAS_STEAM_AUDIO

#include "audio/steam_audio_runtime.hpp"

#include "audio/steam_audio_scene.hpp"
#include "audio/steam_audio_spatialize_filter.hpp"

#include <raylib.h>

#include <cmath>

namespace slopengine {

namespace {

constexpr int kMaxNumRays = 1024;
constexpr int kNumDiffuseSamples = 32;
constexpr int kNumBounces = 8;
constexpr float kIrradianceMinDistance = 1.0f;

float length3(float x, float y, float z) {
    return std::sqrt(x * x + y * y + z * z);
}

void normalize3(float& x, float& y, float& z) {
    const float len = length3(x, y, z);
    if (len <= 1.0e-8f) {
        x = 0.0f;
        y = 0.0f;
        z = 1.0f;
        return;
    }
    x /= len;
    y /= len;
    z /= len;
}

void cross3(
    float ax, float ay, float az,
    float bx, float by, float bz,
    float& ox, float& oy, float& oz) {
    ox = ay * bz - az * by;
    oy = az * bx - ax * bz;
    oz = ax * by - ay * bx;
}

}

SteamAudioRuntime::SteamAudioRuntime() = default;

SteamAudioRuntime::~SteamAudioRuntime() {
    shutdown();
}

bool SteamAudioRuntime::init(int samplingRate, int frameSize) {
    if (ready_) {
        return true;
    }

    IPLContextSettings contextSettings{};
    contextSettings.version = STEAMAUDIO_VERSION;
    if (iplContextCreate(&contextSettings, &context_) != IPL_STATUS_SUCCESS) {
        TraceLog(LOG_ERROR, "STEAM AUDIO: iplContextCreate failed");
        return false;
    }

    audioSettings_.samplingRate = samplingRate > 0 ? samplingRate : 44100;
    (void)frameSize;
    audioSettings_.frameSize = 512;

    IPLHRTFSettings hrtfSettings{};
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1.0f;
    if (iplHRTFCreate(context_, &audioSettings_, &hrtfSettings, &hrtf_) != IPL_STATUS_SUCCESS) {
        TraceLog(LOG_ERROR, "STEAM AUDIO: iplHRTFCreate failed");
        shutdown();
        return false;
    }

    IPLSimulationSettings simulationSettings{};
    simulationSettings.flags = static_cast<IPLSimulationFlags>(
        IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
    simulationSettings.sceneType = IPL_SCENETYPE_DEFAULT;
    simulationSettings.reflectionType = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
    simulationSettings.maxNumOcclusionSamples = 16;
    simulationSettings.maxNumRays = kMaxNumRays;
    simulationSettings.numDiffuseSamples = kNumDiffuseSamples;
    simulationSettings.maxDuration = kMaxReflectionDuration;
    simulationSettings.maxOrder = kReflectionOrder;
    simulationSettings.maxNumSources = kMaxSlots;
    simulationSettings.numThreads = 2;
    simulationSettings.rayBatchSize = 16;
    simulationSettings.numVisSamples = 4;
    simulationSettings.samplingRate = audioSettings_.samplingRate;
    simulationSettings.frameSize = audioSettings_.frameSize;
    if (iplSimulatorCreate(context_, &simulationSettings, &simulator_) != IPL_STATUS_SUCCESS) {
        TraceLog(LOG_ERROR, "STEAM AUDIO: iplSimulatorCreate failed");
        shutdown();
        return false;
    }

    IPLSourceSettings reverbSettings{};
    reverbSettings.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
    if (iplSourceCreate(simulator_, &reverbSettings, &reverbSource_) != IPL_STATUS_SUCCESS) {
        TraceLog(LOG_ERROR, "STEAM AUDIO: iplSourceCreate failed for the listener reverb");
        shutdown();
        return false;
    }
    iplSourceAdd(reverbSource_, simulator_);
    iplSimulatorCommit(simulator_);

    spatializeFilter_ = std::make_unique<SteamAudioSpatializeFilter>(this);
    ready_ = true;
    TraceLog(
        LOG_DEBUG,
        "STEAM AUDIO: ready (rate=%d frame=%d reflections=parametric)",
        audioSettings_.samplingRate,
        audioSettings_.frameSize);
    return true;
}

void SteamAudioRuntime::shutdown() {
    clearScene();

    {
        std::lock_guard<std::mutex> lock(playMutex_);
        for (int i = 0; i < kMaxSlots; ++i) {
            freeSlotUnlocked(i);
        }
    }

    spatializeFilter_.reset();

    if (reverbSource_ != nullptr) {
        iplSourceRemove(reverbSource_, simulator_);
        iplSimulatorCommit(simulator_);
        iplSourceRelease(&reverbSource_);
    }

    if (simulator_ != nullptr) {
        iplSimulatorRelease(&simulator_);
    }
    if (hrtf_ != nullptr) {
        iplHRTFRelease(&hrtf_);
    }
    if (context_ != nullptr) {
        iplContextRelease(&context_);
    }

    ready_ = false;
}

SteamAudioSpatializeFilter& SteamAudioRuntime::spatializeFilter() {
    return *spatializeFilter_;
}

bool SteamAudioRuntime::setSceneFromBrushes(
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* excludeBrushIds,
    AssetStore& assets) {
    if (!ready_) {
        return false;
    }
    buildSteamAudioMeshFromBrushes(brushes, excludeBrushIds, assets, debugMesh_);
    const bool ok = createSteamAudioScene(
        context_, simulator_, brushes, excludeBrushIds, assets, &scene_, &staticMesh_);
    sceneReady_ = ok;
    reflectionTimer_ = kReflectionInterval;
    if (ok) {
        TraceLog(LOG_DEBUG, "STEAM AUDIO: scene built from brushes");
    } else {
        TraceLog(LOG_WARNING, "STEAM AUDIO: failed to build scene from brushes");
    }
    return ok;
}

void SteamAudioRuntime::clearScene() {
    if (!ready_ && scene_ == nullptr && staticMesh_ == nullptr) {
        return;
    }
    destroySteamAudioScene(simulator_, &scene_, &staticMesh_);
    sceneReady_ = false;
    debugMesh_ = SteamAudioSceneMesh{};
}

int SteamAudioRuntime::trackVoice(
    SoLoud::handle voice,
    float x,
    float y,
    float z,
    float minDistance,
    float maxDistance,
    float baseVolume) {
    if (voice == 0 || !ready_) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(playMutex_);
    const int slot = allocSlotUnlocked();
    if (slot < 0) {
        return -1;
    }

    Slot& s = slots_[static_cast<std::size_t>(slot)];
    s.voice = voice;
    s.x = x;
    s.y = y;
    s.z = z;
    s.minDistance = minDistance;
    s.maxDistance = maxDistance;
    s.baseVolume = baseVolume;

    IPLSourceSettings sourceSettings{};
    sourceSettings.flags = IPL_SIMULATIONFLAGS_DIRECT;
    if (iplSourceCreate(simulator_, &sourceSettings, &s.source) != IPL_STATUS_SUCCESS) {
        TraceLog(LOG_WARNING, "STEAM AUDIO: iplSourceCreate failed");
        freeSlotUnlocked(slot);
        return -1;
    }
    iplSourceAdd(s.source, simulator_);
    iplSimulatorCommit(simulator_);

    s.smoothedOcclusion = 1.0f;
    s.smoothedTransmission[0] = 1.0f;
    s.smoothedTransmission[1] = 1.0f;
    s.smoothedTransmission[2] = 1.0f;
    s.smoothedDistanceAttenuation = 1.0f;
    s.smoothedReverbTimes[0] = 0.0f;
    s.smoothedReverbTimes[1] = 0.0f;
    s.smoothedReverbTimes[2] = 0.0f;
    s.smoothingInit = false;

    SteamAudioSlotRenderParams seeded{};
    seeded.occlusion = 1.0f;
    seeded.transmission[0] = 1.0f;
    seeded.transmission[1] = 1.0f;
    seeded.transmission[2] = 1.0f;
    seeded.distanceAttenuation = 1.0f;
    seeded.active = true;
    {
        std::lock_guard<std::mutex> renderLock(renderMutex_);
        renderParams_[static_cast<std::size_t>(slot)] = seeded;
    }
    return slot;
}

void SteamAudioRuntime::releaseVoice(SoLoud::handle voice) {
    if (voice == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(playMutex_);
    for (int i = 0; i < kMaxSlots; ++i) {
        if (slots_[static_cast<std::size_t>(i)].used && slots_[static_cast<std::size_t>(i)].voice == voice) {
            freeSlotUnlocked(i);
            break;
        }
    }
}

void SteamAudioRuntime::pruneFinishedVoices(SoLoud::Soloud& soloud) {
    std::lock_guard<std::mutex> lock(playMutex_);
    for (int i = 0; i < kMaxSlots; ++i) {
        Slot& slot = slots_[static_cast<std::size_t>(i)];
        if (slot.used && slot.voice != 0 && !soloud.isValidVoiceHandle(slot.voice)) {
            freeSlotUnlocked(i);
        }
    }
}

void SteamAudioRuntime::setVoicePosition(SoLoud::handle voice, float x, float y, float z) {
    if (voice == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(playMutex_);
    for (int i = 0; i < kMaxSlots; ++i) {
        Slot& slot = slots_[static_cast<std::size_t>(i)];
        if (slot.used && slot.voice == voice) {
            slot.x = x;
            slot.y = y;
            slot.z = z;
            break;
        }
    }
}

void SteamAudioRuntime::update(
    const SteamAudioListenerPose& listener,
    const std::vector<SteamAudioSourcePose>& sources) {
    if (!ready_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    float dt = 0.0f;
    if (hasLastUpdateTime_) {
        dt = std::chrono::duration<float>(now - lastUpdateTime_).count();
    }
    lastUpdateTime_ = now;
    hasLastUpdateTime_ = true;
    // Stalls (scene load, breakpoint, frame hitch) or the very first update should snap
    // straight to target values rather than slowly fading from stale defaults.
    const bool snap = dt <= 0.0f || dt > 0.5f;
    const float directAlpha = snap ? 1.0f : 1.0f - std::exp(-dt / kDirectSmoothingTime);
    const float reverbAlpha = snap ? 1.0f : 1.0f - std::exp(-dt / kReverbSmoothingTime);

    std::lock_guard<std::mutex> playLock(playMutex_);

    for (const SteamAudioSourcePose& src : sources) {
        for (int i = 0; i < kMaxSlots; ++i) {
            Slot& slot = slots_[static_cast<std::size_t>(i)];
            if (!slot.used || slot.voice != src.voice) {
                continue;
            }
            slot.x = src.x;
            slot.y = src.y;
            slot.z = src.z;
            slot.minDistance = src.minDistance;
            slot.maxDistance = src.maxDistance;
            break;
        }
    }

    const IPLCoordinateSpace3 listenerCoords = makeCoords(
        listener.posX,
        listener.posY,
        listener.posZ,
        listener.aheadX,
        listener.aheadY,
        listener.aheadZ,
        listener.upX,
        listener.upY,
        listener.upZ);

    IPLSimulationSharedInputs sharedInputs{};
    sharedInputs.listener = listenerCoords;
    sharedInputs.numRays = kMaxNumRays;
    sharedInputs.numBounces = kNumBounces;
    sharedInputs.duration = kMaxReflectionDuration;
    sharedInputs.order = kReflectionOrder;
    sharedInputs.irradianceMinDistance = kIrradianceMinDistance;
    iplSimulatorSetSharedInputs(
        simulator_,
        static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS),
        &sharedInputs);

    for (int i = 0; i < kMaxSlots; ++i) {
        Slot& slot = slots_[static_cast<std::size_t>(i)];
        if (!slot.used || slot.source == nullptr) {
            continue;
        }

        IPLSimulationInputs inputs{};
        inputs.flags = IPL_SIMULATIONFLAGS_DIRECT;
        inputs.directFlags = static_cast<IPLDirectSimulationFlags>(
            IPL_DIRECTSIMULATIONFLAGS_OCCLUSION | IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION);
        inputs.source = makeCoords(slot.x, slot.y, slot.z, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);
        inputs.occlusionType = IPL_OCCLUSIONTYPE_RAYCAST;
        inputs.numTransmissionRays = 1;
        inputs.baked = IPL_FALSE;
        iplSourceSetInputs(slot.source, IPL_SIMULATIONFLAGS_DIRECT, &inputs);
    }

    iplSimulatorRunDirect(simulator_);
    reflectionTimer_ += dt;
    if (sceneReady_ && reverbSource_ != nullptr && (snap || reflectionTimer_ >= kReflectionInterval)) {
        reflectionTimer_ = 0.0f;
        IPLSimulationInputs reverbInputs{};
        reverbInputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
        reverbInputs.source = listenerCoords;
        reverbInputs.reverbScale[0] = 1.0f;
        reverbInputs.reverbScale[1] = 1.0f;
        reverbInputs.reverbScale[2] = 1.0f;
        reverbInputs.hybridReverbTransitionTime = 0.0f;
        reverbInputs.hybridReverbOverlapPercent = 0.0f;
        reverbInputs.baked = IPL_FALSE;
        iplSourceSetInputs(reverbSource_, IPL_SIMULATIONFLAGS_REFLECTIONS, &reverbInputs);
        iplSimulatorRunReflections(simulator_);
        IPLSimulationOutputs reverbOutputs{};
        iplSourceGetOutputs(reverbSource_, IPL_SIMULATIONFLAGS_REFLECTIONS, &reverbOutputs);
        for (int band = 0; band < 3; ++band) {
            reverbTimes_[band] = reverbOutputs.reflections.reverbTimes[band];
        }
    }

    const IPLVector3 listenerPos{listener.posX, listener.posY, listener.posZ};
    const IPLVector3 listenerAhead{listener.aheadX, listener.aheadY, listener.aheadZ};
    const IPLVector3 listenerUp{listener.upX, listener.upY, listener.upZ};

    IPLDistanceAttenuationModel distanceModel{};
    distanceModel.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
    distanceModel.minDistance = 1.0f;
    distanceModel.callback = nullptr;
    distanceModel.userData = nullptr;
    distanceModel.dirty = IPL_FALSE;

    std::lock_guard<std::mutex> renderLock(renderMutex_);
    for (int i = 0; i < kMaxSlots; ++i) {
        Slot& slot = slots_[static_cast<std::size_t>(i)];
        SteamAudioSlotRenderParams params{};
        if (!slot.used || slot.source == nullptr) {
            renderParams_[static_cast<std::size_t>(i)] = params;
            continue;
        }

        IPLSimulationOutputs outputs{};
        iplSourceGetOutputs(slot.source, IPL_SIMULATIONFLAGS_DIRECT, &outputs);

        const IPLVector3 sourcePos{slot.x, slot.y, slot.z};
        const IPLVector3 relativeDir = iplCalculateRelativeDirection(
            context_,
            sourcePos,
            listenerPos,
            listenerAhead,
            listenerUp);
        params.dirX = relativeDir.x;
        params.dirY = relativeDir.y;
        params.dirZ = relativeDir.z;

        distanceModel.minDistance = slot.minDistance > 0.0f ? slot.minDistance : 1.0f;
        const float rawDistanceAttenuation = iplDistanceAttenuationCalculate(
            context_,
            sourcePos,
            listenerPos,
            &distanceModel);
        const float rawOcclusion = sceneReady_ ? outputs.direct.occlusion : 1.0f;
        const float rawTransmission[3] = {
            outputs.direct.transmission[0],
            outputs.direct.transmission[1],
            outputs.direct.transmission[2],
        };
        const float rawReverbTimes[3] = {
            sceneReady_ ? reverbTimes_[0] : 0.0f,
            sceneReady_ ? reverbTimes_[1] : 0.0f,
            sceneReady_ ? reverbTimes_[2] : 0.0f,
        };

        // Raw simulation outputs are recomputed from scratch every call: raycast occlusion can
        // flip instantly as the source/listener cross an edge, and the Monte Carlo reflection
        // estimate carries sampling noise even for a static scene. Smooth both toward the target
        // over time so occlusion transitions and reverb tails don't zipper/click.
        const float slotDirectAlpha = slot.smoothingInit ? directAlpha : 1.0f;
        const float slotReverbAlpha = slot.smoothingInit ? reverbAlpha : 1.0f;
        slot.smoothingInit = true;

        slot.smoothedDistanceAttenuation +=
            slotDirectAlpha * (rawDistanceAttenuation - slot.smoothedDistanceAttenuation);
        slot.smoothedOcclusion += slotDirectAlpha * (rawOcclusion - slot.smoothedOcclusion);
        for (int band = 0; band < 3; ++band) {
            slot.smoothedTransmission[band] +=
                slotDirectAlpha * (rawTransmission[band] - slot.smoothedTransmission[band]);
            slot.smoothedReverbTimes[band] +=
                slotReverbAlpha * (rawReverbTimes[band] - slot.smoothedReverbTimes[band]);
        }

        params.distanceAttenuation = slot.smoothedDistanceAttenuation;
        params.occlusion = slot.smoothedOcclusion;
        params.transmission[0] = slot.smoothedTransmission[0];
        params.transmission[1] = slot.smoothedTransmission[1];
        params.transmission[2] = slot.smoothedTransmission[2];
        params.reverbTimes[0] = slot.smoothedReverbTimes[0];
        params.reverbTimes[1] = slot.smoothedReverbTimes[1];
        params.reverbTimes[2] = slot.smoothedReverbTimes[2];
        // Small epsilon (not >0) so a decaying tail keeps rendering instead of gating off abruptly.
        params.reflectionsActive = sceneReady_
            && (params.reverbTimes[0] > 1.0e-3f || params.reverbTimes[1] > 1.0e-3f
                || params.reverbTimes[2] > 1.0e-3f);

        params.active = true;
        renderParams_[static_cast<std::size_t>(i)] = params;
    }
}

void SteamAudioRuntime::applyOcclusionGains(SoLoud::Soloud& soloud) {
    if (!ready_) {
        return;
    }

    std::lock_guard<std::mutex> playLock(playMutex_);
    std::lock_guard<std::mutex> renderLock(renderMutex_);
    for (int i = 0; i < kMaxSlots; ++i) {
        const Slot& slot = slots_[static_cast<std::size_t>(i)];
        if (!slot.used || slot.voice == 0 || !soloud.isValidVoiceHandle(slot.voice)) {
            continue;
        }

        const SteamAudioSlotRenderParams& params = renderParams_[static_cast<std::size_t>(i)];
        float gain = 1.0f;
        if (sceneReady_ && params.active) {
            const float transmission =
                (params.transmission[0] + params.transmission[1] + params.transmission[2])
                * (1.0f / 3.0f);
            const float occlusion = params.occlusion;
            gain = occlusion + (1.0f - occlusion) * transmission;
            if (gain < 0.0f) {
                gain = 0.0f;
            } else if (gain > 1.0f) {
                gain = 1.0f;
            }
        }
        soloud.setVolume(slot.voice, slot.baseVolume * gain);
    }
}

SteamAudioSlotRenderParams SteamAudioRuntime::slotParams(int slot) const {
    if (slot < 0 || slot >= kMaxSlots) {
        return {};
    }
    std::lock_guard<std::mutex> lock(renderMutex_);
    return renderParams_[static_cast<std::size_t>(slot)];
}

std::vector<SteamAudioDebugSourceInfo> SteamAudioRuntime::debugSources() {
    std::vector<SteamAudioDebugSourceInfo> result;
    if (!ready_) {
        return result;
    }

    std::lock_guard<std::mutex> playLock(playMutex_);
    std::lock_guard<std::mutex> renderLock(renderMutex_);
    for (int i = 0; i < kMaxSlots; ++i) {
        const Slot& slot = slots_[static_cast<std::size_t>(i)];
        if (!slot.used) {
            continue;
        }
        const SteamAudioSlotRenderParams& params = renderParams_[static_cast<std::size_t>(i)];
        SteamAudioDebugSourceInfo info{};
        info.voice = slot.voice;
        info.x = slot.x;
        info.y = slot.y;
        info.z = slot.z;
        info.occlusion = params.occlusion;
        info.transmission[0] = params.transmission[0];
        info.transmission[1] = params.transmission[1];
        info.transmission[2] = params.transmission[2];
        info.reverbTimes[0] = params.reverbTimes[0];
        info.reverbTimes[1] = params.reverbTimes[1];
        info.reverbTimes[2] = params.reverbTimes[2];
        info.reflectionsActive = params.reflectionsActive;
        result.push_back(info);
    }
    return result;
}

SteamAudioDebugScene SteamAudioRuntime::debugScene() const {
    SteamAudioDebugScene result;
    if (!sceneReady_) {
        return result;
    }
    result.triangles.reserve(debugMesh_.triangles.size());
    for (const IPLTriangle& tri : debugMesh_.triangles) {
        const IPLVector3& a = debugMesh_.vertices[static_cast<std::size_t>(tri.indices[0])];
        const IPLVector3& b = debugMesh_.vertices[static_cast<std::size_t>(tri.indices[1])];
        const IPLVector3& c = debugMesh_.vertices[static_cast<std::size_t>(tri.indices[2])];
        SteamAudioDebugTriangle debugTri{};
        debugTri.v0[0] = a.x;
        debugTri.v0[1] = a.y;
        debugTri.v0[2] = a.z;
        debugTri.v1[0] = b.x;
        debugTri.v1[1] = b.y;
        debugTri.v1[2] = b.z;
        debugTri.v2[0] = c.x;
        debugTri.v2[1] = c.y;
        debugTri.v2[2] = c.z;
        result.triangles.push_back(debugTri);
    }
    return result;
}

int SteamAudioRuntime::allocSlotUnlocked() {
    for (int i = 0; i < kMaxSlots; ++i) {
        if (!slots_[static_cast<std::size_t>(i)].used) {
            slots_[static_cast<std::size_t>(i)] = Slot{};
            slots_[static_cast<std::size_t>(i)].used = true;
            return i;
        }
    }
    TraceLog(LOG_WARNING, "STEAM AUDIO: no free spatialize slots");
    return -1;
}

void SteamAudioRuntime::freeSlotUnlocked(int slot) {
    if (slot < 0 || slot >= kMaxSlots) {
        return;
    }
    Slot& s = slots_[static_cast<std::size_t>(slot)];
    if (s.source != nullptr) {
        iplSourceRemove(s.source, simulator_);
        iplSimulatorCommit(simulator_);
        iplSourceRelease(&s.source);
    }
    s = Slot{};
    std::lock_guard<std::mutex> lock(renderMutex_);
    renderParams_[static_cast<std::size_t>(slot)] = SteamAudioSlotRenderParams{};
}

IPLCoordinateSpace3 SteamAudioRuntime::makeCoords(
    float ox,
    float oy,
    float oz,
    float aheadX,
    float aheadY,
    float aheadZ,
    float upX,
    float upY,
    float upZ) {
    normalize3(aheadX, aheadY, aheadZ);
    normalize3(upX, upY, upZ);

    float rightX = 0.0f;
    float rightY = 0.0f;
    float rightZ = 0.0f;
    cross3(upX, upY, upZ, aheadX, aheadY, aheadZ, rightX, rightY, rightZ);
    normalize3(rightX, rightY, rightZ);
    cross3(aheadX, aheadY, aheadZ, rightX, rightY, rightZ, upX, upY, upZ);
    normalize3(upX, upY, upZ);

    IPLCoordinateSpace3 coords{};
    coords.right = IPLVector3{rightX, rightY, rightZ};
    coords.up = IPLVector3{upX, upY, upZ};
    coords.ahead = IPLVector3{aheadX, aheadY, aheadZ};
    coords.origin = IPLVector3{ox, oy, oz};
    return coords;
}

}

#endif
