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

    spatializeFilter_ = std::make_unique<SteamAudioSpatializeFilter>(this);
    ready_ = true;
    TraceLog(
        LOG_INFO,
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

bool SteamAudioRuntime::setSceneFromFac(const FacFile& vis) {
    if (!ready_) {
        return false;
    }
    const bool ok = createSteamAudioScene(context_, simulator_, vis, &scene_, &staticMesh_);
    sceneReady_ = ok;
    if (ok) {
        TraceLog(LOG_INFO, "STEAM AUDIO: scene built from VIS faces");
    } else {
        TraceLog(LOG_WARNING, "STEAM AUDIO: failed to build scene from VIS");
    }
    return ok;
}

void SteamAudioRuntime::clearScene() {
    if (!ready_ && scene_ == nullptr && staticMesh_ == nullptr) {
        return;
    }
    destroySteamAudioScene(simulator_, &scene_, &staticMesh_);
    sceneReady_ = false;
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
    sourceSettings.flags = static_cast<IPLSimulationFlags>(
        IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
    if (iplSourceCreate(simulator_, &sourceSettings, &s.source) != IPL_STATUS_SUCCESS) {
        TraceLog(LOG_WARNING, "STEAM AUDIO: iplSourceCreate failed");
        freeSlotUnlocked(slot);
        return -1;
    }
    iplSourceAdd(s.source, simulator_);
    iplSimulatorCommit(simulator_);

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
        inputs.flags = static_cast<IPLSimulationFlags>(
            IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
        inputs.directFlags = static_cast<IPLDirectSimulationFlags>(
            IPL_DIRECTSIMULATIONFLAGS_OCCLUSION | IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION);
        inputs.source = makeCoords(slot.x, slot.y, slot.z, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);
        inputs.occlusionType = IPL_OCCLUSIONTYPE_RAYCAST;
        inputs.numTransmissionRays = 1;
        inputs.reverbScale[0] = 1.0f;
        inputs.reverbScale[1] = 1.0f;
        inputs.reverbScale[2] = 1.0f;
        inputs.hybridReverbTransitionTime = 0.0f;
        inputs.hybridReverbOverlapPercent = 0.0f;
        inputs.baked = IPL_FALSE;
        iplSourceSetInputs(
            slot.source,
            static_cast<IPLSimulationFlags>(
                IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS),
            &inputs);
    }

    iplSimulatorRunDirect(simulator_);
    if (sceneReady_) {
        iplSimulatorRunReflections(simulator_);
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
        iplSourceGetOutputs(
            slot.source,
            static_cast<IPLSimulationFlags>(
                IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS),
            &outputs);

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
        params.distanceAttenuation = iplDistanceAttenuationCalculate(
            context_,
            sourcePos,
            listenerPos,
            &distanceModel);

        params.occlusion = sceneReady_ ? outputs.direct.occlusion : 1.0f;
        params.transmission[0] = outputs.direct.transmission[0];
        params.transmission[1] = outputs.direct.transmission[1];
        params.transmission[2] = outputs.direct.transmission[2];

        if (sceneReady_) {
            params.reverbTimes[0] = outputs.reflections.reverbTimes[0];
            params.reverbTimes[1] = outputs.reflections.reverbTimes[1];
            params.reverbTimes[2] = outputs.reflections.reverbTimes[2];
            params.reflectionsActive =
                params.reverbTimes[0] > 0.0f || params.reverbTimes[1] > 0.0f
                || params.reverbTimes[2] > 0.0f;
        }

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
