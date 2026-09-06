#pragma once

#include "waveform_view.hpp"

#include "assets/asset_store.hpp"
#include "assets/audio_def.hpp"

#include <soloud.h>

#include <filesystem>
#include <string>

namespace slopengine {
class AudioWorld;
}

namespace slopaudio {

enum class SourceKind {
    Definition, // .saudio: source (file or procedural) + bus/volume/distance/loop/spatial/filters
    Procedural, // .sfxr: raw sfxr synth params, no playback wrapper
    Clip,       // sound/*.{ogg,wav}: preview-only raw file
};

struct EditorDocument {
    std::string virtualPath;
    SourceKind sourceKind = SourceKind::Definition;
    slopengine::AudioDef asset{};           // Definition fields, or {source,volume} for a Clip preview
    slopengine::ProceduralDef procedural{}; // Procedural fields when sourceKind == Procedural
    bool dirty = false;
    bool open = false;

    SoLoud::handle previewVoice = 0;
    bool previewPlaying = false;

    WaveformData waveform;
    bool waveformDirty = true;
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

    void setStatus(std::string message, float seconds = 3.0f);
    void markDirty();
    bool loadDefinition(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool loadProcedural(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool loadClip(const std::string& virtualPath);
    bool newDefinition(const std::string& virtualPath);
    bool newProcedural(const std::string& virtualPath);
    bool save(slopengine::AssetStore& assets);
    void previewPlay(slopengine::AudioWorld& audio, slopengine::AssetStore& assets);
    void previewStop(slopengine::AudioWorld& audio);
    void rebuildWaveform(slopengine::AssetStore& assets);
};

}
