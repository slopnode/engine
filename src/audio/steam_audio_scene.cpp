#ifdef SLOPENGINE_HAS_STEAM_AUDIO

#include "audio/steam_audio_scene.hpp"

#include <raylib.h>

#include <unordered_map>
#include <vector>

namespace slopengine {

namespace {

IPLMaterial toIplMaterial(const MaterialSoundProfile& profile) {
    IPLMaterial material{};
    material.absorption[0] = profile.absorptionLow;
    material.absorption[1] = profile.absorptionMid;
    material.absorption[2] = profile.absorptionHigh;
    material.scattering = profile.scattering;
    material.transmission[0] = profile.transmissionLow;
    material.transmission[1] = profile.transmissionMid;
    material.transmission[2] = profile.transmissionHigh;
    return material;
}

}

bool buildSteamAudioMeshFromBrushes(
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* excludeBrushIds,
    AssetStore& assets,
    SteamAudioSceneMesh& out) {
    out.vertices.clear();
    out.triangles.clear();
    out.materialIndices.clear();
    out.materials.clear();

    std::unordered_map<std::string, IPLint32> materialIndexByPath;

    for (const Brush& brush : brushes) {
        if (excludeBrushIds != nullptr && !brush.id.empty() && excludeBrushIds->count(brush.id) != 0) {
            continue;
        }
        for (const BrushFace& face : brush.faces) {
            if (face.nodraw || face.vertices.size() < 3) {
                continue;
            }

            const auto [it, inserted] = materialIndexByPath.try_emplace(
                face.material, static_cast<IPLint32>(out.materials.size()));
            if (inserted) {
                const MaterialAsset* materialAsset = assets.getMaterialAsset(face.material);
                out.materials.push_back(toIplMaterial(
                    materialAsset != nullptr ? materialAsset->sound : MaterialSoundProfile{}));
            }
            const IPLint32 materialIndex = it->second;

            const IPLint32 base = static_cast<IPLint32>(out.vertices.size());
            for (const Vector3& v : face.vertices) {
                out.vertices.push_back(IPLVector3{v.x, v.y, v.z});
            }
            for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                IPLTriangle tri{};
                tri.indices[0] = base;
                tri.indices[1] = base + static_cast<IPLint32>(i);
                tri.indices[2] = base + static_cast<IPLint32>(i + 1);
                out.triangles.push_back(tri);
                out.materialIndices.push_back(materialIndex);
            }
        }
    }

    return !out.triangles.empty();
}

bool createSteamAudioScene(
    IPLContext context,
    IPLSimulator simulator,
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* excludeBrushIds,
    AssetStore& assets,
    IPLScene* outScene,
    IPLStaticMesh* outMesh) {
    if (context == nullptr || simulator == nullptr || outScene == nullptr || outMesh == nullptr) {
        return false;
    }

    destroySteamAudioScene(simulator, outScene, outMesh);

    SteamAudioSceneMesh mesh;
    if (!buildSteamAudioMeshFromBrushes(brushes, excludeBrushIds, assets, mesh)) {
        return false;
    }

    IPLSceneSettings sceneSettings{};
    sceneSettings.type = IPL_SCENETYPE_DEFAULT;
    IPLScene scene = nullptr;
    if (iplSceneCreate(context, &sceneSettings, &scene) != IPL_STATUS_SUCCESS) {
        return false;
    }

    IPLStaticMeshSettings meshSettings{};
    meshSettings.numVertices = static_cast<IPLint32>(mesh.vertices.size());
    meshSettings.numTriangles = static_cast<IPLint32>(mesh.triangles.size());
    meshSettings.numMaterials = static_cast<IPLint32>(mesh.materials.size());
    meshSettings.vertices = mesh.vertices.data();
    meshSettings.triangles = mesh.triangles.data();
    meshSettings.materialIndices = mesh.materialIndices.data();
    meshSettings.materials = mesh.materials.data();

    IPLStaticMesh staticMesh = nullptr;
    if (iplStaticMeshCreate(scene, &meshSettings, &staticMesh) != IPL_STATUS_SUCCESS) {
        iplSceneRelease(&scene);
        return false;
    }

    iplStaticMeshAdd(staticMesh, scene);
    iplSceneCommit(scene);
    iplSimulatorSetScene(simulator, scene);
    iplSimulatorCommit(simulator);

    *outScene = scene;
    *outMesh = staticMesh;
    return true;
}

void destroySteamAudioScene(
    IPLSimulator simulator,
    IPLScene* scene,
    IPLStaticMesh* mesh) {
    if (simulator != nullptr) {
        iplSimulatorSetScene(simulator, nullptr);
        iplSimulatorCommit(simulator);
    }
    if (mesh != nullptr && *mesh != nullptr) {
        iplStaticMeshRelease(mesh);
        *mesh = nullptr;
    }
    if (scene != nullptr && *scene != nullptr) {
        iplSceneRelease(scene);
        *scene = nullptr;
    }
}

}

#endif
