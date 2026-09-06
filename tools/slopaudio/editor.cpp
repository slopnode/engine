#include "editor.hpp"

#include "waveform_view.hpp"

#include "assets/saudio_loader.hpp"
#include "audio/audio_world.hpp"

#include <fstream>
#include <system_error>
#include <utility>

namespace slopaudio {

void Editor::setStatus(std::string message, float seconds) {
    statusMessage = std::move(message);
    statusTimer = seconds;
}

void Editor::markDirty() {
    doc.dirty = true;
    doc.waveformDirty = true;
}

bool Editor::loadAudio(slopengine::AssetStore& assets, const std::string& virtualPath) {
    const slopengine::AudioDef* def = assets.getAudioDef(nullptr, virtualPath);
    if (def == nullptr) {
        setStatus("Failed to load " + virtualPath);
        return false;
    }

    doc = EditorDocument{};
    doc.virtualPath = virtualPath;
    doc.sourceKind = SourceKind::Saudio;
    doc.asset = *def;
    doc.open = true;
    doc.dirty = false;
    doc.waveformDirty = true;
    setStatus("Loaded " + virtualPath);
    return true;
}

bool Editor::loadClip(const std::string& virtualPath) {
    doc = EditorDocument{};
    doc.virtualPath = virtualPath;
    doc.sourceKind = SourceKind::Clip;
    doc.asset.kind = slopengine::AudioDefKind::Sample;
    doc.asset.source = virtualPath;
    doc.asset.volume = 1.0f;
    doc.open = true;
    doc.dirty = false;
    doc.waveformDirty = true;
    setStatus("Previewing " + virtualPath);
    return true;
}

bool Editor::newAudio(const std::string& virtualPath) {
    if (virtualPath.empty()) {
        setStatus("Enter a path first");
        return false;
    }

    doc = EditorDocument{};
    doc.virtualPath = virtualPath;
    doc.sourceKind = SourceKind::Saudio;
    doc.asset.kind = slopengine::AudioDefKind::Sfxr;
    doc.asset.sfxr.hasPreset = true;
    doc.asset.sfxr.preset = 6; // blip
    doc.asset.sfxr.presetSeed = 0;
    doc.open = true;
    doc.dirty = true;
    doc.waveformDirty = true;
    setStatus("New sound " + virtualPath + " (unsaved)");
    return true;
}

bool Editor::save(slopengine::AssetStore& assets) {
    (void)assets;
    if (!doc.open || doc.virtualPath.empty()) {
        setStatus("Nothing to save");
        return false;
    }
    if (doc.sourceKind != SourceKind::Saudio) {
        setStatus("Preview-only asset, nothing to save");
        return false;
    }
    if (targetRoot.empty()) {
        setStatus("No --target package");
        return false;
    }

    const std::filesystem::path outPath = targetRoot / "audio" / (doc.virtualPath + ".saudio");
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    if (ec) {
        setStatus("Failed to create directories for save");
        return false;
    }

    const std::string text = slopengine::serializeSaudioAsset(doc.asset);
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        setStatus("Failed to open for write: " + outPath.string());
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        setStatus("Failed writing: " + outPath.string());
        return false;
    }

    doc.dirty = false;
    setStatus("Saved " + outPath.string());
    return true;
}

void Editor::previewPlay(slopengine::AudioWorld& audio, slopengine::AssetStore& assets) {
    if (!doc.open) {
        return;
    }
    if (doc.previewPlaying) {
        previewStop(audio);
    }

    if (doc.sourceKind == SourceKind::Saudio) {
        doc.previewVoice = audio.playAudioDef(assets, doc.virtualPath, doc.asset);
    } else {
        doc.previewVoice = audio.playSound(assets, doc.asset.source, doc.asset.volume, doc.asset.loop);
    }
    doc.previewPlaying = doc.previewVoice != 0;
}

void Editor::previewStop(slopengine::AudioWorld& audio) {
    if (doc.previewVoice != 0) {
        audio.stop(doc.previewVoice);
    }
    doc.previewVoice = 0;
    doc.previewPlaying = false;
}

void Editor::rebuildWaveform(slopengine::AssetStore& assets) {
    if (doc.sourceKind == SourceKind::Saudio) {
        doc.waveform = bakeSaudioWaveform(doc.asset.sfxr);
    } else {
        doc.waveform = bakeClipWaveform(assets, doc.asset.source);
    }
    doc.waveformDirty = false;
}

}
