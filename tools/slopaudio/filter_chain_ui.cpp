#include "filter_chain_ui.hpp"

#include "audio/audio_world.hpp"

#include "ui/icon_ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>

using slopengine::AudioWorld;
using slopengine::BusFilterSlot;

namespace slopaudio {

namespace {

// Assumes (and preserves) the invariant that occupied slots are a contiguous prefix of
// `slots` -- add/remove/reorder below always keep it that way.
unsigned int activeFilterCount(const std::array<BusFilterSlot, slopengine::kMaxFiltersPerBus>& slots) {
    unsigned int count = 0;
    while (count < slots.size() && !slots[count].name.empty()) {
        ++count;
    }
    return count;
}

float toPercent(float raw) { return raw * 100.0f; }
float fromPercent(float pct) { return pct / 100.0f; }

float toMilliseconds(float seconds) { return seconds * 1000.0f; }
float fromMilliseconds(float ms) { return ms / 1000.0f; }

float toDecibels(float gain) { return 20.0f * std::log10(std::max(gain, 0.001f)); }
float fromDecibels(float db) { return db <= -59.9f ? 0.0f : std::pow(10.0f, db / 20.0f); }

struct ParamDisplay {
    const char* label;
    const char* format;
    float (*toDisplay)(float);
    float (*toRaw)(float);
};

// Reinterprets SoLoud's raw per-filter dials (mostly bare 0..1/0..N multipliers) as labels
// and units an audio editor would expect. Falls back to SoLoud's own name/range for anything
// not called out here, so unrecognized filters and params still get a working, if plainer, UI.
ParamDisplay paramDisplay(std::string_view filterName, unsigned int paramIndex, const char* soloudName) {
    if (std::string_view(soloudName) == "Wet") {
        return {"Mix", "%.0f%%", toPercent, fromPercent};
    }
    if (filterName == "echo") {
        if (paramIndex == 1) return {"Delay", "%.0f ms", toMilliseconds, fromMilliseconds};
        if (paramIndex == 2) return {"Feedback", "%.0f%%", toPercent, fromPercent};
        if (paramIndex == 3) return {"Damping", "%.0f%%", toPercent, fromPercent};
    } else if (filterName == "biquad") {
        if (paramIndex == 2) return {"Cutoff", "%.0f Hz", nullptr, nullptr};
        if (paramIndex == 3) return {"Resonance (Q)", "%.2f", nullptr, nullptr};
    } else if (filterName == "eq") {
        if (paramIndex >= 1) return {soloudName, "%.1f dB", toDecibels, fromDecibels};
    } else if (filterName == "lofi") {
        if (paramIndex == 1) return {"Sample Rate", "%.0f Hz", nullptr, nullptr};
        if (paramIndex == 2) return {"Bit Depth", "%.1f bit", nullptr, nullptr};
    } else if (filterName == "flanger") {
        if (paramIndex == 1) return {"Delay", "%.1f ms", toMilliseconds, fromMilliseconds};
        if (paramIndex == 2) return {"Rate", "%.2f Hz", nullptr, nullptr};
    } else if (filterName == "bassboost") {
        if (paramIndex == 1) return {"Boost", "%.1f dB", toDecibels, fromDecibels};
    } else if (filterName == "freeverb") {
        if (paramIndex == 2) return {"Room Size", "%.0f%%", toPercent, fromPercent};
        if (paramIndex == 3) return {"Damping", "%.0f%%", toPercent, fromPercent};
        if (paramIndex == 4) return {"Stereo Width", "%.0f%%", toPercent, fromPercent};
    } else if (filterName == "robotize") {
        if (paramIndex == 1) return {"Rate", "%.1f Hz", nullptr, nullptr};
    } else if (filterName == "waveshaper") {
        if (paramIndex == 1) return {"Drive", "%.0f%%", toPercent, fromPercent};
    }
    return {soloudName, "%.3f", nullptr, nullptr};
}

inline constexpr const char* kBiquadTypeItems = "Lowpass\0Highpass\0Bandpass\0";
inline constexpr const char* kRobotizeWaveItems = "Square\0Saw\0Sine\0Triangle\0Bounce\0Jaws\0Humps\0";

// Widest label among a filter's slider-rendered params (skips checkboxes and the Type/Waveform
// combos, which don't use the shared slider width), so every slider in one filter's param block
// lines up on the same column instead of each stretching to its own label.
float filterParamGroupWidth(std::string_view filterName, SoLoud::Filter& filter) {
    float maxWidth = 0.0f;
    const int count = filter.getParamCount();
    for (int i = 0; i < count; ++i) {
        const unsigned int paramIndex = static_cast<unsigned int>(i);
        const unsigned int type = filter.getParamType(paramIndex);
        if (type == SoLoud::Filter::BOOL_PARAM) {
            continue;
        }
        const char* name = filter.getParamName(paramIndex);
        if (type == SoLoud::Filter::INT_PARAM) {
            if ((filterName == "biquad" && paramIndex == 1) || (filterName == "robotize" && paramIndex == 2)) {
                continue;
            }
            maxWidth = std::max(maxWidth, ImGui::CalcTextSize(name, nullptr, true).x);
            continue;
        }
        const ParamDisplay display = paramDisplay(filterName, paramIndex, name);
        maxWidth = std::max(maxWidth, ImGui::CalcTextSize(display.label, nullptr, true).x);
    }
    return maxWidth;
}

// One param widget, its kind (slider/int-slider/checkbox) driven by SoLoud's own
// Filter::getParamType/Min/Max, with a per-filter label/unit/combo override from
// paramDisplay() layered on top so it reads like an audio tool instead of a raw param dump.
bool drawFilterParam(
    unsigned int slot,
    std::string_view filterName,
    SoLoud::Filter& filter,
    unsigned int paramIndex,
    float& value,
    float labelWidth,
    const FilterParamChangedFn& onParamChanged) {
    const char* name = filter.getParamName(paramIndex);
    const unsigned int type = filter.getParamType(paramIndex);
    const float lo = filter.getParamMin(paramIndex);
    const float hi = filter.getParamMax(paramIndex);

    ImGui::PushID(static_cast<int>(paramIndex));
    bool changed = false;
    if (type == SoLoud::Filter::BOOL_PARAM) {
        bool flag = value != 0.0f;
        if (ImGui::Checkbox(name, &flag)) {
            value = flag ? 1.0f : 0.0f;
            changed = true;
        }
    } else if (type == SoLoud::Filter::INT_PARAM) {
        int intValue = static_cast<int>(value);
        if (filterName == "biquad" && paramIndex == 1) {
            changed = ImGui::Combo("Type", &intValue, kBiquadTypeItems);
        } else if (filterName == "robotize" && paramIndex == 2) {
            changed = ImGui::Combo("Waveform", &intValue, kRobotizeWaveItems);
        } else {
            changed = slopengine::sliderIntWithInput(
                name, &intValue, static_cast<int>(lo), static_cast<int>(hi), 64.0f, labelWidth);
        }
        if (changed) {
            value = static_cast<float>(intValue);
        }
    } else {
        const ParamDisplay display = paramDisplay(filterName, paramIndex, name);
        if (display.toDisplay == nullptr) {
            changed = slopengine::sliderFloatWithInput(display.label, &value, lo, hi, display.format, 64.0f, labelWidth);
        } else {
            float shown = display.toDisplay(value);
            const float dispLo = display.toDisplay(lo);
            const float dispHi = display.toDisplay(hi);
            if (slopengine::sliderFloatWithInput(
                    display.label, &shown, std::min(dispLo, dispHi), std::max(dispLo, dispHi), display.format,
                    64.0f, labelWidth)) {
                value = std::clamp(display.toRaw(shown), lo, hi);
                changed = true;
            }
        }
    }
    ImGui::PopID();

    if (changed) {
        onParamChanged(slot, paramIndex, value);
    }
    return changed;
}

enum class FilterRowAction { None, MoveUp, MoveDown, Remove };

// One row in the active filter chain: name, reorder/remove controls, then its param widgets.
// Returns the action the user took so the caller can mutate `slots` and resync afterward --
// this function only reads slot state, it doesn't reshuffle slots itself.
FilterRowAction drawActiveFilterRow(
    unsigned int slot,
    unsigned int activeCount,
    BusFilterSlot& slotDoc,
    const FilterParamChangedFn& onParamChanged) {
    FilterRowAction action = FilterRowAction::None;
    ImGui::PushID(static_cast<int>(slot));

    ImGui::BeginDisabled(slot == 0);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
        action = FilterRowAction::MoveUp;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(slot + 1 >= activeCount);
    if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
        action = FilterRowAction::MoveDown;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted(slotDoc.name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) {
        action = FilterRowAction::Remove;
    }

    const std::unique_ptr<SoLoud::Filter> filter = AudioWorld::createBuiltinFilter(slotDoc.name);
    if (filter) {
        ImGui::Indent();
        const int count = filter->getParamCount();
        slotDoc.params.resize(static_cast<std::size_t>(count));
        const float labelWidth = filterParamGroupWidth(slotDoc.name, *filter);
        for (int i = 0; i < count; ++i) {
            drawFilterParam(
                slot, slotDoc.name, *filter, static_cast<unsigned int>(i),
                slotDoc.params[static_cast<std::size_t>(i)], labelWidth, onParamChanged);
        }
        ImGui::Unindent();
    }

    ImGui::PopID();
    return action;
}

} // namespace

std::vector<float> defaultFilterParams(std::string_view name) {
    const std::unique_ptr<SoLoud::Filter> filter = AudioWorld::createBuiltinFilter(name);
    if (!filter) {
        return {};
    }
    const std::unique_ptr<SoLoud::FilterInstance> instance{filter->createInstance()};
    if (!instance) {
        return {};
    }
    std::vector<float> params;
    const int count = filter->getParamCount();
    params.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        params.push_back(instance->getFilterParameter(static_cast<unsigned int>(i)));
    }
    return params;
}

bool drawFilterChain(
    std::array<BusFilterSlot, slopengine::kMaxFiltersPerBus>& slots,
    const FilterParamChangedFn& onParamChanged) {
    bool structureChanged = false;

    const unsigned int activeCount = activeFilterCount(slots);
    for (unsigned int slot = 0; slot < activeCount; ++slot) {
        const FilterRowAction action = drawActiveFilterRow(slot, activeCount, slots[slot], onParamChanged);
        if (action == FilterRowAction::MoveUp) {
            std::swap(slots[slot], slots[slot - 1]);
            structureChanged = true;
            break;
        }
        if (action == FilterRowAction::MoveDown) {
            std::swap(slots[slot], slots[slot + 1]);
            structureChanged = true;
            break;
        }
        if (action == FilterRowAction::Remove) {
            for (unsigned int j = slot; j + 1 < slopengine::kMaxFiltersPerBus; ++j) {
                slots[j] = std::move(slots[j + 1]);
            }
            slots[slopengine::kMaxFiltersPerBus - 1] = BusFilterSlot{};
            structureChanged = true;
            break;
        }
    }

    ImGui::BeginDisabled(activeCount >= slopengine::kMaxFiltersPerBus);
    if (ImGui::Button("Add Filter...")) {
        ImGui::OpenPopup("AddFilterPopup");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("AddFilterPopup")) {
        for (int i = 1; i < IM_ARRAYSIZE(kFilterNames); ++i) {
            if (ImGui::Selectable(kFilterNames[i])) {
                BusFilterSlot& newSlot = slots[activeCount];
                newSlot.name = kFilterNames[i];
                newSlot.params = defaultFilterParams(newSlot.name);
                structureChanged = true;
            }
        }
        ImGui::EndPopup();
    }

    return structureChanged;
}

}
