#pragma once

#include "assets/asset_store.hpp"
#include "assets/audio_def.hpp"

#include "imgui.h"

#include <array>
#include <string>
#include <vector>

namespace slopaudio {

struct WaveformData {
    // Channel-major: samples[ch * frameCount + i], frameCount == samples.size() / channels.
    // Matches SoLoud::Wav's own buffer layout and what SoLoud::FilterInstance::filter expects.
    std::vector<float> samples;
    float sampleRate = 44100.0f;
    unsigned int channels = 1;
};

/** Renders a throwaway SoLoud::Sfxr instance to samples for waveform display. */
WaveformData bakeSaudioWaveform(const slopengine::SaudioParams& params, unsigned int maxSamples = 88200);

/** Decodes a sounds/ clip (.ogg) to per-channel samples for waveform display. */
WaveformData bakeClipWaveform(
    slopengine::AssetStore& assets,
    const std::string& virtualPath,
    unsigned int maxSamples = 441000);

/** Runs `dry` through a filter chain offline -- a single pass over the whole buffer, not the
 *  live realtime chunked path -- to approximate what SoLoud's per-def filters sound like.
 *  Empty slots are skipped. Returns an empty WaveformData if the chain has no filters
 *  attached or `dry` has no samples, so callers can tell "nothing to show" from "silence". */
WaveformData applyFilterChain(
    const WaveformData& dry,
    const std::array<slopengine::BusFilterSlot, slopengine::kMaxFiltersPerBus>& filters);

/**
 * Draws a zoomable, horizontally scrollable waveform with a millisecond ruler, filling `size`
 * at the current ImGui cursor. `windowMs` is the visible time span at 1x scroll and is mutable
 * via the +/- buttons, the ms input box, or mouse wheel over the wave area (all zoom by a
 * percentage of the current span, so they stay usable from 50ms up to 20s); this function never
 * resets it on its own, even when the data changes -- callers that want a "fit to content" reset
 * (e.g. on loading a new sound) set `windowMs` themselves before calling.
 * `sample`/`filtered` are drawn as independently toggleable overlays (blue/red) via the
 * `showSample`/`showFiltered` checkboxes this function draws at the top. The checkboxes stay
 * settable even when their data is currently empty, so the on/off preference survives across
 * filter changes instead of resetting; a checked-but-empty toggle simply draws nothing.
 * `splitChannels` toggles between a single downmixed-to-mono lane and two stacked lanes (L on
 * top, R on bottom); the checkbox is disabled when neither `sample` nor `filtered` has more
 * than one channel, since there is nothing to split.
 */
void drawWaveformPanel(
    const WaveformData& sample,
    const WaveformData& filtered,
    bool& showSample,
    bool& showFiltered,
    bool& splitChannels,
    float& windowMs,
    float playheadFraction,
    ImVec2 size);

/** Runs SoLoud::Sfxr::loadPreset and reads the generated params back out, so the Inspector's
 *  sliders can show the concrete values a preset produces instead of leaving them untouched. */
slopengine::SaudioParams materializePreset(int preset, int seed);

}
