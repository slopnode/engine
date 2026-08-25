#include "test_assert.hpp"

#include "map/brush.hpp"
#include "map/light_occlusion.hpp"
#include "map/lightmap.hpp"
#include "map/quad_bvh.hpp"
#include "map/radiosity.hpp"
#include "map/uv_math.hpp"

#include <raylib.h>

#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace slopengine {

namespace {

LightmapFace makeTestFace(
    std::string id,
    float z,
    bool transparent,
    std::string material = "mat/test") {
    LightmapFace face;
    face.id = std::move(id);
    face.material = std::move(material);
    face.normal = {0.0f, 0.0f, 1.0f};
    face.vertices = {
        {-1.0f, -1.0f, z},
        {1.0f, -1.0f, z},
        {1.0f, 1.0f, z},
        {-1.0f, 1.0f, z},
    };
    face.transparent = transparent;
    return face;
}

MaterialBakeInfo makeSolidMaterial() {
    MaterialBakeInfo info;
    info.asset.baseColor = WHITE;
    return info;
}

MaterialBakeInfo makeAlphaMaterial(unsigned char alpha) {
    MaterialBakeInfo info;
    info.asset.baseColor = WHITE;
    info.albedoImage = GenImageColor(1, 1, Color{255, 255, 255, alpha});
    info.hasAlbedoImage = true;
    return info;
}

std::unordered_map<std::string, MaterialBakeInfo> makeMaterialCache(
    const std::vector<LightmapFace>& faces,
    MaterialBakeInfo material) {
    std::unordered_map<std::string, MaterialBakeInfo> cache;
    for (const LightmapFace& face : faces) {
        cache.emplace(face.material, material);
    }
    return cache;
}

void testCollectLightmapFacesPreservesTransparent() {
    Brush opaqueBrush;
    opaqueBrush.role = BrushRole::Hull;
    BrushFace opaqueFace;
    opaqueFace.id = "opaque";
    opaqueFace.material = "mat/a";
    opaqueFace.normal = {0.0f, 1.0f, 0.0f};
    opaqueFace.vertices = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 1.0f},
    };
    opaqueBrush.faces.push_back(std::move(opaqueFace));

    Brush glassBrush;
    glassBrush.role = BrushRole::Transparent;
    BrushFace glassFace;
    glassFace.id = "glass";
    glassFace.material = "mat/glass";
    glassFace.normal = {0.0f, 1.0f, 0.0f};
    glassFace.vertices = opaqueBrush.faces[0].vertices;
    glassBrush.faces.push_back(std::move(glassFace));

    const std::vector<LightmapFace> faces = collectLightmapFaces({opaqueBrush, glassBrush});
    CHECK(faces.size() == 2);
    CHECK_FALSE(faces[0].transparent);
    CHECK(faces[1].transparent);
}

void testCollectLightmapFacesFromTransparentBrush() {
    Brush brush;
    brush.role = BrushRole::Transparent;
    BrushFace face;
    face.id = "pane";
    face.material = "mat/glass";
    face.normal = {0.0f, 0.0f, 1.0f};
    face.vertices = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
    };
    brush.faces.push_back(std::move(face));

    const std::vector<LightmapFace> faces = collectLightmapFaces({brush});
    CHECK(faces.size() == 1);
    CHECK(faces[0].transparent);
}

void testSampleFaceOcclusionAlphaUsesTextureAlpha() {
    std::vector<LightmapFace> faces;
    faces.push_back(makeTestFace("grate", 1.0f, true, "mat/grate"));
    MaterialBakeInfo material = makeAlphaMaterial(64);
    const float alpha = sampleFaceOcclusionAlpha(
        faces[0],
        material,
        {0.0f, 0.0f, 1.0f});
    CHECK(alpha < kLightOcclusionAlphaThreshold);
    UnloadImage(material.albedoImage);
}

void testSolidTransparentFaceBlocksSegment() {
    std::vector<LightmapFace> faces;
    faces.push_back(makeTestFace("post", 1.0f, true));
    const QuadBvh bvh = buildLightmapFaceBvh(faces);
    const std::vector<char> faceTransparent = {1};
    const auto cache = makeMaterialCache(faces, makeSolidMaterial());

    const bool occluded = segmentOccludedWithAlphaOcclusion(
        bvh,
        {0.0f, 0.0f, 3.0f},
        {0.0f, 0.0f, -3.0f},
        -1,
        -1,
        faces,
        cache,
        faceTransparent);
    CHECK(occluded);
}

void testLowAlphaTransparentFaceTransmitsToOpaqueBehind() {
    std::vector<LightmapFace> faces;
    faces.push_back(makeTestFace("grate", 1.0f, true, "mat/grate"));
    faces.push_back(makeTestFace("wall", 0.0f, false, "mat/wall"));
    const QuadBvh bvh = buildLightmapFaceBvh(faces);
    const std::vector<char> faceTransparent = {1, 0};
    MaterialBakeInfo grateMaterial = makeAlphaMaterial(0);
    std::unordered_map<std::string, MaterialBakeInfo> cache;
    cache.emplace("mat/grate", grateMaterial);
    cache.emplace("mat/wall", makeSolidMaterial());

    const auto hit = raycastWithAlphaOcclusion(
        bvh,
        {0.0f, 0.0f, 2.0f},
        {0.0f, 0.0f, -1.0f},
        10.0f,
        -1,
        -1,
        faces,
        cache,
        faceTransparent);
    CHECK(hit.has_value());
    CHECK(hit->faceIndex == 1);
    UnloadImage(grateMaterial.albedoImage);
}

void testHighAlphaTransparentFaceBlocksBeforeOpaqueBehind() {
    std::vector<LightmapFace> faces;
    faces.push_back(makeTestFace("post", 1.0f, true, "mat/post"));
    faces.push_back(makeTestFace("wall", 0.0f, false, "mat/wall"));
    const QuadBvh bvh = buildLightmapFaceBvh(faces);
    const std::vector<char> faceTransparent = {1, 0};
    MaterialBakeInfo postMaterial = makeAlphaMaterial(255);
    std::unordered_map<std::string, MaterialBakeInfo> cache;
    cache.emplace("mat/post", postMaterial);
    cache.emplace("mat/wall", makeSolidMaterial());

    const auto hit = raycastWithAlphaOcclusion(
        bvh,
        {0.0f, 0.0f, 2.0f},
        {0.0f, 0.0f, -1.0f},
        10.0f,
        -1,
        -1,
        faces,
        cache,
        faceTransparent);
    CHECK(hit.has_value());
    CHECK(hit->faceIndex == 0);
    UnloadImage(postMaterial.albedoImage);
}

void testSunSkyVisibilityThroughLowAlphaTransparentFace() {
    std::vector<LightmapFace> faces;
    faces.push_back(makeTestFace("grate", 2.0f, true, "mat/grate"));
    faces.push_back(makeTestFace("sky", 10.0f, false, "mat/sky"));
    const QuadBvh bvh = buildLightmapFaceBvh(faces);
    std::vector<char> faceSky(faces.size(), 0);
    faceSky[1] = 1;
    std::vector<char> faceTransparent(faces.size(), 0);
    faceTransparent[0] = 1;

    MaterialBakeInfo grateMaterial = makeAlphaMaterial(0);
    std::unordered_map<std::string, MaterialBakeInfo> cache;
    cache.emplace("mat/grate", grateMaterial);
    cache.emplace("mat/sky", makeSolidMaterial());

    SunShadowSoftnessParams sunParams;
    const float visibility = sunSkyVisibilityWithAlphaOcclusion(
        {0.0f, 0.0f, 0.0f},
        -1,
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        sunParams,
        bvh,
        faceSky,
        faceTransparent,
        faces,
        cache);
    CHECK(visibility > 0.99f);
    UnloadImage(grateMaterial.albedoImage);
}

void testSunSkyVisibilityBlockedByHighAlphaTransparentFace() {
    std::vector<LightmapFace> faces;
    faces.push_back(makeTestFace("post", 2.0f, true, "mat/post"));
    faces.push_back(makeTestFace("sky", 10.0f, false, "mat/sky"));
    const QuadBvh bvh = buildLightmapFaceBvh(faces);
    std::vector<char> faceSky(faces.size(), 0);
    faceSky[1] = 1;
    std::vector<char> faceTransparent(faces.size(), 0);
    faceTransparent[0] = 1;

    MaterialBakeInfo postMaterial = makeAlphaMaterial(255);
    std::unordered_map<std::string, MaterialBakeInfo> cache;
    cache.emplace("mat/post", postMaterial);
    cache.emplace("mat/sky", makeSolidMaterial());

    SunShadowSoftnessParams sunParams;
    const float visibility = sunSkyVisibilityWithAlphaOcclusion(
        {0.0f, 0.0f, 0.0f},
        -1,
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        sunParams,
        bvh,
        faceSky,
        faceTransparent,
        faces,
        cache);
    CHECK(visibility <= 0.01f);
    UnloadImage(postMaterial.albedoImage);
}

void testRadGpuAtlasMatchesSourceImage() {
    RadGpuOcclusionResources resources{};
    resources.valid = true;
    resources.atlasWidth = 2;
    resources.atlasHeight = 2;
    resources.alphaAtlasImage = GenImageColor(2, 2, BLANK);
    ImageDrawPixel(&resources.alphaAtlasImage, 0, 0, Color{0, 0, 0, 0});
    ImageDrawPixel(&resources.alphaAtlasImage, 1, 0, Color{0, 0, 0, 255});
    resources.materialRects.push_back({});
    resources.materialPaths.emplace_back();
    resources.materialRects.push_back(
        RadGpuMaterialRect{.textureWidth = 2.0f, .textureHeight = 1.0f, .yPixelOffset = 0});
    resources.materialPaths.push_back("mat/grate");

    MaterialBakeInfo material = makeAlphaMaterial(0);
    material.albedoImage = GenImageColor(2, 1, BLANK);
    ImageDrawPixel(&material.albedoImage, 0, 0, Color{0, 0, 0, 0});
    ImageDrawPixel(&material.albedoImage, 1, 0, Color{0, 0, 0, 255});

    std::unordered_map<std::string, MaterialBakeInfo> cache;
    cache.emplace("mat/grate", material);

    CHECK(verifyRadGpuOcclusionAtlas(resources, cache));
    CHECK(sampleRadGpuAtlasAlpha(resources, 1, 0.25f, 0.5f) < kLightOcclusionAlphaThreshold);
    CHECK(sampleRadGpuAtlasAlpha(resources, 1, 0.75f, 0.5f) >= kLightOcclusionAlphaThreshold);

    UnloadImage(material.albedoImage);
    UnloadImage(resources.alphaAtlasImage);
}

void testSegmentOcclusionTintsByGlassBaseColor() {
    std::vector<LightmapFace> faces;
    faces.push_back(makeTestFace("glass", 1.0f, true, "mat/glass"));
    const QuadBvh bvh = buildLightmapFaceBvh(faces);
    const std::vector<char> faceTransparent = {1};

    MaterialBakeInfo glassMaterial = makeSolidMaterial();
    glassMaterial.asset.baseColor = Color{100, 150, 200, 64};
    std::unordered_map<std::string, MaterialBakeInfo> cache;
    cache.emplace("mat/glass", glassMaterial);

    Vector3 tint{};
    const bool occluded = segmentOccludedWithAlphaOcclusion(
        bvh,
        {0.0f, 0.0f, 3.0f},
        {0.0f, 0.0f, -3.0f},
        -1,
        -1,
        faces,
        cache,
        faceTransparent,
        &tint);
    CHECK_FALSE(occluded);
    CHECK(std::fabs(tint.x - 100.0f / 255.0f) < 0.01f);
    CHECK(std::fabs(tint.y - 150.0f / 255.0f) < 0.01f);
    CHECK(std::fabs(tint.z - 200.0f / 255.0f) < 0.01f);
}

void testSunSkyVisibilityStraightThroughGlassWithoutBend() {
    std::vector<LightmapFace> faces;

    LightmapFace glass = makeTestFace("glass", 2.0f, true, "mat/glass");
    glass.normal = {0.0f, 0.0f, -1.0f};
    faces.push_back(glass);

    LightmapFace sky;
    sky.id = "sky";
    sky.material = "mat/sky";
    sky.normal = {0.0f, 0.0f, 1.0f};
    sky.vertices = {
        {4.0f, -1.0f, 10.0f},
        {5.0f, -1.0f, 10.0f},
        {5.0f, 1.0f, 10.0f},
        {4.0f, 1.0f, 10.0f},
    };
    faces.push_back(sky);

    const QuadBvh bvh = buildLightmapFaceBvh(faces);
    std::vector<char> faceSky(faces.size(), 0);
    faceSky[1] = 1;
    std::vector<char> faceTransparent(faces.size(), 0);
    faceTransparent[0] = 1;

    MaterialBakeInfo glassMaterial = makeSolidMaterial();
    glassMaterial.asset.baseColor = Color{100, 150, 200, 64};
    std::unordered_map<std::string, MaterialBakeInfo> cache;
    cache.emplace("mat/glass", glassMaterial);
    cache.emplace("mat/sky", makeSolidMaterial());

    SunShadowSoftnessParams sunParams;
    Vector3 tint{};
    const float visibility = sunSkyVisibilityWithAlphaOcclusion(
        {0.0f, 0.0f, 0.0f},
        -1,
        {0.9f, 0.0f, 2.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        sunParams,
        bvh,
        faceSky,
        faceTransparent,
        faces,
        cache,
        &tint);
    CHECK(visibility > 0.99f);
    CHECK(std::fabs(tint.x - 100.0f / 255.0f) < 0.01f);
    CHECK(std::fabs(tint.y - 150.0f / 255.0f) < 0.01f);
    CHECK(std::fabs(tint.z - 200.0f / 255.0f) < 0.01f);
}

void testSunSkyVisibilityBentAwayByGlassIor() {
    std::vector<LightmapFace> faces;

    LightmapFace glass = makeTestFace("glass", 2.0f, true, "mat/glass");
    glass.normal = {0.0f, 0.0f, -1.0f};
    faces.push_back(glass);

    LightmapFace sky;
    sky.id = "sky";
    sky.material = "mat/sky";
    sky.normal = {0.0f, 0.0f, 1.0f};
    sky.vertices = {
        {4.0f, -1.0f, 10.0f},
        {5.0f, -1.0f, 10.0f},
        {5.0f, 1.0f, 10.0f},
        {4.0f, 1.0f, 10.0f},
    };
    faces.push_back(sky);

    const QuadBvh bvh = buildLightmapFaceBvh(faces);
    std::vector<char> faceSky(faces.size(), 0);
    faceSky[1] = 1;
    std::vector<char> faceTransparent(faces.size(), 0);
    faceTransparent[0] = 1;

    MaterialBakeInfo glassMaterial = makeSolidMaterial();
    glassMaterial.asset.baseColor = Color{255, 255, 255, 64};
    glassMaterial.asset.ior = 1.5f;
    std::unordered_map<std::string, MaterialBakeInfo> cache;
    cache.emplace("mat/glass", glassMaterial);
    cache.emplace("mat/sky", makeSolidMaterial());

    SunShadowSoftnessParams sunParams;
    const float visibility = sunSkyVisibilityWithAlphaOcclusion(
        {0.0f, 0.0f, 0.0f},
        -1,
        {0.9f, 0.0f, 2.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        sunParams,
        bvh,
        faceSky,
        faceTransparent,
        faces,
        cache);
    CHECK(visibility <= 0.01f);
}

void testSunRefractionThroughThickGlassDoesNotDoubleBend() {
    std::vector<LightmapFace> faces;

    LightmapFace glassEntry = makeTestFace("glass-entry", 2.0f, true, "mat/glass");
    glassEntry.normal = {0.0f, 0.0f, -1.0f};
    faces.push_back(glassEntry);

    LightmapFace glassExit = makeTestFace("glass-exit", 2.05f, true, "mat/glass");
    glassExit.normal = {0.0f, 0.0f, 1.0f};
    faces.push_back(glassExit);

    LightmapFace sky;
    sky.id = "sky";
    sky.material = "mat/sky";
    sky.normal = {0.0f, 0.0f, 1.0f};
    sky.vertices = {
        {3.0f, -1.0f, 10.0f},
        {3.7f, -1.0f, 10.0f},
        {3.7f, 1.0f, 10.0f},
        {3.0f, 1.0f, 10.0f},
    };
    faces.push_back(sky);

    const QuadBvh bvh = buildLightmapFaceBvh(faces);
    std::vector<char> faceSky(faces.size(), 0);
    faceSky[2] = 1;
    std::vector<char> faceTransparent(faces.size(), 0);
    faceTransparent[0] = 1;
    faceTransparent[1] = 1;

    MaterialBakeInfo glassMaterial = makeSolidMaterial();
    glassMaterial.asset.baseColor = Color{255, 255, 255, 64};
    glassMaterial.asset.ior = 1.4f;
    std::unordered_map<std::string, MaterialBakeInfo> cache;
    cache.emplace("mat/glass", glassMaterial);
    cache.emplace("mat/sky", makeSolidMaterial());

    SunShadowSoftnessParams sunParams;
    const float visibility = sunSkyVisibilityWithAlphaOcclusion(
        {0.0f, 0.0f, 0.0f},
        -1,
        {0.9f, 0.0f, 2.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        sunParams,
        bvh,
        faceSky,
        faceTransparent,
        faces,
        cache);
    CHECK(visibility > 0.99f);
}

void testBuildRadGpuOcclusionResourcesPopulatesTintAndIor() {
    std::vector<LightmapFace> faces;
    faces.push_back(makeTestFace("glass", 1.0f, true, "mat/glass"));
    std::vector<char> faceTransparent = {1};

    MaterialBakeInfo glassMaterial = makeSolidMaterial();
    glassMaterial.asset.baseColor = Color{100, 150, 200, 64};
    glassMaterial.asset.ior = 1.5f;
    std::unordered_map<std::string, MaterialBakeInfo> cache;
    cache.emplace("mat/glass", glassMaterial);

    RadGpuOcclusionResources resources{};
    buildRadGpuOcclusionResources(faces, cache, faceTransparent, resources);
    CHECK_EQ(resources.faceOcclusion.size(), std::size_t{1});
    const RadGpuFaceOcclusion& gpuFace = resources.faceOcclusion[0];
    CHECK(std::fabs(gpuFace.baseColorR - 100.0f / 255.0f) < 0.01f);
    CHECK(std::fabs(gpuFace.baseColorG - 150.0f / 255.0f) < 0.01f);
    CHECK(std::fabs(gpuFace.baseColorB - 200.0f / 255.0f) < 0.01f);
    CHECK(std::fabs(gpuFace.ior - 1.5f) < 0.001f);
    unloadRadGpuOcclusionResources(resources);
}

void testAxialUvAxesCrossProductRecoversFaceNormal() {
    const Vector3 normals[6] = {
        {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f},
    };
    for (const Vector3& normal : normals) {
        Vector3 uAxis{};
        Vector3 vAxis{};
        faceUvAxes(false, normal, {}, {}, uAxis, vAxis);
        const Vector3 cross{
            vAxis.y * uAxis.z - vAxis.z * uAxis.y,
            vAxis.z * uAxis.x - vAxis.x * uAxis.z,
            vAxis.x * uAxis.y - vAxis.y * uAxis.x,
        };
        const float len = std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
        CHECK(len > 1e-6f);
        const Vector3 recovered{cross.x / len, cross.y / len, cross.z / len};
        const float dot = recovered.x * normal.x + recovered.y * normal.y + recovered.z * normal.z;
        CHECK(dot > 0.99f);
    }
}

} // namespace

void runLightmapTransparentTests() {
    testCollectLightmapFacesPreservesTransparent();
    testCollectLightmapFacesFromTransparentBrush();
    testSampleFaceOcclusionAlphaUsesTextureAlpha();
    testSolidTransparentFaceBlocksSegment();
    testLowAlphaTransparentFaceTransmitsToOpaqueBehind();
    testHighAlphaTransparentFaceBlocksBeforeOpaqueBehind();
    testSunSkyVisibilityThroughLowAlphaTransparentFace();
    testSunSkyVisibilityBlockedByHighAlphaTransparentFace();
    testRadGpuAtlasMatchesSourceImage();
    testSegmentOcclusionTintsByGlassBaseColor();
    testSunSkyVisibilityStraightThroughGlassWithoutBend();
    testSunSkyVisibilityBentAwayByGlassIor();
    testSunRefractionThroughThickGlassDoesNotDoubleBend();
    testBuildRadGpuOcclusionResourcesPopulatesTintAndIor();
    testAxialUvAxesCrossProductRecoversFaceNormal();
}

} // namespace slopengine
