#pragma once

#ifdef SLOPENGINE_HAS_STEAM_AUDIO

#include "map/fac.hpp"

#include <phonon.h>

#include <vector>

namespace slopengine {

struct SteamAudioSceneMesh {
    std::vector<IPLVector3> vertices;
    std::vector<IPLTriangle> triangles;
    std::vector<IPLint32> materialIndices;
    IPLMaterial material{};
};

bool buildSteamAudioMeshFromFac(const FacFile& vis, SteamAudioSceneMesh& out);

bool createSteamAudioScene(
    IPLContext context,
    IPLSimulator simulator,
    const FacFile& vis,
    IPLScene* outScene,
    IPLStaticMesh* outMesh);

void destroySteamAudioScene(
    IPLSimulator simulator,
    IPLScene* scene,
    IPLStaticMesh* mesh);

}

#endif
