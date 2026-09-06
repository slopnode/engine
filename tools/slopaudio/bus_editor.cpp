#include "bus_editor.hpp"

#include "filter_chain_ui.hpp"

#include "assets/asset_store.hpp"
#include "assets/saudio_loader.hpp"
#include "audio/audio_world.hpp"

#include "imgui.h"

#include <array>
#include <fstream>
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

    const bool structureChanged = drawFilterChain(
        def.slots,
        [&](unsigned int slot, unsigned int paramIndex, float value) {
            audio.setBusFilterParam(kind, slot, paramIndex, value);
            dirty = true;
        });
    if (structureChanged) {
        resyncBusFilters(audio, kind, def);
        dirty = true;
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
