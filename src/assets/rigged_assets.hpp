#pragma once

#include <raylib.h>

#include <cstddef>
#include <string>
#include <vector>

namespace slopengine {

struct SkeletonBone {
    std::string name;
    int parent = -1;
    Transform bindPose{};
};

struct SkeletonAsset {
    std::string id;
    int version = 0;
    std::vector<SkeletonBone> bones;
};

struct GeoPrimitive {
    std::string name;
    std::string material;
    std::size_t vertexOffset = 0;
    std::size_t vertexCount = 0;
    std::size_t indexOffset = 0;
    std::size_t indexCount = 0;
    std::string rigidBone;
};

struct GeoAsset {
    std::string skeletonId;
    bool verticesImplicit = true;
    bool weightsImplicit = false;
    std::vector<GeoPrimitive> primitives;
};

struct AnimClip {
    std::string name;
    float fps = 0.0f;
    float duration = 0.0f;
    bool tracksImplicit = true;
    std::string tracksFile;
};

struct AnimAsset {
    std::string skeletonId;
    std::vector<AnimClip> clips;
};

}
