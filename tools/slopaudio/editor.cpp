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

bool Editor::loadDefinition(slopengine::AssetStore& assets, const std::string& virtualPath) {
    const slopengine::AudioDef* def = assets.getAudioDef(nullptr, virtualPath);
    if (def == nullptr) {
        setStatus("Failed to load " + virtualPath);
        return false;
    }

    doc = EditorDocument{};
    doc.virtualPath = virtualPath;
    doc.sourceKind = SourceKind::Definition;
    doc.asset = *def;
    doc.open = true;
    doc.dirty = false;
    doc.waveformDirty = true;
    setStatus("Loaded " + virtualPath);
    return true;
}

bool Editor::loadProcedural(slopengine::AssetStore& assets, const std::string& virtualPath) {
    const slopengine::ProceduralDef* def = assets.getProceduralDef(virtualPath);
    if (def == nullptr) {
        setStatus("Failed to load " + virtualPath);
        return false;
    }

    doc = EditorDocument{};
    doc.virtualPath = virtualPath;
    doc.sourceKind = SourceKind::Procedural;
    doc.procedural = *def;
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
    doc.asset.source = virtualPath;
    doc.asset.volume = 1.0f;
    doc.open = true;
    doc.dirty = false;
    doc.waveformDirty = true;
    setStatus("Previewing " + virtualPath);
    return true;
}

bool Editor::newDefinition(const std::string& virtualPath) {
    if (virtualPath.empty()) {
        setStatus("Enter a path first");
        return false;
    }

    doc = EditorDocument{};
    doc.virtualPath = virtualPath;
    doc.sourceKind = SourceKind::Definition;
    doc.open = true;
    doc.dirty = true;
    doc.waveformDirty = true;
    setStatus("New definition " + virtualPath + " (unsaved)");
    return true;
}

bool Editor::newProcedural(const std::string& virtualPath) {
    if (virtualPath.empty()) {
        setStatus("Enter a path first");
        return false;
    }

    doc = EditorDocument{};
    doc.virtualPath = virtualPath;
    doc.sourceKind = SourceKind::Procedural;
    doc.procedural.sfxr.hasPreset = true;
    doc.procedural.sfxr.preset = 6; // blip
    doc.procedural.sfxr.presetSeed = 0;
    doc.open = true;
    doc.dirty = true;
    doc.waveformDirty = true;
    setStatus("New procedural " + virtualPath + " (unsaved)");
    return true;
}

bool Editor::save(slopengine::AssetStore& assets) {
    if (!doc.open || doc.virtualPath.empty()) {
        setStatus("Nothing to save");
        return false;
    }
    if (doc.sourceKind == SourceKind::Clip) {
        setStatus("Preview-only asset, nothing to save");
        return false;
    }
    if (targetRoot.empty()) {
        setStatus("No --target package");
        return false;
    }

    const char* ext = doc.sourceKind == SourceKind::Definition ? ".saudio" : ".sfxr";
    const std::filesystem::path outPath = targetRoot / "audio" / (doc.virtualPath + ext);
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    if (ec) {
        setStatus("Failed to create directories for save");
        return false;
    }

    const std::string text = doc.sourceKind == SourceKind::Definition
        ? slopengine::serializeSaudioAsset(doc.asset)
        : slopengine::serializeProceduralAsset(doc.procedural);
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

    if (doc.sourceKind == SourceKind::Definition) {
        assets.invalidateAudioDef(doc.virtualPath);
    } else {
        assets.invalidateProceduralDef(doc.virtualPath);
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

    switch (doc.sourceKind) {
        case SourceKind::Definition:
            doc.previewVoice = audio.playAudioDef(assets, doc.virtualPath, doc.asset);
            break;
        case SourceKind::Procedural:
            doc.previewVoice = audio.playProcedural(doc.virtualPath, doc.procedural.sfxr);
            break;
        case SourceKind::Clip:
            doc.previewVoice = audio.playSound(assets, doc.asset.source, doc.asset.volume, doc.asset.loop);
            break;
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
    switch (doc.sourceKind) {
        case SourceKind::Definition:
            if (const slopengine::ProceduralDef* proc = assets.getProceduralDef(doc.asset.source)) {
                doc.waveform = bakeSaudioWaveform(proc->sfxr);
            } else {
                doc.waveform = bakeClipWaveform(assets, doc.asset.source);
            }
            break;
        case SourceKind::Procedural:
            doc.waveform = bakeSaudioWaveform(doc.procedural.sfxr);
            break;
        case SourceKind::Clip:
            doc.waveform = bakeClipWaveform(assets, doc.asset.source);
            break;
    }
    doc.waveformDirty = false;
}

}
