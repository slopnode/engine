#pragma once

#include <filesystem>
#include <string>

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
SoundFontInfo readSoundFontInfo(const std::filesystem::path& path);

}
