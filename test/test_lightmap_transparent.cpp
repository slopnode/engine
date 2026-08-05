#include "test_assert.hpp"

#include "map/brush.hpp"
#include "map/fac.hpp"
#include "map/light_occlusion.hpp"
#include "map/lightmap.hpp"
#include "map/quad_bvh.hpp"
#include "map/radiosity.hpp"

#include <raylib.h>

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
    FacFile fac;
    VisibleFace opaque;
    opaque.id = "opaque";
    opaque.material = "mat/a";
    opaque.normal = {0.0f, 1.0f, 0.0f};
    opaque.vertices = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 1.0f},
    };
    opaque.transparent = false;

    VisibleFace glass;
    glass.id = "glass";
    glass.material = "mat/glass";
    glass.normal = {0.0f, 1.0f, 0.0f};
    glass.vertices = opaque.vertices;
    glass.transparent = true;

    fac.faces.push_back(std::move(opaque));
    fac.faces.push_back(std::move(glass));

    const std::vector<LightmapFace> faces = collectLightmapFaces(fac);
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
}

} // namespace slopengine
