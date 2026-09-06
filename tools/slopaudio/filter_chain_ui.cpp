#include "filter_chain_ui.hpp"

#include "audio/audio_world.hpp"

#include "imgui.h"

#include <memory>
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

// One param widget, its kind (slider/int-slider/checkbox) driven entirely by SoLoud's own
// Filter::getParamType/Min/Max -- new builtin filters get a working UI with no changes here.
bool drawFilterParam(
    unsigned int slot,
    SoLoud::Filter& filter,
    unsigned int paramIndex,
    float& value,
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
        if (ImGui::SliderInt(name, &intValue, static_cast<int>(lo), static_cast<int>(hi))) {
            value = static_cast<float>(intValue);
            changed = true;
        }
    } else {
        changed = ImGui::SliderFloat(name, &value, lo, hi);
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
        for (int i = 0; i < count; ++i) {
            drawFilterParam(slot, *filter, static_cast<unsigned int>(i), slotDoc.params[static_cast<std::size_t>(i)], onParamChanged);
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
