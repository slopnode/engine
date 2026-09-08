#include "editor.hpp"

#include "waveform_view.hpp"

#include "assets/saudio_loader.hpp"
#include "audio/audio_world.hpp"

#include <algorithm>
#include <cstddef>
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
    const slopengine::AudioDef* def = assets.getAudioDef(virtualPath);
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

bool Editor::loadMidi(slopengine::AssetStore& assets, const std::string& virtualPath) {
    const auto resolved = assets.resolveMidiPath(virtualPath);
    if (!resolved) {
        setStatus("Failed to load " + virtualPath);
        return false;
    }

    doc = EditorDocument{};
    doc.virtualPath = virtualPath;
    doc.sourceKind = SourceKind::Midi;
    doc.midiInfo = readMidiInfo(*resolved);
    doc.midiTracks = readMidiTrackView(*resolved);
    doc.open = true;
    doc.dirty = false;
    doc.waveformDirty = false;
    if (doc.midiTracks.durationSec > 0.0f) {
        midiWindowMs = std::clamp(doc.midiTracks.durationSec * 1000.0f, 200.0f, 120000.0f);
    }
    setStatus("Loaded " + virtualPath);
    return true;
}

bool Editor::loadSoundFont(slopengine::AssetStore& assets, const std::string& virtualPath) {
    const auto resolved = assets.resolveSoundFontPath(virtualPath);
    if (!resolved) {
        setStatus("Failed to load " + virtualPath);
        return false;
    }

    doc = EditorDocument{};
    doc.virtualPath = virtualPath;
    doc.sourceKind = SourceKind::SoundFont;
    doc.soundFontInfo = readSoundFontInfo(*resolved);
    doc.open = true;
    doc.dirty = false;
    doc.waveformDirty = false;
    setStatus("Loaded " + virtualPath);
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
    if (doc.sourceKind != SourceKind::Definition && doc.sourceKind != SourceKind::Procedural) {
        setStatus("Preview-only asset, nothing to save");
        return false;
    }
    if (targetRoot.empty()) {
        setStatus("No --target package");
        return false;
    }

    const bool isDefinition = doc.sourceKind == SourceKind::Definition;
    const char* ext = isDefinition ? ".saudio" : ".sfxr";
    const char* dir = isDefinition ? "audio" : "sounds";
    const std::filesystem::path outPath = targetRoot / dir / (doc.virtualPath + ext);
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

void Editor::previewPlaySource(slopengine::AudioWorld& audio, slopengine::AssetStore& assets) {
    previewPlayInternal(audio, assets, true, false);
}

void Editor::previewPlayFiltered(slopengine::AudioWorld& audio, slopengine::AssetStore& assets) {
    previewPlayInternal(audio, assets, false, false);
}

void Editor::previewPlayBus(slopengine::AudioWorld& audio, slopengine::AssetStore& assets) {
    previewPlayInternal(audio, assets, false, true);
}

void Editor::previewPlayInternal(
    slopengine::AudioWorld& audio,
    slopengine::AssetStore& assets,
    bool dry,
    bool viaBus) {
    if (!doc.open) {
        return;
    }
    if (doc.previewPlaying) {
        previewStop(audio);
    }

    switch (doc.sourceKind) {
        case SourceKind::Definition: {
            audio.invalidateDefCache(doc.virtualPath);
            slopengine::AudioDef def = doc.asset;
            if (dry) {
                def.filters = {};
            }
            doc.previewVoice = audio.playAudioDef(assets, doc.virtualPath, def, -1.0f, viaBus);
            break;
        }
        case SourceKind::Procedural:
            doc.previewVoice = audio.playProcedural(doc.virtualPath, doc.procedural.sfxr, 1.0f, viaBus);
            break;
        case SourceKind::Clip:
            doc.previewVoice =
                audio.playSound(assets, doc.asset.source, doc.asset.volume, doc.asset.loop, viaBus);
            break;
        case SourceKind::Midi:
        case SourceKind::SoundFont:
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
    doc.filteredWaveform = WaveformData{};
    doc.midiTracks = MidiTrackView{};
    switch (doc.sourceKind) {
        case SourceKind::Definition:
            if (assets.hasMidi(doc.asset.source)) {
                doc.waveform = WaveformData{};
                if (const auto resolved = assets.resolveMidiPath(doc.asset.source)) {
                    doc.midiTracks = readMidiTrackView(*resolved);
                }
            } else if (const slopengine::ProceduralDef* proc = assets.getProceduralDef(doc.asset.source)) {
                doc.waveform = bakeSaudioWaveform(proc->sfxr);
            } else {
                doc.waveform = bakeClipWaveform(assets, doc.asset.source);
            }
            doc.filteredWaveform = applyFilterChain(doc.waveform, doc.asset.filters);
            break;
        case SourceKind::Procedural:
            doc.waveform = bakeSaudioWaveform(doc.procedural.sfxr);
            break;
        case SourceKind::Clip:
            doc.waveform = bakeClipWaveform(assets, doc.asset.source);
            break;
        case SourceKind::Midi:
        case SourceKind::SoundFont:
            break;
    }
    doc.waveformDirty = false;

    if (doc.waveformFitPending) {
        if (doc.midiTracks.durationSec > 0.0f) {
            midiWindowMs = std::clamp(doc.midiTracks.durationSec * 1000.0f, 200.0f, 120000.0f);
        }
        const unsigned int channels = std::max(1u, doc.waveform.channels);
        const std::size_t frames = doc.waveform.samples.size() / channels;
        if (doc.waveform.sampleRate > 0.0f && frames > 0) {
            waveformWindowMs = static_cast<float>(frames) / doc.waveform.sampleRate * 1000.0f;
        }
        doc.waveformFitPending = false;
    }
}

}
