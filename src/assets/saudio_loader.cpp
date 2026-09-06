#include "assets/saudio_loader.hpp"

#include <charconv>
#include <functional>
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

bool consumeQuoted(std::string_view& text, std::string& out) {
    text = trim(text);
    if (text.empty() || text.front() != '"') {
        return false;
    }
    const std::size_t end = text.find('"', 1);
    if (end == std::string_view::npos) {
        return false;
    }
    out = std::string(text.substr(1, end - 1));
    text.remove_prefix(end + 1);
    return true;
}

bool consumeInt(std::string_view& text, int& out) {
    text = trim(text);
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    int parsed = 0;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr == begin) {
        return false;
    }
    out = parsed;
    text.remove_prefix(static_cast<std::size_t>(result.ptr - begin));
    return true;
}

bool consumeFloat(std::string_view& text, float& out) {
    text = trim(text);
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    float parsed = 0.0f;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr == begin) {
        return false;
    }
    out = parsed;
    text.remove_prefix(static_cast<std::size_t>(result.ptr - begin));
    return true;
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

const char* busName(AudioDefBus bus) {
    switch (bus) {
        case AudioDefBus::Music: return "music";
        case AudioDefBus::Ui: return "ui";
        case AudioDefBus::Hud: return "hud";
        case AudioDefBus::Voices: return "voices";
        case AudioDefBus::Ambience: return "ambience";
        case AudioDefBus::Player: return "player";
        case AudioDefBus::View: return "view";
        case AudioDefBus::Sfx: default: return "sfx";
    }
}

AudioDefBus busFromName(std::string_view name) {
    if (name == "music") return AudioDefBus::Music;
    if (name == "ui") return AudioDefBus::Ui;
    if (name == "hud") return AudioDefBus::Hud;
    if (name == "voices") return AudioDefBus::Voices;
    if (name == "ambience") return AudioDefBus::Ambience;
    if (name == "player") return AudioDefBus::Player;
    if (name == "view") return AudioDefBus::View;
    return AudioDefBus::Sfx;
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

/** Splits `text` into trimmed lines and hands each non-boilerplate one to `handleLine`.
 *  `wrapperTag` lines ("(audio", "(audio)", ")") and blanks are skipped. Stops and
 *  returns false the moment `handleLine` rejects a line. */
bool forEachField(
    std::string_view text,
    std::string_view wrapperTag,
    const std::function<bool(std::string_view)>& handleLine) {
    std::size_t lineStart = 0;
    while (lineStart <= text.size()) {
        const std::size_t lineEnd = text.find('\n', lineStart);
        const std::string_view line = trim(text.substr(
            lineStart,
            lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart));

        if (lineEnd == std::string_view::npos && line.empty()) {
            break;
        }

        std::string wrapperOpen{wrapperTag};
        std::string wrapperEmpty = wrapperOpen + ")";
        if (line.empty() || line == wrapperOpen || line == wrapperEmpty || line == ")") {
            if (lineEnd == std::string_view::npos) {
                break;
            }
            lineStart = lineEnd + 1;
            continue;
        }

        if (!handleLine(line)) {
            return false;
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }
    return true;
}

bool parseProceduralField(std::string_view line, SaudioParams& p) {
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
        p.hasPreset = true;
        p.preset = *preset;
        p.presetSeed = seed;
    } else if (startsWith(line, "(wave ")) {
        const auto wave = parseWave(readToken(after("(wave ")));
        if (!wave) {
            return false;
        }
        p.waveType = *wave;
        p.setMask |= SaudioField_Wave;
    } else if (startsWith(line, "(freq-limit ")) {
        if (!setFloatField(p, SaudioField_FreqLimit, p.freqLimit, after("(freq-limit "))) {
            return false;
        }
    } else if (startsWith(line, "(freq-ramp ")) {
        if (!setFloatField(p, SaudioField_FreqRamp, p.freqRamp, after("(freq-ramp "))) {
            return false;
        }
    } else if (startsWith(line, "(freq-dramp ")) {
        if (!setFloatField(p, SaudioField_FreqDramp, p.freqDramp, after("(freq-dramp "))) {
            return false;
        }
    } else if (startsWith(line, "(freq ")) {
        if (!setFloatField(p, SaudioField_Freq, p.baseFreq, after("(freq "))) {
            return false;
        }
    } else if (startsWith(line, "(duty-ramp ")) {
        if (!setFloatField(p, SaudioField_DutyRamp, p.dutyRamp, after("(duty-ramp "))) {
            return false;
        }
    } else if (startsWith(line, "(duty ")) {
        if (!setFloatField(p, SaudioField_Duty, p.duty, after("(duty "))) {
            return false;
        }
    } else if (startsWith(line, "(vib-strength ")) {
        if (!setFloatField(p, SaudioField_VibStrength, p.vibStrength, after("(vib-strength "))) {
            return false;
        }
    } else if (startsWith(line, "(vib-speed ")) {
        if (!setFloatField(p, SaudioField_VibSpeed, p.vibSpeed, after("(vib-speed "))) {
            return false;
        }
    } else if (startsWith(line, "(vib-delay ")) {
        if (!setFloatField(p, SaudioField_VibDelay, p.vibDelay, after("(vib-delay "))) {
            return false;
        }
    } else if (startsWith(line, "(attack ")) {
        if (!setFloatField(p, SaudioField_Attack, p.envAttack, after("(attack "))) {
            return false;
        }
    } else if (startsWith(line, "(sustain ")) {
        if (!setFloatField(p, SaudioField_Sustain, p.envSustain, after("(sustain "))) {
            return false;
        }
    } else if (startsWith(line, "(decay ")) {
        if (!setFloatField(p, SaudioField_Decay, p.envDecay, after("(decay "))) {
            return false;
        }
    } else if (startsWith(line, "(punch ")) {
        if (!setFloatField(p, SaudioField_Punch, p.envPunch, after("(punch "))) {
            return false;
        }
    } else if (startsWith(line, "(lpf-resonance ")) {
        if (!setFloatField(p, SaudioField_LpfResonance, p.lpfResonance, after("(lpf-resonance "))) {
            return false;
        }
    } else if (startsWith(line, "(lpf-ramp ")) {
        if (!setFloatField(p, SaudioField_LpfRamp, p.lpfRamp, after("(lpf-ramp "))) {
            return false;
        }
    } else if (startsWith(line, "(lpf ")) {
        if (!setFloatField(p, SaudioField_Lpf, p.lpfFreq, after("(lpf "))) {
            return false;
        }
    } else if (startsWith(line, "(hpf-ramp ")) {
        if (!setFloatField(p, SaudioField_HpfRamp, p.hpfRamp, after("(hpf-ramp "))) {
            return false;
        }
    } else if (startsWith(line, "(hpf ")) {
        if (!setFloatField(p, SaudioField_Hpf, p.hpfFreq, after("(hpf "))) {
            return false;
        }
    } else if (startsWith(line, "(pha-offset ")) {
        if (!setFloatField(p, SaudioField_PhaOffset, p.phaOffset, after("(pha-offset "))) {
            return false;
        }
    } else if (startsWith(line, "(pha-ramp ")) {
        if (!setFloatField(p, SaudioField_PhaRamp, p.phaRamp, after("(pha-ramp "))) {
            return false;
        }
    } else if (startsWith(line, "(repeat-speed ")) {
        if (!setFloatField(p, SaudioField_RepeatSpeed, p.repeatSpeed, after("(repeat-speed "))) {
            return false;
        }
    } else if (startsWith(line, "(arp-speed ")) {
        if (!setFloatField(p, SaudioField_ArpSpeed, p.arpSpeed, after("(arp-speed "))) {
            return false;
        }
    } else if (startsWith(line, "(arp-mod ")) {
        if (!setFloatField(p, SaudioField_ArpMod, p.arpMod, after("(arp-mod "))) {
            return false;
        }
    } else if (startsWith(line, "(sound-vol ")) {
        if (!setFloatField(p, SaudioField_SoundVol, p.soundVol, after("(sound-vol "))) {
            return false;
        }
    } else if (startsWith(line, "(master-vol ")) {
        if (!setFloatField(p, SaudioField_MasterVol, p.masterVol, after("(master-vol "))) {
            return false;
        }
    } else {
        return false;
    }
    return true;
}

bool parseBusConfigField(std::string_view line, BusConfig& config) {
    auto after = [&](std::string_view prefix) -> std::string_view {
        return trim(line.substr(prefix.size()));
    };

    if (startsWith(line, "(bus-volume ")) {
        std::string_view rest = stripTrailingParen(after("(bus-volume "));
        std::string name;
        float volume = 1.0f;
        if (!consumeQuoted(rest, name) || !consumeFloat(rest, volume)) {
            return false;
        }
        config.buses[static_cast<std::size_t>(busFromName(name))].volume = volume;
        return true;
    }
    if (startsWith(line, "(bus-muted ")) {
        std::string_view rest = stripTrailingParen(after("(bus-muted "));
        std::string name;
        bool muted = false;
        if (!consumeQuoted(rest, name) || !readBool(rest, muted)) {
            return false;
        }
        config.buses[static_cast<std::size_t>(busFromName(name))].muted = muted;
        return true;
    }
    if (startsWith(line, "(bus-filter ")) {
        std::string_view rest = stripTrailingParen(after("(bus-filter "));
        std::string name;
        int slot = 0;
        std::string filterName;
        if (!consumeQuoted(rest, name) || !consumeInt(rest, slot) || !consumeQuoted(rest, filterName)) {
            return false;
        }
        if (slot < 0 || slot >= static_cast<int>(kMaxFiltersPerBus)) {
            return false;
        }
        BusFilterSlot& target =
            config.buses[static_cast<std::size_t>(busFromName(name))].slots[static_cast<std::size_t>(slot)];
        target.name = filterName;
        target.params.clear();
        float value = 0.0f;
        while (consumeFloat(rest, value)) {
            target.params.push_back(value);
        }
        return true;
    }
    return false;
}

} // namespace

bool parseSaudioAsset(std::string_view text, AudioDef& def) {
    def = {};

    return forEachField(text, "(audio", [&](std::string_view line) {
        auto after = [&](std::string_view prefix) -> std::string_view {
            return trim(line.substr(prefix.size()));
        };

        if (startsWith(line, "(source ")) {
            const auto sourcePath = readQuoted(after("(source "));
            if (!sourcePath) {
                return false;
            }
            def.source = *sourcePath;
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
            def.bus = busFromName(*bus);
        } else if (startsWith(line, "(filter ")) {
            std::string_view rest = stripTrailingParen(after("(filter "));
            std::string filterName;
            if (!consumeQuoted(rest, filterName)) {
                return false;
            }
            BusFilterSlot* target = pushAudioDefFilter(def, filterName);
            if (target == nullptr) {
                return false;
            }
            float value = 0.0f;
            while (consumeFloat(rest, value)) {
                target->params.push_back(value);
            }
        } else {
            return false;
        }
        return true;
    });
}

std::string serializeSaudioAsset(const AudioDef& def) {
    std::string out = "(audio\n";
    out += "  (source \"";
    out += def.source;
    out += "\")\n";
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
    out += busName(def.bus);
    out += "\")\n";
    for (const BusFilterSlot& filterSlot : def.filters) {
        if (filterSlot.name.empty()) {
            continue;
        }
        out += "  (filter \"";
        out += filterSlot.name;
        out += "\"";
        for (float value : filterSlot.params) {
            out += ' ';
            out += formatFloat(value);
        }
        out += ")\n";
    }
    out += ")\n";
    return out;
}

bool parseProceduralAsset(std::string_view text, ProceduralDef& def) {
    def = {};
    return forEachField(text, "(sfxr", [&](std::string_view line) { return parseProceduralField(line, def.sfxr); });
}

std::string serializeProceduralAsset(const ProceduralDef& def) {
    const SaudioParams& p = def.sfxr;
    std::string out = "(sfxr\n";

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

    out += ")\n";
    return out;
}

bool parseBusConfig(std::string_view text, BusConfig& config) {
    config = {};
    return forEachField(text, "(buses", [&](std::string_view line) { return parseBusConfigField(line, config); });
}

std::string serializeBusConfig(const BusConfig& config) {
    std::string out = "(buses\n";
    for (std::size_t i = 0; i < config.buses.size(); ++i) {
        const AudioDefBus bus = static_cast<AudioDefBus>(i);
        const BusDef& def = config.buses[i];
        out += "  (bus-volume \"";
        out += busName(bus);
        out += "\" ";
        out += formatFloat(def.volume);
        out += ")\n";
        out += "  (bus-muted \"";
        out += busName(bus);
        out += "\" ";
        out += (def.muted ? "#t" : "#f");
        out += ")\n";
        for (std::size_t slot = 0; slot < def.slots.size(); ++slot) {
            const BusFilterSlot& filterSlot = def.slots[slot];
            if (filterSlot.name.empty()) {
                continue;
            }
            out += "  (bus-filter \"";
            out += busName(bus);
            out += "\" ";
            out += std::to_string(slot);
            out += " \"";
            out += filterSlot.name;
            out += "\"";
            for (float value : filterSlot.params) {
                out += ' ';
                out += formatFloat(value);
            }
            out += ")\n";
        }
    }
    out += ")\n";
    return out;
}

}
