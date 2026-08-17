#include "things_doc.hpp"

#include <s7.h>

#include <fstream>
#include <sstream>

namespace slopthing {

namespace {

NodePtr valueOf(s7_scheme* sc, const char* name) {
    s7_pointer v = s7_name_to_value(sc, name);
    if (v == nullptr) {
        return makeNil();
    }
    return fromS7(sc, v);
}

NodePtr kv(const std::string& key, NodePtr value) {
    return makeCons(makeSymbol(key), makeCons(value, makeNil()));
}

/** Builds `(key items...)` where @p rest is already the item list (not scalar-wrapped). */
NodePtr kvRest(const std::string& key, NodePtr rest) {
    return makeCons(makeSymbol(key), rest);
}

}

bool ThingsDoc::load(s7_scheme* sc, const std::filesystem::path& file, std::string* error) {
    folders.clear();
    things.clear();
    loaded = false;
    path = file;

    if (!std::filesystem::exists(file)) {
        if (error != nullptr) {
            *error = "file does not exist: " + file.string();
        }
        return false;
    }

    s7_load(sc, file.string().c_str());

    for (const NodePtr& entry : listItems(valueOf(sc, "*package-thing-folders*"))) {
        if (!isPair(entry) || !entry->car) {
            continue;
        }
        FolderEntry f;
        f.path = entry->car->str;
        const NodePtr icon = scalarValue(entry->cdr);
        if (icon) {
            f.icon = icon->str;
        }
        folders.push_back(std::move(f));
    }

    for (const NodePtr& entry : listItems(valueOf(sc, "*package-things*"))) {
        if (!isPair(entry) || !entry->car) {
            continue;
        }
        ThingEntry t;
        t.id = entry->car->str;
        t.alist = scalarValue(entry->cdr);
        if (!t.alist) {
            t.alist = makeNil();
        }
        things.push_back(std::move(t));
    }

    loaded = true;
    return true;
}

bool ThingsDoc::save(const std::filesystem::path& file, std::string* error) const {
    std::ofstream out(file, std::ios::trunc);
    if (!out.is_open()) {
        if (error != nullptr) {
            *error = "failed to open for write: " + file.string();
        }
        return false;
    }
    out << renderThingsDoc(*this);
    return true;
}

ThingEntry* ThingsDoc::find(const std::string& id) {
    for (ThingEntry& t : things) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

const ThingEntry* ThingsDoc::find(const std::string& id) const {
    for (const ThingEntry& t : things) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

bool ThingsDoc::idInUse(const std::string& id) const {
    return find(id) != nullptr;
}

bool ThingsDoc::renameId(const std::string& oldId, const std::string& newId) {
    if (oldId == newId) {
        return true;
    }
    if (idInUse(newId)) {
        return false;
    }
    ThingEntry* entry = find(oldId);
    if (entry == nullptr) {
        return false;
    }
    entry->id = newId;
    return true;
}

std::string renderThingsDoc(const ThingsDoc& doc) {
    std::ostringstream out;

    out << "(define *package-thing-folders*\n";
    out << "  '(";
    for (std::size_t i = 0; i < doc.folders.size(); ++i) {
        if (i > 0) {
            out << "\n    ";
        }
        out << "(" << writeInline(makeString(doc.folders[i].path)) << " . "
            << writeInline(makeString(doc.folders[i].icon)) << ")";
    }
    out << "))\n\n";

    out << "(define *package-things*\n";
    out << "  (list";
    if (doc.things.empty()) {
        out << "))\n";
    } else {
        out << "\n";
        for (std::size_t i = 0; i < doc.things.size(); ++i) {
            const ThingEntry& t = doc.things[i];
            out << "    (cons " << writeInline(makeString(t.id)) << "\n";
            out << "          '(";
            const std::vector<NodePtr> kvs = listItems(t.alist);
            for (std::size_t k = 0; k < kvs.size(); ++k) {
                if (k > 0) {
                    out << "\n            ";
                }
                out << writeInline(kvs[k]);
            }
            out << "))";
            out << (i + 1 == doc.things.size() ? "))\n" : "\n");
        }
    }

    return out.str();
}

bool hasBlock(const NodePtr& alist, const std::string& key) {
    return alistHasKey(alist, key);
}

NodePtr blockAlist(const NodePtr& alist, const std::string& key) {
    NodePtr pair = alistFindPair(alist, key);
    if (!pair) {
        return nullptr;
    }
    return pair->cdr;
}

void setBlock(NodePtr& alist, const std::string& key, NodePtr blockAlistNode) {
    alistSetRest(alist, key, std::move(blockAlistNode));
}

void removeBlock(NodePtr& alist, const std::string& key) {
    alistRemoveKey(alist, key);
}

NodePtr defaultMotorBlock() {
    std::vector<NodePtr> items{
        kv("radius", makeFloat(0.3)),
        kv("height", makeFloat(1.1)),
        kv("speed", makeFloat(6.0)),
        kv("gravity", makeFloat(9.81)),
        kv("step-height", makeFloat(0.4)),
        kv("hull", makeSymbol("capsule")),
        kv("move", makeSymbol("slide")),
    };
    return makeList(items);
}

NodePtr defaultSightBlock() {
    std::vector<NodePtr> items{
        kv("range", makeFloat(28.0)),
        kv("fov", makeFloat(160.0)),
        kvRest("see-tags", makeStringList({"player"})),
        kvRest("ignore-tags", makeStringList({})),
        kv("enabled", makeBool(true)),
    };
    return makeList(items);
}

NodePtr defaultMeleeBlock() {
    std::vector<NodePtr> items{
        kv("damage", makeFloat(10.0)),
        kv("range", makeFloat(1.2)),
        kv("cooldown", makeFloat(1.0)),
        kv("anim", makeString("melee")),
    };
    return makeList(items);
}

NodePtr defaultRangedBlock() {
    std::vector<NodePtr> items{
        kv("cooldown", makeFloat(2.0)),
        kv("range", makeFloat(24.0)),
        kv("min-range", makeFloat(0.0)),
        kv("anim", makeString("attack")),
    };
    return makeList(items);
}

NodePtr defaultLungeBlock() {
    std::vector<NodePtr> items{
        kv("range", makeFloat(14.0)),
        kv("speed", makeFloat(16.0)),
        kv("cooldown", makeFloat(2.5)),
        kv("duration", makeFloat(0.9)),
    };
    return makeList(items);
}

}
