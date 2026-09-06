#include "script/audio_script.hpp"
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

std::optional<AudioDefBus> audioDefBusFromName(const char* name) {
    if (std::strcmp(name, "sfx") == 0) return AudioDefBus::Sfx;
    if (std::strcmp(name, "music") == 0) return AudioDefBus::Music;
    if (std::strcmp(name, "ui") == 0) return AudioDefBus::Ui;
    if (std::strcmp(name, "hud") == 0) return AudioDefBus::Hud;
    if (std::strcmp(name, "voices") == 0) return AudioDefBus::Voices;
    if (std::strcmp(name, "ambience") == 0) return AudioDefBus::Ambience;
    if (std::strcmp(name, "player") == 0) return AudioDefBus::Player;
    if (std::strcmp(name, "view") == 0) return AudioDefBus::View;
    return std::nullopt;
}

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

bool parseAudioField(s7_scheme* sc, s7_pointer form, AudioDef& def, const char* proc) {
    if (!s7_is_pair(form)) {
        s7_wrong_type_arg_error(sc, proc, 0, form, "list");
        return false;
    }

    s7_pointer keyObj = s7_car(form);
    const char* key = nullptr;
    if (s7_is_symbol(keyObj)) {
        key = s7_symbol_name(keyObj);
    } else if (s7_is_string(keyObj)) {
        key = s7_string(keyObj);
    } else {
        s7_wrong_type_arg_error(sc, proc, 0, keyObj, "symbol or string");
        return false;
    }

    s7_pointer rest = s7_cdr(form);
    if (s7_is_null(sc, rest)) {
        return false;
    }
    s7_pointer value = s7_car(rest);

    if (std::strcmp(key, "source") == 0) {
        if (!s7_is_string(value)) {
            s7_wrong_type_arg_error(sc, proc, 0, value, "string");
            return false;
        }
        def.source = s7_string(value);
        return true;
    }
    if (std::strcmp(key, "volume") == 0) {
        if (!s7_is_real(value)) {
            s7_wrong_type_arg_error(sc, proc, 0, value, "real");
            return false;
        }
        def.volume = static_cast<float>(s7_number_to_real(sc, value));
        return true;
    }
    if (std::strcmp(key, "loop") == 0) {
        def.loop = s7_boolean(sc, value);
        return true;
    }
    if (std::strcmp(key, "spatial") == 0) {
        def.spatial = s7_boolean(sc, value);
        return true;
    }
    if (std::strcmp(key, "min-distance") == 0) {
        if (!s7_is_real(value)) {
            s7_wrong_type_arg_error(sc, proc, 0, value, "real");
            return false;
        }
        def.minDistance = static_cast<float>(s7_number_to_real(sc, value));
        return true;
    }
    if (std::strcmp(key, "max-distance") == 0) {
        if (!s7_is_real(value)) {
            s7_wrong_type_arg_error(sc, proc, 0, value, "real");
            return false;
        }
        def.maxDistance = static_cast<float>(s7_number_to_real(sc, value));
        return true;
    }
    if (std::strcmp(key, "bus") == 0) {
        if (!s7_is_string(value)) {
            s7_wrong_type_arg_error(sc, proc, 0, value, "string");
            return false;
        }
        const std::optional<AudioDefBus> bus = audioDefBusFromName(s7_string(value));
        def.bus = bus.value_or(AudioDefBus::Sfx);
        return true;
    }
    if (std::strcmp(key, "filter") == 0) {
        if (!s7_is_string(value)) {
            s7_wrong_type_arg_error(sc, proc, 0, value, "string");
            return false;
        }
        if (pushAudioDefFilter(def, s7_string(value)) == nullptr) {
            s7_wrong_type_arg_error(sc, proc, 0, value, "audio def already has the max number of filters");
            return false;
        }
        return true;
    }

    return false;
}

s7_pointer g_register_audio(s7_scheme* sc, s7_pointer args) {
    AssetStore* assets = audioAssets();
    if (assets == nullptr || !assets->isLoadingAudioDef()) {
        return s7_f(sc);
    }

    AudioDef def{};
    int argIndex = 1;
    for (s7_pointer it = args; !s7_is_null(sc, it); it = s7_cdr(it), ++argIndex) {
        if (!parseAudioField(sc, s7_car(it), def, "register-audio")) {
            return s7_wrong_type_arg_error(sc, "register-audio", argIndex, s7_car(it), "audio field");
        }
    }

    if (def.source.empty()) {
        return s7_f(sc);
    }
    return assets->commitAudioDef(std::move(def)) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_play_audio(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    AssetStore* assets = audioAssets();
    if (audio == nullptr || assets == nullptr || !audio->ready()) {
        return s7_make_integer(sc, 0);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "play-audio", 1, s7_car(args), "string");
    }

    const char* path = s7_string(s7_car(args));
    float volumeOverride = -1.0f;
    if (!s7_is_null(sc, s7_cdr(args))) {
        if (!s7_is_real(s7_cadr(args))) {
            return s7_wrong_type_arg_error(sc, "play-audio", 2, s7_cadr(args), "real");
        }
        volumeOverride = static_cast<float>(s7_number_to_real(sc, s7_cadr(args)));
    }

    const AudioDef* def = assets->getAudioDef(sc, path);
    if (def == nullptr) {
        return s7_make_integer(sc, 0);
    }

    const SoLoud::handle voice = audio->playAudioDef(*assets, path, *def, volumeOverride);
    return s7_make_integer(sc, static_cast<s7_int>(voice));
}

s7_pointer g_play_audio_3d(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    AssetStore* assets = audioAssets();
    if (audio == nullptr || assets == nullptr || !audio->ready()) {
        return s7_make_integer(sc, 0);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "play-audio-3d", 1, s7_car(args), "string");
    }

    const char* path = s7_string(s7_car(args));
    args = s7_cdr(args);

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!s7_is_pair(args) || !s7_is_number(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "play-audio-3d", 2, args, "x y z numbers");
    }
    x = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
    args = s7_cdr(args);
    if (!s7_is_pair(args) || !s7_is_number(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "play-audio-3d", 3, args, "y number");
    }
    y = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
    args = s7_cdr(args);
    if (!s7_is_pair(args) || !s7_is_number(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "play-audio-3d", 4, args, "z number");
    }
    z = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
    args = s7_cdr(args);

    float volumeOverride = -1.0f;
    if (!s7_is_null(sc, args)) {
        if (!s7_is_real(s7_car(args))) {
            return s7_wrong_type_arg_error(sc, "play-audio-3d", 5, s7_car(args), "real");
        }
        volumeOverride = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
    }

    const AudioDef* def = assets->getAudioDef(sc, path);
    if (def == nullptr) {
        return s7_make_integer(sc, 0);
    }

    const SoLoud::handle voice =
        audio->playAudioDef3d(*assets, path, *def, x, y, z, volumeOverride);
    return s7_make_integer(sc, static_cast<s7_int>(voice));
}

s7_pointer g_play_sound(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    AssetStore* assets = audioAssets();
    if (audio == nullptr || assets == nullptr || !audio->ready()) {
        return s7_make_integer(sc, 0);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "play-sound", 1, s7_car(args), "string");
    }

    const char* path = s7_string(s7_car(args));
    float volume = 1.0f;
    bool loop = false;
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_null(sc, rest)) {
        if (!s7_is_real(s7_car(rest))) {
            return s7_wrong_type_arg_error(sc, "play-sound", 2, s7_car(rest), "real");
        }
        volume = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        rest = s7_cdr(rest);
    }
    if (!s7_is_null(sc, rest)) {
        loop = s7_boolean(sc, s7_car(rest));
    }

    const SoLoud::handle voice = audio->playSound(*assets, path, volume, loop);
    return s7_make_integer(sc, static_cast<s7_int>(voice));
}

s7_pointer g_stop_sound(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_integer(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "stop-sound", 1, s7_car(args), "integer");
    }
    audio->stop(static_cast<SoLoud::handle>(s7_integer(s7_car(args))));
    return s7_t(sc);
}

s7_pointer g_set_sound_volume(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_integer(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "set-sound-volume", 1, s7_car(args), "integer");
    }
    if (!s7_is_real(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "set-sound-volume", 2, s7_cadr(args), "real");
    }
    audio->setVolume(
        static_cast<SoLoud::handle>(s7_integer(s7_car(args))),
        static_cast<float>(s7_number_to_real(sc, s7_cadr(args))));
    return s7_t(sc);
}

s7_pointer g_set_bus_volume(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "set-bus-volume", 1, s7_car(args), "string");
    }
    if (!s7_is_real(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "set-bus-volume", 2, s7_cadr(args), "real");
    }

    const std::optional<AudioBusKind> bus = audioBusKindFromName(s7_string(s7_car(args)));
    if (!bus) {
        return s7_f(sc);
    }
    audio->setBusVolume(*bus, static_cast<float>(s7_number_to_real(sc, s7_cadr(args))));
    return s7_t(sc);
}

s7_pointer g_get_bus_volume(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_make_real(sc, 0.0);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_make_real(sc, 0.0);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "get-bus-volume", 1, s7_car(args), "string");
    }

    const std::optional<AudioBusKind> bus = audioBusKindFromName(s7_string(s7_car(args)));
    if (!bus) {
        return s7_make_real(sc, 0.0);
    }
    return s7_make_real(sc, static_cast<double>(audio->getBusVolume(*bus)));
}

s7_pointer g_set_bus_muted(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "set-bus-muted", 1, s7_car(args), "string");
    }

    const std::optional<AudioBusKind> bus = audioBusKindFromName(s7_string(s7_car(args)));
    if (!bus) {
        return s7_f(sc);
    }
    audio->setBusMuted(*bus, s7_boolean(sc, s7_cadr(args)));
    return s7_t(sc);
}

s7_pointer g_bus_muted_p(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "bus-muted?", 1, s7_car(args), "string");
    }

    const std::optional<AudioBusKind> bus = audioBusKindFromName(s7_string(s7_car(args)));
    if (!bus) {
        return s7_f(sc);
    }
    return audio->isBusMuted(*bus) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_audio_filter_clear(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr || !audio->ready()) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-clear", 1, s7_car(args), "string");
    }
    if (!s7_is_integer(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-clear", 2, s7_cadr(args), "integer");
    }

    const std::optional<AudioBusKind> bus = audioBusKindFromName(s7_string(s7_car(args)));
    if (!bus) {
        return s7_f(sc);
    }
    audio->clearBusFilter(*bus, static_cast<unsigned int>(s7_integer(s7_cadr(args))));
    return s7_t(sc);
}

s7_pointer g_audio_filter_name(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_make_string(sc, "");
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_make_string(sc, "");
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-name", 1, s7_car(args), "string");
    }
    if (!s7_is_integer(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-name", 2, s7_cadr(args), "integer");
    }

    const std::optional<AudioBusKind> bus = audioBusKindFromName(s7_string(s7_car(args)));
    if (!bus) {
        return s7_make_string(sc, "");
    }
    const std::string_view name =
        audio->busFilterName(*bus, static_cast<unsigned int>(s7_integer(s7_cadr(args))));
    return s7_make_string_with_length(sc, name.data(), static_cast<int>(name.size()));
}

s7_pointer g_audio_filter_set_param(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr || !audio->ready()) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-set-param", 1, s7_car(args), "string");
    }
    if (!s7_is_integer(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-set-param", 2, s7_cadr(args), "integer");
    }
    if (!s7_is_integer(s7_caddr(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-set-param", 3, s7_caddr(args), "integer");
    }
    if (!s7_is_real(s7_cadddr(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-set-param", 4, s7_cadddr(args), "real");
    }

    const std::optional<AudioBusKind> bus = audioBusKindFromName(s7_string(s7_car(args)));
    if (!bus) {
        return s7_f(sc);
    }
    const unsigned int slot = static_cast<unsigned int>(s7_integer(s7_cadr(args)));
    const unsigned int paramIndex = static_cast<unsigned int>(s7_integer(s7_caddr(args)));
    const float value = static_cast<float>(s7_number_to_real(sc, s7_cadddr(args)));
    return audio->setBusFilterParam(*bus, slot, paramIndex, value) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_audio_filter_get_param(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_make_real(sc, 0.0);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr || !audio->ready()) {
        return s7_make_real(sc, 0.0);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-get-param", 1, s7_car(args), "string");
    }
    if (!s7_is_integer(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-get-param", 2, s7_cadr(args), "integer");
    }
    if (!s7_is_integer(s7_caddr(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-get-param", 3, s7_caddr(args), "integer");
    }

    const std::optional<AudioBusKind> bus = audioBusKindFromName(s7_string(s7_car(args)));
    if (!bus) {
        return s7_make_real(sc, 0.0);
    }
    const unsigned int slot = static_cast<unsigned int>(s7_integer(s7_cadr(args)));
    const unsigned int paramIndex = static_cast<unsigned int>(s7_integer(s7_caddr(args)));
    return s7_make_real(sc, static_cast<double>(audio->getBusFilterParam(*bus, slot, paramIndex)));
}

s7_pointer g_play_music(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    AssetStore* assets = audioAssets();
    if (audio == nullptr || assets == nullptr || !audio->ready()) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "play-music", 1, s7_car(args), "string");
    }
    float volume = 1.0f;
    if (!s7_is_null(sc, s7_cdr(args))) {
        if (!s7_is_real(s7_cadr(args))) {
            return s7_wrong_type_arg_error(sc, "play-music", 2, s7_cadr(args), "real");
        }
        volume = static_cast<float>(s7_number_to_real(sc, s7_cadr(args)));
    }
    return audio->playMusic(*assets, s7_string(s7_car(args)), volume) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_stop_music(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    audio->stopMusic();
    return s7_t(sc);
}

s7_pointer g_audio_filter_attach(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr || !audio->ready()) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-attach", 1, s7_car(args), "string");
    }
    if (!s7_is_string(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "audio-filter-attach", 2, s7_cadr(args), "string");
    }

    const char* target = s7_string(s7_car(args));
    const char* filterName = s7_string(s7_cadr(args));
    unsigned int slot = FILTERS_PER_STREAM;
    if (!s7_is_null(sc, s7_cddr(args))) {
        if (!s7_is_integer(s7_caddr(args))) {
            return s7_wrong_type_arg_error(sc, "audio-filter-attach", 3, s7_caddr(args), "integer");
        }
        slot = static_cast<unsigned int>(s7_integer(s7_caddr(args)));
    }

    bool ok = false;
    if (std::strcmp(target, "global") == 0) {
        ok = audio->attachGlobalFilter(filterName, slot);
    } else if (const std::optional<AudioBusKind> bus = audioBusKindFromName(target)) {
        ok = audio->attachBuiltinFilter(*bus, filterName, slot);
    }
    return ok ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_audio_attach(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_audioWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "audio-attach", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "audio-attach", 2, args, "audio path string");
    }
    const std::string path = s7_string(s7_car(args));

    flecs::entity entity = g_audioWorld->lookup(id.c_str());
    if (!entity.is_valid()) {
        return s7_f(sc);
    }

    AssetStore* assets = audioAssets();
    if (assets == nullptr) {
        return s7_f(sc);
    }
    const AudioDef* def = assets->getAudioDef(sc, path);
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

s7_pointer g_register_audio_filter(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::Audio)) {
        return s7_f(sc);
    }
    AudioWorld* audio = audioWorld();
    if (audio == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "register-audio-filter", 1, s7_car(args), "string");
    }
    if (!s7_is_procedure(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "register-audio-filter", 2, s7_cadr(args), "procedure");
    }

    s7_pointer proc = s7_cadr(args);
    s7_gc_protect(sc, proc);
    audio->registerSchemeFilter(s7_string(s7_car(args)), proc);
    return s7_t(sc);
}

} // namespace

void bindAudioApi(flecs::world& world, s7_scheme* scheme) {
    g_audioWorld = &world;
    if (scheme == nullptr) {
        return;
    }

    s7_define_macro(scheme, "register-audio", g_register_audio, 0, 0, true,
                    "(register-audio field ...)");
    s7_define_function(scheme, "play-audio", g_play_audio, 1, 1, false,
                       "(play-audio path [volume])");
    s7_define_function(scheme, "play-audio-3d", g_play_audio_3d, 4, 1, false,
                       "(play-audio-3d path x y z [volume])");
    s7_define_function(scheme, "audio-attach", g_audio_attach, 2, 0, false,
                       "(audio-attach id path)");
    s7_define_function(scheme, "play-sound", g_play_sound, 1, 2, false,
                       "(play-sound path [volume] [loop?])");
    s7_define_function(scheme, "stop-sound", g_stop_sound, 1, 0, false, "(stop-sound handle)");
    s7_define_function(scheme, "set-sound-volume", g_set_sound_volume, 2, 0, false,
                       "(set-sound-volume handle vol)");
    s7_define_function(scheme, "set-bus-volume", g_set_bus_volume, 2, 0, false,
                       "(set-bus-volume bus vol)");
    s7_define_function(scheme, "get-bus-volume", g_get_bus_volume, 1, 0, false,
                       "(get-bus-volume bus)");
    s7_define_function(scheme, "set-bus-muted", g_set_bus_muted, 2, 0, false,
                       "(set-bus-muted bus muted?)");
    s7_define_function(scheme, "bus-muted?", g_bus_muted_p, 1, 0, false, "(bus-muted? bus)");
    s7_define_function(scheme, "play-music", g_play_music, 1, 1, false, "(play-music path [volume])");
    s7_define_function(scheme, "stop-music", g_stop_music, 0, 0, false, "(stop-music)");
    s7_define_function(scheme, "audio-filter-attach", g_audio_filter_attach, 2, 1, false,
                       "(audio-filter-attach target filter [slot])");
    s7_define_function(scheme, "audio-filter-clear", g_audio_filter_clear, 2, 0, false,
                       "(audio-filter-clear bus slot)");
    s7_define_function(scheme, "audio-filter-name", g_audio_filter_name, 2, 0, false,
                       "(audio-filter-name bus slot)");
    s7_define_function(scheme, "audio-filter-set-param", g_audio_filter_set_param, 4, 0, false,
                       "(audio-filter-set-param bus slot param-index value)");
    s7_define_function(scheme, "audio-filter-get-param", g_audio_filter_get_param, 3, 0, false,
                       "(audio-filter-get-param bus slot param-index)");
    s7_define_function(scheme, "register-audio-filter", g_register_audio_filter, 2, 0, false,
                       "(register-audio-filter name proc)");
}

}
