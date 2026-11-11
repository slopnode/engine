#include "test_assert.hpp"

#include "assets/sprite_loader.hpp"
#include "render/components.hpp"
#include "render/gpu_ready.hpp"
#include "render/sprite_billboard.hpp"

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

void runHeadlessAssetTests() {
    CHECK_FALSE(gpuReady());

    Image image = GenImageColor(8, 4, RED);
    const Texture2D texture = loadTextureFromImageIfGpu(image);
    UnloadImage(image);
    CHECK(texture.id == 0);
    CHECK(texture.width == 8 && texture.height == 4);

    float vertices[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    Mesh mesh{};
    mesh.vertexCount = 3;
    mesh.triangleCount = 1;
    mesh.vertices = vertices;
    uploadMeshIfGpu(&mesh, false);
    CHECK(mesh.vaoId == 0);
    CHECK(mesh.vboId == nullptr);

    CHECK(loadShaderFromMemoryIfGpu("", "").id == 0);

    SpriteAsset asset;
    SpriteFrame frame;
    frame.id = "a";
    SpriteRotation rotation;
    rotation.texturePath = "sprites/a";
    rotation.pixelWidth = 8;
    rotation.pixelHeight = 4;
    frame.rotations[0] = rotation;
    asset.frames.push_back(frame);
    SpriteAtlas atlas;
    atlas.textures.push_back(texture);
    atlas.rects["sprites/a"] = SpriteAtlasRect{0, Rectangle{0.0f, 0.0f, 8.0f, 4.0f}};
    GlobalTransformation global{};
    global.matrix = MatrixIdentity();
    CHECK(resolveSpriteBillboard(asset, atlas, "a", 0.0f, global, {0.0f, 0.0f, 5.0f}, 0.0f).has_value());
    atlas.textures[0] = Texture2D{};
    CHECK_FALSE(resolveSpriteBillboard(asset, atlas, "a", 0.0f, global, {0.0f, 0.0f, 5.0f}, 0.0f).has_value());
}

}
