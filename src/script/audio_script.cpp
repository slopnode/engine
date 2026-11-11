#include "script/audio_script.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include "assets/audio_def.hpp"
#include "audio/audio_module.hpp"
#include "audio/audio_world.hpp"
#include "audio/components.hpp"

#include <s7.h>

#include <cstring>
#include <optional>
#include <string>

namespace slopengine {

namespace {

flecs::world* g_audioWorld = nullptr;

std::optional<AudioBusKind> audioBusKindFromName(const char* name) {
    if (std::strcmp(name, "sfx") == 0) return AudioBusKind::Sfx;
    if (std::strcmp(name, "music") == 0) return AudioBusKind::Music;
    if (std::strcmp(name, "ui") == 0) return AudioBusKind::Ui;
    if (std::strcmp(name, "hud") == 0) return AudioBusKind::Hud;
    if (std::strcmp(name, "voices") == 0) return AudioBusKind::Voices;
    if (std::strcmp(name, "ambience") == 0) return AudioBusKind::Ambience;
    if (std::strcmp(name, "player") == 0) return AudioBusKind::Player;
    if (std::strcmp(name, "view") == 0) return AudioBusKind::View;
    return std::nullopt;
}

AudioWorld* audioWorld() {
    if (g_audioWorld == nullptr || !g_audioWorld->has<AudioContext>()) {
        return nullptr;
    }
    return g_audioWorld->get<AudioContext>().world;
}

AssetStore* audioAssets() {
    if (g_audioWorld == nullptr || !g_audioWorld->has<AudioContext>()) {
        return nullptr;
    }
    return g_audioWorld->get<AudioContext>().assets;
}

s7_pointer g_play_audio(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    AssetStore* assets = audioAssets();
    if (audio == nullptr || assets == nullptr || !audio->ready()) {
        return s7_make_integer(sc, 0);
    }
    ArgReader in(sc, "play-audio", args);
    const char* path = in.string("string");
    float volumeOverride = -1.0f;
    if (in.more()) {
        volumeOverride = in.realf("real");
    }

    const AudioDef* def = assets->getAudioDef(path);
    if (def == nullptr) {
        return s7_make_integer(sc, 0);
    }

    const SoLoud::handle voice = audio->playAudioDef(*assets, path, *def, volumeOverride);
    return s7_make_integer(sc, static_cast<s7_int>(voice));
}

s7_pointer g_play_audio_3d(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    AssetStore* assets = audioAssets();
    if (audio == nullptr || assets == nullptr || !audio->ready()) {
        return s7_make_integer(sc, 0);
    }
    ArgReader in(sc, "play-audio-3d", args);
    const char* path = in.string("string");
    const float x = in.realf("x number");
    const float y = in.realf("y number");
    const float z = in.realf("z number");
    float volumeOverride = -1.0f;
    if (in.more()) {
        volumeOverride = in.realf("real");
    }

    const AudioDef* def = assets->getAudioDef(path);
    if (def == nullptr) {
        return s7_make_integer(sc, 0);
    }

    const SoLoud::handle voice =
        audio->playAudioDef3d(*assets, path, *def, x, y, z, volumeOverride);
    return s7_make_integer(sc, static_cast<s7_int>(voice));
}

s7_pointer g_audio_attach(s7_scheme* sc, s7_pointer args) {
    if (g_audioWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "audio-attach", args);
    const std::string id = in.string("id string");
    const std::string path = in.string("audio path string");

    flecs::entity entity = g_audioWorld->lookup(id.c_str());
    if (!entity.is_valid()) {
        return s7_f(sc);
    }

    AssetStore* assets = audioAssets();
    if (assets == nullptr) {
        return s7_f(sc);
    }
    const AudioDef* def = assets->getAudioDef(path);
    if (def == nullptr) {
        return s7_f(sc);
    }

    entity.set<AudioSource>({
        .audio = path,
        .volume = def->volume,
        .minDistance = def->minDistance,
        .maxDistance = def->maxDistance,
        .looping = def->loop,
        .spatial = def->spatial,
        .autoplay = true,
        .playing = false,
        .voice = 0,
    });
    return s7_t(sc);
}

s7_pointer g_play_sound(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    AssetStore* assets = audioAssets();
    if (audio == nullptr || assets == nullptr || !audio->ready()) {
        return s7_make_integer(sc, 0);
    }
    ArgReader in(sc, "play-sound", args);
    const char* path = in.string("string");
    float volume = 1.0f;
    bool loop = false;
    if (in.more()) {
        volume = in.realf("real");
    }
    if (in.more()) {
        loop = in.truthy("boolean");
    }

    const SoLoud::handle voice = audio->playSound(*assets, path, volume, loop);
    return s7_make_integer(sc, static_cast<s7_int>(voice));
}

s7_pointer g_stop_sound(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "stop-sound", args);
    const s7_int handle = in.exactInteger("integer");
    audio->stop(static_cast<SoLoud::handle>(handle));
    return s7_t(sc);
}

s7_pointer g_set_sound_volume(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "set-sound-volume", args);
    const s7_int handle = in.exactInteger("integer");
    const float volume = in.realf("real");
    audio->setVolume(static_cast<SoLoud::handle>(handle), volume);
    return s7_t(sc);
}

s7_pointer g_set_bus_volume(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "set-bus-volume", args);
    const char* busName = in.string("string");
    const float volume = in.realf("real");

    const std::optional<AudioBusKind> bus = audioBusKindFromName(busName);
    if (!bus) {
        return s7_f(sc);
    }
    audio->setBusVolume(*bus, volume);
    return s7_t(sc);
}

s7_pointer g_get_bus_volume(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_make_real(sc, 0.0);
    }
    ArgReader in(sc, "get-bus-volume", args);
    const char* busName = in.string("string");

    const std::optional<AudioBusKind> bus = audioBusKindFromName(busName);
    if (!bus) {
        return s7_make_real(sc, 0.0);
    }
    return s7_make_real(sc, static_cast<double>(audio->getBusVolume(*bus)));
}

s7_pointer g_set_bus_muted(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "set-bus-muted", args);
    const char* busName = in.string("string");
    const bool muted = in.truthy("boolean");

    const std::optional<AudioBusKind> bus = audioBusKindFromName(busName);
    if (!bus) {
        return s7_f(sc);
    }
    audio->setBusMuted(*bus, muted);
    return s7_t(sc);
}

s7_pointer g_bus_muted_p(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "bus-muted?", args);
    const char* busName = in.string("string");

    const std::optional<AudioBusKind> bus = audioBusKindFromName(busName);
    if (!bus) {
        return s7_f(sc);
    }
    return s7_make_boolean(sc, audio->isBusMuted(*bus));
}

s7_pointer g_play_music(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    AssetStore* assets = audioAssets();
    if (audio == nullptr || assets == nullptr || !audio->ready()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "play-music", args);
    const char* path = in.string("string");
    float volume = 1.0f;
    if (in.more()) {
        volume = in.realf("real");
    }
    return s7_make_boolean(sc, audio->playMusic(*assets, path, volume));
}

s7_pointer g_stop_music(s7_scheme* sc, s7_pointer) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    audio->stopMusic();
    return s7_t(sc);
}

constexpr SchemeBinding kPlaybackBindings[] = {
    {"play-audio", g_play_audio, 1, 1, false, "(play-audio path [volume])", ScriptCap::Audio},
    {"play-audio-3d", g_play_audio_3d, 4, 1, false, "(play-audio-3d path x y z [volume])", ScriptCap::Audio},
    {"audio-attach", g_audio_attach, 2, 0, false, "(audio-attach id path)", ScriptCap::WorldMutate},
    {"play-sound", g_play_sound, 1, 2, false, "(play-sound path [volume] [loop?])", ScriptCap::Audio},
    {"stop-sound", g_stop_sound, 1, 0, false, "(stop-sound handle)", ScriptCap::Audio},
    {"set-sound-volume", g_set_sound_volume, 2, 0, false, "(set-sound-volume handle vol)", ScriptCap::Audio},
    {"set-bus-volume", g_set_bus_volume, 2, 0, false, "(set-bus-volume bus vol)", ScriptCap::Audio},
    {"get-bus-volume", g_get_bus_volume, 1, 0, false, "(get-bus-volume bus)", ScriptCap::Audio},
    {"set-bus-muted", g_set_bus_muted, 2, 0, false, "(set-bus-muted bus muted?)", ScriptCap::Audio},
    {"bus-muted?", g_bus_muted_p, 1, 0, false, "(bus-muted? bus)", ScriptCap::Audio},
    {"play-music", g_play_music, 1, 1, false, "(play-music path [volume])", ScriptCap::Audio},
    {"stop-music", g_stop_music, 0, 0, false, "(stop-music)", ScriptCap::Audio},
};

s7_pointer g_audio_filter_attach(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr || !audio->ready()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "audio-filter-attach", args);
    const char* target = in.string("string");
    const char* filterName = in.string("string");
    unsigned int slot = FILTERS_PER_STREAM;
    if (in.more()) {
        slot = static_cast<unsigned int>(in.exactInteger("integer"));
    }

    bool ok = false;
    if (std::strcmp(target, "global") == 0) {
        ok = audio->attachGlobalFilter(filterName, slot);
    } else if (const std::optional<AudioBusKind> bus = audioBusKindFromName(target)) {
        ok = audio->attachBuiltinFilter(*bus, filterName, slot);
    }
    return s7_make_boolean(sc, ok);
}

s7_pointer g_audio_filter_clear(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr || !audio->ready()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "audio-filter-clear", args);
    const char* busName = in.string("string");
    const unsigned int slot = static_cast<unsigned int>(in.exactInteger("integer"));

    const std::optional<AudioBusKind> bus = audioBusKindFromName(busName);
    if (!bus) {
        return s7_f(sc);
    }
    audio->clearBusFilter(*bus, slot);
    return s7_t(sc);
}

s7_pointer g_audio_filter_name(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_make_string(sc, "");
    }
    ArgReader in(sc, "audio-filter-name", args);
    const char* busName = in.string("string");
    const unsigned int slot = static_cast<unsigned int>(in.exactInteger("integer"));

    const std::optional<AudioBusKind> bus = audioBusKindFromName(busName);
    if (!bus) {
        return s7_make_string(sc, "");
    }
    const std::string_view name = audio->busFilterName(*bus, slot);
    return s7_make_string_with_length(sc, name.data(), static_cast<int>(name.size()));
}

s7_pointer g_audio_filter_set_param(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr || !audio->ready()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "audio-filter-set-param", args);
    const char* busName = in.string("string");
    const unsigned int slot = static_cast<unsigned int>(in.exactInteger("integer"));
    const unsigned int paramIndex = static_cast<unsigned int>(in.exactInteger("integer"));
    const float value = in.realf("real");

    const std::optional<AudioBusKind> bus = audioBusKindFromName(busName);
    if (!bus) {
        return s7_f(sc);
    }
    return s7_make_boolean(sc, audio->setBusFilterParam(*bus, slot, paramIndex, value));
}

s7_pointer g_audio_filter_get_param(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr || !audio->ready()) {
        return s7_make_real(sc, 0.0);
    }
    ArgReader in(sc, "audio-filter-get-param", args);
    const char* busName = in.string("string");
    const unsigned int slot = static_cast<unsigned int>(in.exactInteger("integer"));
    const unsigned int paramIndex = static_cast<unsigned int>(in.exactInteger("integer"));

    const std::optional<AudioBusKind> bus = audioBusKindFromName(busName);
    if (!bus) {
        return s7_make_real(sc, 0.0);
    }
    return s7_make_real(sc, static_cast<double>(audio->getBusFilterParam(*bus, slot, paramIndex)));
}

s7_pointer g_register_audio_filter(s7_scheme* sc, s7_pointer args) {
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "register-audio-filter", args);
    const char* name = in.string("string");
    s7_pointer proc = in.any("procedure");
    if (!s7_is_procedure(proc)) {
        return in.wrongTypeAt(2, proc, "procedure");
    }

    s7_gc_protect(sc, proc);
    audio->registerSchemeFilter(name, proc);
    return s7_t(sc);
}

constexpr SchemeBinding kFilterBindings[] = {
    {"audio-filter-attach", g_audio_filter_attach, 2, 1, false, "(audio-filter-attach target filter [slot])", ScriptCap::Audio},
    {"audio-filter-clear", g_audio_filter_clear, 2, 0, false, "(audio-filter-clear bus slot)", ScriptCap::Audio},
    {"audio-filter-name", g_audio_filter_name, 2, 0, false, "(audio-filter-name bus slot)", ScriptCap::Audio},
    {"audio-filter-set-param", g_audio_filter_set_param, 4, 0, false, "(audio-filter-set-param bus slot param-index value)", ScriptCap::Audio},
    {"audio-filter-get-param", g_audio_filter_get_param, 3, 0, false, "(audio-filter-get-param bus slot param-index)", ScriptCap::Audio},
    {"register-audio-filter", g_register_audio_filter, 2, 0, false, "(register-audio-filter name proc)", ScriptCap::Audio},
};

} // namespace

void bindAudioApi(flecs::world& world, s7_scheme* scheme) {
    g_audioWorld = &world;
    if (scheme == nullptr) {
        return;
    }

    registerBindings(scheme, kPlaybackBindings);
    registerBindings(scheme, kFilterBindings);
}

}
