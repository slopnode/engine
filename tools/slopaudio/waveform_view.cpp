#include "waveform_view.hpp"

#include "audio/audio_world.hpp"

#include <soloud_filter.h>
#include <soloud_sfxr.h>
#include <soloud_wav.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>

namespace slopaudio {

namespace {

// Mirrors slopengine::AudioWorld::applySaudioParams (audio_world.cpp), which is private to
// that class. Kept in sync by hand since it's a small, stable field-by-field mapping.
void applyParams(SoLoud::Sfxr& sfxr, const slopengine::SaudioParams& params) {
    using slopengine::SaudioField_ArpMod;
    using slopengine::SaudioField_ArpSpeed;
    using slopengine::SaudioField_Attack;
    using slopengine::SaudioField_Decay;
    using slopengine::SaudioField_Duty;
    using slopengine::SaudioField_DutyRamp;
    using slopengine::SaudioField_Freq;
    using slopengine::SaudioField_FreqDramp;
    using slopengine::SaudioField_FreqLimit;
    using slopengine::SaudioField_FreqRamp;
    using slopengine::SaudioField_Hpf;
    using slopengine::SaudioField_HpfRamp;
    using slopengine::SaudioField_Lpf;
    using slopengine::SaudioField_LpfRamp;
    using slopengine::SaudioField_LpfResonance;
    using slopengine::SaudioField_MasterVol;
    using slopengine::SaudioField_PhaOffset;
    using slopengine::SaudioField_PhaRamp;
    using slopengine::SaudioField_Punch;
    using slopengine::SaudioField_RepeatSpeed;
    using slopengine::SaudioField_SoundVol;
    using slopengine::SaudioField_Sustain;
    using slopengine::SaudioField_VibDelay;
    using slopengine::SaudioField_VibSpeed;
    using slopengine::SaudioField_VibStrength;
    using slopengine::SaudioField_Wave;

    if (params.hasPreset) {
        sfxr.loadPreset(params.preset, params.presetSeed);
    } else {
        sfxr.resetParams();
    }

    const auto set = [&](std::uint32_t field) { return (params.setMask & field) != 0; };

    if (set(SaudioField_Wave)) {
        sfxr.mParams.wave_type = params.waveType;
    }
    if (set(SaudioField_Freq)) {
        sfxr.mParams.p_base_freq = params.baseFreq;
    }
    if (set(SaudioField_FreqLimit)) {
        sfxr.mParams.p_freq_limit = params.freqLimit;
    }
    if (set(SaudioField_FreqRamp)) {
        sfxr.mParams.p_freq_ramp = params.freqRamp;
    }
    if (set(SaudioField_FreqDramp)) {
        sfxr.mParams.p_freq_dramp = params.freqDramp;
    }
    if (set(SaudioField_Duty)) {
        sfxr.mParams.p_duty = params.duty;
    }
    if (set(SaudioField_DutyRamp)) {
        sfxr.mParams.p_duty_ramp = params.dutyRamp;
    }
    if (set(SaudioField_VibStrength)) {
        sfxr.mParams.p_vib_strength = params.vibStrength;
    }
    if (set(SaudioField_VibSpeed)) {
        sfxr.mParams.p_vib_speed = params.vibSpeed;
    }
    if (set(SaudioField_VibDelay)) {
        sfxr.mParams.p_vib_delay = params.vibDelay;
    }
    if (set(SaudioField_Attack)) {
        sfxr.mParams.p_env_attack = params.envAttack;
    }
    if (set(SaudioField_Sustain)) {
        sfxr.mParams.p_env_sustain = params.envSustain;
    }
    if (set(SaudioField_Decay)) {
        sfxr.mParams.p_env_decay = params.envDecay;
    }
    if (set(SaudioField_Punch)) {
        sfxr.mParams.p_env_punch = params.envPunch;
    }
    if (set(SaudioField_Lpf) || set(SaudioField_LpfResonance) || set(SaudioField_LpfRamp)) {
        sfxr.mParams.filter_on = true;
    }
    if (set(SaudioField_LpfResonance)) {
        sfxr.mParams.p_lpf_resonance = params.lpfResonance;
    }
    if (set(SaudioField_Lpf)) {
        sfxr.mParams.p_lpf_freq = params.lpfFreq;
    }
    if (set(SaudioField_LpfRamp)) {
        sfxr.mParams.p_lpf_ramp = params.lpfRamp;
    }
    if (set(SaudioField_Hpf)) {
        sfxr.mParams.p_hpf_freq = params.hpfFreq;
    }
    if (set(SaudioField_HpfRamp)) {
        sfxr.mParams.p_hpf_ramp = params.hpfRamp;
    }
    if (set(SaudioField_PhaOffset)) {
        sfxr.mParams.p_pha_offset = params.phaOffset;
    }
    if (set(SaudioField_PhaRamp)) {
        sfxr.mParams.p_pha_ramp = params.phaRamp;
    }
    if (set(SaudioField_RepeatSpeed)) {
        sfxr.mParams.p_repeat_speed = params.repeatSpeed;
    }
    if (set(SaudioField_ArpSpeed)) {
        sfxr.mParams.p_arp_speed = params.arpSpeed;
    }
    if (set(SaudioField_ArpMod)) {
        sfxr.mParams.p_arp_mod = params.arpMod;
    }
    if (set(SaudioField_SoundVol)) {
        sfxr.mParams.sound_vol = params.soundVol;
    }
    if (set(SaudioField_MasterVol)) {
        sfxr.mParams.master_vol = params.masterVol;
    }
}

} // namespace

slopengine::SaudioParams materializePreset(int preset, int seed) {
    SoLoud::Sfxr sfxr;
    sfxr.loadPreset(preset, seed);

    slopengine::SaudioParams params;
    params.hasPreset = true;
    params.preset = preset;
    params.presetSeed = seed;
    params.setMask = 0;
    params.waveType = sfxr.mParams.wave_type;
    params.baseFreq = sfxr.mParams.p_base_freq;
    params.freqLimit = sfxr.mParams.p_freq_limit;
    params.freqRamp = sfxr.mParams.p_freq_ramp;
    params.freqDramp = sfxr.mParams.p_freq_dramp;
    params.duty = sfxr.mParams.p_duty;
    params.dutyRamp = sfxr.mParams.p_duty_ramp;
    params.vibStrength = sfxr.mParams.p_vib_strength;
    params.vibSpeed = sfxr.mParams.p_vib_speed;
    params.vibDelay = sfxr.mParams.p_vib_delay;
    params.envAttack = sfxr.mParams.p_env_attack;
    params.envSustain = sfxr.mParams.p_env_sustain;
    params.envDecay = sfxr.mParams.p_env_decay;
    params.envPunch = sfxr.mParams.p_env_punch;
    params.lpfResonance = sfxr.mParams.p_lpf_resonance;
    params.lpfFreq = sfxr.mParams.p_lpf_freq;
    params.lpfRamp = sfxr.mParams.p_lpf_ramp;
    params.hpfFreq = sfxr.mParams.p_hpf_freq;
    params.hpfRamp = sfxr.mParams.p_hpf_ramp;
    params.phaOffset = sfxr.mParams.p_pha_offset;
    params.phaRamp = sfxr.mParams.p_pha_ramp;
    params.repeatSpeed = sfxr.mParams.p_repeat_speed;
    params.arpSpeed = sfxr.mParams.p_arp_speed;
    params.arpMod = sfxr.mParams.p_arp_mod;
    params.masterVol = sfxr.mParams.master_vol;
    params.soundVol = sfxr.mParams.sound_vol;
    return params;
}

WaveformData bakeSaudioWaveform(const slopengine::SaudioParams& params, unsigned int maxSamples) {
    WaveformData data;

    SoLoud::Sfxr sfxr;
    applyParams(sfxr, params);
    SoLoud::AudioSourceInstance* instance = sfxr.createInstance();
    if (instance == nullptr) {
        return data;
    }

    data.sampleRate = sfxr.mBaseSamplerate > 0.0f ? sfxr.mBaseSamplerate : 44100.0f;
    data.samples.reserve(maxSamples);
    constexpr unsigned int kChunk = 512;
    float buffer[kChunk];
    while (data.samples.size() < maxSamples) {
        const unsigned int got = instance->getAudio(buffer, kChunk, kChunk);
        if (got == 0) {
            break;
        }
        data.samples.insert(data.samples.end(), buffer, buffer + got);
        if (instance->hasEnded()) {
            break;
        }
    }
    delete instance;
    return data;
}

WaveformData bakeClipWaveform(
    slopengine::AssetStore& assets,
    const std::string& virtualPath,
    unsigned int maxSamples) {
    WaveformData data;

    const auto resolved = assets.resolveSoundPath(virtualPath);
    if (!resolved) {
        return data;
    }

    SoLoud::Wav wav;
    if (wav.load(resolved->string().c_str()) != SoLoud::SO_NO_ERROR) {
        return data;
    }

    data.sampleRate = wav.mBaseSamplerate > 0.0f ? wav.mBaseSamplerate : 44100.0f;
    data.channels = std::max(1u, wav.mChannels);
    const unsigned int frames = std::min(wav.mSampleCount, maxSamples);
    data.samples.resize(static_cast<std::size_t>(frames) * data.channels);
    for (unsigned int ch = 0; ch < data.channels; ++ch) {
        const float* src = wav.mData + static_cast<std::size_t>(ch) * wav.mSampleCount;
        float* dst = data.samples.data() + static_cast<std::size_t>(ch) * frames;
        std::copy(src, src + frames, dst);
    }
    return data;
}

WaveformData applyFilterChain(
    const WaveformData& dry,
    const std::array<slopengine::BusFilterSlot, slopengine::kMaxFiltersPerBus>& filters) {
    if (dry.samples.empty() || !slopengine::hasAnyFilterSlots(filters)) {
        return {};
    }

    WaveformData wet = dry;
    // Some builtin filters (e.g. Freeverb) hard-assert on exactly 2 channels, matching how
    // SoLoud's live engine always mixes voices to stereo before bus filters run. Upmix mono
    // sources here so this offline pass sees the same channel count real playback would.
    if (wet.channels < 2) {
        const std::size_t frames = wet.samples.size();
        std::vector<float> stereo(frames * 2u);
        std::copy(wet.samples.begin(), wet.samples.end(), stereo.begin());
        std::copy(wet.samples.begin(), wet.samples.end(), stereo.begin() + static_cast<std::ptrdiff_t>(frames));
        wet.samples = std::move(stereo);
        wet.channels = 2;
    }
    const unsigned int channels = wet.channels;
    const unsigned int frames = static_cast<unsigned int>(wet.samples.size() / channels);
    for (const slopengine::BusFilterSlot& slot : filters) {
        if (slot.name.empty()) {
            continue;
        }
        const std::unique_ptr<SoLoud::Filter> filter = slopengine::AudioWorld::createBuiltinFilter(slot.name);
        if (!filter) {
            continue;
        }
        const std::unique_ptr<SoLoud::FilterInstance> instance{filter->createInstance()};
        if (!instance) {
            continue;
        }
        for (unsigned int p = 0; p < slot.params.size(); ++p) {
            instance->setFilterParameter(p, slot.params[p]);
        }
        // aBufferSize == frames: filter()'s per-channel stride into `samples`, matching how we
        // laid the buffer out (channel-major, same as SoLoud::Wav itself).
        instance->filter(wet.samples.data(), frames, frames, channels, wet.sampleRate, 0.0);
    }
    return wet;
}

namespace {

// Rounds `rawStepMs` up to a "nice" 1/2/5 * 10^n value so ruler labels land on tidy numbers.
float niceTimeStepMs(float rawStepMs) {
    if (rawStepMs <= 0.0f) {
        return 1.0f;
    }
    const float magnitude = std::pow(10.0f, std::floor(std::log10(rawStepMs)));
    const float fraction = rawStepMs / magnitude;
    float niceFraction = 10.0f;
    if (fraction <= 1.0f) {
        niceFraction = 1.0f;
    } else if (fraction <= 2.0f) {
        niceFraction = 2.0f;
    } else if (fraction <= 5.0f) {
        niceFraction = 5.0f;
    }
    return niceFraction * magnitude;
}

} // namespace

void drawWaveformPanel(
    const WaveformData& sample,
    const WaveformData& filtered,
    bool& showSample,
    bool& showFiltered,
    bool& splitChannels,
    float& windowMs,
    float playheadFraction,
    ImVec2 size) {
    constexpr float kMinWindowMs = 50.0f;
    constexpr float kMaxWindowMs = 20000.0f;
    constexpr float kButtonZoomStep = 1.25f;
    constexpr float kWheelZoomStep = 1.1f;
    constexpr float kRulerHeight = 18.0f;
    constexpr float kYAxisWidth = 34.0f;
    constexpr float kLaneGap = 6.0f;
    constexpr float kAmplitudeLevels[] = {1.0f, 0.5f, 0.0f, -0.5f, -1.0f};

    windowMs = std::clamp(windowMs, kMinWindowMs, kMaxWindowMs);

    const bool canSplit =
        (sample.channels >= 2 && !sample.samples.empty()) ||
        (filtered.channels >= 2 && !filtered.samples.empty());
    const bool effectiveSplit = splitChannels && canSplit;

    ImGui::Text("Window:");
    ImGui::SameLine();
    if (ImGui::SmallButton("-##windowshrink")) {
        windowMs = std::max(kMinWindowMs, windowMs / kButtonZoomStep);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(56.0f);
    float windowMsInput = windowMs;
    if (ImGui::InputFloat("##windowms", &windowMsInput, 0.0f, 0.0f, "%.0f", ImGuiInputTextFlags_EnterReturnsTrue)) {
        windowMs = std::clamp(windowMsInput, kMinWindowMs, kMaxWindowMs);
    }
    ImGui::SameLine();
    ImGui::Text("ms");
    ImGui::SameLine();
    if (ImGui::SmallButton("+##windowgrow")) {
        windowMs = std::min(kMaxWindowMs, windowMs * kButtonZoomStep);
    }
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine();
    ImGui::Checkbox("Sample", &showSample);
    ImGui::SameLine();
    ImGui::Checkbox("Filtered", &showFiltered);
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine();
    ImGui::BeginDisabled(!canSplit);
    ImGui::Checkbox("Split L/R", &splitChannels);
    ImGui::EndDisabled();
    if (!canSplit && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mono source");
    }

    // Both waveforms are baked from the same source and stay the same length regardless of
    // filters (applyFilterChain processes in place), so either can stand in for total duration.
    const WaveformData& reference = !sample.samples.empty() ? sample : filtered;
    const unsigned int referenceChannels = std::max(1u, reference.channels);
    const float totalMs = reference.sampleRate > 0.0f
        ? static_cast<float>(reference.samples.size() / referenceChannels) / reference.sampleRate * 1000.0f
        : 0.0f;

    const float fullWidth = std::max(1.0f, size.x);
    const float availWidth = std::max(1.0f, fullWidth - kYAxisWidth);
    const float childHeight = std::max(40.0f, size.y - ImGui::GetFrameHeightWithSpacing());

    // Fixed Y-axis gutter, reserved outside the horizontally-scrolling child so it stays put
    // while the waveform pans left/right. The labels themselves are drawn further down, once
    // the child's real content origin (which includes its own window padding) is known, so
    // they land exactly on the grid lines drawn inside the child.
    ImDrawList* axisDraw = ImGui::GetWindowDrawList();
    const ImVec2 axisOrigin = ImGui::GetCursorScreenPos();

    // Mouse wheel over the wave area zooms (in/out of the window span) rather than panning --
    // panning stays on the horizontal scrollbar drag, since ImGuiWindowFlags_NoScrollWithMouse
    // keeps the wheel from also scrolling the child. Checked against the child's screen rect
    // before it exists this frame so the new windowMs is already in effect for this frame's draw.
    const ImVec2 waveScreenMin(axisOrigin.x + kYAxisWidth, axisOrigin.y);
    const ImVec2 waveScreenMax(waveScreenMin.x + availWidth, waveScreenMin.y + childHeight);
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f && ImGui::IsMouseHoveringRect(waveScreenMin, waveScreenMax)) {
        windowMs = std::clamp(windowMs * std::pow(kWheelZoomStep, -wheel), kMinWindowMs, kMaxWindowMs);
    }

    const float pixelsPerMs = availWidth / windowMs;
    const float contentWidth = std::max(availWidth, totalMs * pixelsPerMs);
    const float waveHeight = childHeight - kRulerHeight;

    axisDraw->AddRectFilled(
        axisOrigin, ImVec2(axisOrigin.x + kYAxisWidth, axisOrigin.y + childHeight), IM_COL32(18, 18, 22, 255));
    ImGui::Dummy(ImVec2(kYAxisWidth, childHeight));
    ImGui::SameLine(0.0f, 0.0f);

    // Zero window padding for the scroll child: its content Dummy below is sized to exactly
    // fill `childHeight`, so any padding would push the true content height past that and pop a
    // needless vertical scrollbar even though nothing is actually cut off.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild(
        "##wavescroll",
        ImVec2(availWidth, childHeight),
        false,
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(contentWidth, childHeight));

    draw->AddRectFilled(
        origin, ImVec2(origin.x + contentWidth, origin.y + childHeight), IM_COL32(18, 18, 22, 255));

    const float scrollX = ImGui::GetScrollX();
    const float visibleStartPx = scrollX;
    const float visibleEndPx = std::min(contentWidth, scrollX + availWidth);

    // Ruler: pick a "nice" label step so ticks land ~70px apart regardless of zoom.
    const float rawStepMs = 70.0f / std::max(0.0001f, pixelsPerMs);
    const float stepMs = niceTimeStepMs(rawStepMs);
    const float rulerY0 = origin.y;
    const float rulerY1 = origin.y + kRulerHeight;
    const int firstTick = static_cast<int>(std::floor((visibleStartPx / pixelsPerMs) / stepMs)) - 1;
    const int lastTick = static_cast<int>(std::ceil((visibleEndPx / pixelsPerMs) / stepMs)) + 1;
    for (int tick = std::max(0, firstTick); tick <= lastTick; ++tick) {
        const float tickMs = static_cast<float>(tick) * stepMs;
        const float x = origin.x + tickMs * pixelsPerMs;
        if (x < origin.x + visibleStartPx - 4.0f || x > origin.x + visibleEndPx + 4.0f) {
            continue;
        }
        draw->AddLine(ImVec2(x, rulerY1 - 6.0f), ImVec2(x, rulerY1), IM_COL32(120, 120, 130, 255));
        char label[32];
        std::snprintf(label, sizeof(label), "%.0fms", tickMs);
        draw->AddText(ImVec2(x + 2.0f, rulerY0), IM_COL32(150, 150, 158, 255), label);
        draw->AddLine(ImVec2(x, rulerY1), ImVec2(x, origin.y + childHeight), IM_COL32(40, 40, 46, 255));
    }
    draw->AddLine(
        ImVec2(origin.x, rulerY1), ImVec2(origin.x + contentWidth, rulerY1), IM_COL32(60, 60, 68, 255));

    const float waveTop = rulerY1;

    // One lane == one horizontal strip of the wave area with its own zero line and amplitude
    // scale. Mono (or "Split L/R" off) is a single lane spanning the whole area; a split stereo
    // view is two lanes stacked with a gap between them. `channel` selects which channel of a
    // WaveformData a lane draws: -1 means "downmix all channels" (used by the single mono lane),
    // otherwise it's a concrete channel index (0 = L, 1 = R) for a split lane.
    struct Lane {
        float midY;
        float outerHalfHeight;
        float innerHalfHeight;
        int channel;
        const char* label;
    };
    constexpr float kAmplitudeHeadroom = 0.15f;
    std::vector<Lane> lanes;
    if (effectiveSplit) {
        const float laneHeight = (waveHeight - kLaneGap) * 0.5f;
        for (int i = 0; i < 2; ++i) {
            const float laneTop = waveTop + static_cast<float>(i) * (laneHeight + kLaneGap);
            const float laneMidY = laneTop + laneHeight * 0.5f;
            const float outerHalf = laneHeight * 0.5f - 2.0f;
            lanes.push_back(Lane{laneMidY, outerHalf, outerHalf * (1.0f - kAmplitudeHeadroom), i, i == 0 ? "L" : "R"});
        }
        const float sepY = waveTop + laneHeight + kLaneGap * 0.5f;
        draw->AddLine(
            ImVec2(origin.x, sepY), ImVec2(origin.x + contentWidth, sepY), IM_COL32(60, 60, 68, 255));
    } else {
        const float midY = waveTop + waveHeight * 0.5f;
        // outerHalfHeight is the true edge of the wave area; innerHalfHeight (where the +-1.0
        // grid lines sit) is inset from it, leaving headroom so a sample that clips past +-1.0
        // has somewhere to visibly bulge into instead of being invisible against the edge.
        const float outerHalf = waveHeight * 0.5f - 2.0f;
        lanes.push_back(Lane{midY, outerHalf, outerHalf * (1.0f - kAmplitudeHeadroom), -1, nullptr});
    }

    for (const Lane& lane : lanes) {
        for (float level : kAmplitudeLevels) {
            const float y = lane.midY - level * lane.innerHalfHeight;
            const bool isZero = level == 0.0f;
            draw->AddLine(
                ImVec2(origin.x, y),
                ImVec2(origin.x + contentWidth, y),
                isZero ? IM_COL32(60, 60, 68, 255) : IM_COL32(38, 38, 44, 255));
        }
        if (lane.label != nullptr) {
            draw->AddText(
                ImVec2(origin.x + 4.0f, lane.midY - lane.outerHalfHeight + 2.0f),
                IM_COL32(150, 150, 158, 255),
                lane.label);
        }
    }

    // Draws one series (one channel, or a downmix of all channels when lane.channel < 0) as a
    // column of min/max vertical bars into `lane`; called once per enabled waveform per lane so
    // Sample and Filtered can be overlaid in different colors, and L/R can be drawn separately.
    const auto drawSeries = [&](const WaveformData& data, const Lane& lane, ImU32 color) {
        const unsigned int dataChannels = std::max(1u, data.channels);
        const std::size_t frameCount = data.samples.size() / dataChannels;
        if (frameCount == 0) {
            return;
        }
        const unsigned int fixedChannel =
            lane.channel >= 0 ? std::min(static_cast<unsigned int>(lane.channel), dataChannels - 1) : 0;
        const auto valueAt = [&](std::size_t frame) -> float {
            if (lane.channel >= 0) {
                return data.samples[static_cast<std::size_t>(fixedChannel) * frameCount + frame];
            }
            float sum = 0.0f;
            for (unsigned int ch = 0; ch < dataChannels; ++ch) {
                sum += data.samples[static_cast<std::size_t>(ch) * frameCount + frame];
            }
            return sum / static_cast<float>(dataChannels);
        };
        const int colStart = std::max(0, static_cast<int>(std::floor(visibleStartPx)));
        const int colEnd = static_cast<int>(std::ceil(visibleEndPx));
        for (int col = colStart; col < colEnd; ++col) {
            const float msAtCol = static_cast<float>(col) / pixelsPerMs;
            const float msAtColEnd = static_cast<float>(col + 1) / pixelsPerMs;
            const auto toFrameIndex = [&](float ms) -> std::size_t {
                return static_cast<std::size_t>(std::max(0.0f, ms) / 1000.0f * data.sampleRate);
            };
            const std::size_t start = toFrameIndex(msAtCol);
            if (start >= frameCount) {
                break;
            }
            const std::size_t end = std::min(frameCount, toFrameIndex(msAtColEnd) + 1);
            if (start >= end) {
                continue;
            }
            float lo = valueAt(start);
            float hi = lo;
            for (std::size_t i = start; i < end; ++i) {
                const float v = valueAt(i);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            // Not clamped to +-1.0 here: a sample that clips past that range should visibly
            // bulge into the headroom above/below the +-1.0 grid line rather than being
            // silently flattened against it. Only the final pixel position is clamped, to the
            // wave area's true edge, so extreme outliers still can't escape the widget.
            const float x = origin.x + static_cast<float>(col);
            const float y0 = std::clamp(
                lane.midY - hi * lane.innerHalfHeight, lane.midY - lane.outerHalfHeight, lane.midY + lane.outerHalfHeight);
            const float y1 = std::clamp(
                lane.midY - lo * lane.innerHalfHeight, lane.midY - lane.outerHalfHeight, lane.midY + lane.outerHalfHeight);
            draw->AddLine(ImVec2(x, y0), ImVec2(x, std::max(y1, y0 + 1.0f)), color);
        }
    };

    const bool drawnSample = showSample && !sample.samples.empty();
    const bool drawnFiltered = showFiltered && !filtered.samples.empty();
    if ((drawnSample || drawnFiltered) && totalMs > 0.0f) {
        for (const Lane& lane : lanes) {
            // Filtered drawn second (on top) since it's the one being compared against.
            if (drawnSample) {
                drawSeries(sample, lane, IM_COL32(120, 190, 255, 220));
            }
            if (drawnFiltered) {
                drawSeries(filtered, lane, IM_COL32(255, 130, 120, 220));
            }
        }
    } else {
        const char* label = showSample || showFiltered ? "No audio" : "Nothing selected";
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        draw->AddText(
            ImVec2(origin.x + (availWidth - textSize.x) * 0.5f, waveTop + (waveHeight - textSize.y) * 0.5f),
            IM_COL32(140, 140, 148, 255),
            label);
    }

    if (playheadFraction >= 0.0f && playheadFraction <= 1.0f && totalMs > 0.0f) {
        const float playMs = playheadFraction * totalMs;
        const float x = origin.x + playMs * pixelsPerMs;
        draw->AddLine(ImVec2(x, waveTop), ImVec2(x, origin.y + childHeight), IM_COL32(255, 200, 80, 255), 2.0f);
    }

    ImGui::EndChild();

    // Draw the Y-axis tick marks + labels now, reusing the exact per-lane midY/innerHalfHeight
    // the grid lines above used, so they land on the same rows. This has to happen after
    // EndChild() because origin (and everything derived from it) only became known once the
    // child's own content-region cursor — offset from axisOrigin by the child's window padding
    // — existed.
    for (const Lane& lane : lanes) {
        for (float level : kAmplitudeLevels) {
            const float y = lane.midY - level * lane.innerHalfHeight;
            char label[8];
            std::snprintf(label, sizeof(label), "%.1f", level);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            axisDraw->AddLine(
                ImVec2(axisOrigin.x + kYAxisWidth - 5.0f, y),
                ImVec2(axisOrigin.x + kYAxisWidth, y),
                IM_COL32(120, 120, 130, 255));
            axisDraw->AddText(
                ImVec2(axisOrigin.x + kYAxisWidth - 8.0f - textSize.x, y - textSize.y * 0.5f),
                IM_COL32(150, 150, 158, 255),
                label);
        }
    }
}

}
