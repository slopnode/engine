#pragma once

#include "assets/asset_store.hpp"
#include "assets/audio_def.hpp"

#include <soloud.h>
#include <soloud_bus.h>
#include <soloud_sfxr.h>
#include <soloud_wav.h>
#include <soloud_wavstream.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace slopengine {

enum class AudioBusKind {
    Sfx,
    Music,
};

class AudioWorld {
public:
    AudioWorld();
    ~AudioWorld();

    AudioWorld(const AudioWorld&) = delete;
    AudioWorld& operator=(const AudioWorld&) = delete;

    bool init();
    void deinit();
    bool ready() const { return ready_; }

    SoLoud::Soloud& soloud() { return soloud_; }
    const SoLoud::Soloud& soloud() const { return soloud_; }

    SoLoud::handle playSound(
        AssetStore& assets,
        std::string_view path,
        float volume = 1.0f,
        bool loop = false);
    SoLoud::handle playSound3d(
        AssetStore& assets,
        std::string_view path,
        float x,
        float y,
        float z,
        float volume = 1.0f,
        bool loop = false,
        float minDistance = 1.0f,
        float maxDistance = 30.0f);
    SoLoud::handle playAudioDef(
        AssetStore& assets,
        std::string_view defPath,
        const AudioDef& def,
        float volumeOverride = -1.0f);
    SoLoud::handle playAudioDef3d(
        AssetStore& assets,
        std::string_view defPath,
        const AudioDef& def,
        float x,
        float y,
        float z,
        float volumeOverride = -1.0f);

    void stop(SoLoud::handle voice);
    void setVolume(SoLoud::handle voice, float volume);
    void setBusVolume(AudioBusKind bus, float volume);

    bool playMusic(AssetStore& assets, std::string_view path, float volume = 1.0f);
    void stopMusic();

    void setListener(
        float posX,
        float posY,
        float posZ,
        float atX,
        float atY,
        float atZ,
        float upX,
        float upY,
        float upZ);
    void setSourcePosition(SoLoud::handle voice, float x, float y, float z);
    void update3d();

    bool attachBuiltinFilter(AudioBusKind bus, std::string_view name, unsigned int slot = 0);
    bool attachGlobalFilter(std::string_view name, unsigned int slot = 0);
    void registerSchemeFilter(std::string name, void* proc);
    bool hasSchemeFilter(std::string_view name) const;

private:
    SoLoud::Wav* loadClip(AssetStore& assets, std::string_view path);
    SoLoud::Wav* loadClipForDef(AssetStore& assets, std::string_view defPath, const AudioDef& def);
    SoLoud::WavStream* loadStream(AssetStore& assets, std::string_view path);
    SoLoud::WavStream* loadStreamForDef(AssetStore& assets, std::string_view defPath, const AudioDef& def);
    SoLoud::Sfxr* loadSfxrForDef(std::string_view defPath, const AudioDef& def);
    SoLoud::handle playSfxrDef(
        std::string_view defPath,
        const AudioDef& def,
        float volume,
        bool spatial3d,
        float x,
        float y,
        float z);
    void applyFilters(SoLoud::AudioSource& source, const std::vector<std::string>& names);
    SoLoud::Filter* makeBuiltinFilter(std::string_view name);
    SoLoud::Bus& bus(AudioBusKind kind);
    unsigned int& busFilterCount(AudioBusKind kind);
    static AudioBusKind busFromDef(AudioDefBus bus);
    static void applySaudioParams(SoLoud::Sfxr& sfxr, const SaudioParams& params);

    SoLoud::Soloud soloud_;
    SoLoud::Bus sfxBus_;
    SoLoud::Bus musicBus_;
    SoLoud::handle sfxBusHandle_ = 0;
    SoLoud::handle musicBusHandle_ = 0;
    SoLoud::handle musicVoice_ = 0;
    bool ready_ = false;

    std::unordered_map<std::string, std::unique_ptr<SoLoud::Wav>> clips_;
    std::unordered_map<std::string, std::unique_ptr<SoLoud::Wav>> clipsByDef_;
    std::unordered_map<std::string, std::unique_ptr<SoLoud::WavStream>> streams_;
    std::unordered_map<std::string, std::unique_ptr<SoLoud::WavStream>> streamsByDef_;
    std::unordered_map<std::string, std::unique_ptr<SoLoud::Sfxr>> sfxrByDef_;
    std::vector<std::unique_ptr<SoLoud::Filter>> filters_;
    unsigned int sfxFilterCount_ = 0;
    unsigned int musicFilterCount_ = 0;
    std::unordered_map<std::string, void*> schemeFilters_;
    unsigned int globalFilterCount_ = 0;
};

}
