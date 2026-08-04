#include "test_assert.hpp"

#include "map/brush.hpp"
#include "map/fac.hpp"
#include "map/lightmap.hpp"
#include "map/quad_bvh.hpp"

#include <vector>

namespace slopengine {

namespace {

LightmapFace makeTestFace(
    std::string id,
    float z,
    bool transparent) {
    LightmapFace face;
    face.id = std::move(id);
    face.material = "mat/test";
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

void testTransparentFacesDoNotOccludeSegments() {
    std::vector<LightmapFace> faces;
    faces.push_back(makeTestFace("glass", 1.0f, true));
    const QuadBvh bvh = buildLightmapFaceBvh(faces);

    const std::vector<char> skipFaces = {1};
    const bool occluded = quadSegmentOccluded(
        bvh,
        {0.0f, 0.0f, 3.0f},
        {0.0f, 0.0f, -3.0f},
        -1,
        -1,
        &skipFaces);
    CHECK_FALSE(occluded);
}

void testRaycastSkipsTransparentToHitOpaqueBehind() {
    std::vector<LightmapFace> faces;
    faces.push_back(makeTestFace("glass", 1.0f, true));
    faces.push_back(makeTestFace("wall", 0.0f, false));
    const QuadBvh bvh = buildLightmapFaceBvh(faces);

    const std::vector<char> skipFaces = {1, 0};
    const auto hit = raycastQuadBvh(
        bvh,
        {0.0f, 0.0f, 2.0f},
        {0.0f, 0.0f, -1.0f},
        10.0f,
        -1,
        &skipFaces);
    CHECK(hit.has_value());
    CHECK(hit->faceIndex == 1);
}

} // namespace

void runLightmapTransparentTests() {
    testCollectLightmapFacesPreservesTransparent();
    testCollectLightmapFacesFromTransparentBrush();
    testTransparentFacesDoNotOccludeSegments();
    testRaycastSkipsTransparentToHitOpaqueBehind();
}

} // namespace slopengine
