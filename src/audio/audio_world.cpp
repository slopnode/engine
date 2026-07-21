#include "audio/audio_world.hpp"

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

#include <utility>

namespace slopengine {

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

    sfxBusHandle_ = soloud_.play(sfxBus_);
    musicBusHandle_ = soloud_.play(musicBus_);
    ready_ = true;
    return true;
}

void AudioWorld::deinit() {
    if (!ready_) {
        return;
    }

    stopMusic();
    soloud_.stopAll();
    clips_.clear();
    clipsByDef_.clear();
    streams_.clear();
    streamsByDef_.clear();
    sfxrByDef_.clear();
    filters_.clear();
    sfxFilterCount_ = 0;
    musicFilterCount_ = 0;
    globalFilterCount_ = 0;
    schemeFilters_.clear();
    soloud_.deinit();
    sfxBusHandle_ = 0;
    musicBusHandle_ = 0;
    ready_ = false;
}

SoLoud::Bus& AudioWorld::bus(AudioBusKind kind) {
    return kind == AudioBusKind::Music ? musicBus_ : sfxBus_;
}

unsigned int& AudioWorld::busFilterCount(AudioBusKind kind) {
    return kind == AudioBusKind::Music ? musicFilterCount_ : sfxFilterCount_;
}

AudioBusKind AudioWorld::busFromDef(AudioDefBus bus) {
    return bus == AudioDefBus::Music ? AudioBusKind::Music : AudioBusKind::Sfx;
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
    if (def.filters.empty()) {
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
    if (def.filters.empty()) {
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

void AudioWorld::applyFilters(SoLoud::AudioSource& source, const std::vector<std::string>& names) {
    for (unsigned int i = 0; i < names.size() && i < FILTERS_PER_STREAM; ++i) {
        SoLoud::Filter* filter = makeBuiltinFilter(names[i]);
        if (filter != nullptr) {
            source.setFilter(i, filter);
        }
    }
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

SoLoud::Sfxr* AudioWorld::loadSfxrForDef(std::string_view defPath, const AudioDef& def) {
    const std::string key{defPath};
    if (auto it = sfxrByDef_.find(key); it != sfxrByDef_.end()) {
        return it->second.get();
    }

    auto sfxr = std::make_unique<SoLoud::Sfxr>();
    applySaudioParams(*sfxr, def.sfxr);
    applyFilters(*sfxr, def.filters);
    SoLoud::Sfxr* ptr = sfxr.get();
    sfxrByDef_.emplace(key, std::move(sfxr));
    return ptr;
}

SoLoud::handle AudioWorld::playSfxrDef(
    std::string_view defPath,
    const AudioDef& def,
    float volume,
    bool spatial3d,
    float x,
    float y,
    float z) {
    SoLoud::Sfxr* sfxr = loadSfxrForDef(defPath, def);
    if (sfxr == nullptr) {
        return 0;
    }

    applySaudioParams(*sfxr, def.sfxr);
    sfxr->setLooping(def.loop);

    if (def.bus == AudioDefBus::Music) {
        stopMusic();
    }

    SoLoud::Bus& target = bus(busFromDef(def.bus));
    SoLoud::handle voice = 0;
    if (spatial3d || def.spatial) {
        sfxr->set3dMinMaxDistance(def.minDistance, def.maxDistance);
        voice = target.play3d(*sfxr, x, y, z, 0.0f, 0.0f, 0.0f, volume);
    } else {
        voice = target.play(*sfxr, volume);
    }

    if (def.bus == AudioDefBus::Music) {
        musicVoice_ = voice;
    }
    return voice;
}

SoLoud::handle AudioWorld::playSound(
    AssetStore& assets,
    std::string_view path,
    float volume,
    bool loop) {
    if (!ready_) {
        return 0;
    }
    SoLoud::Wav* clip = loadClip(assets, path);
    if (clip == nullptr) {
        return 0;
    }
    clip->setLooping(loop);
    return sfxBus_.play(*clip, volume);
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
    float maxDistance) {
    if (!ready_) {
        return 0;
    }
    SoLoud::Wav* clip = loadClip(assets, path);
    if (clip == nullptr) {
        return 0;
    }
    clip->setLooping(loop);
    clip->set3dMinMaxDistance(minDistance, maxDistance);
    return sfxBus_.play3d(*clip, x, y, z, 0.0f, 0.0f, 0.0f, volume);
}

SoLoud::handle AudioWorld::playAudioDef(
    AssetStore& assets,
    std::string_view defPath,
    const AudioDef& def,
    float volumeOverride) {
    if (!ready_) {
        return 0;
    }

    const float volume = volumeOverride >= 0.0f ? volumeOverride : def.volume;

    if (def.kind == AudioDefKind::Sfxr) {
        return playSfxrDef(defPath, def, volume, false, 0.0f, 0.0f, 0.0f);
    }

    if (def.source.empty()) {
        return 0;
    }

    const AudioBusKind busKind = busFromDef(def.bus);

    if (busKind == AudioBusKind::Music) {
        SoLoud::WavStream* stream = loadStreamForDef(assets, defPath, def);
        if (stream == nullptr) {
            return 0;
        }
        stopMusic();
        stream->setLooping(def.loop);
        musicVoice_ = musicBus_.play(*stream, volume);
        return musicVoice_;
    }

    SoLoud::Wav* clip = loadClipForDef(assets, defPath, def);
    if (clip == nullptr) {
        return 0;
    }
    clip->setLooping(def.loop);
    if (def.spatial) {
        clip->set3dMinMaxDistance(def.minDistance, def.maxDistance);
        return sfxBus_.play3d(*clip, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, volume);
    }
    return sfxBus_.play(*clip, volume);
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

    if (def.kind == AudioDefKind::Sfxr) {
        if (def.bus == AudioDefBus::Music) {
            return playSfxrDef(defPath, def, volume, false, 0.0f, 0.0f, 0.0f);
        }
        return playSfxrDef(defPath, def, volume, true, x, y, z);
    }

    if (def.source.empty()) {
        return 0;
    }

    if (def.bus == AudioDefBus::Music) {
        return playAudioDef(assets, defPath, def, volumeOverride);
    }

    SoLoud::Wav* clip = loadClipForDef(assets, defPath, def);
    if (clip == nullptr) {
        return 0;
    }
    clip->setLooping(def.loop);
    clip->set3dMinMaxDistance(def.minDistance, def.maxDistance);
    return sfxBus_.play3d(*clip, x, y, z, 0.0f, 0.0f, 0.0f, volume);
}

void AudioWorld::stop(SoLoud::handle voice) {
    if (!ready_ || voice == 0) {
        return;
    }
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
    const SoLoud::handle handle = kind == AudioBusKind::Music ? musicBusHandle_ : sfxBusHandle_;
    if (handle != 0) {
        soloud_.setVolume(handle, volume);
    }
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
    musicVoice_ = musicBus_.play(*stream, volume);
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

SoLoud::Filter* AudioWorld::makeBuiltinFilter(std::string_view name) {
    std::unique_ptr<SoLoud::Filter> filter;
    if (name == "echo") {
        filter = std::make_unique<SoLoud::EchoFilter>();
    } else if (name == "biquad") {
        filter = std::make_unique<SoLoud::BiquadResonantFilter>();
    } else if (name == "lofi") {
        filter = std::make_unique<SoLoud::LofiFilter>();
    } else if (name == "flanger") {
        filter = std::make_unique<SoLoud::FlangerFilter>();
    } else if (name == "bassboost") {
        filter = std::make_unique<SoLoud::BassboostFilter>();
    } else if (name == "freeverb") {
        filter = std::make_unique<SoLoud::FreeverbFilter>();
    } else if (name == "robotize") {
        filter = std::make_unique<SoLoud::RobotizeFilter>();
    } else if (name == "waveshaper") {
        filter = std::make_unique<SoLoud::WaveShaperFilter>();
    } else if (name == "dcremoval") {
        filter = std::make_unique<SoLoud::DCRemovalFilter>();
    } else {
        return nullptr;
    }

    SoLoud::Filter* ptr = filter.get();
    filters_.push_back(std::move(filter));
    return ptr;
}

bool AudioWorld::attachBuiltinFilter(AudioBusKind kind, std::string_view name, unsigned int slot) {
    if (!ready_) {
        return false;
    }
    unsigned int& count = busFilterCount(kind);
    const unsigned int useSlot = slot < FILTERS_PER_STREAM ? slot : count;
    if (useSlot >= FILTERS_PER_STREAM) {
        return false;
    }
    SoLoud::Filter* filter = makeBuiltinFilter(name);
    if (filter == nullptr) {
        return false;
    }
    bus(kind).setFilter(useSlot, filter);
    if (useSlot >= count) {
        count = useSlot + 1;
    }
    return true;
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
