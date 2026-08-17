#include "editor.hpp"

#include <algorithm>
#include <utility>

namespace slopthing {

void Editor::setStatus(std::string message, float seconds) {
    statusMessage = std::move(message);
    statusTimer = seconds;
}

bool Editor::load() {
    std::string error;
    if (!doc.load(scheme, thingsFilePath, &error)) {
        setStatus("Load failed: " + error, 5.0f);
        return false;
    }
    dirty = false;
    selectedId.clear();
    if (!doc.things.empty()) {
        selectedId = doc.things.front().id;
    }
    setStatus("Loaded " + std::to_string(doc.things.size()) + " things");
    return true;
}

bool Editor::save() {
    std::string error;
    if (!doc.save(thingsFilePath, &error)) {
        setStatus("Save failed: " + error, 5.0f);
        return false;
    }
    dirty = false;
    setStatus("Saved " + thingsFilePath.filename().string());
    return true;
}

void Editor::markDirty() {
    dirty = true;
}

ThingEntry* Editor::selected() {
    if (selectedId.empty()) {
        return nullptr;
    }
    return doc.find(selectedId);
}

const ThingEntry* Editor::selected() const {
    if (selectedId.empty()) {
        return nullptr;
    }
    return doc.find(selectedId);
}

void Editor::select(const std::string& id) {
    selectedId = id;
}

bool Editor::createThing(const std::string& id, const std::string& folderPath) {
    if (id.empty() || doc.idInUse(id)) {
        return false;
    }
    ThingEntry entry;
    entry.id = id;
    entry.alist = makeNil();
    setStr(entry.alist, "label", id);
    setStr(entry.alist, "path", folderPath);
    setStr(entry.alist, "icon", "page");
    setSymbol(entry.alist, "kind", "prop");
    doc.things.push_back(std::move(entry));
    select(id);
    markDirty();
    return true;
}

bool Editor::duplicateThing(const std::string& sourceId, const std::string& newId) {
    if (newId.empty() || doc.idInUse(newId)) {
        return false;
    }
    const ThingEntry* source = doc.find(sourceId);
    if (source == nullptr) {
        return false;
    }
    ThingEntry entry;
    entry.id = newId;
    entry.alist = deepClone(source->alist);
    setStr(entry.alist, "label", newId);
    doc.things.push_back(std::move(entry));
    select(newId);
    markDirty();
    return true;
}

void Editor::deleteThing(const std::string& id) {
    auto it = std::find_if(
        doc.things.begin(), doc.things.end(), [&](const ThingEntry& t) { return t.id == id; });
    if (it == doc.things.end()) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(it - doc.things.begin());
    doc.things.erase(it);
    if (selectedId == id) {
        selectedId.clear();
        if (!doc.things.empty()) {
            const std::size_t next = index < doc.things.size() ? index : doc.things.size() - 1;
            selectedId = doc.things[next].id;
        }
    }
    markDirty();
}

}
