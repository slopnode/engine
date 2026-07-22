#pragma once

#ifdef SLOPENGINE_HAS_STEAM_AUDIO

#include <phonon.h>
#include <soloud.h>
#include <soloud_filter.h>

#include <deque>

namespace slopengine {

class SteamAudioRuntime;
struct SteamAudioSlotRenderParams;

class SteamAudioSpatializeFilterInstance : public SoLoud::FilterInstance {
public:
    explicit SteamAudioSpatializeFilterInstance(SteamAudioRuntime* runtime);
    ~SteamAudioSpatializeFilterInstance() override;

    void filter(
        float* aBuffer,
        unsigned int aSamples,
        unsigned int aBufferSize,
        unsigned int aChannels,
        float aSamplerate,
        SoLoud::time aTime) override;

private:
    bool ensureEffects();
    void releaseEffects();
    void processFrame(const SteamAudioSlotRenderParams& params);
    void pushMono(float sample);
    bool popStereo(float& left, float& right);

    SteamAudioRuntime* runtime_ = nullptr;
    IPLDirectEffect direct_ = nullptr;
    IPLBinauralEffect binaural_ = nullptr;
    IPLReflectionEffect reflection_ = nullptr;
    IPLAudioBuffer monoIn_{};
    IPLAudioBuffer monoDirect_{};
    IPLAudioBuffer monoWet_{};
    IPLAudioBuffer stereoOut_{};
    int frameSize_ = 0;
    bool buffersReady_ = false;

    std::deque<float> inQueue_;
    std::deque<float> outQueueL_;
    std::deque<float> outQueueR_;
};

class SteamAudioSpatializeFilter : public SoLoud::Filter {
public:
    explicit SteamAudioSpatializeFilter(SteamAudioRuntime* runtime);
    SoLoud::FilterInstance* createInstance() override;

private:
    SteamAudioRuntime* runtime_ = nullptr;
};

}

#endif
