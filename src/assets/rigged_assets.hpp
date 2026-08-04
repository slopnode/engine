#pragma once

#include <raylib.h>

#include <cstddef>
#include <string>
#include <vector>

namespace slopengine {

/** One bone in a skeleton hierarchy. */
struct SkeletonBone {
    std::string name;
    int parent = -1; /**< Parent bone index, or -1 for root. */
    Transform bindPose{};
};

/** Parsed .skel skeleton description. */
struct SkeletonAsset {
    std::string id;
    int version = 0;
    std::vector<SkeletonBone> bones;
};

/** One draw primitive inside a .geo asset. */
struct GeoPrimitive {
    std::string name;
    std::string material; /**< Material virtual path. */
    std::size_t vertexOffset = 0;
    std::size_t vertexCount = 0;
    std::size_t indexOffset = 0;
    std::size_t indexCount = 0;
    std::string rigidBone; /**< Optional bone name for rigid attachment. */
    bool transparent = false;
};

/** Parsed .geo mesh description (buffers live in sibling .vert / .weights). */
struct GeoAsset {
    std::string skeletonId; /**< Empty for static props. */
    bool verticesImplicit = true;
    bool weightsImplicit = false;
    std::vector<GeoPrimitive> primitives;
};

/** One clip entry from a .anim file. */
struct AnimClip {
    std::string name;
    float fps = 0.0f;
    float duration = 0.0f;
    bool tracksImplicit = true;
    std::string tracksFile; /**< Optional explicit tracks path. */
};

/** Parsed .anim bank metadata before tracks are loaded. */
struct AnimAsset {
    std::string skeletonId;
    std::vector<AnimClip> clips;
};

}
