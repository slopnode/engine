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

#include <array>
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
    Ui,
    Hud,
    Voices,
    Ambience,
    Player,
    View,
    Count,
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
        bool loop = false,
        bool viaBus = true);
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
        float volumeOverride = -1.0f,
        bool viaBus = true);
    SoLoud::handle playAudioDef3d(
        AssetStore& assets,
        std::string_view defPath,
        const AudioDef& def,
        float x,
        float y,
        float z,
        float volumeOverride = -1.0f);
    // Auditions a bare procedural (.sfxr) def with no owning Definition: plain Sfx bus,
    // no loop/spatial/filters. For tooling (slopaudio) previewing a Procedural document.
    SoLoud::handle playProcedural(
        std::string_view proceduralPath,
        const SaudioParams& params,
        float volume = 1.0f,
        bool viaBus = true);

    void stop(SoLoud::handle voice);
    void setVolume(SoLoud::handle voice, float volume);
    void setBusVolume(AudioBusKind bus, float volume);
    float getBusVolume(AudioBusKind bus) const;
    // Mutes without losing the volume set via setBusVolume; unmuting restores it.
    void setBusMuted(AudioBusKind bus, bool muted);
    bool isBusMuted(AudioBusKind bus) const;

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

    // Instantiates one of the builtin SoLoud filters by name ("echo", "biquad", "lofi",
    // "flanger", "bassboost", "freeverb", "robotize", "waveshaper", "dcremoval"), or nullptr
    // for an unknown name. Ownerless -- callers who just want param metadata (getParamCount /
    // getParamName / getParamMin / getParamMax) can query it and let it destruct; attaching it
    // to a bus goes through attachBuiltinFilter instead, which keeps it alive.
    static std::unique_ptr<SoLoud::Filter> createBuiltinFilter(std::string_view name);

    bool attachBuiltinFilter(AudioBusKind bus, std::string_view name, unsigned int slot = 0);
    bool attachGlobalFilter(std::string_view name, unsigned int slot = 0);
    void clearBusFilter(AudioBusKind bus, unsigned int slot);
    // Name of the builtin filter attached at @p bus / @p slot, or empty when unattached.
    std::string_view busFilterName(AudioBusKind bus, unsigned int slot) const;
    bool setBusFilterParam(AudioBusKind bus, unsigned int slot, unsigned int paramIndex, float value);
    // Not const: SoLoud::Soloud::getFilterParameter isn't const-qualified even though it
    // only reads live filter-instance state.
    float getBusFilterParam(AudioBusKind bus, unsigned int slot, unsigned int paramIndex);
    void registerSchemeFilter(std::string name, void* proc);
    bool hasSchemeFilter(std::string_view name) const;

    // Drops any per-definition-cached clip/stream/sfxr for defPath so the next play rebuilds
    // it from scratch. Needed after editing a def's filter chain (add/remove/reorder), since
    // filters are attached to the AudioSource once at load time and cached by defPath --
    // param value edits don't need this, they're re-applied to every voice on play.
    void invalidateDefCache(std::string_view defPath);

    // Applies a full mixer snapshot (volume/mute/filter chains) to every bus, replacing
    // whatever is currently attached. Used at startup to apply audio/buses.saudio.
    void loadBusConfig(const BusConfig& config);
    // Reads the live mixer state back out, e.g. for slopaudio to save what's being auditioned.
    BusConfig exportBusConfig() const;
    // Maps a BusConfig::buses index (AudioDefBus) to the AudioBusKind used everywhere else
    // in this API. Public so tooling can iterate a BusConfig without reaching into privates.
    static AudioBusKind busKindFromDefBus(AudioDefBus bus) { return busFromDef(bus); }

private:
    SoLoud::Wav* loadClip(AssetStore& assets, std::string_view path);
    SoLoud::Wav* loadClipForDef(AssetStore& assets, std::string_view defPath, const AudioDef& def);
    SoLoud::WavStream* loadStream(AssetStore& assets, std::string_view path);
    SoLoud::WavStream* loadStreamForDef(AssetStore& assets, std::string_view defPath, const AudioDef& def);
    SoLoud::Sfxr* loadSfxrForDef(std::string_view defPath, const SaudioParams& params, const AudioDef& def);
    SoLoud::handle playSfxrDef(
        std::string_view defPath,
        const AudioDef& def,
        const SaudioParams& params,
        float volume,
        bool spatial3d,
        float x,
        float y,
        float z,
        bool viaBus = true);
    void applyFilters(SoLoud::AudioSource& source, const std::array<BusFilterSlot, kMaxFiltersPerBus>& slots);
    // Pushes def.filters' persisted param values onto the just-created voice, overriding
    // whatever defaults applyFilters/createInstance baked in. Safe to call on any voice --
    // SoLoud no-ops a setFilterParameter call for a slot the voice has nothing attached to.
    void applyDefFilterParams(SoLoud::handle voice, const AudioDef& def);
    SoLoud::Filter* makeBuiltinFilter(std::string_view name);
    SoLoud::Bus& bus(AudioBusKind kind);
    unsigned int nextFreeBusSlot(AudioBusKind kind) const;
    static AudioBusKind busFromDef(AudioDefBus bus);
    static AudioDefBus defFromBus(AudioBusKind bus);
    static void applySaudioParams(SoLoud::Sfxr& sfxr, const SaudioParams& params);

    void trackListenerAttached(SoLoud::handle voice, float forwardOffset);

#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    SoLoud::Wav* ensureStereoClip(SoLoud::Wav* clip, std::string_view cacheKey);
    SoLoud::Wav* bakeSfxrStereo(SoLoud::Sfxr& sfxr, std::string_view cacheKey, bool loop);
    SoLoud::Wav* prepareSpatialSteamWav(SoLoud::Wav* clip, std::string_view cacheKey, bool loop);
    SoLoud::handle playSpatialSteam(
        AudioBusKind busKind,
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

    // What's attached at one filter slot on a bus, mirroring what SoLoud itself holds so
    // slopaudio can read back the live chain (name + param values) for the Bus tab and saves.
    struct BusFilterState {
        std::string name;
        SoLoud::Filter* filter = nullptr;
    };

    static constexpr std::size_t kBusCount = static_cast<std::size_t>(AudioBusKind::Count);

    SoLoud::Soloud soloud_;
    std::array<SoLoud::Bus, kBusCount> buses_;
    std::array<SoLoud::handle, kBusCount> busHandles_{};
    std::array<std::array<BusFilterState, FILTERS_PER_STREAM>, kBusCount> busFilterSlots_{};
    std::array<float, kBusCount> busVolumes_{};
    std::array<bool, kBusCount> busMuted_{};
    SoLoud::handle musicVoice_ = 0;
    bool ready_ = false;

    std::unordered_map<std::string, std::unique_ptr<SoLoud::Wav>> clips_;
    std::unordered_map<std::string, std::unique_ptr<SoLoud::Wav>> clipsByDef_;
    std::unordered_map<std::string, std::unique_ptr<SoLoud::WavStream>> streams_;
    std::unordered_map<std::string, std::unique_ptr<SoLoud::WavStream>> streamsByDef_;
    std::unordered_map<std::string, std::unique_ptr<SoLoud::Sfxr>> sfxrByDef_;
    std::vector<std::unique_ptr<SoLoud::Filter>> filters_;
    std::vector<ListenerAttachedVoice> listenerAttached_;
    int underwaterFilterSlot_ = -1;
    std::unordered_map<std::string, void*> schemeFilters_;
    unsigned int globalFilterCount_ = 0;

#ifdef SLOPENGINE_HAS_STEAM_AUDIO
    std::unique_ptr<SteamAudioRuntime> steamAudio_;
    std::unordered_map<std::string, std::unique_ptr<SoLoud::Wav>> stereoClips_;
#endif
};

}
