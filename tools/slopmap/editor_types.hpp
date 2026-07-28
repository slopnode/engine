#pragma once

#include "map/brush.hpp"
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
    enum class Kind { Thing, Instance } kind = Kind::Thing;
    int index = -1;

    bool valid() const { return index >= 0; }
    bool operator==(const EntityRef& other) const {
        return kind == other.kind && index == other.index;
    }
};

struct EditorDocument {
    std::string assetPath;
    std::vector<slopengine::Brush> brushes;
    std::vector<slopengine::PrefabInstance> instances;
    std::vector<slopengine::Thing> things;
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
};

}
