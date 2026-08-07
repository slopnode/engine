#include "map/lightmap.hpp"
#include "test_assert.hpp"

#include <vector>

namespace slopengine {

namespace {

LightmapFace makeLightmapFace(
    std::string id,
    Vector3 normal,
    std::vector<Vector3> verts,
    const std::string& material = "mat/a") {
    LightmapFace face;
    face.id = std::move(id);
    face.material = material;
    face.normal = normal;
    face.vertices = std::move(verts);
    return face;
}

} // namespace

void runLightmapMergeTests() {
    {
        LightmapFace a = makeLightmapFace(
            "a",
            {0.0f, 1.0f, 0.0f},
            {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        LightmapFace b = makeLightmapFace(
            "b",
            {0.0f, 1.0f, 0.0f},
            {{1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}});
        const std::vector<LightmapFace> faces{a, b};
        const std::vector<LightmapFaceGroup> groups = groupCoplanarLightmapFaces(faces);
        CHECK_EQ(groups.size(), 1u);
        CHECK_EQ(groups[0].faceIndices.size(), 2u);
    }

    {
        LightmapFace a = makeLightmapFace(
            "a",
            {0.0f, 1.0f, 0.0f},
            {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            "mat/a");
        LightmapFace b = makeLightmapFace(
            "b",
            {0.0f, 1.0f, 0.0f},
            {{1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}},
            "mat/b");
        const std::vector<LightmapFace> faces{a, b};
        const std::vector<LightmapFaceGroup> groups = groupCoplanarLightmapFaces(faces);
        CHECK_EQ(groups.size(), 2u);
    }

    {
        LightmapFace a = makeLightmapFace(
            "a",
            {0.0f, 1.0f, 0.0f},
            {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        LightmapFace b = makeLightmapFace(
            "b",
            {0.0f, 1.0f, 0.0f},
            {{1.0f, 3.0f, 0.0f}, {2.0f, 3.0f, 0.0f}, {2.0f, 3.0f, 1.0f}, {1.0f, 3.0f, 1.0f}});
        const std::vector<LightmapFace> faces{a, b};
        const std::vector<LightmapFaceGroup> groups = groupCoplanarLightmapFaces(faces);
        CHECK_EQ(groups.size(), 2u);
    }

    {
        LightmapFace big = makeLightmapFace(
            "big",
            {0.0f, 1.0f, 0.0f},
            {{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        LightmapFace left = makeLightmapFace(
            "left",
            {0.0f, 1.0f, 0.0f},
            {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 2.0f}, {0.0f, 0.0f, 2.0f}});
        LightmapFace right = makeLightmapFace(
            "right",
            {0.0f, 1.0f, 0.0f},
            {{1.0f, 0.0f, 1.0f}, {2.0f, 0.0f, 1.0f}, {2.0f, 0.0f, 2.0f}, {1.0f, 0.0f, 2.0f}});
        const std::vector<LightmapFace> faces{big, left, right};
        const std::vector<LightmapFaceGroup> groups = groupCoplanarLightmapFaces(faces);
        CHECK_EQ(groups.size(), 1u);
        CHECK_EQ(groups[0].faceIndices.size(), 3u);
    }

    {
        LightmapFace a = makeLightmapFace(
            "a",
            {0.0f, 1.0f, 0.0f},
            {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        LightmapFace b = makeLightmapFace(
            "b",
            {0.0f, 1.0f, 0.0f},
            {{5.0f, 0.0f, 5.0f}, {6.0f, 0.0f, 5.0f}, {6.0f, 0.0f, 6.0f}, {5.0f, 0.0f, 6.0f}});
        const std::vector<LightmapFace> faces{a, b};
        const std::vector<LightmapFaceGroup> groups = groupCoplanarLightmapFaces(faces);
        CHECK_EQ(groups.size(), 2u);
        CHECK_EQ(groups[0].faceIndices.size(), 1u);
        CHECK_EQ(groups[1].faceIndices.size(), 1u);
    }
}

} // namespace slopengine
