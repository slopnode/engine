#pragma once

#ifdef SLOPENGINE_HAS_STEAM_AUDIO

#include "assets/asset_store.hpp"
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
    std::vector<IPLMaterial> materials; /**< Unique per-face-material acoustic profiles. */
};

/** @p excludeBrushIds skips movable/door brushes so they aren't baked in as permanent occluders.
 *  Each face's acoustic profile is resolved via @p assets from its BrushFace::material path. */
bool buildSteamAudioMeshFromBrushes(
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* excludeBrushIds,
    AssetStore& assets,
    SteamAudioSceneMesh& out);

bool createSteamAudioScene(
    IPLContext context,
    IPLSimulator simulator,
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* excludeBrushIds,
    AssetStore& assets,
    IPLScene* outScene,
    IPLStaticMesh* outMesh);

void destroySteamAudioScene(
    IPLSimulator simulator,
    IPLScene* scene,
    IPLStaticMesh* mesh);

}

#endif
