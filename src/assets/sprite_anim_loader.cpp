#include "assets/sprite_anim_loader.hpp"

#include <cctype>
#include <charconv>
#include <optional>
#include <sstream>
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

std::optional<std::string> readQuotedField(std::string_view line, std::string_view prefix) {
    const std::size_t prefixPos = line.find(prefix);
    if (prefixPos == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t quoteStart = line.find('"', prefixPos + prefix.size());
    if (quoteStart == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t quoteEnd = line.find('"', quoteStart + 1);
    if (quoteEnd == std::string_view::npos) {
        return std::nullopt;
    }

    return std::string{line.substr(quoteStart + 1, quoteEnd - quoteStart - 1)};
}

bool readFloat(std::string_view text, float& out, std::string_view* remaining = nullptr) {
    text = trim(text);
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
    if (remaining != nullptr) {
        *remaining = trim(std::string_view{result.ptr, static_cast<std::size_t>(end - result.ptr)});
    }
    return true;
}

bool readInt(std::string_view text, int& out) {
    text = trim(text);
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

bool parseTweenTokens(std::string_view text, SpriteAnimFrame& out) {
    text = trim(text);
    while (!text.empty()) {
        std::size_t end = 0;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) &&
               text[end] != ')') {
            ++end;
        }
        const std::string_view token = text.substr(0, end);
        if (token == "all") {
            out.tweenRotation = true;
            out.tweenScale = true;
            out.tweenTranslate = true;
        } else if (token == "rot") {
            out.tweenRotation = true;
        } else if (token == "scale") {
            out.tweenScale = true;
        } else if (token == "translate") {
            out.tweenTranslate = true;
        } else if (token == "offset") {
            // Legacy token ignored; offset is not tweenable.
        } else if (!token.empty()) {
            return false;
        }
        text = trim(text.substr(end));
    }
    return true;
}

bool parseFrameLine(std::string_view line, SpriteAnimFrame& out) {
    if (line.rfind("(frame ", 0) != 0) {
        return false;
    }

    std::string_view rest = trim(line.substr(std::string_view("(frame ").size()));
    if (!rest.empty() && rest.back() == ')') {
        rest.remove_suffix(1);
        rest = trim(rest);
    }

    if (rest.empty() || rest.front() != '"') {
        return false;
    }
    const std::size_t quoteEnd = rest.find('"', 1);
    if (quoteEnd == std::string_view::npos) {
        return false;
    }

    out = {};
    out.id = std::string{rest.substr(1, quoteEnd - 1)};
    std::string_view afterId = trim(rest.substr(quoteEnd + 1));
    float duration = 0.0f;
    std::string_view remaining;
    if (out.id.empty() || !readFloat(afterId, duration, &remaining) || duration <= 0.0f) {
        return false;
    }
    out.duration = duration;

    remaining = trim(remaining);
    if (remaining.empty()) {
        return true;
    }

    if (remaining.rfind("(tween", 0) == 0) {
        std::string_view tweenBody = trim(remaining.substr(std::string_view("(tween").size()));
        if (!tweenBody.empty() && tweenBody.back() == ')') {
            tweenBody.remove_suffix(1);
            tweenBody = trim(tweenBody);
        }
        return parseTweenTokens(tweenBody, out);
    }

    return false;
}

void writeTweenSuffix(std::ostringstream& out, const SpriteAnimFrame& frame) {
    if (!frame.hasTween()) {
        return;
    }
    if (frame.tweenRotation && frame.tweenScale && frame.tweenTranslate) {
        out << " (tween all)";
        return;
    }
    out << " (tween";
    if (frame.tweenRotation) {
        out << " rot";
    }
    if (frame.tweenScale) {
        out << " scale";
    }
    if (frame.tweenTranslate) {
        out << " translate";
    }
    out << ')';
}

} // namespace

bool parseSpriteAnimBank(std::string_view source, SpriteAnimBank& bank) {
    bank = {};
    SpriteAnimClip* currentClip = nullptr;
    bool legacyClipTween = false;

    auto finishClip = [&]() {
        if (currentClip == nullptr) {
            return;
        }
        if (legacyClipTween) {
            for (SpriteAnimFrame& frame : currentClip->frames) {
                frame.tweenRotation = true;
                frame.tweenScale = true;
                frame.tweenTranslate = true;
            }
        }
        legacyClipTween = false;
    };

    std::size_t lineStart = 0;
    while (lineStart <= source.size()) {
        const std::size_t lineEnd = source.find('\n', lineStart);
        const std::string_view line = trim(source.substr(
            lineStart,
            lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart));

        if (lineEnd == std::string_view::npos && line.empty()) {
            break;
        }

        if (auto clipName = readQuotedField(line, "(clip ")) {
            finishClip();
            bank.clips.push_back(SpriteAnimClip{});
            currentClip = &bank.clips.back();
            currentClip->name = *clipName;
            legacyClipTween = false;
        } else if (currentClip != nullptr && line.rfind("(loop ", 0) == 0) {
            int loopValue = 1;
            if (!readInt(line.substr(std::string_view("(loop ").size()), loopValue)) {
                return false;
            }
            currentClip->loop = loopValue != 0;
        } else if (currentClip != nullptr && line.rfind("(tween ", 0) == 0) {
            int tweenValue = 0;
            if (!readInt(line.substr(std::string_view("(tween ").size()), tweenValue)) {
                return false;
            }
            legacyClipTween = tweenValue != 0;
        } else if (currentClip != nullptr && line.rfind("(frame ", 0) == 0) {
            SpriteAnimFrame frame{};
            if (!parseFrameLine(line, frame)) {
                return false;
            }
            currentClip->frames.push_back(std::move(frame));
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }
    finishClip();

    if (bank.clips.empty()) {
        return false;
    }

    for (std::size_t index = 0; index < bank.clips.size(); ++index) {
        const SpriteAnimClip& clip = bank.clips[index];
        if (clip.name.empty() || clip.frames.empty()) {
            return false;
        }
        for (const SpriteAnimFrame& frame : clip.frames) {
            if (frame.id.empty() || frame.duration <= 0.0f) {
                return false;
            }
        }
        bank.clipIndexByName.emplace(clip.name, index);
    }

    return true;
}

std::string serializeSpriteAnimBank(const SpriteAnimBank& bank) {
    std::ostringstream out;
    out << "(sprite-anim\n";
    for (const SpriteAnimClip& clip : bank.clips) {
        out << "  (clip \"" << clip.name << "\"\n";
        out << "    (loop " << (clip.loop ? 1 : 0) << ")\n";
        for (const SpriteAnimFrame& frame : clip.frames) {
            out << "    (frame \"" << frame.id << "\" " << frame.duration;
            writeTweenSuffix(out, frame);
            out << ")\n";
        }
        out << "  )\n";
    }
    out << ")\n";
    return out.str();
}

}
