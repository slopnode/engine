#include "history.hpp"

namespace slopmap {

DocumentSnapshot DocumentHistory::capture(const EditorDocument& doc, std::uint64_t gen) {
    DocumentSnapshot snap{};
    snap.gen = gen;
    snap.brushes = doc.brushes;
    snap.instances = doc.instances;
    snap.things = doc.things;
    snap.selectionMode = doc.selectionMode;
    snap.selectedBrushes = doc.selectedBrushes;
    snap.selectedFaces = doc.selectedFaces;
    snap.selectedEdges = doc.selectedEdges;
    snap.selectedVerts = doc.selectedVerts;
    snap.selectedEntities = doc.selectedEntities;
    snap.activeBrush = doc.activeBrush;
    snap.activeFace = doc.activeFace;
    snap.activeEdge = doc.activeEdge;
    snap.activeVert = doc.activeVert;
    snap.activeEntity = doc.activeEntity;
    snap.defaultMaterial = doc.defaultMaterial;
    snap.nextBrushSerial = doc.nextBrushSerial;
    snap.nextPrefabSerial = doc.nextPrefabSerial;
    snap.nextThingSerial = doc.nextThingSerial;
    return snap;
}

void DocumentHistory::apply(EditorDocument& doc, const DocumentSnapshot& snap) {
    doc.brushes = snap.brushes;
    doc.instances = snap.instances;
    doc.things = snap.things;
    doc.selectionMode = snap.selectionMode;
    doc.selectedBrushes = snap.selectedBrushes;
    doc.selectedFaces = snap.selectedFaces;
    doc.selectedEdges = snap.selectedEdges;
    doc.selectedVerts = snap.selectedVerts;
    doc.selectedEntities = snap.selectedEntities;
    doc.activeBrush = snap.activeBrush;
    doc.activeFace = snap.activeFace;
    doc.activeEdge = snap.activeEdge;
    doc.activeVert = snap.activeVert;
    doc.activeEntity = snap.activeEntity;
    doc.defaultMaterial = snap.defaultMaterial;
    doc.nextBrushSerial = snap.nextBrushSerial;
    doc.nextPrefabSerial = snap.nextPrefabSerial;
    doc.nextThingSerial = snap.nextThingSerial;
}

void DocumentHistory::trim() {
    while (undoStack.size() > kMaxEntries) {
        undoStack.erase(undoStack.begin());
    }
}

void DocumentHistory::clear() {
    undoStack.clear();
    redoStack.clear();
    coalescing = false;
    nextGen = 1;
    currentGen = 0;
    cleanGen = 0;
}

void DocumentHistory::markClean() {
    cleanGen = currentGen;
}

void DocumentHistory::prepareEdit(EditorDocument& doc) {
    if (coalescing) {
        return;
    }
    undoStack.push_back(capture(doc, currentGen));
    trim();
    currentGen = nextGen++;
    redoStack.clear();
    coalescing = true;
}

void DocumentHistory::abortEdit(EditorDocument& doc) {
    if (!coalescing) {
        return;
    }
    if (!undoStack.empty()) {
        currentGen = undoStack.back().gen;
        apply(doc, undoStack.back());
        undoStack.pop_back();
    }
    coalescing = false;
}

void DocumentHistory::endEdit() {
    coalescing = false;
}

bool DocumentHistory::canUndo() const {
    return coalescing || !undoStack.empty();
}

bool DocumentHistory::canRedo() const {
    return !coalescing && !redoStack.empty();
}

bool DocumentHistory::undo(EditorDocument& doc) {
    if (coalescing) {
        abortEdit(doc);
        doc.dirty = currentGen != cleanGen;
        return true;
    }
    if (undoStack.empty()) {
        return false;
    }
    redoStack.push_back(capture(doc, currentGen));
    currentGen = undoStack.back().gen;
    apply(doc, undoStack.back());
    undoStack.pop_back();
    doc.dirty = currentGen != cleanGen;
    return true;
}

bool DocumentHistory::redo(EditorDocument& doc) {
    if (coalescing) {
        endEdit();
    }
    if (redoStack.empty()) {
        return false;
    }
    undoStack.push_back(capture(doc, currentGen));
    trim();
    currentGen = redoStack.back().gen;
    apply(doc, redoStack.back());
    redoStack.pop_back();
    doc.dirty = currentGen != cleanGen;
    return true;
}

}
