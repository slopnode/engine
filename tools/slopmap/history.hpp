#pragma once

#include "editor_types.hpp"

#include <cstdint>
#include <vector>

namespace slopmap {

struct DocumentSnapshot {
    std::uint64_t gen = 0;
    std::vector<slopengine::Brush> brushes;
    std::vector<slopengine::PrefabInstance> instances;
    std::vector<slopengine::Thing> things;
    SelectionMode selectionMode = SelectionMode::Brush;
    std::vector<int> selectedBrushes;
    std::vector<FaceRef> selectedFaces;
    std::vector<EdgeRef> selectedEdges;
    std::vector<VertRef> selectedVerts;
    std::vector<EntityRef> selectedEntities;
    int activeBrush = -1;
    FaceRef activeFace{};
    EdgeRef activeEdge{};
    VertRef activeVert{};
    EntityRef activeEntity{};
    std::string defaultMaterial = "default/cube";
    int nextBrushSerial = 1;
    int nextPrefabSerial = 1;
    int nextThingSerial = 1;
};

struct DocumentHistory {
    static constexpr std::size_t kMaxEntries = 64;

    std::vector<DocumentSnapshot> undoStack;
    std::vector<DocumentSnapshot> redoStack;
    bool coalescing = false;
    std::uint64_t nextGen = 1;
    std::uint64_t currentGen = 0;
    std::uint64_t cleanGen = 0;

    void clear();
    void markClean();
    void prepareEdit(EditorDocument& doc);
    void abortEdit(EditorDocument& doc);
    void endEdit();
    bool canUndo() const;
    bool canRedo() const;
    bool undo(EditorDocument& doc);
    bool redo(EditorDocument& doc);

private:
    static DocumentSnapshot capture(const EditorDocument& doc, std::uint64_t gen);
    static void apply(EditorDocument& doc, const DocumentSnapshot& snap);
    void trim();
};

}
