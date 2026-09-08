#include "asset_info.hpp"

#include "tsf.h"
#include "tml.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace slopaudio {

namespace {

std::vector<unsigned char> readWholeFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string trimTrailing(std::string s) {
    const auto nul = s.find('\0');
    if (nul != std::string::npos) {
        s.resize(nul);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
    return s;
}

struct ByteReader {
    const unsigned char* data;
    std::size_t size;
    std::size_t pos = 0;

    bool eof() const { return pos >= size; }

    unsigned char u8() { return pos < size ? data[pos++] : 0; }

    unsigned int u16be() {
        const unsigned int hi = u8();
        const unsigned int lo = u8();
        return (hi << 8) | lo;
    }

    unsigned int u32be() {
        const unsigned int a = u8();
        const unsigned int b = u8();
        const unsigned int c = u8();
        const unsigned int d = u8();
        return (a << 24) | (b << 16) | (c << 8) | d;
    }

    unsigned int vlq() {
        unsigned int value = 0;
        for (int i = 0; i < 4 && !eof(); ++i) {
            const unsigned char b = u8();
            value = (value << 7) | (b & 0x7Fu);
            if (!(b & 0x80u)) {
                break;
            }
        }
        return value;
    }

    std::string bytes(unsigned int n) {
        const std::size_t take = std::min<std::size_t>(n, size - pos);
        std::string s(reinterpret_cast<const char*>(data + pos), take);
        pos += take;
        return s;
    }

    void skip(unsigned int n) { pos += std::min<std::size_t>(n, size - pos); }
};

void scanMidiMetaEvents(const std::vector<unsigned char>& buf, MidiInfo& info) {
    ByteReader r{buf.data(), buf.size()};
    if (r.bytes(4) != "MThd") {
        return;
    }
    const unsigned int headerLen = r.u32be();
    const std::size_t headerEnd = r.pos + headerLen;
    r.u16be(); // format
    const unsigned int ntrks = r.u16be();
    r.u16be(); // division
    r.pos = std::min(headerEnd, r.size);

    for (unsigned int t = 0; t < ntrks && !r.eof(); ++t) {
        if (r.bytes(4) != "MTrk") {
            break;
        }
        const unsigned int trackLen = r.u32be();
        const std::size_t trackEnd = std::min(r.pos + trackLen, r.size);
        unsigned char runningStatus = 0;

        while (r.pos < trackEnd) {
            r.vlq(); // delta time
            const unsigned char status = r.u8();
            if (status == 0xFF) {
                const unsigned char metaType = r.u8();
                const unsigned int len = r.vlq();
                const std::string text = trimTrailing(r.bytes(len));
                if (metaType == 0x03 && info.trackName.empty()) {
                    info.trackName = text;
                } else if (metaType == 0x02 && info.copyright.empty()) {
                    info.copyright = text;
                } else if (metaType == 0x01 && info.text.empty()) {
                    info.text = text;
                }
            } else if (status == 0xF0 || status == 0xF7) {
                r.skip(r.vlq());
            } else if (status >= 0x80) {
                runningStatus = status;
                const unsigned char hi = status & 0xF0u;
                r.skip((hi == 0xC0 || hi == 0xD0) ? 1 : 2);
            } else {
                const unsigned char hi = runningStatus & 0xF0u;
                r.skip((hi == 0xC0 || hi == 0xD0) ? 0 : 1);
            }
        }
        r.pos = trackEnd;
    }
}

unsigned int readU32LE(const unsigned char* p) {
    return static_cast<unsigned int>(p[0]) | (static_cast<unsigned int>(p[1]) << 8) |
        (static_cast<unsigned int>(p[2]) << 16) | (static_cast<unsigned int>(p[3]) << 24);
}

void scanSoundFontInfoChunk(const std::vector<unsigned char>& buf, SoundFontInfo& info) {
    if (buf.size() < 12 || std::memcmp(buf.data(), "RIFF", 4) != 0 ||
        std::memcmp(buf.data() + 8, "sfbk", 4) != 0) {
        return;
    }

    std::size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const unsigned char* id = buf.data() + pos;
        const unsigned int chunkSize = readU32LE(buf.data() + pos + 4);
        const std::size_t dataStart = pos + 8;
        const std::size_t dataEnd = dataStart + chunkSize;
        if (dataEnd > buf.size()) {
            break;
        }

        if (std::memcmp(id, "LIST", 4) == 0 && dataEnd - dataStart >= 4 &&
            std::memcmp(buf.data() + dataStart, "INFO", 4) == 0) {
            std::size_t sub = dataStart + 4;
            while (sub + 8 <= dataEnd) {
                const unsigned char* subId = buf.data() + sub;
                const unsigned int subSize = readU32LE(buf.data() + sub + 4);
                const std::size_t subDataStart = sub + 8;
                const std::size_t subDataEnd = subDataStart + subSize;
                if (subDataEnd > dataEnd) {
                    break;
                }

                const std::string text = trimTrailing(std::string(
                    reinterpret_cast<const char*>(buf.data() + subDataStart), subSize));
                if (std::memcmp(subId, "INAM", 4) == 0) {
                    info.name = text;
                } else if (std::memcmp(subId, "IENG", 4) == 0) {
                    info.engineer = text;
                } else if (std::memcmp(subId, "ICOP", 4) == 0) {
                    info.copyright = text;
                } else if (std::memcmp(subId, "ICMT", 4) == 0) {
                    info.comment = text;
                } else if (std::memcmp(subId, "ISFT", 4) == 0) {
                    info.software = text;
                } else if (std::memcmp(subId, "ICRD", 4) == 0) {
                    info.creationDate = text;
                }

                sub = subDataEnd + (subSize % 2);
            }
            return;
        }

        pos = dataEnd + (chunkSize % 2);
    }
}

} // namespace

MidiInfo readMidiInfo(const std::filesystem::path& path) {
    MidiInfo info{};
    const std::vector<unsigned char> buf = readWholeFile(path);
    if (buf.empty()) {
        return info;
    }
    scanMidiMetaEvents(buf, info);

    if (tml_message* messages = tml_load_filename(path.string().c_str())) {
        int usedChannels = 0;
        int usedPrograms = 0;
        int totalNotes = 0;
        unsigned int timeFirstNote = 0;
        unsigned int timeLength = 0;
        tml_get_info(messages, &usedChannels, &usedPrograms, &totalNotes, &timeFirstNote, &timeLength);
        info.usedChannels = usedChannels;
        info.usedPrograms = usedPrograms;
        info.totalNotes = totalNotes;
        info.durationMs = timeLength;
        tml_free(messages);
    }

    info.valid = true;
    return info;
}

SoundFontInfo readSoundFontInfo(const std::filesystem::path& path) {
    SoundFontInfo info{};
    const std::vector<unsigned char> buf = readWholeFile(path);
    if (buf.empty()) {
        return info;
    }
    scanSoundFontInfoChunk(buf, info);

    if (tsf* font = tsf_load_filename(path.string().c_str())) {
        info.presetCount = tsf_get_presetcount(font);
        tsf_close(font);
    }

    info.valid = true;
    return info;
}

}
