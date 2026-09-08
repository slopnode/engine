#include "audio/audio_world.hpp"

#ifdef SLOPENGINE_HAS_STEAM_AUDIO
#include "audio/steam_audio_runtime.hpp"
#include "audio/steam_audio_spatialize_filter.hpp"
#endif

#include "audio/graphic_eq_filter.hpp"

#include "tsf.h"
#include "tml.h"

#include <soloud_bassboostfilter.h>
#include <soloud_biquadresonantfilter.h>
#include <soloud_dcremovalfilter.h>
#include <soloud_echofilter.h>
#include <soloud_flangerfilter.h>
#include <soloud_freeverbfilter.h>
#include <soloud_lofifilter.h>
#include <soloud_robotizefilter.h>
#include <soloud_waveshaperfilter.h>

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace slopengine {

static_assert(kMaxFiltersPerBus == FILTERS_PER_STREAM, "BusDef slot count must match SoLoud's per-source filter limit");

AudioWorld::AudioWorld() = default;

AudioWorld::~AudioWorld() {
    deinit();
}

bool AudioWorld::init() {
    if (ready_) {
        return true;
    }

    const SoLoud::result result =
        soloud_.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::MINIAUDIO);
    if (result != SoLoud::SO_NO_ERROR) {
        TraceLog(LOG_ERROR, "AUDIO: SoLoud init failed (%d)", static_cast<int>(result));
        return false;
    }

    for (std::size_t i = 0; i < kBusCount; ++i) {
        busHandles_[i] = soloud_.play(buses_[i]);
    }
    busVolumes_.fill(1.0f);
    busMuted_.fill(false);
    ready_ = true;

#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    steamAudio_ = std::make_unique<SteamAudioRuntime>();
    const int sampleRate = static_cast<int>(soloud_.getBackendSamplerate());
    const int frameSize = static_cast<int>(soloud_.getBackendBufferSize());
    if (!steamAudio_->init(sampleRate, frameSize > 0 ? frameSize : 1024)) {
        TraceLog(LOG_WARNING, "AUDIO: Steam Audio init failed; falling back to SoLoud 3D");
        steamAudio_.reset();
    }
#endif

    return true;
}

void AudioWorld::deinit() {
    if (!ready_) {
        return;
    }

    stopMusic();
    soloud_.stopAll();

#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    if (steamAudio_) {
        steamAudio_->shutdown();
        steamAudio_.reset();
    }
    stereoClips_.clear();
#endif
    listenerAttached_.clear();

    clips_.clear();
    clipsByDef_.clear();
    streams_.clear();
    streamsByDef_.clear();
    sfxrByDef_.clear();
    midiByDef_.clear();
    midiFiles_.clear();
    soundfonts_.clear();
    filters_.clear();
    busFilterSlots_ = {};
    globalFilterCount_ = 0;
    schemeFilters_.clear();
    soloud_.deinit();
    busHandles_.fill(0);
    ready_ = false;
}

SoLoud::Bus& AudioWorld::bus(AudioBusKind kind) {
    return buses_[static_cast<std::size_t>(kind)];
}

unsigned int AudioWorld::nextFreeBusSlot(AudioBusKind kind) const {
    const auto& slots = busFilterSlots_[static_cast<std::size_t>(kind)];
    for (unsigned int i = 0; i < FILTERS_PER_STREAM; ++i) {
        if (slots[i].name.empty()) {
            return i;
        }
    }
    return FILTERS_PER_STREAM;
}

AudioBusKind AudioWorld::busFromDef(AudioDefBus bus) {
    switch (bus) {
        case AudioDefBus::Music: return AudioBusKind::Music;
        case AudioDefBus::Ui: return AudioBusKind::Ui;
        case AudioDefBus::Hud: return AudioBusKind::Hud;
        case AudioDefBus::Voices: return AudioBusKind::Voices;
        case AudioDefBus::Ambience: return AudioBusKind::Ambience;
        case AudioDefBus::Player: return AudioBusKind::Player;
        case AudioDefBus::View: return AudioBusKind::View;
        case AudioDefBus::Sfx: default: return AudioBusKind::Sfx;
    }
}

AudioDefBus AudioWorld::defFromBus(AudioBusKind bus) {
    switch (bus) {
        case AudioBusKind::Music: return AudioDefBus::Music;
        case AudioBusKind::Ui: return AudioDefBus::Ui;
        case AudioBusKind::Hud: return AudioDefBus::Hud;
        case AudioBusKind::Voices: return AudioDefBus::Voices;
        case AudioBusKind::Ambience: return AudioDefBus::Ambience;
        case AudioBusKind::Player: return AudioDefBus::Player;
        case AudioBusKind::View: return AudioDefBus::View;
        case AudioBusKind::Sfx: default: return AudioDefBus::Sfx;
    }
}

SoLoud::Wav* AudioWorld::loadClip(AssetStore& assets, std::string_view path) {
    const std::string key{path};
    if (auto it = clips_.find(key); it != clips_.end()) {
        return it->second.get();
    }

    const auto resolved = assets.resolveSoundPath(path);
    if (!resolved) {
        TraceLog(LOG_WARNING, "AUDIO: sound not found: %.*s", static_cast<int>(path.size()), path.data());
        return nullptr;
    }

    auto wav = std::make_unique<SoLoud::Wav>();
    const SoLoud::result result = wav->load(resolved->string().c_str());
    if (result != SoLoud::SO_NO_ERROR) {
        TraceLog(LOG_WARNING, "AUDIO: failed to load clip %s (%d)", resolved->string().c_str(),
                 static_cast<int>(result));
        return nullptr;
    }

    SoLoud::Wav* ptr = wav.get();
    clips_.emplace(key, std::move(wav));
    return ptr;
}

SoLoud::Wav* AudioWorld::loadClipForDef(
    AssetStore& assets,
    std::string_view defPath,
    const AudioDef& def) {
    if (!hasAnyFilterSlots(def.filters)) {
        return loadClip(assets, def.source);
    }

    const std::string key{defPath};
    if (auto it = clipsByDef_.find(key); it != clipsByDef_.end()) {
        return it->second.get();
    }

    const auto resolved = assets.resolveSoundPath(def.source);
    if (!resolved) {
        TraceLog(LOG_WARNING, "AUDIO: sound not found for def %.*s: %s",
                 static_cast<int>(defPath.size()), defPath.data(), def.source.c_str());
        return nullptr;
    }

    auto wav = std::make_unique<SoLoud::Wav>();
    const SoLoud::result result = wav->load(resolved->string().c_str());
    if (result != SoLoud::SO_NO_ERROR) {
        TraceLog(LOG_WARNING, "AUDIO: failed to load clip for def %s (%d)", key.c_str(),
                 static_cast<int>(result));
        return nullptr;
    }

    applyFilters(*wav, def.filters);
    SoLoud::Wav* ptr = wav.get();
    clipsByDef_.emplace(key, std::move(wav));
    return ptr;
}

SoLoud::WavStream* AudioWorld::loadStream(AssetStore& assets, std::string_view path) {
    const std::string key{path};
    if (auto it = streams_.find(key); it != streams_.end()) {
        return it->second.get();
    }

    const auto resolved = assets.resolveSoundPath(path);
    if (!resolved) {
        TraceLog(LOG_WARNING, "AUDIO: music not found: %.*s", static_cast<int>(path.size()), path.data());
        return nullptr;
    }

    auto stream = std::make_unique<SoLoud::WavStream>();
    const SoLoud::result result = stream->load(resolved->string().c_str());
    if (result != SoLoud::SO_NO_ERROR) {
        TraceLog(LOG_WARNING, "AUDIO: failed to load stream %s (%d)", resolved->string().c_str(),
                 static_cast<int>(result));
        return nullptr;
    }

    SoLoud::WavStream* ptr = stream.get();
    streams_.emplace(key, std::move(stream));
    return ptr;
}

SoLoud::WavStream* AudioWorld::loadStreamForDef(
    AssetStore& assets,
    std::string_view defPath,
    const AudioDef& def) {
    if (!hasAnyFilterSlots(def.filters)) {
        return loadStream(assets, def.source);
    }

    const std::string key{defPath};
    if (auto it = streamsByDef_.find(key); it != streamsByDef_.end()) {
        return it->second.get();
    }

    const auto resolved = assets.resolveSoundPath(def.source);
    if (!resolved) {
        TraceLog(LOG_WARNING, "AUDIO: music not found for def %.*s: %s",
                 static_cast<int>(defPath.size()), defPath.data(), def.source.c_str());
        return nullptr;
    }

    auto stream = std::make_unique<SoLoud::WavStream>();
    const SoLoud::result result = stream->load(resolved->string().c_str());
    if (result != SoLoud::SO_NO_ERROR) {
        TraceLog(LOG_WARNING, "AUDIO: failed to load stream for def %s (%d)", key.c_str(),
                 static_cast<int>(result));
        return nullptr;
    }

    applyFilters(*stream, def.filters);
    SoLoud::WavStream* ptr = stream.get();
    streamsByDef_.emplace(key, std::move(stream));
    return ptr;
}

void AudioWorld::applyFilters(SoLoud::AudioSource& source, const std::array<BusFilterSlot, kMaxFiltersPerBus>& slots) {
    for (unsigned int i = 0; i < slots.size(); ++i) {
        if (slots[i].name.empty()) {
            continue;
        }
        SoLoud::Filter* filter = makeBuiltinFilter(slots[i].name);
        if (filter != nullptr) {
            source.setFilter(i, filter);
        }
    }
}

void AudioWorld::applyDefFilterParams(SoLoud::handle voice, const AudioDef& def) {
    if (voice == 0) {
        return;
    }
    for (unsigned int slot = 0; slot < def.filters.size(); ++slot) {
        const BusFilterSlot& filterSlot = def.filters[slot];
        for (unsigned int p = 0; p < filterSlot.params.size(); ++p) {
            soloud_.setFilterParameter(voice, slot, p, filterSlot.params[p]);
        }
    }
}

void AudioWorld::invalidateDefCache(std::string_view defPath) {
    const std::string key{defPath};
    clipsByDef_.erase(key);
    streamsByDef_.erase(key);
    sfxrByDef_.erase(key);
    midiByDef_.erase(key);
}

void AudioWorld::applySaudioParams(SoLoud::Sfxr& sfxr, const SaudioParams& params) {
    if (params.hasPreset) {
        sfxr.loadPreset(params.preset, params.presetSeed);
    } else {
        sfxr.resetParams();
    }

    const auto set = [&](std::uint32_t field) {
        return (params.setMask & field) != 0;
    };

    if (set(SaudioField_Wave)) {
        sfxr.mParams.wave_type = params.waveType;
    }
    if (set(SaudioField_Freq)) {
        sfxr.mParams.p_base_freq = params.baseFreq;
    }
    if (set(SaudioField_FreqLimit)) {
        sfxr.mParams.p_freq_limit = params.freqLimit;
    }
    if (set(SaudioField_FreqRamp)) {
        sfxr.mParams.p_freq_ramp = params.freqRamp;
    }
    if (set(SaudioField_FreqDramp)) {
        sfxr.mParams.p_freq_dramp = params.freqDramp;
    }
    if (set(SaudioField_Duty)) {
        sfxr.mParams.p_duty = params.duty;
    }
    if (set(SaudioField_DutyRamp)) {
        sfxr.mParams.p_duty_ramp = params.dutyRamp;
    }
    if (set(SaudioField_VibStrength)) {
        sfxr.mParams.p_vib_strength = params.vibStrength;
    }
    if (set(SaudioField_VibSpeed)) {
        sfxr.mParams.p_vib_speed = params.vibSpeed;
    }
    if (set(SaudioField_VibDelay)) {
        sfxr.mParams.p_vib_delay = params.vibDelay;
    }
    if (set(SaudioField_Attack)) {
        sfxr.mParams.p_env_attack = params.envAttack;
    }
    if (set(SaudioField_Sustain)) {
        sfxr.mParams.p_env_sustain = params.envSustain;
    }
    if (set(SaudioField_Decay)) {
        sfxr.mParams.p_env_decay = params.envDecay;
    }
    if (set(SaudioField_Punch)) {
        sfxr.mParams.p_env_punch = params.envPunch;
    }
    if (set(SaudioField_Lpf) || set(SaudioField_LpfResonance) || set(SaudioField_LpfRamp)) {
        sfxr.mParams.filter_on = true;
    }
    if (set(SaudioField_LpfResonance)) {
        sfxr.mParams.p_lpf_resonance = params.lpfResonance;
    }
    if (set(SaudioField_Lpf)) {
        sfxr.mParams.p_lpf_freq = params.lpfFreq;
    }
    if (set(SaudioField_LpfRamp)) {
        sfxr.mParams.p_lpf_ramp = params.lpfRamp;
    }
    if (set(SaudioField_Hpf)) {
        sfxr.mParams.p_hpf_freq = params.hpfFreq;
    }
    if (set(SaudioField_HpfRamp)) {
        sfxr.mParams.p_hpf_ramp = params.hpfRamp;
    }
    if (set(SaudioField_PhaOffset)) {
        sfxr.mParams.p_pha_offset = params.phaOffset;
    }
    if (set(SaudioField_PhaRamp)) {
        sfxr.mParams.p_pha_ramp = params.phaRamp;
    }
    if (set(SaudioField_RepeatSpeed)) {
        sfxr.mParams.p_repeat_speed = params.repeatSpeed;
    }
    if (set(SaudioField_ArpSpeed)) {
        sfxr.mParams.p_arp_speed = params.arpSpeed;
    }
    if (set(SaudioField_ArpMod)) {
        sfxr.mParams.p_arp_mod = params.arpMod;
    }
    if (set(SaudioField_SoundVol)) {
        sfxr.mParams.sound_vol = params.soundVol;
    }
    if (set(SaudioField_MasterVol)) {
        sfxr.mParams.master_vol = params.masterVol;
    }
}

SoLoud::Sfxr* AudioWorld::loadSfxrForDef(
    std::string_view defPath,
    const SaudioParams& params,
    const AudioDef& def) {
    const std::string key{defPath};
    if (auto it = sfxrByDef_.find(key); it != sfxrByDef_.end()) {
        return it->second.get();
    }

    auto sfxr = std::make_unique<SoLoud::Sfxr>();
    applySaudioParams(*sfxr, params);
    applyFilters(*sfxr, def.filters);
    SoLoud::Sfxr* ptr = sfxr.get();
    sfxrByDef_.emplace(key, std::move(sfxr));
    return ptr;
}

SoLoud::handle AudioWorld::playSfxrDef(
    std::string_view defPath,
    const AudioDef& def,
    const SaudioParams& params,
    float volume,
    bool spatial3d,
    float x,
    float y,
    float z,
    bool viaBus) {
    SoLoud::Sfxr* sfxr = loadSfxrForDef(defPath, params, def);
    if (sfxr == nullptr) {
        return 0;
    }

    applySaudioParams(*sfxr, params);
    sfxr->setLooping(def.loop);

    if (def.bus == AudioDefBus::Music) {
        stopMusic();
    }

    SoLoud::Bus& target = bus(busFromDef(def.bus));
    SoLoud::handle voice = 0;
    if (spatial3d || def.spatial) {
#ifdef SLOPENGINE_HAS_STEAM_AUDIO
        if (steamAudioEnabled() && def.bus != AudioDefBus::Music) {
            SoLoud::Wav* baked = bakeSfxrStereo(*sfxr, defPath, def.loop);
            if (baked == nullptr) {
                return 0;
            }
            voice = playSpatialSteam(
                busFromDef(def.bus),
                *baked,
                x,
                y,
                z,
                volume,
                def.minDistance,
                def.maxDistance,
                false,
                0.0f);
        } else
#endif
        {
            sfxr->set3dMinMaxDistance(def.minDistance, def.maxDistance);
            voice = viaBus
                ? target.play3d(*sfxr, x, y, z, 0.0f, 0.0f, 0.0f, volume)
                : soloud_.play3d(*sfxr, x, y, z, 0.0f, 0.0f, 0.0f, volume);
            applyDefFilterParams(voice, def);
        }
    } else {
        voice = viaBus ? target.play(*sfxr, volume) : soloud_.play(*sfxr, volume);
        applyDefFilterParams(voice, def);
    }

    if (def.bus == AudioDefBus::Music) {
        musicVoice_ = voice;
    }
    return voice;
}

tsf* AudioWorld::loadSoundFont(AssetStore& assets, std::string_view path) {
    const std::string key{path};
    if (auto it = soundfonts_.find(key); it != soundfonts_.end()) {
        return it->second.get();
    }

    const auto resolved = assets.resolveSoundFontPath(path);
    if (!resolved) {
        TraceLog(LOG_WARNING, "AUDIO: soundfont not found: %.*s", static_cast<int>(path.size()), path.data());
        return nullptr;
    }

    tsf* font = tsf_load_filename(resolved->string().c_str());
    if (font == nullptr) {
        TraceLog(LOG_WARNING, "AUDIO: failed to load soundfont %s", resolved->string().c_str());
        return nullptr;
    }

    tsf_set_output(font, TSF_STEREO_INTERLEAVED, static_cast<int>(soloud_.mSamplerate), 0.0f);
    tsf_channel_set_bank_preset(font, 9, 128, 0);

    tsf* ptr = font;
    soundfonts_.emplace(key, std::unique_ptr<tsf, void (*)(tsf*)>(font, &tsf_close));
    return ptr;
}

const tml_message* AudioWorld::loadMidiMessages(AssetStore& assets, std::string_view path) {
    const std::string key{path};
    if (auto it = midiFiles_.find(key); it != midiFiles_.end()) {
        return it->second.get();
    }

    const auto resolved = assets.resolveMidiPath(path);
    if (!resolved) {
        TraceLog(LOG_WARNING, "AUDIO: midi not found: %.*s", static_cast<int>(path.size()), path.data());
        return nullptr;
    }

    tml_message* messages = tml_load_filename(resolved->string().c_str());
    if (messages == nullptr) {
        TraceLog(LOG_WARNING, "AUDIO: failed to load midi %s", resolved->string().c_str());
        return nullptr;
    }

    const tml_message* ptr = messages;
    midiFiles_.emplace(key, std::unique_ptr<tml_message, void (*)(tml_message*)>(messages, &tml_free));
    return ptr;
}

MidiSource* AudioWorld::loadMidiForDef(AssetStore& assets, std::string_view defPath, const AudioDef& def) {
    const std::string key{defPath};
    if (auto it = midiByDef_.find(key); it != midiByDef_.end()) {
        return it->second.get();
    }

    if (def.soundfont.empty()) {
        TraceLog(
            LOG_WARNING, "AUDIO: midi def %.*s has no soundfont assigned",
            static_cast<int>(defPath.size()), defPath.data());
        return nullptr;
    }

    tsf* font = loadSoundFont(assets, def.soundfont);
    if (font == nullptr) {
        return nullptr;
    }
    const tml_message* messages = loadMidiMessages(assets, def.source);
    if (messages == nullptr) {
        return nullptr;
    }

    auto midi = std::make_unique<MidiSource>(font, messages, soloud_.mSamplerate);
    applyFilters(*midi, def.filters);
    MidiSource* ptr = midi.get();
    midiByDef_.emplace(key, std::move(midi));
    return ptr;
}

SoLoud::handle AudioWorld::playMidiDef(
    const AudioDef& def,
    MidiSource& midi,
    float volume,
    bool spatial3d,
    float x,
    float y,
    float z,
    bool viaBus) {
    midi.setLooping(def.loop);

    if (def.bus == AudioDefBus::Music) {
        stopMusic();
    }

    SoLoud::Bus& target = bus(busFromDef(def.bus));
    SoLoud::handle voice = 0;
    if (spatial3d || def.spatial) {
        midi.set3dMinMaxDistance(def.minDistance, def.maxDistance);
        voice = viaBus
            ? target.play3d(midi, x, y, z, 0.0f, 0.0f, 0.0f, volume)
            : soloud_.play3d(midi, x, y, z, 0.0f, 0.0f, 0.0f, volume);
    } else {
        voice = viaBus ? target.play(midi, volume) : soloud_.play(midi, volume);
    }
    applyDefFilterParams(voice, def);

    if (def.bus == AudioDefBus::Music) {
        musicVoice_ = voice;
    }
    return voice;
}

SoLoud::handle AudioWorld::playSound(
    AssetStore& assets,
    std::string_view path,
    float volume,
    bool loop,
    bool viaBus) {
    if (!ready_) {
        return 0;
    }
    SoLoud::Wav* clip = loadClip(assets, path);
    if (clip == nullptr) {
        return 0;
    }
    clip->setLooping(loop);
    return viaBus ? bus(AudioBusKind::Sfx).play(*clip, volume) : soloud_.play(*clip, volume);
}

SoLoud::handle AudioWorld::playSound3d(
    AssetStore& assets,
    std::string_view path,
    float x,
    float y,
    float z,
    float volume,
    bool loop,
    float minDistance,
    float maxDistance,
    bool followListener,
    float followForwardOffset) {
    if (!ready_) {
        return 0;
    }
    SoLoud::Wav* clip = loadClip(assets, path);
    if (clip == nullptr) {
        return 0;
    }
    clip->setLooping(loop);
#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    if (steamAudioEnabled()) {
        SoLoud::Wav* spatial = prepareSpatialSteamWav(clip, path, loop);
        if (spatial == nullptr) {
            return 0;
        }
        return playSpatialSteam(
            AudioBusKind::Sfx,
            *spatial,
            x,
            y,
            z,
            volume,
            minDistance,
            maxDistance,
            followListener,
            followForwardOffset);
    }
#endif
    clip->set3dMinMaxDistance(minDistance, maxDistance);
    const SoLoud::handle voice = bus(AudioBusKind::Sfx).play3d(*clip, x, y, z, 0.0f, 0.0f, 0.0f, volume);
    if (followListener && voice != 0) {
        trackListenerAttached(voice, followForwardOffset);
    }
    return voice;
}

SoLoud::handle AudioWorld::playAudioDef(
    AssetStore& assets,
    std::string_view defPath,
    const AudioDef& def,
    float volumeOverride,
    bool viaBus) {
    if (!ready_) {
        return 0;
    }

    const float volume = volumeOverride >= 0.0f ? volumeOverride : def.volume;

    if (def.source.empty()) {
        return 0;
    }

    if (const ProceduralDef* proc = assets.getProceduralDef(def.source)) {
        return playSfxrDef(defPath, def, proc->sfxr, volume, false, 0.0f, 0.0f, 0.0f, viaBus);
    }

    if (assets.hasMidi(def.source)) {
        MidiSource* midi = loadMidiForDef(assets, defPath, def);
        if (midi == nullptr) {
            return 0;
        }
        return playMidiDef(def, *midi, volume, false, 0.0f, 0.0f, 0.0f, viaBus);
    }

    const AudioBusKind busKind = busFromDef(def.bus);

    if (busKind == AudioBusKind::Music) {
        SoLoud::WavStream* stream = loadStreamForDef(assets, defPath, def);
        if (stream == nullptr) {
            return 0;
        }
        stopMusic();
        stream->setLooping(def.loop);
        musicVoice_ = bus(AudioBusKind::Music).play(*stream, volume);
        applyDefFilterParams(musicVoice_, def);
        return musicVoice_;
    }

    SoLoud::Wav* clip = loadClipForDef(assets, defPath, def);
    if (clip == nullptr) {
        return 0;
    }
    clip->setLooping(def.loop);
    if (def.spatial) {
#ifdef SLOPENGINE_HAS_STEAM_AUDIO
        if (steamAudioEnabled()) {
            SoLoud::Wav* spatial = prepareSpatialSteamWav(clip, defPath, def.loop);
            if (spatial == nullptr) {
                return 0;
            }
            return playSpatialSteam(
                busKind,
                *spatial,
                0.0f,
                0.0f,
                0.0f,
                volume,
                def.minDistance,
                def.maxDistance,
                false,
                0.0f);
        }
#endif
        clip->set3dMinMaxDistance(def.minDistance, def.maxDistance);
        const SoLoud::handle voice = viaBus
            ? bus(busKind).play3d(*clip, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, volume)
            : soloud_.play3d(*clip, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, volume);
        applyDefFilterParams(voice, def);
        return voice;
    }
    const SoLoud::handle voice =
        viaBus ? bus(busKind).play(*clip, volume) : soloud_.play(*clip, volume);
    applyDefFilterParams(voice, def);
    return voice;
}

SoLoud::handle AudioWorld::playProcedural(
    std::string_view proceduralPath,
    const SaudioParams& params,
    float volume,
    bool viaBus) {
    if (!ready_) {
        return 0;
    }
    const AudioDef previewDef{};
    return playSfxrDef(proceduralPath, previewDef, params, volume, false, 0.0f, 0.0f, 0.0f, viaBus);
}

SoLoud::handle AudioWorld::playAudioDef3d(
    AssetStore& assets,
    std::string_view defPath,
    const AudioDef& def,
    float x,
    float y,
    float z,
    float volumeOverride) {
    if (!ready_) {
        return 0;
    }

    const float volume = volumeOverride >= 0.0f ? volumeOverride : def.volume;

    if (def.source.empty()) {
        return 0;
    }

    if (const ProceduralDef* proc = assets.getProceduralDef(def.source)) {
        if (def.bus == AudioDefBus::Music) {
            return playSfxrDef(defPath, def, proc->sfxr, volume, false, 0.0f, 0.0f, 0.0f);
        }
        return playSfxrDef(defPath, def, proc->sfxr, volume, true, x, y, z);
    }

    if (assets.hasMidi(def.source)) {
        if (def.bus == AudioDefBus::Music) {
            return playAudioDef(assets, defPath, def, volumeOverride);
        }
        MidiSource* midi = loadMidiForDef(assets, defPath, def);
        if (midi == nullptr) {
            return 0;
        }
        return playMidiDef(def, *midi, volume, true, x, y, z);
    }

    if (def.bus == AudioDefBus::Music) {
        return playAudioDef(assets, defPath, def, volumeOverride);
    }

    const AudioBusKind busKind = busFromDef(def.bus);

    SoLoud::Wav* clip = loadClipForDef(assets, defPath, def);
    if (clip == nullptr) {
        return 0;
    }
    clip->setLooping(def.loop);
#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    if (steamAudioEnabled()) {
        SoLoud::Wav* spatial = prepareSpatialSteamWav(clip, defPath, def.loop);
        if (spatial == nullptr) {
            return 0;
        }
        return playSpatialSteam(
            busKind,
            *spatial,
            x,
            y,
            z,
            volume,
            def.minDistance,
            def.maxDistance,
            false,
            0.0f);
    }
#endif
    clip->set3dMinMaxDistance(def.minDistance, def.maxDistance);
    const SoLoud::handle voice = bus(busKind).play3d(*clip, x, y, z, 0.0f, 0.0f, 0.0f, volume);
    applyDefFilterParams(voice, def);
    return voice;
}

void AudioWorld::stop(SoLoud::handle voice) {
    if (!ready_ || voice == 0) {
        return;
    }
#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    if (steamAudio_) {
        steamAudio_->releaseVoice(voice);
    }
#endif
    listenerAttached_.erase(
        std::remove_if(
            listenerAttached_.begin(),
            listenerAttached_.end(),
            [voice](const ListenerAttachedVoice& attached) { return attached.voice == voice; }),
        listenerAttached_.end());
    soloud_.stop(voice);
}

void AudioWorld::setVolume(SoLoud::handle voice, float volume) {
    if (!ready_ || voice == 0) {
        return;
    }
    soloud_.setVolume(voice, volume);
}

void AudioWorld::setBusVolume(AudioBusKind kind, float volume) {
    if (!ready_) {
        return;
    }
    busVolumes_[static_cast<std::size_t>(kind)] = volume;
    if (busMuted_[static_cast<std::size_t>(kind)]) {
        return;
    }
    const SoLoud::handle handle = busHandles_[static_cast<std::size_t>(kind)];
    if (handle != 0) {
        soloud_.setVolume(handle, volume);
    }
}

float AudioWorld::getBusVolume(AudioBusKind kind) const {
    return busVolumes_[static_cast<std::size_t>(kind)];
}

void AudioWorld::setBusMuted(AudioBusKind kind, bool muted) {
    if (!ready_) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(kind);
    if (busMuted_[index] == muted) {
        return;
    }
    busMuted_[index] = muted;
    const SoLoud::handle handle = busHandles_[index];
    if (handle != 0) {
        soloud_.setVolume(handle, muted ? 0.0f : busVolumes_[index]);
    }
}

bool AudioWorld::isBusMuted(AudioBusKind kind) const {
    return busMuted_[static_cast<std::size_t>(kind)];
}

bool AudioWorld::playMusic(AssetStore& assets, std::string_view path, float volume) {
    if (!ready_) {
        return false;
    }
    SoLoud::WavStream* stream = loadStream(assets, path);
    if (stream == nullptr) {
        return false;
    }
    stopMusic();
    stream->setLooping(true);
    musicVoice_ = bus(AudioBusKind::Music).play(*stream, volume);
    return musicVoice_ != 0;
}

void AudioWorld::stopMusic() {
    if (!ready_ || musicVoice_ == 0) {
        return;
    }
    soloud_.stop(musicVoice_);
    musicVoice_ = 0;
}

void AudioWorld::setListener(
    float posX,
    float posY,
    float posZ,
    float atX,
    float atY,
    float atZ,
    float upX,
    float upY,
    float upZ) {
    if (!ready_) {
        return;
    }
    soloud_.set3dListenerParameters(posX, posY, posZ, atX, atY, atZ, upX, upY, upZ);
}

void AudioWorld::setSourcePosition(SoLoud::handle voice, float x, float y, float z) {
    if (!ready_ || voice == 0) {
        return;
    }
    soloud_.set3dSourcePosition(voice, x, y, z);
}

void AudioWorld::update3d() {
    if (!ready_) {
        return;
    }
    soloud_.update3dAudio();
}

void AudioWorld::trackListenerAttached(SoLoud::handle voice, float forwardOffset) {
    if (voice == 0) {
        return;
    }
    listenerAttached_.push_back(ListenerAttachedVoice{voice, forwardOffset});
}

void AudioWorld::updateListenerAttachedSources(const SteamAudioListenerPose& listener) {
    if (!ready_) {
        return;
    }

    float aheadX = listener.aheadX;
    float aheadY = listener.aheadY;
    float aheadZ = listener.aheadZ;
    const float aheadLen = std::sqrt(aheadX * aheadX + aheadY * aheadY + aheadZ * aheadZ);
    if (aheadLen > 1.0e-8f) {
        aheadX /= aheadLen;
        aheadY /= aheadLen;
        aheadZ /= aheadLen;
    } else {
        aheadX = 0.0f;
        aheadY = 0.0f;
        aheadZ = 1.0f;
    }

    listenerAttached_.erase(
        std::remove_if(
            listenerAttached_.begin(),
            listenerAttached_.end(),
            [this](const ListenerAttachedVoice& attached) {
                return attached.voice == 0 || !soloud_.isValidVoiceHandle(attached.voice);
            }),
        listenerAttached_.end());

    for (const ListenerAttachedVoice& attached : listenerAttached_) {
        const float x = listener.posX + aheadX * attached.forwardOffset;
        const float y = listener.posY + aheadY * attached.forwardOffset;
        const float z = listener.posZ + aheadZ * attached.forwardOffset;
        soloud_.set3dSourcePosition(attached.voice, x, y, z);
#ifdef SLOPENGINE_HAS_STEAM_AUDIO
        if (steamAudioEnabled()) {
            steamAudio_->setVoicePosition(attached.voice, x, y, z);
        }
#endif
    }
}

bool AudioWorld::steamAudioEnabled() const {
#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    return steamAudio_ != nullptr && steamAudio_->ready();
#else
    return false;
#endif
}

void AudioWorld::setSteamAudioScene(
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* excludeBrushIds) {
#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    if (steamAudioEnabled()) {
        steamAudio_->setSceneFromBrushes(brushes, excludeBrushIds);
    }
#else
    (void)brushes;
    (void)excludeBrushIds;
#endif
}

void AudioWorld::clearSteamAudioScene() {
#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    if (steamAudio_) {
        steamAudio_->clearScene();
    }
#endif
}

void AudioWorld::updateSteamAudio(
    const SteamAudioListenerPose& listener,
    const std::vector<SteamAudioSourcePose>& sources) {
#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    if (steamAudioEnabled()) {
        steamAudio_->pruneFinishedVoices(soloud_);
        steamAudio_->update(listener, sources);
    }
#else
    (void)listener;
    (void)sources;
#endif
}

#ifdef SLOPENGINE_HAS_STEAM_AUDIO
SoLoud::Wav* AudioWorld::ensureStereoClip(SoLoud::Wav* clip, std::string_view cacheKey) {
    if (clip == nullptr) {
        return nullptr;
    }
    if (clip->mChannels >= 2) {
        return clip;
    }

    const std::string key{cacheKey};
    if (auto it = stereoClips_.find(key); it != stereoClips_.end()) {
        return it->second.get();
    }

    auto stereo = std::make_unique<SoLoud::Wav>();
    const unsigned int samples = clip->mSampleCount;
    std::vector<float> data(static_cast<std::size_t>(samples) * 2u);
    if (clip->mData != nullptr && samples > 0) {
        std::memcpy(data.data(), clip->mData, samples * sizeof(float));
        std::memcpy(data.data() + samples, clip->mData, samples * sizeof(float));
    }
    const SoLoud::result result = stereo->loadRawWave(
        data.data(),
        static_cast<unsigned int>(data.size()),
        clip->mBaseSamplerate,
        2,
        true,
        true);
    if (result != SoLoud::SO_NO_ERROR) {
        TraceLog(LOG_WARNING, "AUDIO: failed to create stereo spatialize clip for %s", key.c_str());
        return clip;
    }

    SoLoud::Wav* ptr = stereo.get();
    stereoClips_.emplace(key, std::move(stereo));
    return ptr;
}

SoLoud::Wav* AudioWorld::prepareSpatialSteamWav(
    SoLoud::Wav* clip,
    std::string_view cacheKey,
    bool loop) {
    if (clip == nullptr || !steamAudioEnabled()) {
        return nullptr;
    }

    const std::string key = std::string("steam-wav:") + std::string(cacheKey);
    if (auto it = stereoClips_.find(key); it != stereoClips_.end()) {
        it->second->setLooping(loop);
        return it->second.get();
    }

    auto stereo = std::make_unique<SoLoud::Wav>();
    const unsigned int samples = clip->mSampleCount;
    if (samples == 0 || clip->mData == nullptr) {
        TraceLog(LOG_WARNING, "AUDIO: empty clip for steam spatialize %s", key.c_str());
        return nullptr;
    }

    std::vector<float> data(static_cast<std::size_t>(samples) * 2u);
    if (clip->mChannels >= 2) {
        std::memcpy(data.data(), clip->mData, data.size() * sizeof(float));
    } else {
        std::memcpy(data.data(), clip->mData, samples * sizeof(float));
        std::memcpy(data.data() + samples, clip->mData, samples * sizeof(float));
    }

    const SoLoud::result result = stereo->loadRawWave(
        data.data(),
        static_cast<unsigned int>(data.size()),
        clip->mBaseSamplerate,
        2,
        true,
        true);
    if (result != SoLoud::SO_NO_ERROR) {
        TraceLog(LOG_WARNING, "AUDIO: failed to create steam spatialize wav for %s", key.c_str());
        return nullptr;
    }

    stereo->setLooping(loop);
    stereo->setFilter(0, &steamAudio_->spatializeFilter());
    SoLoud::Wav* ptr = stereo.get();
    stereoClips_.emplace(key, std::move(stereo));
    return ptr;
}

SoLoud::Wav* AudioWorld::bakeSfxrStereo(SoLoud::Sfxr& sfxr, std::string_view cacheKey, bool loop) {
    if (!steamAudioEnabled()) {
        return nullptr;
    }

    const std::string key = std::string("steam-sfxr:") + std::string(cacheKey);
    if (auto it = stereoClips_.find(key); it != stereoClips_.end()) {
        it->second->setLooping(loop);
        return it->second.get();
    }

    sfxr.setLooping(loop);
    SoLoud::AudioSourceInstance* instance = sfxr.createInstance();
    if (instance == nullptr) {
        TraceLog(LOG_WARNING, "AUDIO: failed to create sfxr instance for %s", key.c_str());
        return nullptr;
    }
    instance->init(sfxr, 0);

    const unsigned int rate =
        sfxr.mBaseSamplerate > 0.0f ? static_cast<unsigned int>(sfxr.mBaseSamplerate) : 44100u;
    const unsigned int targetSamples = loop ? rate : rate * 5u;
    std::vector<float> mono;
    mono.reserve(targetSamples);
    std::vector<float> chunk(512);
    while (mono.size() < targetSamples && !instance->hasEnded()) {
        const unsigned int want = std::min(
            static_cast<unsigned int>(chunk.size()),
            targetSamples - static_cast<unsigned int>(mono.size()));
        const unsigned int got = instance->getAudio(chunk.data(), want, want);
        if (got == 0) {
            break;
        }
        mono.insert(mono.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
    }
    delete instance;

    if (mono.empty()) {
        TraceLog(LOG_WARNING, "AUDIO: empty sfxr bake for %s", key.c_str());
        return nullptr;
    }

    std::vector<float> data(mono.size() * 2u);
    std::memcpy(data.data(), mono.data(), mono.size() * sizeof(float));
    std::memcpy(data.data() + mono.size(), mono.data(), mono.size() * sizeof(float));

    auto stereo = std::make_unique<SoLoud::Wav>();
    const SoLoud::result result = stereo->loadRawWave(
        data.data(),
        static_cast<unsigned int>(data.size()),
        static_cast<float>(rate),
        2,
        true,
        true);
    if (result != SoLoud::SO_NO_ERROR) {
        TraceLog(LOG_WARNING, "AUDIO: failed to bake sfxr stereo wav for %s", key.c_str());
        return nullptr;
    }

    stereo->setLooping(loop);
    stereo->setFilter(0, &steamAudio_->spatializeFilter());
    SoLoud::Wav* ptr = stereo.get();
    stereoClips_.emplace(key, std::move(stereo));
    return ptr;
}

SoLoud::handle AudioWorld::playSpatialSteam(
    AudioBusKind busKind,
    SoLoud::Wav& source,
    float x,
    float y,
    float z,
    float volume,
    float minDistance,
    float maxDistance,
    bool followListener,
    float followForwardOffset) {
    if (!steamAudioEnabled()) {
        return 0;
    }

    source.setFilter(0, &steamAudio_->spatializeFilter());
    const SoLoud::handle voice = bus(busKind).play(source, volume);
    if (voice == 0) {
        return 0;
    }

    const int slot =
        steamAudio_->trackVoice(voice, x, y, z, minDistance, maxDistance, volume);
    if (slot >= 0) {
        soloud_.setFilterParameter(
            voice,
            0,
            static_cast<unsigned int>(SteamAudioRuntime::kSlotParam),
            static_cast<float>(slot));
    }
    if (followListener) {
        trackListenerAttached(voice, followForwardOffset);
    }
    return voice;
}
#endif

std::unique_ptr<SoLoud::Filter> AudioWorld::createBuiltinFilter(std::string_view name) {
    if (name == "echo") {
        return std::make_unique<SoLoud::EchoFilter>();
    } else if (name == "biquad") {
        return std::make_unique<SoLoud::BiquadResonantFilter>();
    } else if (name == "lofi") {
        return std::make_unique<SoLoud::LofiFilter>();
    } else if (name == "flanger") {
        return std::make_unique<SoLoud::FlangerFilter>();
    } else if (name == "bassboost") {
        return std::make_unique<SoLoud::BassboostFilter>();
    } else if (name == "freeverb") {
        return std::make_unique<SoLoud::FreeverbFilter>();
    } else if (name == "robotize") {
        return std::make_unique<SoLoud::RobotizeFilter>();
    } else if (name == "waveshaper") {
        return std::make_unique<SoLoud::WaveShaperFilter>();
    } else if (name == "dcremoval") {
        return std::make_unique<SoLoud::DCRemovalFilter>();
    } else if (name == "eq") {
        return std::make_unique<GraphicEqFilter>();
    }
    return nullptr;
}

SoLoud::Filter* AudioWorld::makeBuiltinFilter(std::string_view name) {
    std::unique_ptr<SoLoud::Filter> filter = createBuiltinFilter(name);
    if (filter == nullptr) {
        return nullptr;
    }
    SoLoud::Filter* ptr = filter.get();
    filters_.push_back(std::move(filter));
    return ptr;
}

bool AudioWorld::setUnderwaterMuffle(float amount01) {
    if (!ready_) {
        return false;
    }
    if (underwaterFilterSlot_ < 0) {
        const unsigned int slot = nextFreeBusSlot(AudioBusKind::Sfx);
        if (slot >= FILTERS_PER_STREAM) {
            return false;
        }
        SoLoud::Filter* filter = makeBuiltinFilter("biquad");
        if (filter == nullptr) {
            return false;
        }
        auto* biquad = static_cast<SoLoud::BiquadResonantFilter*>(filter);
        biquad->setParams(SoLoud::BiquadResonantFilter::LOWPASS, 900.0f, 2.0f);
        bus(AudioBusKind::Sfx).setFilter(slot, filter);
        BusFilterState& state = busFilterSlots_[static_cast<std::size_t>(AudioBusKind::Sfx)][slot];
        state.name = "biquad";
        state.filter = filter;
        underwaterFilterSlot_ = static_cast<int>(slot);
    }
    const float wet = std::clamp(amount01, 0.0f, 1.0f);
    soloud_.setFilterParameter(
        busHandles_[static_cast<std::size_t>(AudioBusKind::Sfx)],
        static_cast<unsigned int>(underwaterFilterSlot_),
        static_cast<unsigned int>(SoLoud::BiquadResonantFilter::WET),
        wet);
    return true;
}

bool AudioWorld::attachBuiltinFilter(AudioBusKind kind, std::string_view name, unsigned int slot) {
    if (!ready_) {
        return false;
    }
    const unsigned int useSlot = slot < FILTERS_PER_STREAM ? slot : nextFreeBusSlot(kind);
    if (useSlot >= FILTERS_PER_STREAM) {
        return false;
    }
    SoLoud::Filter* filter = makeBuiltinFilter(name);
    if (filter == nullptr) {
        return false;
    }
    bus(kind).setFilter(useSlot, filter);
    BusFilterState& state = busFilterSlots_[static_cast<std::size_t>(kind)][useSlot];
    state.name = std::string{name};
    state.filter = filter;
    return true;
}

void AudioWorld::clearBusFilter(AudioBusKind kind, unsigned int slot) {
    if (!ready_ || slot >= FILTERS_PER_STREAM) {
        return;
    }
    bus(kind).setFilter(slot, nullptr);
    busFilterSlots_[static_cast<std::size_t>(kind)][slot] = BusFilterState{};
}

std::string_view AudioWorld::busFilterName(AudioBusKind kind, unsigned int slot) const {
    if (slot >= FILTERS_PER_STREAM) {
        return {};
    }
    return busFilterSlots_[static_cast<std::size_t>(kind)][slot].name;
}

bool AudioWorld::setBusFilterParam(AudioBusKind kind, unsigned int slot, unsigned int paramIndex, float value) {
    if (!ready_ || slot >= FILTERS_PER_STREAM) {
        return false;
    }
    const SoLoud::handle handle = busHandles_[static_cast<std::size_t>(kind)];
    if (handle == 0) {
        return false;
    }
    soloud_.setFilterParameter(handle, slot, paramIndex, value);
    return true;
}

float AudioWorld::getBusFilterParam(AudioBusKind kind, unsigned int slot, unsigned int paramIndex) {
    if (!ready_ || slot >= FILTERS_PER_STREAM) {
        return 0.0f;
    }
    const SoLoud::handle handle = busHandles_[static_cast<std::size_t>(kind)];
    if (handle == 0) {
        return 0.0f;
    }
    return soloud_.getFilterParameter(handle, slot, paramIndex);
}

void AudioWorld::loadBusConfig(const BusConfig& config) {
    if (!ready_) {
        return;
    }
    for (std::size_t i = 0; i < config.buses.size(); ++i) {
        const AudioBusKind kind = busFromDef(static_cast<AudioDefBus>(i));
        const BusDef& def = config.buses[i];

        for (unsigned int slot = 0; slot < FILTERS_PER_STREAM; ++slot) {
            clearBusFilter(kind, slot);
        }
        for (unsigned int slot = 0; slot < FILTERS_PER_STREAM; ++slot) {
            const BusFilterSlot& filterSlot = def.slots[slot];
            if (filterSlot.name.empty()) {
                continue;
            }
            if (!attachBuiltinFilter(kind, filterSlot.name, slot)) {
                continue;
            }
            for (unsigned int p = 0; p < filterSlot.params.size(); ++p) {
                setBusFilterParam(kind, slot, p, filterSlot.params[p]);
            }
        }

        setBusVolume(kind, def.volume);
        setBusMuted(kind, def.muted);
    }
}

BusConfig AudioWorld::exportBusConfig() const {
    BusConfig config;
    for (std::size_t i = 0; i < config.buses.size(); ++i) {
        const AudioBusKind kind = busFromDef(static_cast<AudioDefBus>(i));
        BusDef& def = config.buses[i];
        def.volume = getBusVolume(kind);
        def.muted = isBusMuted(kind);
        const auto& slots = busFilterSlots_[static_cast<std::size_t>(kind)];
        for (unsigned int slot = 0; slot < FILTERS_PER_STREAM; ++slot) {
            def.slots[slot].name = slots[slot].name;
        }
    }
    return config;
}

bool AudioWorld::attachGlobalFilter(std::string_view name, unsigned int slot) {
    if (!ready_) {
        return false;
    }
    const unsigned int useSlot = slot < FILTERS_PER_STREAM ? slot : globalFilterCount_;
    if (useSlot >= FILTERS_PER_STREAM) {
        return false;
    }
    SoLoud::Filter* filter = makeBuiltinFilter(name);
    if (filter == nullptr) {
        return false;
    }
    soloud_.setGlobalFilter(useSlot, filter);
    if (useSlot >= globalFilterCount_) {
        globalFilterCount_ = useSlot + 1;
    }
    return true;
}

void AudioWorld::registerSchemeFilter(std::string name, void* proc) {
    schemeFilters_[std::move(name)] = proc;
}

bool AudioWorld::hasSchemeFilter(std::string_view name) const {
    return schemeFilters_.contains(std::string{name});
}

}
