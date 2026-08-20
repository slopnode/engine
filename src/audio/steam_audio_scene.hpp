#pragma once

#ifdef SLOPENGINE_HAS_STEAM_AUDIO

#include "map/brush.hpp"

#include <phonon.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

struct SteamAudioSceneMesh {
    std::vector<IPLVector3> vertices;
    std::vector<IPLTriangle> triangles;
    std::vector<IPLint32> materialIndices;
    IPLMaterial material{};
};

/** @p excludeBrushIds skips movable/door brushes so they aren't baked in as permanent occluders. */
bool buildSteamAudioMeshFromBrushes(
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* excludeBrushIds,
    SteamAudioSceneMesh& out);

bool createSteamAudioScene(
    IPLContext context,
    IPLSimulator simulator,
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* excludeBrushIds,
    IPLScene* outScene,
    IPLStaticMesh* outMesh);

void destroySteamAudioScene(
    IPLSimulator simulator,
    IPLScene* scene,
    IPLStaticMesh* mesh);

}

#endif
