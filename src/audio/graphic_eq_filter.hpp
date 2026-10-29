#pragma once

#include <soloud.h>
#include <soloud_filter.h>

#include <array>

namespace slopengine {

inline constexpr int kGraphicEqBandCount = 10;
inline constexpr std::array<float, kGraphicEqBandCount> kGraphicEqFrequencies = {
    31.0f, 62.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};

class GraphicEqFilter;

struct GraphicEqBandCoeffs {
    float ff0 = 1.0f;
    float ff1 = 0.0f;
    float ff2 = 0.0f;
    float fb1 = 0.0f;
    float fb2 = 0.0f;
};

struct GraphicEqBandState {
    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;
};

class GraphicEqFilterInstance : public SoLoud::FilterInstance {
public:
    enum FILTERATTRIBUTE {
        WET = 0,
        GAIN0
    };

    explicit GraphicEqFilterInstance(GraphicEqFilter* parent);

    void filterChannel(
        float* aBuffer,
        unsigned int aSamples,
        float aSamplerate,
        SoLoud::time aTime,
        unsigned int aChannel,
        unsigned int aChannels) override;

private:
    void updateBandCoeffs(int band);

    std::array<GraphicEqBandCoeffs, kGraphicEqBandCount> coeffs_{};
    std::array<std::array<GraphicEqBandState, kGraphicEqBandCount>, MAX_CHANNELS> state_{};
    float samplerate_ = 44100.0f;
};

class GraphicEqFilter : public SoLoud::Filter {
public:
    int getParamCount() override;
    const char* getParamName(unsigned int aParamIndex) override;
    unsigned int getParamType(unsigned int aParamIndex) override;
    float getParamMax(unsigned int aParamIndex) override;
    float getParamMin(unsigned int aParamIndex) override;

    GraphicEqFilterInstance* createInstance() override;
};

}
