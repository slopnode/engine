#pragma once

#include "asset_info.hpp"
#include "waveform_view.hpp"

#include "assets/asset_store.hpp"
#include "assets/audio_def.hpp"

#if defined(_WIN32)
// soloud.h pulls in <windows.h> for its debug assert macro (OutputDebugStringA).
// winuser.h's CloseWindow/ShowCursor then collide with raylib.h's extern "C"
// declarations of the same names, which corrupts MSVC's parse of raylib.h for
// the rest of this translation unit. NOUSER/NOGDI keep winuser.h from declaring
// them; this tool never needs the USER32/GDI APIs they gate.
#ifndef NOUSER
#define NOUSER
#endif
#ifndef NOGDI
#define NOGDI
#endif
#endif

#include <soloud.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace slopengine {
class AudioWorld;
}

namespace slopaudio {

enum class SourceKind {
    Definition, // .saudio: source (file or procedural) + bus/volume/distance/loop/spatial/filters
    Procedural, // .sfxr: raw sfxr synth params, no playback wrapper
    Clip,       // sounds/*.ogg: preview-only raw file
    Midi,       // sounds/*.mid: browse-only, shows basic properties
    SoundFont,  // sounds/*.sf2: browse-only, shows basic properties
};

struct EditorDocument {
    std::string virtualPath;
    SourceKind sourceKind = SourceKind::Definition;
    slopengine::AudioDef asset{};           // Definition fields, or {source,volume} for a Clip preview
    slopengine::ProceduralDef procedural{}; // Procedural fields when sourceKind == Procedural
    MidiInfo midiInfo{};                    // Populated when sourceKind == Midi
    MidiTrackView midiTracks{};             // Populated when sourceKind == Midi
    SoundFontInfo soundFontInfo{};          // Populated when sourceKind == SoundFont
    bool dirty = false;
    bool open = false;

    SoLoud::handle previewVoice = 0;
    bool previewPlaying = false;

    WaveformData waveform;
    WaveformData filteredWaveform; // Only populated for Definition docs with filters attached.
    bool showSampleWaveform = true;
    bool showFilteredWaveform = false;
    bool splitChannelsWaveform = true;
    bool waveformDirty = true;
    bool waveformFitPending = true; // Consumed by Editor::rebuildWaveform to fit windowMs once.
};

struct Editor {
    std::filesystem::path targetRoot;
    EditorDocument doc;
    std::string filter;
    std::string statusMessage;
    float statusTimer = 0.0f;
    bool showNewModal = false;
    SourceKind newModalKind = SourceKind::Definition;
    char newPathBuf[256] = {};
    float waveformWindowMs = 250.0f;
    float midiWindowMs = 4000.0f;

    // Keyed by resolved filesystem path. readSoundFontInfo is cheap now (it no longer decodes
    // sample data), but a soundfont's contents can't change out from under the tool while it's
    // running, so there's no reason to reparse on every repeat click either.
    std::unordered_map<std::string, SoundFontInfo> soundFontInfoCache;

    void setStatus(std::string message, float seconds = 3.0f);
    void markDirty();
    bool loadDefinition(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool loadProcedural(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool loadClip(const std::string& virtualPath);
    bool loadMidi(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool loadSoundFont(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool newDefinition(const std::string& virtualPath);
    bool newProcedural(const std::string& virtualPath);
    bool save(slopengine::AssetStore& assets);
    void previewPlaySource(slopengine::AudioWorld& audio, slopengine::AssetStore& assets);
    void previewPlayFiltered(slopengine::AudioWorld& audio, slopengine::AssetStore& assets);
    void previewPlayBus(slopengine::AudioWorld& audio, slopengine::AssetStore& assets);
    void previewStop(slopengine::AudioWorld& audio);
    void rebuildWaveform(slopengine::AssetStore& assets);

private:
    void previewPlayInternal(
        slopengine::AudioWorld& audio,
        slopengine::AssetStore& assets,
        bool dry,
        bool viaBus);
};

}
