#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace slopengine {

inline constexpr unsigned int kMaxFiltersPerBus = 8;

/** One SoLoud builtin filter attached to a filter chain slot -- either a bus's mixer
 *  chain or an AudioDef's per-definition chain -- with its param values in
 *  SoLoud::Filter param-index order. Empty name means the slot is unused. */
struct BusFilterSlot {
    std::string name;
    std::vector<float> params;
};

enum class AudioDefBus {
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

enum SaudioField : std::uint32_t {
    SaudioField_Wave = 1u << 0,
    SaudioField_Freq = 1u << 1,
    SaudioField_FreqLimit = 1u << 2,
    SaudioField_FreqRamp = 1u << 3,
    SaudioField_FreqDramp = 1u << 4,
    SaudioField_Duty = 1u << 5,
    SaudioField_DutyRamp = 1u << 6,
    SaudioField_VibStrength = 1u << 7,
    SaudioField_VibSpeed = 1u << 8,
    SaudioField_VibDelay = 1u << 9,
    SaudioField_Attack = 1u << 10,
    SaudioField_Sustain = 1u << 11,
    SaudioField_Decay = 1u << 12,
    SaudioField_Punch = 1u << 13,
    SaudioField_Lpf = 1u << 14,
    SaudioField_LpfResonance = 1u << 15,
    SaudioField_LpfRamp = 1u << 16,
    SaudioField_Hpf = 1u << 17,
    SaudioField_HpfRamp = 1u << 18,
    SaudioField_PhaOffset = 1u << 19,
    SaudioField_PhaRamp = 1u << 20,
    SaudioField_RepeatSpeed = 1u << 21,
    SaudioField_ArpSpeed = 1u << 22,
    SaudioField_ArpMod = 1u << 23,
    SaudioField_SoundVol = 1u << 24,
    SaudioField_MasterVol = 1u << 25,
};

struct SaudioParams {
    bool hasPreset = false;
    int preset = 0;
    int presetSeed = 0;
    std::uint32_t setMask = 0;

    int waveType = 0;
    float baseFreq = 0.3f;
    float freqLimit = 0.0f;
    float freqRamp = 0.0f;
    float freqDramp = 0.0f;
    float duty = 0.0f;
    float dutyRamp = 0.0f;
    float vibStrength = 0.0f;
    float vibSpeed = 0.0f;
    float vibDelay = 0.0f;
    float envAttack = 0.0f;
    float envSustain = 0.3f;
    float envDecay = 0.4f;
    float envPunch = 0.0f;
    float lpfResonance = 0.0f;
    float lpfFreq = 1.0f;
    float lpfRamp = 0.0f;
    float hpfFreq = 0.0f;
    float hpfRamp = 0.0f;
    float phaOffset = 0.0f;
    float phaRamp = 0.0f;
    float repeatSpeed = 0.0f;
    float arpSpeed = 0.0f;
    float arpMod = 0.0f;
    float masterVol = 0.05f;
    float soundVol = 0.5f;
};

/** A pure procedural sound source: sfxr synth params, nothing else. Never played
 *  directly -- an AudioDef::source references it the same way it can reference a
 *  sound/ clip, and playback picks whichever one resolves. */
struct ProceduralDef {
    SaudioParams sfxr;
};

/** The thing the game actually plays: a source (either a sound/ clip or an
 *  AudioProcedural def, resolved by name at load time) wrapped with bus routing,
 *  volume/distance, looping/spatialization, and post filters. */
struct AudioDef {
    std::string source;
    float volume = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 30.0f;
    bool loop = false;
    bool spatial = false;
    AudioDefBus bus = AudioDefBus::Sfx;
    std::array<BusFilterSlot, kMaxFiltersPerBus> filters{};
};

/** Appends a filter into the first unused slot of `def.filters`; returns nullptr if the
 *  chain is already full (kMaxFiltersPerBus slots) instead of appending. */
inline BusFilterSlot* pushAudioDefFilter(AudioDef& def, std::string name) {
    for (BusFilterSlot& slot : def.filters) {
        if (slot.name.empty()) {
            slot.name = std::move(name);
            slot.params.clear();
            return &slot;
        }
    }
    return nullptr;
}

/** True if any slot in a filter chain is occupied. */
inline bool hasAnyFilterSlots(const std::array<BusFilterSlot, kMaxFiltersPerBus>& slots) {
    for (const BusFilterSlot& slot : slots) {
        if (!slot.name.empty()) {
            return true;
        }
    }
    return false;
}

/** Persisted mixer state for a single AudioBusKind: volume, mute, and its
 *  ordered filter chain (up to kMaxFiltersPerBus slots). */
struct BusDef {
    float volume = 1.0f;
    bool muted = false;
    std::array<BusFilterSlot, kMaxFiltersPerBus> slots{};
};

/** The full bus mixer, one BusDef per AudioDefBus. Loaded from
 *  audio/buses.saudio and applied to AudioWorld at startup. */
struct BusConfig {
    std::array<BusDef, static_cast<std::size_t>(AudioDefBus::Count)> buses{};
};

}
