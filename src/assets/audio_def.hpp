#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace slopengine {

enum class AudioDefBus {
    Sfx,
    Music,
};

enum class AudioDefKind {
    Sample,
    Sfxr,
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

struct AudioDef {
    AudioDefKind kind = AudioDefKind::Sample;
    std::string source;
    SaudioParams sfxr;
    float volume = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 30.0f;
    bool loop = false;
    bool spatial = false;
    AudioDefBus bus = AudioDefBus::Sfx;
    std::vector<std::string> filters;
};

}
