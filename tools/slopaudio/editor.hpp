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
    Saudio,
    Clip,
};

struct EditorDocument {
    std::string virtualPath;
    SourceKind sourceKind = SourceKind::Saudio;
    slopengine::AudioDef asset{};
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
    char newPathBuf[256] = {};
    float waveformWindowMs = 250.0f;

    void setStatus(std::string message, float seconds = 3.0f);
    void markDirty();
    bool loadAudio(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool loadClip(const std::string& virtualPath);
    bool newAudio(const std::string& virtualPath);
    bool save(slopengine::AssetStore& assets);
    void previewPlay(slopengine::AudioWorld& audio, slopengine::AssetStore& assets);
    void previewStop(slopengine::AudioWorld& audio);
    void rebuildWaveform(slopengine::AssetStore& assets);
};

}
