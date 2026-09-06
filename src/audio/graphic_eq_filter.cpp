#include "audio/graphic_eq_filter.hpp"

#include <cmath>

namespace slopengine {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kBandQ = 1.4142135f;

const char* bandParamName(int band) {
    static const char* kNames[kGraphicEqBandCount] =
        {"31 Hz", "62 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz"};
    return kNames[band];
}

}

GraphicEqFilterInstance::GraphicEqFilterInstance(GraphicEqFilter* parent) {
    (void)parent;
    initParams(1 + kGraphicEqBandCount);
    mParam[WET] = 1.0f;
    for (int band = 0; band < kGraphicEqBandCount; ++band) {
        mParam[GAIN0 + band] = 0.0f;
        updateBandCoeffs(band);
    }
}

void GraphicEqFilterInstance::updateBandCoeffs(int band) {
    const float freq = kGraphicEqFrequencies[static_cast<std::size_t>(band)];
    const float dbGain = mParam[GAIN0 + band];
    const float omega = 2.0f * kPi * freq / samplerate_;

    GraphicEqBandCoeffs& c = coeffs_[static_cast<std::size_t>(band)];
    if (omega >= kPi) {
        c = GraphicEqBandCoeffs{};
        return;
    }

    const float a = std::pow(10.0f, dbGain / 40.0f);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.0f * kBandQ);

    const float b0 = 1.0f + alpha * a;
    const float b1 = -2.0f * cosOmega;
    const float b2 = 1.0f - alpha * a;
    const float a0 = 1.0f + alpha / a;
    const float a1 = -2.0f * cosOmega;
    const float a2 = 1.0f - alpha / a;

    c.ff0 = b0 / a0;
    c.ff1 = b1 / a0;
    c.ff2 = b2 / a0;
    c.fb1 = a1 / a0;
    c.fb2 = a2 / a0;
}

void GraphicEqFilterInstance::filterChannel(
    float* aBuffer,
    unsigned int aSamples,
    float aSamplerate,
    SoLoud::time aTime,
    unsigned int aChannel,
    unsigned int /*aChannels*/) {
    if (aChannel == 0) {
        updateParams(aTime);
        if (aSamplerate != samplerate_) {
            samplerate_ = aSamplerate;
            for (int band = 0; band < kGraphicEqBandCount; ++band) {
                updateBandCoeffs(band);
            }
        } else if (mParamChanged != 0) {
            for (int band = 0; band < kGraphicEqBandCount; ++band) {
                if ((mParamChanged & (1u << (GAIN0 + band))) != 0) {
                    updateBandCoeffs(band);
                }
            }
        }
        mParamChanged = 0;
    }

    std::array<GraphicEqBandState, kGraphicEqBandCount>& bandState = state_[aChannel];
    const float wet = mParam[WET];
    for (unsigned int i = 0; i < aSamples; ++i) {
        const float dry = aBuffer[i];
        float x = dry;
        for (int band = 0; band < kGraphicEqBandCount; ++band) {
            const GraphicEqBandCoeffs& c = coeffs_[static_cast<std::size_t>(band)];
            GraphicEqBandState& s = bandState[static_cast<std::size_t>(band)];
            const float y = c.ff0 * x + c.ff1 * s.x1 + c.ff2 * s.x2 - c.fb1 * s.y1 - c.fb2 * s.y2;
            s.x2 = s.x1;
            s.x1 = x;
            s.y2 = s.y1;
            s.y1 = y;
            x = y;
        }
        aBuffer[i] = dry + (x - dry) * wet;
    }
}

int GraphicEqFilter::getParamCount() {
    return 1 + kGraphicEqBandCount;
}

const char* GraphicEqFilter::getParamName(unsigned int aParamIndex) {
    if (aParamIndex == 0) {
        return "Wet";
    }
    return bandParamName(static_cast<int>(aParamIndex) - 1);
}

unsigned int GraphicEqFilter::getParamType(unsigned int /*aParamIndex*/) {
    return SoLoud::Filter::FLOAT_PARAM;
}

float GraphicEqFilter::getParamMax(unsigned int aParamIndex) {
    return aParamIndex == 0 ? 1.0f : 24.0f;
}

float GraphicEqFilter::getParamMin(unsigned int aParamIndex) {
    return aParamIndex == 0 ? 0.0f : -24.0f;
}

GraphicEqFilterInstance* GraphicEqFilter::createInstance() {
    return new GraphicEqFilterInstance(this);
}

}
