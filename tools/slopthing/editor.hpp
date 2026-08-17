#pragma once

#include "things_doc.hpp"

#include <filesystem>
#include <string>

struct s7_scheme;

namespace slopthing {

struct Editor {
    s7_scheme* scheme = nullptr;
    std::filesystem::path targetRoot;
    std::string targetPackageId;
    std::filesystem::path thingsFilePath;

    ThingsDoc doc;
    std::string selectedId;
    std::string filter;
    bool dirty = false;

    std::string statusMessage;
    float statusTimer = 0.0f;

    bool showNewThingModal = false;
    bool newThingIsDuplicate = false;
    char idInputBuf[128] = {};

    void setStatus(std::string message, float seconds = 3.0f);
    bool load();
    bool save();
    void markDirty();

    ThingEntry* selected();
    const ThingEntry* selected() const;
    void select(const std::string& id);

    bool createThing(const std::string& id, const std::string& folderPath);
    bool duplicateThing(const std::string& sourceId, const std::string& newId);
    void deleteThing(const std::string& id);
};

}
