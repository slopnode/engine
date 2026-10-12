#pragma once

#include "map/brush.hpp"
#include "map/graph.hpp"
#include "map/thing.hpp"
#include "map/prefab.hpp"

#include <string>
#include <vector>

namespace slopmap {

enum class SelectionMode {
    Brush,
    Face,
    Edge,
    Vert,
    Entity,
};

struct FaceRef {
    int brush = -1;
    int face = -1;

    bool valid() const { return brush >= 0 && face >= 0; }
    bool operator==(const FaceRef& other) const {
        return brush == other.brush && face == other.face;
    }
};

struct VertRef {
    int brush = -1;
    int face = -1;
    int vert = -1;

    bool valid() const { return brush >= 0 && face >= 0 && vert >= 0; }
    bool operator==(const VertRef& other) const {
        return brush == other.brush && face == other.face && vert == other.vert;
    }
};

struct EdgeRef {
    int brush = -1;
    int face = -1;
    int edge = -1;

    bool valid() const { return brush >= 0 && face >= 0 && edge >= 0; }
    bool operator==(const EdgeRef& other) const {
        return brush == other.brush && face == other.face && edge == other.edge;
    }
};

struct EntityRef {
    enum class Kind { Thing, Instance, Knot } kind = Kind::Thing;
    int index = -1;
    int path = -1;

    bool valid() const { return index >= 0 && (kind != Kind::Knot || path >= 0); }
    bool operator==(const EntityRef& other) const {
        return kind == other.kind && index == other.index && path == other.path;
    }
};

inline EntityRef knotRef(int path, int knot) {
    EntityRef ref{};
    ref.kind = EntityRef::Kind::Knot;
    ref.index = knot;
    ref.path = path;
    return ref;
}

/** One brush that Editor::detectLeak() found sitting in exterior-flagged
 *  space (i.e. flagged by MapHullAnalysis::detailOutsideWarnings). Kept as
 *  data rather than just the warning string so the leak window can offer a
 *  "Select" button without re-parsing text. */
struct LeakBrushHit {
    std::string id;
    std::string role;
    Vector3 center{};
};

struct EditorDocument {
    std::string assetPath;
    std::vector<slopengine::Brush> brushes;
    std::vector<slopengine::PrefabInstance> instances;
    std::vector<slopengine::Thing> things;
    slopengine::GraphDocument graphs;
    int activePath = -1;
    bool dirty = false;
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

    bool hasSelection() const;
    bool isBrushSelected(int index) const;
    bool isFaceSelected(FaceRef ref) const;
    bool isEdgeSelected(EdgeRef ref) const;
    bool isVertSelected(VertRef ref) const;
    bool isEntitySelected(EntityRef ref) const;
    slopengine::CameraPath* pathAt(int index);
    const slopengine::CameraPath* pathAt(int index) const;
    slopengine::PathKnot* knotAt(EntityRef ref);
    const slopengine::PathKnot* knotAt(EntityRef ref) const;
};

}
