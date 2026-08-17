#pragma once

#include "sexpr.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct s7_scheme;

namespace slopthing {

struct FolderEntry {
    std::string path;
    std::string icon;
};

struct ThingEntry {
    std::string id;
    NodePtr alist; // proper list of (key . value) / (key value...) pairs
};

/** Raw, order-preserving model of a package's data/things.s7 file. */
struct ThingsDoc {
    std::filesystem::path path;
    std::vector<FolderEntry> folders;
    std::vector<ThingEntry> things;
    bool loaded = false;

    bool load(s7_scheme* sc, const std::filesystem::path& file, std::string* error = nullptr);
    bool save(const std::filesystem::path& file, std::string* error = nullptr) const;

    ThingEntry* find(const std::string& id);
    const ThingEntry* find(const std::string& id) const;
    bool renameId(const std::string& oldId, const std::string& newId);
    bool idInUse(const std::string& id) const;
};

/** Renders the full file text for a document (used by save() and for previewing). */
std::string renderThingsDoc(const ThingsDoc& doc);

// --- known-block helpers ---------------------------------------------------
// things.s7 nests several optional sub-alists under a single key (motor,
// melee, ranged, lunge, sight). These helpers add/remove/read those blocks
// as a unit so the inspector can drive an "enabled" checkbox per block while
// leaving anything the schema doesn't know about in the alist untouched.

bool hasBlock(const NodePtr& alist, const std::string& key);
NodePtr blockAlist(const NodePtr& alist, const std::string& key);
void setBlock(NodePtr& alist, const std::string& key, NodePtr blockAlist);
void removeBlock(NodePtr& alist, const std::string& key);

NodePtr defaultMotorBlock();
NodePtr defaultSightBlock();

}
