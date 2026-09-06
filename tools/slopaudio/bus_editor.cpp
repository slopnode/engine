#include "bus_editor.hpp"

#include "assets/asset_store.hpp"
#include "assets/saudio_loader.hpp"
#include "audio/audio_world.hpp"

#include "imgui.h"

#include <array>
#include <fstream>
#include <memory>
#include <utility>

using slopengine::AssetStore;
using slopengine::AudioBusKind;
using slopengine::AudioDefBus;
using slopengine::AudioWorld;
using slopengine::BusConfig;
using slopengine::BusDef;
using slopengine::BusFilterSlot;

namespace slopaudio {

namespace {

constexpr const char* kBusLabels[] = {"Sfx", "Music", "Ui", "Hud", "Voices", "Ambience", "Player", "View"};
// Index 0 ("none") is a placeholder skipped by the Add Filter popup; the rest match
// AudioWorld::createBuiltinFilter's names.
constexpr const char* kFilterNames[] =
    {"none", "echo", "biquad", "lofi", "flanger", "bassboost", "freeverb", "robotize", "waveshaper", "dcremoval"};

// A BusDef loaded from disk could in principle have gaps (e.g. a hand-edited file with only
// slot 3 occupied) -- the Add/Remove/Reorder UI assumes occupied slots are always a
// contiguous prefix, so this re-establishes that once right after load.
void compactBusFilters(BusDef& def) {
    std::array<BusFilterSlot, slopengine::kMaxFiltersPerBus> compacted{};
    unsigned int next = 0;
    for (BusFilterSlot& slot : def.slots) {
        if (!slot.name.empty()) {
            compacted[next++] = std::move(slot);
        }
    }
    def.slots = std::move(compacted);
}

// Builds a fresh instance just to read the filter's own constructor defaults (SoLoud always
// defaults WET to 1 and everything else to whatever the concrete filter's fields say), so a
// newly attached filter starts at sane values instead of all-zero.
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

// One param widget, its kind (slider/int-slider/checkbox) driven entirely by SoLoud's own
// Filter::getParamType/Min/Max -- new builtin filters get a working UI with no changes here.
bool drawFilterParam(
    AudioWorld& audio,
    AudioBusKind kind,
    unsigned int slot,
    SoLoud::Filter& filter,
    unsigned int paramIndex,
    float& value) {
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
        audio.setBusFilterParam(kind, slot, paramIndex, value);
    }
    return changed;
}

// Assumes (and preserves) the invariant that occupied slots are a contiguous prefix of
// def.slots -- add/remove/reorder always keep it that way, and compactBusFilters()
// re-establishes it for whatever was loaded from disk.
unsigned int activeFilterCount(const BusDef& def) {
    unsigned int count = 0;
    while (count < def.slots.size() && !def.slots[count].name.empty()) {
        ++count;
    }
    return count;
}

// Re-applies def's whole filter chain to the live bus in slot order. Simplest correct way to
// push a structural change (add/remove/reorder all touch multiple slots at once) without
// having to reason about which individual slots moved.
void resyncBusFilters(AudioWorld& audio, AudioBusKind kind, BusDef& def) {
    for (unsigned int slot = 0; slot < slopengine::kMaxFiltersPerBus; ++slot) {
        audio.clearBusFilter(kind, slot);
    }
    for (unsigned int slot = 0; slot < slopengine::kMaxFiltersPerBus; ++slot) {
        const BusFilterSlot& slotDoc = def.slots[slot];
        if (slotDoc.name.empty()) {
            continue;
        }
        audio.attachBuiltinFilter(kind, slotDoc.name, slot);
        for (unsigned int p = 0; p < slotDoc.params.size(); ++p) {
            audio.setBusFilterParam(kind, slot, p, slotDoc.params[p]);
        }
    }
}

// One row in the active filter chain: name, reorder/remove controls, then its param widgets.
// Returns the action the user took so the caller can mutate def.slots and resync afterward --
// this function only reads slot state, it doesn't reshuffle slots itself.
enum class FilterRowAction { None, MoveUp, MoveDown, Remove };

FilterRowAction drawActiveFilterRow(
    AudioWorld& audio,
    AudioBusKind kind,
    unsigned int slot,
    unsigned int activeCount,
    BusFilterSlot& slotDoc,
    bool& dirty) {
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
            if (drawFilterParam(
                    audio, kind, slot, *filter, static_cast<unsigned int>(i),
                    slotDoc.params[static_cast<std::size_t>(i)])) {
                dirty = true;
            }
        }
        ImGui::Unindent();
    }

    ImGui::PopID();
    return action;
}

void drawBusPanel(AudioWorld& audio, AudioBusKind kind, BusDef& def, bool& dirty) {
    if (ImGui::SliderFloat("Volume", &def.volume, 0.0f, 2.0f)) {
        audio.setBusVolume(kind, def.volume);
        dirty = true;
    }
    ImGui::SameLine();
    bool muted = def.muted;
    if (ImGui::Checkbox("Mute", &muted)) {
        def.muted = muted;
        audio.setBusMuted(kind, muted);
        dirty = true;
    }

    ImGui::SeparatorText("Filters");

    const unsigned int activeCount = activeFilterCount(def);
    for (unsigned int slot = 0; slot < activeCount; ++slot) {
        const FilterRowAction action = drawActiveFilterRow(audio, kind, slot, activeCount, def.slots[slot], dirty);
        if (action == FilterRowAction::MoveUp) {
            std::swap(def.slots[slot], def.slots[slot - 1]);
            resyncBusFilters(audio, kind, def);
            dirty = true;
            break;
        }
        if (action == FilterRowAction::MoveDown) {
            std::swap(def.slots[slot], def.slots[slot + 1]);
            resyncBusFilters(audio, kind, def);
            dirty = true;
            break;
        }
        if (action == FilterRowAction::Remove) {
            for (unsigned int j = slot; j + 1 < slopengine::kMaxFiltersPerBus; ++j) {
                def.slots[j] = std::move(def.slots[j + 1]);
            }
            def.slots[slopengine::kMaxFiltersPerBus - 1] = BusFilterSlot{};
            resyncBusFilters(audio, kind, def);
            dirty = true;
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
                BusFilterSlot& newSlot = def.slots[activeCount];
                newSlot.name = kFilterNames[i];
                newSlot.params = defaultFilterParams(newSlot.name);
                resyncBusFilters(audio, kind, def);
                dirty = true;
            }
        }
        ImGui::EndPopup();
    }
}

} // namespace

void BusEditor::ensureLoaded(AssetStore& assets, AudioWorld& audio) {
    if (loaded) {
        return;
    }
    if (const BusConfig* existing = assets.getBusConfig()) {
        doc = *existing;
    } else {
        doc = BusConfig{};
    }
    for (BusDef& bus : doc.buses) {
        compactBusFilters(bus);
    }
    audio.loadBusConfig(doc);
    loaded = true;
    dirty = false;
}

bool BusEditor::save(AssetStore& assets) {
    const std::filesystem::path path = targetRoot / "audio" / "buses.saudio";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << slopengine::serializeBusConfig(doc);
    out.close();
    assets.invalidateBusConfig();
    dirty = false;
    return true;
}

void BusEditor::drawBusList(AssetStore& assets, AudioWorld& audio) {
    ensureLoaded(assets, audio);

    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Save", ImVec2(-1.0f, 0.0f))) {
        statusMessage = save(assets) ? "Saved audio/buses.saudio" : "Save failed";
    }
    ImGui::EndDisabled();
    if (!statusMessage.empty()) {
        ImGui::TextDisabled("%s", statusMessage.c_str());
    }
    ImGui::Separator();

    for (std::size_t i = 0; i < doc.buses.size(); ++i) {
        const AudioDefBus bus = static_cast<AudioDefBus>(i);
        if (ImGui::Selectable(kBusLabels[i], bus == selectedBus)) {
            selectedBus = bus;
        }
    }
}

void BusEditor::drawBusSettings(AssetStore& assets, AudioWorld& audio) {
    ensureLoaded(assets, audio);

    const std::size_t index = static_cast<std::size_t>(selectedBus);
    const AudioBusKind kind = AudioWorld::busKindFromDefBus(selectedBus);

    ImGui::TextUnformatted(kBusLabels[index]);
    ImGui::Separator();
    drawBusPanel(audio, kind, doc.buses[index], dirty);
}

}
