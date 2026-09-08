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

// One MIDI file-track chunk (MTrk) with the notes it played. This is a real file track, not a
// channel: two tracks commonly share the same channel (e.g. a piano piece with separate
// left-hand/right-hand tracks both on channel 0), and grouping by channel alone would silently
// merge them into one color. `channel` is the track's predominant channel (most note-on events),
// used to look up its GM program/instrument and to detect GM percussion (channel 9).
struct MidiTrackData {
    int index = 0;      // 0-based position among the file's MTrk chunks.
    std::string name;   // From a 0x03 track-name meta-event, if present.
    int channel = -1;   // Predominant channel, or -1 if the track had no note events.
    int program = 0;    // GM program number on `channel`; 0 (Acoustic Grand Piano) if never set.
    bool programSet = false;
    std::vector<MidiNote> notes;
};

struct MidiTrackView {
    std::vector<MidiTrackData> tracks; // Only tracks that played at least one note.
    float durationSec = 0.0f;
    bool valid = false;
};

struct SoundFontPreset {
    std::string name;
    int bank = 0;
    int program = 0;
};

struct SoundFontInfo {
    std::string name;
    std::string engineer;
    std::string copyright;
    std::string comment;
    std::string software;
    std::string creationDate;
    int presetCount = 0;
    std::vector<SoundFontPreset> presets;
    bool valid = false;
};

MidiInfo readMidiInfo(const std::filesystem::path& path);
MidiTrackView readMidiTrackView(const std::filesystem::path& path);
SoundFontInfo readSoundFontInfo(const std::filesystem::path& path);

/** General MIDI program (instrument) name, 0-127. Returns "Unknown" out of range. */
const char* gmProgramName(int program);

}
