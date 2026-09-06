#pragma once

#include "assets/audio_def.hpp"

#include <filesystem>
#include <string>

namespace slopengine {
class AssetStore;
class AudioWorld;
}

namespace slopaudio {

// State backing the Bus tab: an editable BusConfig plus a dirty flag. Edits apply live to
// the tool's AudioWorld as they're made (so Play/Stop on the Sources tab auditions them) and
// Save serializes to audio/buses.saudio in the target package.
struct BusEditor {
    std::filesystem::path targetRoot;
    slopengine::BusConfig doc{};
    bool loaded = false;
    bool dirty = false;
    std::string statusMessage;
    slopengine::AudioDefBus selectedBus = slopengine::AudioDefBus::Sfx;

    // Loads from AssetStore on first call (or starts from defaults when no buses.saudio
    // exists yet) and pushes the result onto the AudioWorld. No-op on later calls.
    void ensureLoaded(slopengine::AssetStore& assets, slopengine::AudioWorld& audio);
    bool save(slopengine::AssetStore& assets);
    // Left panel: Save button + the bus picker list, mirroring the Sources tab's Browser.
    void drawBusList(slopengine::AssetStore& assets, slopengine::AudioWorld& audio);
    // Right panel: volume/mute/filter chain for whichever bus is selected.
    void drawBusSettings(slopengine::AssetStore& assets, slopengine::AudioWorld& audio);
};

}
