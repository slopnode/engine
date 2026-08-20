#ifdef SLOPENGINE_HAS_STEAM_AUDIO

#include "audio/steam_audio_scene.hpp"

#include <raylib.h>

#include <vector>

namespace slopengine {

namespace {

IPLMaterial defaultMaterial() {
    IPLMaterial material{};
    material.absorption[0] = 0.05f;
    material.absorption[1] = 0.08f;
    material.absorption[2] = 0.12f;
    material.scattering = 0.15f;
    material.transmission[0] = 0.120f;
    material.transmission[1] = 0.070f;
    material.transmission[2] = 0.040f;
    return material;
}

}

bool buildSteamAudioMeshFromBrushes(
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* excludeBrushIds,
    SteamAudioSceneMesh& out) {
    out.vertices.clear();
    out.triangles.clear();
    out.materialIndices.clear();
    out.material = defaultMaterial();

    for (const Brush& brush : brushes) {
        if (excludeBrushIds != nullptr && !brush.id.empty() && excludeBrushIds->count(brush.id) != 0) {
            continue;
        }
        for (const BrushFace& face : brush.faces) {
            if (face.nodraw || face.vertices.size() < 3) {
                continue;
            }
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
                out.materialIndices.push_back(0);
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
    IPLScene* outScene,
    IPLStaticMesh* outMesh) {
    if (context == nullptr || simulator == nullptr || outScene == nullptr || outMesh == nullptr) {
        return false;
    }

    destroySteamAudioScene(simulator, outScene, outMesh);

    SteamAudioSceneMesh mesh;
    if (!buildSteamAudioMeshFromBrushes(brushes, excludeBrushIds, mesh)) {
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
    meshSettings.numMaterials = 1;
    meshSettings.vertices = mesh.vertices.data();
    meshSettings.triangles = mesh.triangles.data();
    meshSettings.materialIndices = mesh.materialIndices.data();
    meshSettings.materials = &mesh.material;

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
