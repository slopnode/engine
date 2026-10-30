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
    bool twoSided = false;
};

/** A named point on a bone, Half-Life attachment style: where a held geo or effect hangs. */
struct GeoAttachment {
    std::string name;
    std::string bone;
    Vector3 position{0.0f, 0.0f, 0.0f};
    Vector3 rotationDeg{0.0f, 0.0f, 0.0f}; /**< Pitch, yaw, roll in degrees. */
    float scale = 1.0f;
};

/** First-person placement defaults for a geo attached to a view socket. */
struct GeoView {
    bool present = false;
    Vector3 position{0.0f, 0.0f, 0.0f};
    Vector3 rotationDeg{0.0f, 0.0f, 0.0f}; /**< Pitch, yaw, roll in degrees. */
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

/** Parsed .geo mesh description (buffers live in sibling .vert / .weights). */
struct GeoAsset {
    std::string skeletonId; /**< Empty for static props. */
    bool verticesImplicit = true;
    bool weightsImplicit = false;
    GeoView view;
    std::vector<GeoAttachment> attachments;
    std::vector<GeoPrimitive> primitives;
};

/** A named frame inside a clip, fired as a script hook when playback crosses it. */
struct AnimEvent {
    int frame = 0;
    std::string name;
};

/** One clip entry from a .anim file. */
struct AnimClip {
    std::string name;
    float fps = 0.0f;
    float duration = 0.0f;
    bool tracksImplicit = true;
    std::string tracksFile; /**< Optional explicit tracks path. */
    std::vector<AnimEvent> events;
};

/** Parsed .anim bank metadata before tracks are loaded. */
struct AnimAsset {
    std::string skeletonId;
    std::vector<AnimClip> clips;
};

}
