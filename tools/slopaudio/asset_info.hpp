#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace slopaudio {

struct MidiInfo {
    std::string trackName;
    std::string copyright;
    std::string text;
    int usedChannels = 0;
    int usedPrograms = 0;
    int totalNotes = 0;
    unsigned int durationMs = 0;
    bool valid = false;
};

struct MidiNote {
    float startSec = 0.0f;
    float endSec = 0.0f;
    unsigned char key = 0;      // MIDI note number, 0-127
    unsigned char velocity = 0; // 1-127
};

// One MIDI channel (0-15) with the notes it played. Channels, not file-track chunks, are the
// grouping used here: General MIDI files conventionally give each instrument its own channel,
// and channel is also what carries the program (instrument) number, so it doubles as an
// instrument grouping. Channel 9 is the GM percussion channel by convention.
struct MidiChannelTrack {
    int channel = 0;
    int program = 0; // GM program number last set on this channel; 0 (Acoustic Grand Piano) if never set.
    bool programSet = false;
    std::vector<MidiNote> notes;
};

struct MidiTrackView {
    std::vector<MidiChannelTrack> channels; // Only channels that played at least one note.
    float durationSec = 0.0f;
    bool valid = false;
};

struct SoundFontInfo {
    std::string name;
    std::string engineer;
    std::string copyright;
    std::string comment;
    std::string software;
    std::string creationDate;
    int presetCount = 0;
    bool valid = false;
};

MidiInfo readMidiInfo(const std::filesystem::path& path);
MidiTrackView readMidiTrackView(const std::filesystem::path& path);
SoundFontInfo readSoundFontInfo(const std::filesystem::path& path);

/** General MIDI program (instrument) name, 0-127. Returns "Unknown" out of range. */
const char* gmProgramName(int program);

}
