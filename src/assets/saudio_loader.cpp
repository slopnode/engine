#include "assets/saudio_loader.hpp"

#include <charconv>
#include <optional>
#include <string>

namespace slopengine {

namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return value;
}

std::string_view stripTrailingParen(std::string_view value) {
    value = trim(value);
    if (!value.empty() && value.back() == ')') {
        value.remove_suffix(1);
    }
    return trim(value);
}

bool readFloat(std::string_view text, float& out) {
    text = stripTrailingParen(text);
    if (text.empty()) {
        return false;
    }
    float parsed = 0.0f;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr == begin) {
        return false;
    }
    out = parsed;
    return true;
}

bool readInt(std::string_view text, int& out) {
    text = stripTrailingParen(text);
    if (text.empty()) {
        return false;
    }
    int parsed = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr == begin) {
        return false;
    }
    out = parsed;
    return true;
}

std::optional<std::string> readQuoted(std::string_view text) {
    text = trim(text);
    const std::size_t quoteStart = text.find('"');
    if (quoteStart == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t quoteEnd = text.find('"', quoteStart + 1);
    if (quoteEnd == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string{text.substr(quoteStart + 1, quoteEnd - quoteStart - 1)};
}

std::string_view readToken(std::string_view text) {
    text = stripTrailingParen(text);
    text = trim(text);
    std::size_t end = 0;
    while (end < text.size() && text[end] != ' ' && text[end] != '\t' && text[end] != ')') {
        ++end;
    }
    return text.substr(0, end);
}

bool readBool(std::string_view text, bool& out) {
    const std::string_view token = readToken(text);
    if (token == "#t" || token == "true" || token == "1") {
        out = true;
        return true;
    }
    if (token == "#f" || token == "false" || token == "0") {
        out = false;
        return true;
    }
    return false;
}

std::optional<int> parseWave(std::string_view token) {
    if (token == "square") {
        return 0;
    }
    if (token == "saw" || token == "sawtooth") {
        return 1;
    }
    if (token == "sine") {
        return 2;
    }
    if (token == "noise") {
        return 3;
    }
    return std::nullopt;
}

std::optional<int> parsePreset(std::string_view token) {
    if (token == "coin") {
        return 0;
    }
    if (token == "laser") {
        return 1;
    }
    if (token == "explosion") {
        return 2;
    }
    if (token == "powerup") {
        return 3;
    }
    if (token == "hurt") {
        return 4;
    }
    if (token == "jump") {
        return 5;
    }
    if (token == "blip") {
        return 6;
    }
    return std::nullopt;
}

bool startsWith(std::string_view line, std::string_view prefix) {
    return line.size() >= prefix.size() && line.compare(0, prefix.size(), prefix) == 0;
}

bool setFloatField(SaudioParams& params, std::uint32_t field, float& dst, std::string_view text) {
    if (!readFloat(text, dst)) {
        return false;
    }
    params.setMask |= field;
    return true;
}

std::string formatFloat(float value) {
    char buf[32];
    const auto result = std::to_chars(buf, buf + sizeof(buf), value);
    return std::string(buf, result.ptr);
}

const char* waveName(int waveType) {
    switch (waveType) {
        case 0: return "square";
        case 1: return "saw";
        case 2: return "sine";
        case 3: return "noise";
        default: return "square";
    }
}

const char* presetName(int preset) {
    switch (preset) {
        case 0: return "coin";
        case 1: return "laser";
        case 2: return "explosion";
        case 3: return "powerup";
        case 4: return "hurt";
        case 5: return "jump";
        case 6: return "blip";
        default: return "coin";
    }
}

void appendFloatField(std::string& out, const char* name, std::uint32_t mask, std::uint32_t field, float value) {
    if ((mask & field) == 0) {
        return;
    }
    out += "  (";
    out += name;
    out += ' ';
    out += formatFloat(value);
    out += ")\n";
}

} // namespace

bool parseSaudioAsset(std::string_view source, AudioDef& def) {
    def = {};
    def.kind = AudioDefKind::Sfxr;

    std::size_t lineStart = 0;
    while (lineStart <= source.size()) {
        const std::size_t lineEnd = source.find('\n', lineStart);
        const std::string_view line = trim(source.substr(
            lineStart,
            lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart));

        if (lineEnd == std::string_view::npos && line.empty()) {
            break;
        }

        if (line.empty() || line == "(audio" || line == "(audio)" || line == ")") {
            if (lineEnd == std::string_view::npos) {
                break;
            }
            lineStart = lineEnd + 1;
            continue;
        }

        auto after = [&](std::string_view prefix) -> std::string_view {
            return trim(line.substr(prefix.size()));
        };

        if (startsWith(line, "(preset ")) {
            std::string_view rest = after("(preset ");
            const std::string_view name = readToken(rest);
            const auto preset = parsePreset(name);
            if (!preset) {
                return false;
            }
            rest = trim(rest.substr(name.size()));
            int seed = 0;
            if (!rest.empty() && rest.front() != ')') {
                if (!readInt(rest, seed)) {
                    return false;
                }
            }
            def.sfxr.hasPreset = true;
            def.sfxr.preset = *preset;
            def.sfxr.presetSeed = seed;
        } else if (startsWith(line, "(wave ")) {
            const auto wave = parseWave(readToken(after("(wave ")));
            if (!wave) {
                return false;
            }
            def.sfxr.waveType = *wave;
            def.sfxr.setMask |= SaudioField_Wave;
        } else if (startsWith(line, "(freq ")) {
            if (!setFloatField(def.sfxr, SaudioField_Freq, def.sfxr.baseFreq, after("(freq "))) {
                return false;
            }
        } else if (startsWith(line, "(freq-limit ")) {
            if (!setFloatField(def.sfxr, SaudioField_FreqLimit, def.sfxr.freqLimit, after("(freq-limit "))) {
                return false;
            }
        } else if (startsWith(line, "(freq-ramp ")) {
            if (!setFloatField(def.sfxr, SaudioField_FreqRamp, def.sfxr.freqRamp, after("(freq-ramp "))) {
                return false;
            }
        } else if (startsWith(line, "(freq-dramp ")) {
            if (!setFloatField(def.sfxr, SaudioField_FreqDramp, def.sfxr.freqDramp, after("(freq-dramp "))) {
                return false;
            }
        } else if (startsWith(line, "(duty ")) {
            if (!setFloatField(def.sfxr, SaudioField_Duty, def.sfxr.duty, after("(duty "))) {
                return false;
            }
        } else if (startsWith(line, "(duty-ramp ")) {
            if (!setFloatField(def.sfxr, SaudioField_DutyRamp, def.sfxr.dutyRamp, after("(duty-ramp "))) {
                return false;
            }
        } else if (startsWith(line, "(vib-strength ")) {
            if (!setFloatField(def.sfxr, SaudioField_VibStrength, def.sfxr.vibStrength, after("(vib-strength "))) {
                return false;
            }
        } else if (startsWith(line, "(vib-speed ")) {
            if (!setFloatField(def.sfxr, SaudioField_VibSpeed, def.sfxr.vibSpeed, after("(vib-speed "))) {
                return false;
            }
        } else if (startsWith(line, "(vib-delay ")) {
            if (!setFloatField(def.sfxr, SaudioField_VibDelay, def.sfxr.vibDelay, after("(vib-delay "))) {
                return false;
            }
        } else if (startsWith(line, "(attack ")) {
            if (!setFloatField(def.sfxr, SaudioField_Attack, def.sfxr.envAttack, after("(attack "))) {
                return false;
            }
        } else if (startsWith(line, "(sustain ")) {
            if (!setFloatField(def.sfxr, SaudioField_Sustain, def.sfxr.envSustain, after("(sustain "))) {
                return false;
            }
        } else if (startsWith(line, "(decay ")) {
            if (!setFloatField(def.sfxr, SaudioField_Decay, def.sfxr.envDecay, after("(decay "))) {
                return false;
            }
        } else if (startsWith(line, "(punch ")) {
            if (!setFloatField(def.sfxr, SaudioField_Punch, def.sfxr.envPunch, after("(punch "))) {
                return false;
            }
        } else if (startsWith(line, "(lpf-resonance ")) {
            if (!setFloatField(def.sfxr, SaudioField_LpfResonance, def.sfxr.lpfResonance, after("(lpf-resonance "))) {
                return false;
            }
        } else if (startsWith(line, "(lpf ")) {
            if (!setFloatField(def.sfxr, SaudioField_Lpf, def.sfxr.lpfFreq, after("(lpf "))) {
                return false;
            }
        } else if (startsWith(line, "(lpf-ramp ")) {
            if (!setFloatField(def.sfxr, SaudioField_LpfRamp, def.sfxr.lpfRamp, after("(lpf-ramp "))) {
                return false;
            }
        } else if (startsWith(line, "(hpf ")) {
            if (!setFloatField(def.sfxr, SaudioField_Hpf, def.sfxr.hpfFreq, after("(hpf "))) {
                return false;
            }
        } else if (startsWith(line, "(hpf-ramp ")) {
            if (!setFloatField(def.sfxr, SaudioField_HpfRamp, def.sfxr.hpfRamp, after("(hpf-ramp "))) {
                return false;
            }
        } else if (startsWith(line, "(pha-offset ")) {
            if (!setFloatField(def.sfxr, SaudioField_PhaOffset, def.sfxr.phaOffset, after("(pha-offset "))) {
                return false;
            }
        } else if (startsWith(line, "(pha-ramp ")) {
            if (!setFloatField(def.sfxr, SaudioField_PhaRamp, def.sfxr.phaRamp, after("(pha-ramp "))) {
                return false;
            }
        } else if (startsWith(line, "(repeat-speed ")) {
            if (!setFloatField(def.sfxr, SaudioField_RepeatSpeed, def.sfxr.repeatSpeed, after("(repeat-speed "))) {
                return false;
            }
        } else if (startsWith(line, "(arp-speed ")) {
            if (!setFloatField(def.sfxr, SaudioField_ArpSpeed, def.sfxr.arpSpeed, after("(arp-speed "))) {
                return false;
            }
        } else if (startsWith(line, "(arp-mod ")) {
            if (!setFloatField(def.sfxr, SaudioField_ArpMod, def.sfxr.arpMod, after("(arp-mod "))) {
                return false;
            }
        } else if (startsWith(line, "(sound-vol ")) {
            if (!setFloatField(def.sfxr, SaudioField_SoundVol, def.sfxr.soundVol, after("(sound-vol "))) {
                return false;
            }
        } else if (startsWith(line, "(master-vol ")) {
            if (!setFloatField(def.sfxr, SaudioField_MasterVol, def.sfxr.masterVol, after("(master-vol "))) {
                return false;
            }
        } else if (startsWith(line, "(volume ")) {
            if (!readFloat(after("(volume "), def.volume)) {
                return false;
            }
        } else if (startsWith(line, "(min-distance ")) {
            if (!readFloat(after("(min-distance "), def.minDistance)) {
                return false;
            }
        } else if (startsWith(line, "(max-distance ")) {
            if (!readFloat(after("(max-distance "), def.maxDistance)) {
                return false;
            }
        } else if (startsWith(line, "(loop ")) {
            if (!readBool(after("(loop "), def.loop)) {
                return false;
            }
        } else if (startsWith(line, "(spatial ")) {
            if (!readBool(after("(spatial "), def.spatial)) {
                return false;
            }
        } else if (startsWith(line, "(bus ")) {
            const auto bus = readQuoted(after("(bus "));
            if (!bus) {
                return false;
            }
            def.bus = (*bus == "music") ? AudioDefBus::Music : AudioDefBus::Sfx;
        } else if (startsWith(line, "(filter ")) {
            const auto filter = readQuoted(after("(filter "));
            if (!filter) {
                return false;
            }
            def.filters.push_back(*filter);
        } else if (!startsWith(line, "(audio")) {
            return false;
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    return true;
}

std::string serializeSaudioAsset(const AudioDef& def) {
    const SaudioParams& p = def.sfxr;
    std::string out = "(audio\n";

    if (p.hasPreset) {
        out += "  (preset ";
        out += presetName(p.preset);
        out += ' ';
        out += std::to_string(p.presetSeed);
        out += ")\n";
    }

    if (p.setMask & SaudioField_Wave) {
        out += "  (wave ";
        out += waveName(p.waveType);
        out += ")\n";
    }
    appendFloatField(out, "freq", p.setMask, SaudioField_Freq, p.baseFreq);
    appendFloatField(out, "freq-limit", p.setMask, SaudioField_FreqLimit, p.freqLimit);
    appendFloatField(out, "freq-ramp", p.setMask, SaudioField_FreqRamp, p.freqRamp);
    appendFloatField(out, "freq-dramp", p.setMask, SaudioField_FreqDramp, p.freqDramp);
    appendFloatField(out, "duty", p.setMask, SaudioField_Duty, p.duty);
    appendFloatField(out, "duty-ramp", p.setMask, SaudioField_DutyRamp, p.dutyRamp);
    appendFloatField(out, "vib-strength", p.setMask, SaudioField_VibStrength, p.vibStrength);
    appendFloatField(out, "vib-speed", p.setMask, SaudioField_VibSpeed, p.vibSpeed);
    appendFloatField(out, "vib-delay", p.setMask, SaudioField_VibDelay, p.vibDelay);
    appendFloatField(out, "attack", p.setMask, SaudioField_Attack, p.envAttack);
    appendFloatField(out, "sustain", p.setMask, SaudioField_Sustain, p.envSustain);
    appendFloatField(out, "decay", p.setMask, SaudioField_Decay, p.envDecay);
    appendFloatField(out, "punch", p.setMask, SaudioField_Punch, p.envPunch);
    appendFloatField(out, "lpf", p.setMask, SaudioField_Lpf, p.lpfFreq);
    appendFloatField(out, "lpf-resonance", p.setMask, SaudioField_LpfResonance, p.lpfResonance);
    appendFloatField(out, "lpf-ramp", p.setMask, SaudioField_LpfRamp, p.lpfRamp);
    appendFloatField(out, "hpf", p.setMask, SaudioField_Hpf, p.hpfFreq);
    appendFloatField(out, "hpf-ramp", p.setMask, SaudioField_HpfRamp, p.hpfRamp);
    appendFloatField(out, "pha-offset", p.setMask, SaudioField_PhaOffset, p.phaOffset);
    appendFloatField(out, "pha-ramp", p.setMask, SaudioField_PhaRamp, p.phaRamp);
    appendFloatField(out, "repeat-speed", p.setMask, SaudioField_RepeatSpeed, p.repeatSpeed);
    appendFloatField(out, "arp-speed", p.setMask, SaudioField_ArpSpeed, p.arpSpeed);
    appendFloatField(out, "arp-mod", p.setMask, SaudioField_ArpMod, p.arpMod);
    appendFloatField(out, "sound-vol", p.setMask, SaudioField_SoundVol, p.soundVol);
    appendFloatField(out, "master-vol", p.setMask, SaudioField_MasterVol, p.masterVol);

    out += "  (volume ";
    out += formatFloat(def.volume);
    out += ")\n";
    out += "  (min-distance ";
    out += formatFloat(def.minDistance);
    out += ")\n";
    out += "  (max-distance ";
    out += formatFloat(def.maxDistance);
    out += ")\n";
    out += "  (loop ";
    out += (def.loop ? "#t" : "#f");
    out += ")\n";
    out += "  (spatial ";
    out += (def.spatial ? "#t" : "#f");
    out += ")\n";
    out += "  (bus \"";
    out += (def.bus == AudioDefBus::Music ? "music" : "sfx");
    out += "\")\n";
    for (const std::string& filterName : def.filters) {
        out += "  (filter \"";
        out += filterName;
        out += "\")\n";
    }

    out += ")\n";
    return out;
}

}
