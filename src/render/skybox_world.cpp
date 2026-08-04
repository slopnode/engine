#include "render/skybox_world.hpp"

#include "map/bsp.hpp"
#include "map/lightmap.hpp"
#include "render/components.hpp"

#include <flecs.h>
#include <raymath.h>
#include <rlgl.h>

#include <string>
#include <unordered_set>

namespace slopengine {

const SkyboxSettings* findActiveSkybox(const flecs::world& world) {
    const SkyboxSettings* found = nullptr;
    world.each([&](const SkyboxSettings& settings) {
        if (found == nullptr) {
            found = &settings;
        }
    });
    return found;
}

void drawSkyMaterialFaces(
    flecs::world& world,
    Camera3D camera,
    AssetStore& assets,
    SkyboxShaderState& shaderState,
    const SkyboxSettings& settings) {
    flecs::entity mapEntity = world.lookup("MapStatic");
    if (!mapEntity.is_valid() || !mapEntity.has<MapLightmapState>() || !mapEntity.has<Model3D>()) {
        return;
    }
    const MapLightmapState& lightmaps = mapEntity.get<MapLightmapState>();
    if (lightmaps.skyMeshIndices.empty() || lightmaps.skyShader.id == 0) {
        return;
    }

    applySkyShaderUniforms(lightmaps.skyShader, assets, shaderState, settings);

    Matrix viewRot = MatrixLookAt(camera.position, camera.target, camera.up);
    viewRot.m12 = 0.0f;
    viewRot.m13 = 0.0f;
    viewRot.m14 = 0.0f;
    const float cameraPos[3] = {camera.position.x, camera.position.y, camera.position.z};
    const int cameraPosLoc = GetShaderLocation(lightmaps.skyShader, "cameraPos");
    const int matViewRotLoc = GetShaderLocation(lightmaps.skyShader, "matViewRot");
    SetShaderValue(lightmaps.skyShader, cameraPosLoc, cameraPos, SHADER_UNIFORM_VEC3);
    if (matViewRotLoc >= 0) {
        SetShaderValueMatrix(lightmaps.skyShader, matViewRotLoc, viewRot);
    }

    Model3D& model3d = mapEntity.get_mut<Model3D>();
    GlobalTransformation global{};
    if (mapEntity.has<GlobalTransformation>()) {
        global = mapEntity.get<GlobalTransformation>();
    }

    const std::unordered_set<int> skyMeshes(
        lightmaps.skyMeshIndices.begin(),
        lightmaps.skyMeshIndices.end());

    rlPushMatrix();
    rlMultMatrixf(MatrixToFloatV(global.matrix).v);
    for (int meshIndex = 0; meshIndex < model3d.model.meshCount; ++meshIndex) {
        if (skyMeshes.count(meshIndex) == 0) {
            continue;
        }
        Material drawMaterial = model3d.model.materials[meshIndex];
        drawMaterial.shader = lightmaps.skyShader;
        DrawMesh(model3d.model.meshes[meshIndex], drawMaterial, MatrixIdentity());
    }
    rlPopMatrix();
}

}
