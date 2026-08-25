#pragma once

#include "core/win32.hpp"

#include "assets/asset_store.hpp"
#include "assets/audio_def.hpp"
#include "audio/steam_audio_types.hpp"
#include "map/brush.hpp"

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
#include <unordered_set>
#include <vector>

namespace slopengine {

enum class AudioBusKind {
    Sfx,
    Music,
};

#ifdef SLOPENGINE_HAS_STEAM_AUDIO
class SteamAudioRuntime;
#endif

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
        float maxDistance = 30.0f,
        bool followListener = false,
        float followForwardOffset = 0.0f);
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
    void updateListenerAttachedSources(const SteamAudioListenerPose& listener);

    bool steamAudioEnabled() const;
    void setSteamAudioScene(
        const std::vector<Brush>& brushes,
        const std::unordered_set<std::string>* excludeBrushIds);
    void clearSteamAudioScene();
    void updateSteamAudio(
        const SteamAudioListenerPose& listener,
        const std::vector<SteamAudioSourcePose>& sources);

    // Fades a lowpass filter on the Sfx bus only (music stays dry) to muffle
    // world sound while the listener is underwater. amount01 0 = dry, 1 = fully
    // muffled; the filter attaches lazily on first non-zero call.
    bool setUnderwaterMuffle(float amount01);

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

    void trackListenerAttached(SoLoud::handle voice, float forwardOffset);

#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    SoLoud::Wav* ensureStereoClip(SoLoud::Wav* clip, std::string_view cacheKey);
    SoLoud::Wav* bakeSfxrStereo(SoLoud::Sfxr& sfxr, std::string_view cacheKey, bool loop);
    SoLoud::Wav* prepareSpatialSteamWav(SoLoud::Wav* clip, std::string_view cacheKey, bool loop);
    SoLoud::handle playSpatialSteam(
        SoLoud::Wav& source,
        float x,
        float y,
        float z,
        float volume,
        float minDistance,
        float maxDistance,
        bool followListener,
        float followForwardOffset);
#endif

    struct ListenerAttachedVoice {
        SoLoud::handle voice = 0;
        float forwardOffset = 0.0f;
    };

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
    std::vector<ListenerAttachedVoice> listenerAttached_;
    unsigned int sfxFilterCount_ = 0;
    unsigned int musicFilterCount_ = 0;
    int underwaterFilterSlot_ = -1;
    std::unordered_map<std::string, void*> schemeFilters_;
    unsigned int globalFilterCount_ = 0;

#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    std::unique_ptr<SteamAudioRuntime> steamAudio_;
    std::unordered_map<std::string, std::unique_ptr<SoLoud::Wav>> stereoClips_;
#endif
};

}
