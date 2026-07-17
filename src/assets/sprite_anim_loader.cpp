#include "assets/sprite_anim_loader.hpp"

#include <cctype>
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

bool readFloat(std::string_view text, float& out) {
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

} // namespace

bool parseSpriteAnimBank(std::string_view source, SpriteAnimBank& bank) {
    bank = {};
    SpriteAnimClip* currentClip = nullptr;

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
            bank.clips.push_back(SpriteAnimClip{});
            currentClip = &bank.clips.back();
            currentClip->name = *clipName;
        } else if (currentClip != nullptr && line.rfind("(fps ", 0) == 0) {
            float fps = 8.0f;
            if (!readFloat(line.substr(std::string_view("(fps ").size()), fps) || fps <= 0.0f) {
                return false;
            }
            currentClip->fps = fps;
        } else if (currentClip != nullptr && line.rfind("(loop ", 0) == 0) {
            int loopValue = 1;
            if (!readInt(line.substr(std::string_view("(loop ").size()), loopValue)) {
                return false;
            }
            currentClip->loop = loopValue != 0;
        } else if (currentClip != nullptr && line.rfind("(frames ", 0) == 0) {
            std::string_view rest = line.substr(std::string_view("(frames ").size());
            while (true) {
                const std::size_t quoteStart = rest.find('"');
                if (quoteStart == std::string_view::npos) {
                    break;
                }
                const std::size_t quoteEnd = rest.find('"', quoteStart + 1);
                if (quoteEnd == std::string_view::npos) {
                    return false;
                }
                currentClip->frames.emplace_back(rest.substr(quoteStart + 1, quoteEnd - quoteStart - 1));
                rest.remove_prefix(quoteEnd + 1);
            }
            if (currentClip->frames.empty()) {
                return false;
            }
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    if (bank.clips.empty()) {
        return false;
    }

    for (std::size_t index = 0; index < bank.clips.size(); ++index) {
        const SpriteAnimClip& clip = bank.clips[index];
        if (clip.name.empty() || clip.frames.empty() || clip.fps <= 0.0f) {
            return false;
        }
        bank.clipIndexByName.emplace(clip.name, index);
    }

    return true;
}

}
