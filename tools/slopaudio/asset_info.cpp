#include "asset_info.hpp"

#include "tml.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>
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

    unsigned char peek() const { return pos < size ? data[pos] : 0; }
};

// One event decoded from an MTrk chunk, handed to walkMidiTrack's callback. `tick` is the
// absolute tick position (running sum of delta-times) within this track. Channel voice messages
// use status/data1/data2; meta events (status == 0xFF) use metaType/metaText instead.
struct MidiEvent {
    unsigned int tick = 0;
    unsigned char status = 0;
    unsigned char data1 = 0;
    unsigned char data2 = 0;
    unsigned char metaType = 0;
    std::string metaText;
};

// Walks one MTrk chunk starting at `r.pos` (just past the "MTrk"+length header, with `r.pos`
// already positioned at the event data and `trackEnd` marking its end), invoking `fn(event)` for
// every event in the chunk: meta events (0xFF), sysex (0xF0/0xF7, with their payload discarded --
// no caller needs it), and channel voice/mode messages with running status expanded so `fn`
// always sees an explicit status byte.
template <class Fn>
void walkMidiTrack(ByteReader& r, std::size_t trackEnd, Fn&& fn) {
    unsigned int tick = 0;
    unsigned char runningStatus = 0;
    while (r.pos < trackEnd) {
        tick += r.vlq();
        unsigned char status;
        if (r.peek() & 0x80u) {
            status = r.u8();
            if (status < 0xF0u) {
                runningStatus = status;
            }
        } else {
            status = runningStatus;
        }

        MidiEvent ev{};
        ev.tick = tick;
        ev.status = status;
        if (status == 0xFF) {
            ev.metaType = r.u8();
            const unsigned int len = r.vlq();
            ev.metaText = trimTrailing(r.bytes(len));
            fn(ev);
        } else if (status == 0xF0 || status == 0xF7) {
            r.skip(r.vlq());
        } else if (status >= 0x80 && status < 0xF0) {
            const unsigned char hi = status & 0xF0u;
            ev.data1 = r.u8();
            if (hi != 0xC0 && hi != 0xD0) {
                ev.data2 = r.u8();
            }
            fn(ev);
        } else {
            // Unexpected status (e.g. running status never established) -- bail rather than
            // desync the rest of the chunk.
            break;
        }
    }
}

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

unsigned int readU16LE(const unsigned char* p) {
    return static_cast<unsigned int>(p[0]) | (static_cast<unsigned int>(p[1]) << 8);
}

// Reads an SF2 LIST chunk's sub-chunks between [start, start+len) into `info`, matching only the
// small text fields under INFO (name/engineer/copyright/...). Leaves the stream positioned
// wherever the last read happened to land -- callers always reseek before their next read, so
// that's never relied upon.
void readSoundFontInfoList(std::ifstream& file, std::streamoff start, unsigned int len, SoundFontInfo& info) {
    std::streamoff pos = start;
    const std::streamoff end = start + static_cast<std::streamoff>(len);
    while (pos + 8 <= end) {
        unsigned char subHeader[8];
        file.seekg(pos, std::ios::beg);
        file.read(reinterpret_cast<char*>(subHeader), sizeof(subHeader));
        if (!file) {
            return;
        }
        const unsigned int subSize = readU32LE(subHeader + 4);
        const std::streamoff subDataStart = pos + 8;
        if (subDataStart + static_cast<std::streamoff>(subSize) > end) {
            return;
        }

        // INFO sub-chunks are always short human-readable strings; anything absurdly large here
        // would mean a malformed/unexpected file, not a field worth reading.
        std::string text;
        if (subSize > 0 && subSize < 4096) {
            std::vector<char> textBuf(subSize);
            file.read(textBuf.data(), static_cast<std::streamsize>(subSize));
            if (!file) {
                return;
            }
            text = trimTrailing(std::string(textBuf.data(), textBuf.size()));
        }

        if (std::memcmp(subHeader, "INAM", 4) == 0) {
            info.name = text;
        } else if (std::memcmp(subHeader, "IENG", 4) == 0) {
            info.engineer = text;
        } else if (std::memcmp(subHeader, "ICOP", 4) == 0) {
            info.copyright = text;
        } else if (std::memcmp(subHeader, "ICMT", 4) == 0) {
            info.comment = text;
        } else if (std::memcmp(subHeader, "ISFT", 4) == 0) {
            info.software = text;
        } else if (std::memcmp(subHeader, "ICRD", 4) == 0) {
            info.creationDate = text;
        }

        pos = subDataStart + static_cast<std::streamoff>(subSize) + static_cast<std::streamoff>(subSize % 2);
    }
}

// Reads an SF2 `pdta` LIST chunk's `phdr` sub-chunk between [start, start+len) into `info`. phdr
// is a flat array of fixed-size 38-byte preset-header records (name, program, bank, plus fields
// this doesn't need) -- the only piece of preset data required for a name/count listing. The
// sibling pbag/pmod/pgen/inst/ibag/imod/igen sub-chunks (and the `sdta` sample-data chunk
// entirely, handled by the caller) are never read.
void readSoundFontPresetList(std::ifstream& file, std::streamoff start, unsigned int len, SoundFontInfo& info) {
    constexpr unsigned int kRecordSize = 38;
    std::streamoff pos = start;
    const std::streamoff end = start + static_cast<std::streamoff>(len);
    while (pos + 8 <= end) {
        unsigned char subHeader[8];
        file.seekg(pos, std::ios::beg);
        file.read(reinterpret_cast<char*>(subHeader), sizeof(subHeader));
        if (!file) {
            return;
        }
        const unsigned int subSize = readU32LE(subHeader + 4);
        const std::streamoff subDataStart = pos + 8;
        if (subDataStart + static_cast<std::streamoff>(subSize) > end) {
            return;
        }

        if (std::memcmp(subHeader, "phdr", 4) == 0) {
            const unsigned int count = subSize / kRecordSize;
            std::vector<unsigned char> recordBuf(subSize);
            file.read(reinterpret_cast<char*>(recordBuf.data()), static_cast<std::streamsize>(subSize));
            if (!file || count == 0) {
                return;
            }
            // The last phdr record is the SF2 "EOP" terminator marking where the previous
            // preset's zones end, not a real preset -- excluded from the list.
            info.presets.reserve(info.presets.size() + count - 1);
            for (unsigned int i = 0; i + 1 < count; ++i) {
                const unsigned char* rec = recordBuf.data() + static_cast<std::size_t>(i) * kRecordSize;
                SoundFontPreset preset{};
                preset.name = trimTrailing(std::string(reinterpret_cast<const char*>(rec), 20));
                preset.program = static_cast<int>(readU16LE(rec + 20));
                preset.bank = static_cast<int>(readU16LE(rec + 22));
                info.presets.push_back(std::move(preset));
            }
            return; // phdr found; no need to keep scanning the remaining pdta sub-chunks.
        }

        pos = subDataStart + static_cast<std::streamoff>(subSize) + static_cast<std::streamoff>(subSize % 2);
    }
}

constexpr const char* kGmProgramNames[128] = {
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
    "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone",
    "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
    "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)",
    "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
    "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass",
    "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    "Violin", "Viola", "Cello", "Contrabass",
    "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2",
    "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
    "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
    "Oboe", "English Horn", "Bassoon", "Clarinet",
    "Piccolo", "Flute", "Recorder", "Pan Flute",
    "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)",
    "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass+lead)",
    "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)",
    "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
    "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
    "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
    "Sitar", "Banjo", "Shamisen", "Koto",
    "Kalimba", "Bag pipe", "Fiddle", "Shanai",
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
    "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
    "Telephone Ring", "Helicopter", "Applause", "Gunshot",
};

} // namespace

const char* gmProgramName(int program) {
    if (program < 0 || program >= static_cast<int>(std::size(kGmProgramNames))) {
        return "Unknown";
    }
    return kGmProgramNames[program];
}

MidiTrackView readMidiTrackView(const std::filesystem::path& path) {
    MidiTrackView view{};
    const std::vector<unsigned char> buf = readWholeFile(path);
    if (buf.empty()) {
        return view;
    }

    ByteReader header{buf.data(), buf.size()};
    if (header.bytes(4) != "MThd") {
        return view;
    }
    const unsigned int headerLen = header.u32be();
    const std::size_t headerEnd = header.pos + headerLen;
    header.u16be(); // format
    const unsigned int ntrks = header.u16be();
    const unsigned int division = header.u16be();
    header.pos = std::min(headerEnd, header.size);

    // Locate each MTrk chunk's event-data range up front so both passes below can jump straight
    // to a given track instead of re-walking every chunk before it.
    std::vector<std::pair<std::size_t, std::size_t>> trackRanges; // [dataStart, dataEnd)
    {
        ByteReader r = header;
        for (unsigned int t = 0; t < ntrks && !r.eof(); ++t) {
            if (r.bytes(4) != "MTrk") {
                break;
            }
            const unsigned int trackLen = r.u32be();
            const std::size_t dataStart = r.pos;
            const std::size_t dataEnd = std::min(r.pos + trackLen, r.size);
            trackRanges.emplace_back(dataStart, dataEnd);
            r.pos = dataEnd;
        }
    }
    if (trackRanges.empty()) {
        return view;
    }

    // SMPTE-divided files (rare -- film/video sync, not music sequencing) tick at a fixed rate
    // and ignore Set Tempo meta-events entirely; ticks-per-quarter files (the common case) need
    // the tempo map built below to convert ticks to seconds.
    const bool smpte = (division & 0x8000u) != 0;
    const int ticksPerQuarter = smpte ? 1 : static_cast<int>(division & 0x7FFFu);
    float smpteSecondsPerTick = 0.0f;
    if (smpte) {
        const int fps = 256 - static_cast<int>((division >> 8) & 0xFFu);
        const int ticksPerFrame = static_cast<int>(division & 0xFFu);
        smpteSecondsPerTick = 1.0f / static_cast<float>(std::max(1, fps * ticksPerFrame));
    }

    // Pass 1: collect (tick, microseconds-per-quarter) tempo changes across all tracks -- General
    // MIDI conventionally puts these in track 0, but nothing requires it, so every track is
    // scanned. tempoTicks[0] is always 0, defaulting to 120 BPM until overridden by an event at
    // tick 0.
    std::vector<unsigned long long> tempoTicks{0};
    std::vector<unsigned int> tempoValues{500000};
    if (!smpte) {
        for (const auto& range : trackRanges) {
            ByteReader r{buf.data(), buf.size(), range.first};
            walkMidiTrack(r, range.second, [&](const MidiEvent& ev) {
                if (ev.status != 0xFF || ev.metaType != 0x51 || ev.metaText.size() != 3) {
                    return;
                }
                const auto* bytes = reinterpret_cast<const unsigned char*>(ev.metaText.data());
                const unsigned int micros = (static_cast<unsigned int>(bytes[0]) << 16) |
                    (static_cast<unsigned int>(bytes[1]) << 8) | bytes[2];
                if (ev.tick == 0) {
                    tempoValues[0] = micros;
                } else {
                    tempoTicks.push_back(ev.tick);
                    tempoValues.push_back(micros);
                }
            });
        }
        std::vector<std::size_t> order(tempoTicks.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return tempoTicks[a] < tempoTicks[b];
        });
        std::vector<unsigned long long> sortedTicks(order.size());
        std::vector<unsigned int> sortedValues(order.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            sortedTicks[i] = tempoTicks[order[i]];
            sortedValues[i] = tempoValues[order[i]];
        }
        tempoTicks = std::move(sortedTicks);
        tempoValues = std::move(sortedValues);
    }

    // Cumulative seconds elapsed at the start of each tempo segment, so a lookup only needs to
    // add the partial-segment remainder instead of re-integrating from tick 0 every time.
    std::vector<double> cumSeconds(tempoTicks.size(), 0.0);
    for (std::size_t i = 1; i < tempoTicks.size(); ++i) {
        const unsigned long long deltaTicks = tempoTicks[i] - tempoTicks[i - 1];
        cumSeconds[i] = cumSeconds[i - 1] +
            static_cast<double>(deltaTicks) * tempoValues[i - 1] / (1000000.0 * ticksPerQuarter);
    }

    const auto tickToSeconds = [&](unsigned int tick) -> float {
        if (smpte) {
            return static_cast<float>(tick) * smpteSecondsPerTick;
        }
        std::size_t idx = 0;
        for (std::size_t i = 0; i < tempoTicks.size(); ++i) {
            if (tempoTicks[i] <= tick) {
                idx = i;
            } else {
                break;
            }
        }
        const double extraTicks = static_cast<double>(tick) - static_cast<double>(tempoTicks[idx]);
        const double secPerTick = tempoValues[idx] / (1000000.0 * ticksPerQuarter);
        return static_cast<float>(cumSeconds[idx] + extraTicks * secPerTick);
    };

    // Pass 2: one MidiTrackData per MTrk chunk that played at least one note, keeping each
    // chunk's own notes separate even when two chunks share a channel.
    unsigned int maxTick = 0;
    for (std::size_t t = 0; t < trackRanges.size(); ++t) {
        ByteReader r{buf.data(), buf.size(), trackRanges[t].first};
        const std::size_t dataEnd = trackRanges[t].second;

        std::string trackName;
        std::array<bool, 128> pendingActive{};
        std::array<unsigned int, 128> pendingStartTick{};
        std::array<unsigned char, 128> pendingVelocity{};
        std::array<int, 16> channelProgram{};
        std::array<bool, 16> channelProgramSet{};
        std::array<int, 16> channelNoteCount{};
        std::vector<MidiNote> notes;

        walkMidiTrack(r, dataEnd, [&](const MidiEvent& ev) {
            maxTick = std::max(maxTick, ev.tick);
            if (ev.status == 0xFF) {
                if (ev.metaType == 0x03 && trackName.empty()) {
                    trackName = ev.metaText;
                }
                return;
            }
            const unsigned char hi = ev.status & 0xF0u;
            const unsigned char ch = ev.status & 0x0Fu;
            if (hi == 0xC0) {
                channelProgram[ch] = ev.data1;
                channelProgramSet[ch] = true;
            } else if (hi == 0x90 || hi == 0x80) {
                const unsigned char key = ev.data1;
                const unsigned char vel = ev.data2;
                if (hi == 0x90 && vel > 0) {
                    pendingActive[key] = true;
                    pendingStartTick[key] = ev.tick;
                    pendingVelocity[key] = vel;
                    channelNoteCount[ch]++;
                } else if (pendingActive[key]) {
                    pendingActive[key] = false;
                    MidiNote note{};
                    note.startSec = tickToSeconds(pendingStartTick[key]);
                    note.endSec = std::max(tickToSeconds(ev.tick), note.startSec + 0.02f);
                    note.key = key;
                    note.velocity = pendingVelocity[key];
                    notes.push_back(note);
                }
            }
        });

        if (notes.empty()) {
            continue;
        }
        int bestChannel = -1;
        int bestCount = -1;
        for (int c = 0; c < 16; ++c) {
            if (channelNoteCount[c] > bestCount) {
                bestCount = channelNoteCount[c];
                bestChannel = c;
            }
        }
        MidiTrackData track{};
        track.index = static_cast<int>(t);
        track.name = std::move(trackName);
        track.channel = bestChannel;
        track.program = bestChannel >= 0 ? channelProgram[bestChannel] : 0;
        track.programSet = bestChannel >= 0 && channelProgramSet[bestChannel];
        track.notes = std::move(notes);
        view.tracks.push_back(std::move(track));
    }

    view.durationSec = tickToSeconds(maxTick);
    view.valid = true;
    return view;
}

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
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return info;
    }

    unsigned char riffHeader[12];
    file.read(reinterpret_cast<char*>(riffHeader), sizeof(riffHeader));
    if (!file || std::memcmp(riffHeader, "RIFF", 4) != 0 || std::memcmp(riffHeader + 8, "sfbk", 4) != 0) {
        return info;
    }

    // Walks the top-level LIST chunks (INFO, sdta, pdta) by seeking from one chunk header to the
    // next -- the (often huge, hundreds of MB) `sdta` sample-data chunk is never read, only
    // skipped over, since neither the text fields nor the preset list live in it.
    std::streamoff pos = 12;
    while (true) {
        unsigned char chunkHeader[8];
        file.seekg(pos, std::ios::beg);
        file.read(reinterpret_cast<char*>(chunkHeader), sizeof(chunkHeader));
        if (!file) {
            break;
        }
        const unsigned int chunkSize = readU32LE(chunkHeader + 4);
        const std::streamoff chunkDataStart = pos + 8;

        if (std::memcmp(chunkHeader, "LIST", 4) == 0 && chunkSize >= 4) {
            unsigned char listId[4];
            file.read(reinterpret_cast<char*>(listId), sizeof(listId));
            if (!file) {
                break;
            }
            if (std::memcmp(listId, "INFO", 4) == 0) {
                readSoundFontInfoList(file, chunkDataStart + 4, chunkSize - 4, info);
            } else if (std::memcmp(listId, "pdta", 4) == 0) {
                readSoundFontPresetList(file, chunkDataStart + 4, chunkSize - 4, info);
            }
            // Anything else (sdta, or an unrecognized LIST) falls through to the seek below
            // without its contents ever being read.
        }

        pos = chunkDataStart + static_cast<std::streamoff>(chunkSize) + static_cast<std::streamoff>(chunkSize % 2);
    }

    info.presetCount = static_cast<int>(info.presets.size());
    info.valid = true;
    return info;
}

}
