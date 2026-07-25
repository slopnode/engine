#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/brush.hpp"
#include "map/fac.hpp"
#include "map/mover_brushes.hpp"
#include "map/thing.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace slopengine {

namespace {

VisibleFace makeFace(
    std::string id,
    Vector3 normal,
    std::vector<Vector3> verts,
    const std::string& material = "mat/a") {
    VisibleFace face;
    face.id = id;
    face.sourceFaceId = id;
    face.material = material;
    face.normal = normal;
    face.vertices = std::move(verts);
    face.uvUAxis = {1.0f, 0.0f, 0.0f};
    face.uvVAxis = {0.0f, 0.0f, 1.0f};
    return face;
}

bool idContainsHashOrMerge(const std::string& id) {
    return id.find('#') != std::string::npos || id.rfind("merge/", 0) == 0;
}

bool nearPoint(Vector3 a, Vector3 b, float eps = 1e-3f) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz <= eps * eps;
}

bool faceHasVertex(const VisibleFace& face, Vector3 p, float eps = 1e-3f) {
    for (const Vector3& v : face.vertices) {
        if (nearPoint(v, p, eps)) {
            return true;
        }
    }
    return false;
}

float polygonArea3(const std::vector<Vector3>& verts) {
    if (verts.size() < 3) {
        return 0.0f;
    }
    Vector3 accum{};
    for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
        const Vector3 ab{
            verts[i].x - verts[0].x,
            verts[i].y - verts[0].y,
            verts[i].z - verts[0].z,
        };
        const Vector3 ac{
            verts[i + 1].x - verts[0].x,
            verts[i + 1].y - verts[0].y,
            verts[i + 1].z - verts[0].z,
        };
        accum.x += ab.y * ac.z - ab.z * ac.y;
        accum.y += ab.z * ac.x - ab.x * ac.z;
        accum.z += ab.x * ac.y - ab.y * ac.x;
    }
    return 0.5f * std::sqrt(accum.x * accum.x + accum.y * accum.y + accum.z * accum.z);
}

float sourceFaceArea(const FacFile& fac, std::string_view sourceId) {
    float area = 0.0f;
    for (const VisibleFace& face : fac.faces) {
        if (face.sourceFaceId == sourceId) {
            area += polygonArea3(face.vertices);
        }
    }
    return area;
}

bool pointInPolygon3(
    Vector3 point,
    const std::vector<Vector3>& verts,
    Vector3 normal,
    float eps = 1e-4f) {
    if (verts.size() < 3) {
        return false;
    }
    const float nlen = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (nlen <= 1e-8f) {
        return false;
    }
    const Vector3 n{normal.x / nlen, normal.y / nlen, normal.z / nlen};
    const float planeD = n.x * verts[0].x + n.y * verts[0].y + n.z * verts[0].z;
    const float dist = n.x * point.x + n.y * point.y + n.z * point.z - planeD;
    if (std::fabs(dist) > 1e-3f) {
        return false;
    }

    Vector3 axisU{};
    if (std::fabs(n.y) < 0.9f) {
        axisU = {n.z, 0.0f, -n.x};
    } else {
        axisU = {0.0f, -n.z, n.y};
    }
    const float ulen = std::sqrt(axisU.x * axisU.x + axisU.y * axisU.y + axisU.z * axisU.z);
    axisU = {axisU.x / ulen, axisU.y / ulen, axisU.z / ulen};
    const Vector3 axisV{
        n.y * axisU.z - n.z * axisU.y,
        n.z * axisU.x - n.x * axisU.z,
        n.x * axisU.y - n.y * axisU.x,
    };

    auto project = [&](Vector3 p) {
        return Vector2{
            (p.x - verts[0].x) * axisU.x + (p.y - verts[0].y) * axisU.y + (p.z - verts[0].z) * axisU.z,
            (p.x - verts[0].x) * axisV.x + (p.y - verts[0].y) * axisV.y + (p.z - verts[0].z) * axisV.z,
        };
    };

    const Vector2 p2 = project(point);
    bool inside = false;
    for (std::size_t i = 0, j = verts.size() - 1; i < verts.size(); j = i++) {
        const Vector2 a = project(verts[i]);
        const Vector2 b = project(verts[j]);
        const bool intersect = ((a.y > p2.y) != (b.y > p2.y))
            && (p2.x < (b.x - a.x) * (p2.y - a.y) / ((b.y - a.y) + eps) + a.x);
        if (intersect) {
            inside = !inside;
        }
    }
    return inside;
}

} // namespace

void runFacBuildTests() {
    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);
        const FacBuildResult result = buildVisibleFaces(tree, analysis, brushes);
        CHECK_FALSE(result.fac.faces.empty());
        CHECK_FALSE(result.inferredNodrawFaceIds.empty());
        for (const VisibleFace& face : result.fac.faces) {
            CHECK(idContainsHashOrMerge(face.id));
        }
    }

    {
        std::vector<Brush> brushes = mapfixtures::sealedRoomWithWindow();
        for (Brush& brush : brushes) {
            if (brush.id != "window-east") {
                continue;
            }
            for (BrushFace& face : brush.faces) {
                face.nodraw = true;
            }
        }
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);
        const FacBuildResult result = buildVisibleFaces(tree, analysis, brushes);

        // Window seals but emits no VIS. Opening center must stay hollow (not filled by merge).
        const Vector3 openingCenter{2.0f, 1.25f, 0.0f};
        const Vector3 eastInward{-1.0f, 0.0f, 0.0f};
        bool openingCovered = false;
        for (const VisibleFace& face : result.fac.faces) {
            if (pointInPolygon3(openingCenter, face.vertices, eastInward)) {
                openingCovered = true;
                break;
            }
        }
        CHECK_FALSE(openingCovered);
    }

    {
        auto checkDoorwaySides = [](const std::vector<Brush>& brushes) {
            const BspTree tree = buildBspFromHullBrushes(brushes);
            const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
            CHECK(analysis.sealed);
            const FacBuildResult result = buildVisibleFaces(tree, analysis, brushes);

            const Vector3 openingCenter{0.0f, 1.1f, 0.0f};
            const Vector3 northInward{0.0f, 0.0f, 1.0f};
            const Vector3 southInward{0.0f, 0.0f, -1.0f};
            bool openingCovered = false;
            for (const VisibleFace& face : result.fac.faces) {
                if (pointInPolygon3(openingCenter, face.vertices, northInward)
                    || pointInPolygon3(openingCenter, face.vertices, southInward)) {
                    openingCovered = true;
                    break;
                }
            }
            CHECK_FALSE(openingCovered);

            const Vector3 floorUp{0.0f, 1.0f, 0.0f};
            const Vector3 ceilDown{0.0f, -1.0f, 0.0f};
            const Vector3 wallEInward{-1.0f, 0.0f, 0.0f};
            const Vector3 floorSouth{0.0f, 0.0f, 3.0f};
            const Vector3 floorNorth{0.0f, 0.0f, -3.0f};
            const Vector3 ceilSouth{0.0f, 4.0f, 3.0f};
            const Vector3 ceilNorth{0.0f, 4.0f, -3.0f};
            const Vector3 wallESouth{4.0f, 1.0f, 3.0f};
            const Vector3 wallENorth{4.0f, 1.0f, -3.0f};

            auto covered = [&](Vector3 point, Vector3 normal) {
                for (const VisibleFace& face : result.fac.faces) {
                    if (pointInPolygon3(point, face.vertices, normal)) {
                        return true;
                    }
                }
                return false;
            };

            CHECK(covered(floorSouth, floorUp));
            CHECK(covered(floorNorth, floorUp));
            CHECK(covered(ceilSouth, ceilDown));
            CHECK(covered(ceilNorth, ceilDown));
            CHECK(covered(wallESouth, wallEInward));
            CHECK(covered(wallENorth, wallEInward));
        };

        checkDoorwaySides(mapfixtures::sealedRoomWithInteriorDoorway(BrushRole::Hull));
        checkDoorwaySides(mapfixtures::sealedRoomWithInteriorDoorway(BrushRole::Detail));
    }

    {
        std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        std::vector<Brush> stairs = makeBrushStairs(
            "stairs",
            {-1.5f, 0.0f, -1.5f},
            {-0.5f, 1.0f, -0.5f},
            4,
            "mat/a",
            BrushRole::Detail);
        brushes.insert(brushes.end(), stairs.begin(), stairs.end());
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);
        const FacBuildResult result = buildVisibleFaces(tree, analysis, brushes);

        int stairFragments = 0;
        for (const VisibleFace& face : result.fac.faces) {
            if (face.sourceFaceId.find("stairs") != std::string::npos
                || face.id.find("stairs") != std::string::npos) {
                ++stairFragments;
            }
        }
        CHECK(stairFragments >= 4);
    }

    {
        VisibleFace left = makeFace(
            "left",
            {0.0f, 1.0f, 0.0f},
            {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 1.0f}});
        VisibleFace right = makeFace(
            "right",
            {0.0f, 1.0f, 0.0f},
            {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        // Mid-edge T-junction vertex on left's shared edge, belonging to right only.
        right.vertices.insert(
            right.vertices.begin() + 1,
            {0.0f, 0.0f, 0.5f});

        std::vector<VisibleFace> faces{left, right};
        weldVisibleFaceTJunctions(faces);
        CHECK(faceHasVertex(faces[0], {0.0f, 0.0f, 0.5f}));
    }

    {
        // Two quads sharing an edge; shared endpoints are not colinear with outer corners
        // after merge (L / stair-side silhouette). Merge must keep the concave vertex.
        VisibleFace a = makeFace(
            "a",
            {1.0f, 0.0f, 0.0f},
            {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        VisibleFace b = makeFace(
            "b",
            {1.0f, 0.0f, 0.0f},
            {{0.0f, 1.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, 2.0f, 0.5f}, {0.0f, 1.0f, 1.0f}});
        std::vector<VisibleFace> faces{a, b};
        mergeCoplanarVisibleFaces(faces);
        CHECK_EQ(faces.size(), 1u);
        // Shared-edge endpoint that is not colinear with the merged outline must remain.
        CHECK(faceHasVertex(faces[0], {0.0f, 1.0f, 1.0f}));
    }

    {
        // Needle sliver between two nearly coincident verts on a sealed room wall via crafted faces
        // after weld/cull path: build a face that is degenerate enough to be culled when passed
        // through weld + merge helpers used by the pipeline.
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        Brush needle = makeBrushBox(
            "needle",
            {-0.5f, 1.0f, 0.0f},
            {0.5f, 1.00005f, 0.00005f},
            "mat/a",
            {},
            BrushRole::Detail);
        std::vector<Brush> withNeedle = brushes;
        withNeedle.push_back(needle);
        const FacBuildResult result = buildVisibleFaces(tree, analysis, withNeedle);

        bool anyNeedleVisible = false;
        for (const VisibleFace& face : result.fac.faces) {
            if (face.sourceFaceId.find("needle") != std::string::npos
                || face.id.find("needle") != std::string::npos) {
                anyNeedleVisible = true;
                break;
            }
        }
        const bool needleInferred = std::any_of(
            result.inferredNodrawFaceIds.begin(),
            result.inferredNodrawFaceIds.end(),
            [](const std::string& id) { return id.find("needle") != std::string::npos; });
        CHECK_FALSE(anyNeedleVisible);
        CHECK(needleInferred);
    }

    {
        const std::vector<Brush> brushes = mapfixtures::leakyHollowRoom();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        MapHullAnalysis analysis{};
        analysis.sealed = false;
        const FacBuildResult result = buildVisibleFaces(tree, analysis, brushes);
        CHECK_FALSE(result.fac.faces.empty());
        // Unsealed path skips interior clip, so authored faces are not discarded as buried.
        CHECK(result.inferredNodrawFaceIds.empty() || result.fac.faces.size() >= 6u);
        for (const VisibleFace& face : result.fac.faces) {
            CHECK_FALSE(face.id.empty());
            CHECK_FALSE(face.sourceFaceId.empty());
        }
    }

    {
        std::vector<Brush> brushes =
            mapfixtures::sealedRoomWithInteriorDoorway(BrushRole::Detail);
        brushes.push_back(makeBrushBox(
            "door-1-leaf",
            {-1.0f, 0.0f, -0.06f},
            {1.0f, 2.2f, 0.06f},
            "mat/a",
            {},
            BrushRole::Detail));

        ThingDocument doc{};
        Thing mover{};
        mover.kind = ThingKind::Mover;
        mover.id = "door-1";
        mover.brush = "door-1-leaf";
        mover.haveMoverOpenOffset = true;
        mover.moverOpenOffset = {0.0f, 2.3f, 0.0f};
        doc.things.push_back(mover);

        const std::unordered_set<std::string> claimed = collectMoverBrushIds(doc);
        CHECK(claimed.count("door-1-leaf") == 1);

        const BspTree tree = buildBspFromHullBrushes(brushes);
        MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const FacBuildResult withDoor = buildVisibleFaces(tree, analysis, brushes);
        bool doorVisible = false;
        for (const VisibleFace& face : withDoor.fac.faces) {
            if (faceIdBelongsToBrush(face.sourceFaceId, "door-1-leaf") ||
                faceIdBelongsToBrush(face.id, "door-1-leaf")) {
                doorVisible = true;
                break;
            }
        }
        CHECK(doorVisible);

        const FacBuildResult omitted = buildVisibleFaces(tree, analysis, brushes, &claimed);
        for (const VisibleFace& face : omitted.fac.faces) {
            CHECK_FALSE(faceIdBelongsToBrush(face.sourceFaceId, "door-1-leaf"));
            CHECK_FALSE(faceIdBelongsToBrush(face.id, "door-1-leaf"));
        }

        const float jambEastWith = sourceFaceArea(withDoor.fac, "partition-w/east");
        const float jambWestWith = sourceFaceArea(withDoor.fac, "partition-e/west");
        const float jambEastOmit = sourceFaceArea(omitted.fac, "partition-w/east");
        const float jambWestOmit = sourceFaceArea(omitted.fac, "partition-e/west");
        CHECK(jambEastOmit > jambEastWith + 0.05f);
        CHECK(jambWestOmit > jambWestWith + 0.05f);

        FacFile stale = withDoor.fac;
        eraseFacFacesForMoverBrushes(stale, claimed);
        for (const VisibleFace& face : stale.faces) {
            CHECK_FALSE(faceIdBelongsToBrush(face.sourceFaceId, "door-1-leaf"));
            CHECK_FALSE(faceIdBelongsToBrush(face.id, "door-1-leaf"));
        }

        CHECK(findBrushById(brushes, "door-1-leaf") != nullptr);
        CHECK(faceIdBelongsToBrush("door-1-leaf/north#0", "door-1-leaf"));
        CHECK(faceIdBelongsToBrush("merge/0/door-1-leaf/north+partition-w/east", "door-1-leaf"));
        CHECK_FALSE(faceIdBelongsToBrush("partition-w/east", "door-1-leaf"));
    }
}

}
