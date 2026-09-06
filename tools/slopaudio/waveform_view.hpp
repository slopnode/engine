#pragma once

#include "assets/asset_store.hpp"
#include "assets/audio_def.hpp"

#include "imgui.h"

#include <string>
#include <vector>

namespace slopaudio {

struct WaveformData {
    std::vector<float> samples;
    float sampleRate = 44100.0f;
};

/** Renders a throwaway SoLoud::Sfxr instance to samples for waveform display. */
WaveformData bakeSaudioWaveform(const slopengine::SaudioParams& params, unsigned int maxSamples = 88200);

/** Decodes a sound/ clip (.ogg or .wav) to mono samples for waveform display. */
WaveformData bakeClipWaveform(
    slopengine::AssetStore& assets,
    const std::string& virtualPath,
    unsigned int maxSamples = 441000);

/**
 * Draws a zoomable, horizontally scrollable waveform with a millisecond ruler, filling `size`
 * at the current ImGui cursor. `windowMs` is the visible time span at 1x scroll and is mutable
 * via the +/- controls this function draws (50ms steps); it does not reset when `data` changes.
 */
void drawWaveformPanel(
    const WaveformData& data,
    float& windowMs,
    float playheadFraction,
    ImVec2 size);

/** Runs SoLoud::Sfxr::loadPreset and reads the generated params back out, so the Inspector's
 *  sliders can show the concrete values a preset produces instead of leaving them untouched. */
slopengine::SaudioParams materializePreset(int preset, int seed);

}
