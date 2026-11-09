#pragma once

#ifdef SLOPENGINE_HAS_STEAM_AUDIO

#include "core/win32.hpp"

#include "assets/asset_store.hpp"
#include "audio/steam_audio_scene.hpp"
#include "audio/steam_audio_types.hpp"
#include "map/brush.hpp"

#include <phonon.h>
#include <soloud.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

struct SteamAudioSlotRenderParams {
    float dirX = 0.0f;
    float dirY = 0.0f;
    float dirZ = 1.0f;
    float distanceAttenuation = 1.0f;
    float occlusion = 1.0f;
    float transmission[3] = {1.0f, 1.0f, 1.0f};
    float reverbTimes[3] = {0.0f, 0.0f, 0.0f};
    bool reflectionsActive = false;
    bool active = false;
};

struct SteamAudioVoiceEffects {
    IPLDirectEffect direct = nullptr;
    IPLBinauralEffect binaural = nullptr;
    IPLReflectionEffect reflection = nullptr;
    IPLAudioBuffer monoIn{};
    IPLAudioBuffer monoDirect{};
    IPLAudioBuffer monoWet{};
    IPLAudioBuffer stereoOut{};
};

class SteamAudioSpatializeFilter;

class SteamAudioRuntime {
public:
    static constexpr int kMaxSlots = 128;
    static constexpr int kSpatialVoiceBudget = 48;
    static constexpr int kSlotParam = 0;
    static constexpr int kDrySamplesParam = 1;
    static constexpr int kTailSamplesParam = 2;
    static constexpr int kFilterParamCount = 3;
    static constexpr int kReflectionOrder = 1;
    static constexpr float kMaxReflectionDuration = 1.5f;
    static constexpr float kWetMix = 0.55f;
    static constexpr float kDirectSmoothingTime = 0.1f;
    static constexpr float kReverbSmoothingTime = 0.25f;
    static constexpr float kReflectionInterval = 0.1f;
    static constexpr float kTailFraction = 0.5f;
    static constexpr float kMinTail = 0.03f;
    static constexpr float kMaxTail = 0.75f;
    static constexpr float kTailPriority = 0.05f;
    static constexpr float kStealFadeTime = 0.02f;

    SteamAudioRuntime();
    ~SteamAudioRuntime();

    SteamAudioRuntime(const SteamAudioRuntime&) = delete;
    SteamAudioRuntime& operator=(const SteamAudioRuntime&) = delete;

    bool init(int samplingRate, int frameSize);
    void shutdown();
    bool ready() const { return ready_; }
    bool sceneReady() const { return sceneReady_; }

    IPLContext context() const { return context_; }
    IPLHRTF hrtf() const { return hrtf_; }
    IPLSimulator simulator() const { return simulator_; }
    const IPLAudioSettings& audioSettings() const { return audioSettings_; }
    int reflectionOrder() const { return kReflectionOrder; }
    float maxReflectionDuration() const { return kMaxReflectionDuration; }
    float wetMix() const { return kWetMix; }

    SteamAudioSpatializeFilter& spatializeFilter();

    bool setSceneFromBrushes(
        const std::vector<Brush>& brushes,
        const std::unordered_set<std::string>* excludeBrushIds,
        AssetStore& assets);
    void clearScene();

    bool admitVoice(
        SoLoud::Soloud& soloud,
        float x,
        float y,
        float z,
        float minDistance,
        float volume,
        bool looping);
    int trackVoice(
        SoLoud::handle voice,
        float x,
        float y,
        float z,
        float minDistance,
        float maxDistance,
        float baseVolume,
        bool looping,
        float dryLength);
    float tailSeconds() const;
    void releaseVoice(SoLoud::handle voice);

    void pruneFinishedVoices(SoLoud::Soloud& soloud);
    void setVoicePosition(SoLoud::handle voice, float x, float y, float z);
    void update(const SteamAudioListenerPose& listener, const std::vector<SteamAudioSourcePose>& sources);
    void applyOcclusionGains(SoLoud::Soloud& soloud);

    SteamAudioSlotRenderParams slotParams(int slot) const;

    std::unique_ptr<SteamAudioVoiceEffects> acquireEffects();
    void recycleEffects(std::unique_ptr<SteamAudioVoiceEffects> effects);
    std::mutex& playMutex() { return playMutex_; }

    // Debug-only introspection for audio visualization tooling; not used by the mixer.
    // Not const: locks playMutex_, which (like the rest of the play-side state) isn't mutable.
    std::vector<SteamAudioDebugSourceInfo> debugSources();
    SteamAudioDebugScene debugScene() const;

private:
    struct Slot {
        IPLSource source = nullptr;
        SoLoud::handle voice = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float minDistance = 1.0f;
        float maxDistance = 30.0f;
        float baseVolume = 1.0f;
        float dryLength = 0.0f;
        bool used = false;
        bool looping = false;
        bool stolen = false;

        float smoothedOcclusion = 1.0f;
        float smoothedTransmission[3] = {1.0f, 1.0f, 1.0f};
        float smoothedDistanceAttenuation = 1.0f;
        float smoothedReverbTimes[3] = {0.0f, 0.0f, 0.0f};
        bool smoothingInit = false;
    };

    int allocSlotUnlocked();
    void freeSlotUnlocked(int slot);
    void setSourceInputsUnlocked(const Slot& slot);
    void refreshSlotUnlocked(int index, float directAlpha, float reverbAlpha);
    float distanceAttenuation(float x, float y, float z, float minDistance) const;
    std::unique_ptr<SteamAudioVoiceEffects> createEffects();
    void destroyEffects(SteamAudioVoiceEffects& effects);
    void resetRecycledEffects();
    void destroySceneUnlocked();
    static IPLCoordinateSpace3 makeCoords(float ox, float oy, float oz, float aheadX, float aheadY, float aheadZ, float upX, float upY, float upZ);

    IPLContext context_ = nullptr;
    IPLHRTF hrtf_ = nullptr;
    IPLSimulator simulator_ = nullptr;
    IPLScene scene_ = nullptr;
    IPLStaticMesh staticMesh_ = nullptr;
    IPLAudioSettings audioSettings_{};
    bool ready_ = false;
    bool sceneReady_ = false;

    // Retained alongside scene_/staticMesh_ purely so debugScene() can wireframe it; only
    // touched from the main thread at scene (re)build time, same as scene_/staticMesh_.
    SteamAudioSceneMesh debugMesh_{};

    std::array<Slot, kMaxSlots> slots_{};
    std::array<SteamAudioSlotRenderParams, kMaxSlots> renderParams_{};
    mutable std::mutex renderMutex_;
    std::mutex playMutex_;

    IPLSource reverbSource_ = nullptr;
    float reverbTimes_[3]{};
    float reflectionTimer_ = 0.0f;

    std::chrono::steady_clock::time_point lastUpdateTime_{};
    bool hasLastUpdateTime_ = false;

    SteamAudioListenerPose lastListener_{};
    bool hasListener_ = false;

    std::vector<std::unique_ptr<SteamAudioVoiceEffects>> cleanEffects_;
    std::vector<std::unique_ptr<SteamAudioVoiceEffects>> dirtyEffects_;
    std::vector<std::unique_ptr<SteamAudioVoiceEffects>> resettingEffects_;
    std::mutex poolMutex_;

    std::unique_ptr<SteamAudioSpatializeFilter> spatializeFilter_;
};

}

#endif
