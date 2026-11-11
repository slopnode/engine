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
    initParams(SteamAudioRuntime::kFilterParamCount);
    mParam[SteamAudioRuntime::kSlotParam] = -1.0f;
    mParam[SteamAudioRuntime::kDrySamplesParam] = 0.0f;
    mParam[SteamAudioRuntime::kTailSamplesParam] = 0.0f;
    if (runtime_ != nullptr && runtime_->ready()) {
        frameSize_ = runtime_->audioSettings().frameSize;
        effects_ = runtime_->acquireEffects();
    }
}

SteamAudioSpatializeFilterInstance::~SteamAudioSpatializeFilterInstance() {
    if (runtime_ != nullptr) {
        runtime_->recycleEffects(std::move(effects_));
    }
}

void SteamAudioSpatializeFilterInstance::processFrame(const SteamAudioSlotRenderParams& params) {
    const SteamAudioSlotRenderParams safe = sanitizeParams(params);
    SteamAudioVoiceEffects& fx = *effects_;

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
    iplDirectEffectApply(fx.direct, &directParams, &fx.monoIn, &fx.monoDirect);

    IPLBinauralEffectParams binauralParams{};
    binauralParams.direction = IPLVector3{safe.dirX, safe.dirY, safe.dirZ};
    binauralParams.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
    binauralParams.spatialBlend = 1.0f;
    binauralParams.hrtf = runtime_->hrtf();
    binauralParams.peakDelays = nullptr;
    iplBinauralEffectApply(fx.binaural, &binauralParams, &fx.monoDirect, &fx.stereoOut);

    if (safe.reflectionsActive) {
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
        iplReflectionEffectApply(fx.reflection, &reflectionParams, &fx.monoIn, &fx.monoWet, nullptr);

        const float wet = runtime_->wetMix();
        for (int i = 0; i < frameSize_; ++i) {
            const float wetSample = fx.monoWet.data[0][i] * wet;
            fx.stereoOut.data[0][i] += wetSample;
            fx.stereoOut.data[1][i] += wetSample;
        }
    }
}

void SteamAudioSpatializeFilterInstance::filter(
    float* aBuffer,
    unsigned int aSamples,
    unsigned int aBufferSize,
    unsigned int aChannels,
    float /*aSamplerate*/,
    SoLoud::time aTime) {
    updateParams(aTime);

    if (aBuffer == nullptr || aSamples == 0 || aChannels == 0 || effects_ == nullptr
        || frameSize_ <= 0) {
        return;
    }

    const int slot = static_cast<int>(mParam[SteamAudioRuntime::kSlotParam]);
    SteamAudioSlotRenderParams params = runtime_->slotParams(slot);
    if (!params.active) {
        params = defaultParams();
    }

    const double drySamples = static_cast<double>(mParam[SteamAudioRuntime::kDrySamplesParam]);
    const double tailSamples =
        std::max(1.0, static_cast<double>(mParam[SteamAudioRuntime::kTailSamplesParam]));
    const bool fades = drySamples > 0.0;
    const unsigned int frame = static_cast<unsigned int>(frameSize_);

    for (unsigned int offset = 0; offset + frame <= aSamples; offset += frame) {
        float* left = aBuffer + offset;
        float* right = aChannels > 1 ? aBuffer + aBufferSize + offset : nullptr;
        const double position = static_cast<double>(processedSamples_);
        processedSamples_ += frame;

        if (fades && position >= drySamples + tailSamples) {
            for (unsigned int ch = 0; ch < aChannels; ++ch) {
                std::memset(aBuffer + ch * aBufferSize + offset, 0, frame * sizeof(float));
            }
            continue;
        }

        float* monoIn = effects_->monoIn.data[0];
        if (aChannels == 1) {
            std::memcpy(monoIn, left, frame * sizeof(float));
        } else {
            const float scale = 1.0f / static_cast<float>(aChannels);
            for (unsigned int i = 0; i < frame; ++i) {
                float sum = 0.0f;
                for (unsigned int ch = 0; ch < aChannels; ++ch) {
                    sum += aBuffer[ch * aBufferSize + offset + i];
                }
                monoIn[i] = sum * scale;
            }
        }

        processFrame(params);

        const float* outLeft = effects_->stereoOut.data[0];
        const float* outRight = effects_->stereoOut.data[1];
        for (unsigned int i = 0; i < frame; ++i) {
            float gain = 1.0f;
            if (fades) {
                const double past = position + static_cast<double>(i) - drySamples;
                if (past > 0.0) {
                    gain = static_cast<float>(std::max(0.0, 1.0 - past / tailSamples));
                }
            }
            if (right == nullptr) {
                left[i] = 0.5f * (outLeft[i] + outRight[i]) * gain;
            } else {
                left[i] = outLeft[i] * gain;
                right[i] = outRight[i] * gain;
            }
        }
        for (unsigned int ch = 2; ch < aChannels; ++ch) {
            std::memset(aBuffer + ch * aBufferSize + offset, 0, frame * sizeof(float));
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
