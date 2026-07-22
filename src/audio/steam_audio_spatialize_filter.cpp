#ifdef SLOPENGINE_HAS_STEAM_AUDIO

#include "audio/steam_audio_spatialize_filter.hpp"

#include "audio/steam_audio_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace slopengine {

namespace {

SteamAudioSlotRenderParams defaultParams() {
    SteamAudioSlotRenderParams params{};
    params.dirX = 0.0f;
    params.dirY = 0.0f;
    params.dirZ = -1.0f;
    params.distanceAttenuation = 1.0f;
    params.occlusion = 1.0f;
    params.transmission[0] = 1.0f;
    params.transmission[1] = 1.0f;
    params.transmission[2] = 1.0f;
    params.active = true;
    return params;
}

SteamAudioSlotRenderParams sanitizeParams(SteamAudioSlotRenderParams params) {
    const float len = std::sqrt(
        params.dirX * params.dirX + params.dirY * params.dirY + params.dirZ * params.dirZ);
    if (len < 1.0e-4f) {
        params.dirX = 0.0f;
        params.dirY = 0.0f;
        params.dirZ = -1.0f;
    } else {
        params.dirX /= len;
        params.dirY /= len;
        params.dirZ /= len;
    }
    params.distanceAttenuation = std::clamp(params.distanceAttenuation, 0.0f, 1.0f);
    params.occlusion = std::clamp(params.occlusion, 0.0f, 1.0f);
    for (float& t : params.transmission) {
        t = std::clamp(t, 0.0f, 1.0f);
    }
    for (float& rt : params.reverbTimes) {
        if (rt < 0.0f) {
            rt = 0.0f;
        }
    }
    return params;
}

}

SteamAudioSpatializeFilterInstance::SteamAudioSpatializeFilterInstance(SteamAudioRuntime* runtime)
    : runtime_(runtime) {
    initParams(1);
    mParam[SteamAudioRuntime::kSlotParam] = -1.0f;
    if (runtime_ != nullptr && runtime_->ready()) {
        frameSize_ = runtime_->audioSettings().frameSize;
    }
    if (frameSize_ <= 0) {
        frameSize_ = 1024;
    }
}

SteamAudioSpatializeFilterInstance::~SteamAudioSpatializeFilterInstance() {
    releaseEffects();
}

bool SteamAudioSpatializeFilterInstance::ensureEffects() {
    if (runtime_ == nullptr || !runtime_->ready() || frameSize_ <= 0) {
        return false;
    }

    if (direct_ == nullptr) {
        IPLDirectEffectSettings directSettings{};
        directSettings.numChannels = 1;
        if (iplDirectEffectCreate(
                runtime_->context(),
                const_cast<IPLAudioSettings*>(&runtime_->audioSettings()),
                &directSettings,
                &direct_)
            != IPL_STATUS_SUCCESS) {
            return false;
        }
    }

    if (binaural_ == nullptr) {
        IPLBinauralEffectSettings binauralSettings{};
        binauralSettings.hrtf = runtime_->hrtf();
        if (iplBinauralEffectCreate(
                runtime_->context(),
                const_cast<IPLAudioSettings*>(&runtime_->audioSettings()),
                &binauralSettings,
                &binaural_)
            != IPL_STATUS_SUCCESS) {
            return false;
        }
    }

    if (reflection_ == nullptr) {
        IPLReflectionEffectSettings reflectionSettings{};
        reflectionSettings.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
        reflectionSettings.numChannels = 1;
        reflectionSettings.irSize = static_cast<IPLint32>(
            runtime_->audioSettings().samplingRate * runtime_->maxReflectionDuration());
        if (reflectionSettings.irSize < frameSize_) {
            reflectionSettings.irSize = frameSize_;
        }
        if (iplReflectionEffectCreate(
                runtime_->context(),
                const_cast<IPLAudioSettings*>(&runtime_->audioSettings()),
                &reflectionSettings,
                &reflection_)
            != IPL_STATUS_SUCCESS) {
            return false;
        }
    }

    if (!buffersReady_) {
        if (iplAudioBufferAllocate(runtime_->context(), 1, frameSize_, &monoIn_) != IPL_STATUS_SUCCESS) {
            return false;
        }
        if (iplAudioBufferAllocate(runtime_->context(), 1, frameSize_, &monoDirect_)
            != IPL_STATUS_SUCCESS) {
            iplAudioBufferFree(runtime_->context(), &monoIn_);
            return false;
        }
        if (iplAudioBufferAllocate(runtime_->context(), 1, frameSize_, &monoWet_)
            != IPL_STATUS_SUCCESS) {
            iplAudioBufferFree(runtime_->context(), &monoIn_);
            iplAudioBufferFree(runtime_->context(), &monoDirect_);
            return false;
        }
        if (iplAudioBufferAllocate(runtime_->context(), 2, frameSize_, &stereoOut_)
            != IPL_STATUS_SUCCESS) {
            iplAudioBufferFree(runtime_->context(), &monoIn_);
            iplAudioBufferFree(runtime_->context(), &monoDirect_);
            iplAudioBufferFree(runtime_->context(), &monoWet_);
            return false;
        }
        monoIn_.numSamples = frameSize_;
        monoDirect_.numSamples = frameSize_;
        monoWet_.numSamples = frameSize_;
        stereoOut_.numSamples = frameSize_;
        buffersReady_ = true;
    }

    return true;
}

void SteamAudioSpatializeFilterInstance::releaseEffects() {
    if (runtime_ == nullptr || runtime_->context() == nullptr) {
        direct_ = nullptr;
        binaural_ = nullptr;
        reflection_ = nullptr;
        buffersReady_ = false;
        return;
    }
    if (buffersReady_) {
        iplAudioBufferFree(runtime_->context(), &monoIn_);
        iplAudioBufferFree(runtime_->context(), &monoDirect_);
        iplAudioBufferFree(runtime_->context(), &monoWet_);
        iplAudioBufferFree(runtime_->context(), &stereoOut_);
        buffersReady_ = false;
    }
    if (direct_ != nullptr) {
        iplDirectEffectRelease(&direct_);
    }
    if (binaural_ != nullptr) {
        iplBinauralEffectRelease(&binaural_);
    }
    if (reflection_ != nullptr) {
        iplReflectionEffectRelease(&reflection_);
    }
}

void SteamAudioSpatializeFilterInstance::processFrame(const SteamAudioSlotRenderParams& params) {
    const SteamAudioSlotRenderParams safe = sanitizeParams(params);
    for (int i = 0; i < frameSize_; ++i) {
        monoIn_.data[0][i] = inQueue_.front();
        inQueue_.pop_front();
    }

    IPLDirectEffectParams directParams{};
    directParams.flags = static_cast<IPLDirectEffectFlags>(
        IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION | IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION
        | IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION);
    directParams.transmissionType = IPL_TRANSMISSIONTYPE_FREQDEPENDENT;
    directParams.distanceAttenuation = safe.distanceAttenuation;
    directParams.occlusion = safe.occlusion;
    directParams.transmission[0] = safe.transmission[0];
    directParams.transmission[1] = safe.transmission[1];
    directParams.transmission[2] = safe.transmission[2];
    iplDirectEffectApply(direct_, &directParams, &monoIn_, &monoDirect_);

    IPLBinauralEffectParams binauralParams{};
    binauralParams.direction = IPLVector3{safe.dirX, safe.dirY, safe.dirZ};
    binauralParams.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
    binauralParams.spatialBlend = 1.0f;
    binauralParams.hrtf = runtime_->hrtf();
    binauralParams.peakDelays = nullptr;
    iplBinauralEffectApply(binaural_, &binauralParams, &monoDirect_, &stereoOut_);

    if (safe.reflectionsActive && reflection_ != nullptr) {
        IPLReflectionEffectParams reflectionParams{};
        reflectionParams.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
        reflectionParams.reverbTimes[0] = safe.reverbTimes[0];
        reflectionParams.reverbTimes[1] = safe.reverbTimes[1];
        reflectionParams.reverbTimes[2] = safe.reverbTimes[2];
        reflectionParams.eq[0] = 1.0f;
        reflectionParams.eq[1] = 1.0f;
        reflectionParams.eq[2] = 1.0f;
        reflectionParams.delay = 0;
        reflectionParams.numChannels = 1;
        reflectionParams.irSize = 0;
        reflectionParams.ir = nullptr;
        reflectionParams.tanDevice = nullptr;
        reflectionParams.tanSlot = 0;
        iplReflectionEffectApply(reflection_, &reflectionParams, &monoIn_, &monoWet_, nullptr);

        const float wet = runtime_->wetMix();
        for (int i = 0; i < frameSize_; ++i) {
            const float wetSample = monoWet_.data[0][i] * wet;
            stereoOut_.data[0][i] += wetSample;
            stereoOut_.data[1][i] += wetSample;
        }
    }

    for (int i = 0; i < frameSize_; ++i) {
        outQueueL_.push_back(stereoOut_.data[0][i]);
        outQueueR_.push_back(stereoOut_.data[1][i]);
    }
}

void SteamAudioSpatializeFilterInstance::pushMono(float sample) {
    inQueue_.push_back(sample);
}

bool SteamAudioSpatializeFilterInstance::popStereo(float& left, float& right) {
    if (outQueueL_.empty() || outQueueR_.empty()) {
        left = 0.0f;
        right = 0.0f;
        return false;
    }
    left = outQueueL_.front();
    right = outQueueR_.front();
    outQueueL_.pop_front();
    outQueueR_.pop_front();
    return true;
}

void SteamAudioSpatializeFilterInstance::filter(
    float* aBuffer,
    unsigned int aSamples,
    unsigned int aBufferSize,
    unsigned int aChannels,
    float /*aSamplerate*/,
    SoLoud::time aTime) {
    updateParams(aTime);

    if (aBuffer == nullptr || aSamples == 0 || aChannels == 0 || runtime_ == nullptr) {
        return;
    }
    if (!ensureEffects()) {
        return;
    }

    const int slot = static_cast<int>(mParam[SteamAudioRuntime::kSlotParam]);
    SteamAudioSlotRenderParams params = runtime_->slotParams(slot);
    if (!params.active) {
        params = defaultParams();
    }

    for (unsigned int i = 0; i < aSamples; ++i) {
        float mono = 0.0f;
        if (aChannels == 1) {
            mono = aBuffer[i];
        } else {
            float sum = 0.0f;
            for (unsigned int ch = 0; ch < aChannels; ++ch) {
                sum += aBuffer[i + ch * aBufferSize];
            }
            mono = sum / static_cast<float>(aChannels);
        }
        pushMono(mono);

        while (static_cast<int>(inQueue_.size()) >= frameSize_) {
            processFrame(params);
        }

        float left = 0.0f;
        float right = 0.0f;
        popStereo(left, right);

        if (aChannels == 1) {
            aBuffer[i] = 0.5f * (left + right);
        } else {
            aBuffer[i] = left;
            if (aChannels > 1) {
                aBuffer[i + aBufferSize] = right;
            }
            for (unsigned int ch = 2; ch < aChannels; ++ch) {
                aBuffer[i + ch * aBufferSize] = 0.0f;
            }
        }
    }
}

SteamAudioSpatializeFilter::SteamAudioSpatializeFilter(SteamAudioRuntime* runtime)
    : runtime_(runtime) {}

SoLoud::FilterInstance* SteamAudioSpatializeFilter::createInstance() {
    return new SteamAudioSpatializeFilterInstance(runtime_);
}

}

#endif
