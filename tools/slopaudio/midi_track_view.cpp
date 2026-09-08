#include "midi_track_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace slopaudio {

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

const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

} // namespace

void drawMidiChannelLegend(const MidiTrackView& midi, float width) {
    if (midi.channels.empty()) {
        ImGui::TextDisabled("No channels");
        return;
    }

    const float windowVisibleX2 = ImGui::GetCursorScreenPos().x + width;
    for (std::size_t i = 0; i < midi.channels.size(); ++i) {
        const MidiChannelTrack& track = midi.channels[i];
        const ImU32 color = midiChannelColor(track.channel);
        const std::string label = track.channel == 9
            ? std::string("Drums")
            : ("Ch " + std::to_string(track.channel + 1) + ": " + gmProgramName(track.program));
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        const float chipWidth = 12.0f + 4.0f + textSize.x;

        if (i > 0) {
            const float nextX2 = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + chipWidth;
            if (nextX2 < windowVisibleX2) {
                ImGui::SameLine(0.0f, 16.0f);
            }
        }

        ImGui::BeginGroup();
        const ImVec2 swatchPos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            swatchPos, ImVec2(swatchPos.x + 12.0f, swatchPos.y + textSize.y), color, 2.0f);
        ImGui::Dummy(ImVec2(12.0f, textSize.y));
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndGroup();
    }
}

ImU32 midiChannelColor(int channel) {
    if (channel == 9) {
        return IM_COL32(180, 182, 190, 235);
    }
    const float hue = std::fmod(static_cast<float>(channel) * 0.6180339887f, 1.0f);
    float r = 0.0f, g = 0.0f, b = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, 0.62f, 0.95f, r, g, b);
    return IM_COL32(
        static_cast<int>(r * 255.0f), static_cast<int>(g * 255.0f), static_cast<int>(b * 255.0f), 235);
}

void drawMidiTrackPanel(const MidiTrackView& midi, float& windowMs, float playheadFraction, ImVec2 size) {
    constexpr float kMinWindowMs = 200.0f;
    constexpr float kMaxWindowMs = 120000.0f;
    constexpr float kButtonZoomStep = 1.25f;
    constexpr float kWheelZoomStep = 1.1f;
    constexpr float kRulerHeight = 18.0f;
    constexpr float kYAxisWidth = 40.0f;

    windowMs = std::clamp(windowMs, kMinWindowMs, kMaxWindowMs);

    ImGui::Text("Window:");
    ImGui::SameLine();
    if (ImGui::SmallButton("-##midiwindowshrink")) {
        windowMs = std::max(kMinWindowMs, windowMs / kButtonZoomStep);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64.0f);
    float windowMsInput = windowMs;
    if (ImGui::InputFloat("##midiwindowms", &windowMsInput, 0.0f, 0.0f, "%.0f", ImGuiInputTextFlags_EnterReturnsTrue)) {
        windowMs = std::clamp(windowMsInput, kMinWindowMs, kMaxWindowMs);
    }
    ImGui::SameLine();
    ImGui::Text("ms");
    ImGui::SameLine();
    if (ImGui::SmallButton("+##midiwindowgrow")) {
        windowMs = std::min(kMaxWindowMs, windowMs * kButtonZoomStep);
    }

    // Pitch range actually used, padded by a couple semitones so notes at the extremes aren't
    // drawn flush against the top/bottom edge. Falls back to two octaves around middle C when
    // there's nothing to show yet.
    int minKey = 60;
    int maxKey = 60;
    bool any = false;
    for (const MidiChannelTrack& track : midi.channels) {
        for (const MidiNote& note : track.notes) {
            if (!any) {
                minKey = maxKey = note.key;
                any = true;
            } else {
                minKey = std::min(minKey, static_cast<int>(note.key));
                maxKey = std::max(maxKey, static_cast<int>(note.key));
            }
        }
    }
    if (!any) {
        minKey = 48;
        maxKey = 72;
    }
    minKey = std::clamp(minKey - 2, 0, 127);
    maxKey = std::clamp(maxKey + 2, 0, 127);
    const int keyCount = maxKey - minKey + 1;

    const float totalMs = midi.durationSec * 1000.0f;
    const float fullWidth = std::max(1.0f, size.x);
    const float availWidth = std::max(1.0f, fullWidth - kYAxisWidth);
    const float childHeight = std::max(60.0f, size.y - ImGui::GetFrameHeightWithSpacing());

    ImDrawList* axisDraw = ImGui::GetWindowDrawList();
    const ImVec2 axisOrigin = ImGui::GetCursorScreenPos();

    const ImVec2 rollScreenMin(axisOrigin.x + kYAxisWidth, axisOrigin.y);
    const ImVec2 rollScreenMax(rollScreenMin.x + availWidth, rollScreenMin.y + childHeight);
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f && ImGui::IsMouseHoveringRect(rollScreenMin, rollScreenMax)) {
        windowMs = std::clamp(windowMs * std::pow(kWheelZoomStep, -wheel), kMinWindowMs, kMaxWindowMs);
    }

    const float pixelsPerMs = availWidth / windowMs;
    const float contentWidth = std::max(availWidth, totalMs * pixelsPerMs);
    const float rollHeight = childHeight - kRulerHeight;
    const float semitoneHeight = rollHeight / static_cast<float>(keyCount);

    axisDraw->AddRectFilled(
        axisOrigin, ImVec2(axisOrigin.x + kYAxisWidth, axisOrigin.y + childHeight), IM_COL32(18, 18, 22, 255));
    ImGui::Dummy(ImVec2(kYAxisWidth, childHeight));
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild(
        "##midiscroll",
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

    const float rollTop = rulerY1;
    const auto yForKey = [&](int key) {
        return rollTop + static_cast<float>(maxKey - key) * semitoneHeight;
    };

    // Octave (C-note) gridlines double as the pitch axis's labeled rows, mirroring the amplitude
    // gridlines in drawWaveformPanel.
    for (int key = minKey; key <= maxKey; ++key) {
        if (key % 12 != 0) {
            continue;
        }
        const float y = yForKey(key);
        draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + contentWidth, y), IM_COL32(45, 45, 52, 255));
    }

    bool drewAnyNote = false;
    for (const MidiChannelTrack& track : midi.channels) {
        const ImU32 baseColor = midiChannelColor(track.channel);
        for (const MidiNote& note : track.notes) {
            const float startPx = note.startSec * 1000.0f * pixelsPerMs;
            const float endPx = note.endSec * 1000.0f * pixelsPerMs;
            if (endPx < visibleStartPx || startPx > visibleEndPx) {
                continue;
            }
            const float x0 = origin.x + startPx;
            const float x1 = std::max(x0 + 2.0f, origin.x + endPx);
            const float y0 = yForKey(static_cast<int>(note.key)) + 1.0f;
            const float y1 = y0 + std::max(1.0f, semitoneHeight - 2.0f);
            const int alpha = 130 + (static_cast<int>(note.velocity) * 100) / 127;
            const ImU32 color = (baseColor & 0x00FFFFFFu) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << 24);
            draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color, 1.5f);
            drewAnyNote = true;
        }
    }

    if (!drewAnyNote) {
        const char* label = "No notes";
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        draw->AddText(
            ImVec2(origin.x + (availWidth - textSize.x) * 0.5f, rollTop + (rollHeight - textSize.y) * 0.5f),
            IM_COL32(140, 140, 148, 255),
            label);
    }

    if (playheadFraction >= 0.0f && playheadFraction <= 1.0f && totalMs > 0.0f) {
        const float playMs = playheadFraction * totalMs;
        const float x = origin.x + playMs * pixelsPerMs;
        draw->AddLine(ImVec2(x, rollTop), ImVec2(x, origin.y + childHeight), IM_COL32(255, 200, 80, 255), 2.0f);
    }

    ImGui::EndChild();

    // Pitch axis labels, drawn after EndChild for the same reason drawWaveformPanel draws its
    // amplitude labels after EndChild: the child's true content origin isn't known until then.
    for (int key = minKey; key <= maxKey; ++key) {
        if (key % 12 != 0) {
            continue;
        }
        const float y = yForKey(key);
        char label[16];
        std::snprintf(label, sizeof(label), "%s%d", kNoteNames[key % 12], key / 12 - 1);
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
