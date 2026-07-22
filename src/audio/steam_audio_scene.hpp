#pragma once

#ifdef SLOPENGINE_HAS_STEAM_AUDIO

#include "map/bsp.hpp"

#include <phonon.h>

#include <vector>

namespace slopengine {

struct SteamAudioSceneMesh {
    std::vector<IPLVector3> vertices;
    std::vector<IPLTriangle> triangles;
    std::vector<IPLint32> materialIndices;
    IPLMaterial material{};
};

bool buildSteamAudioMeshFromBsp(const BspTree& tree, SteamAudioSceneMesh& out);

bool createSteamAudioScene(
    IPLContext context,
    IPLSimulator simulator,
    const BspTree& tree,
    IPLScene* outScene,
    IPLStaticMesh* outMesh);

void destroySteamAudioScene(
    IPLSimulator simulator,
    IPLScene* scene,
    IPLStaticMesh* mesh);

}

#endif
