#pragma once

#ifdef SLOPENGINE_HAS_STEAM_AUDIO

#include "core/win32.hpp"

#include <phonon.h>
#include <soloud.h>
#include <soloud_filter.h>

#include <cstdint>
#include <memory>

namespace slopengine {

class SteamAudioRuntime;
struct SteamAudioSlotRenderParams;
struct SteamAudioVoiceEffects;

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
    void processFrame(const SteamAudioSlotRenderParams& params);

    SteamAudioRuntime* runtime_ = nullptr;
    std::unique_ptr<SteamAudioVoiceEffects> effects_;
    int frameSize_ = 0;
    std::uint64_t processedSamples_ = 0;
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
