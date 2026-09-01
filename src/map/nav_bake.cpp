#include "map/nav_bake.hpp"

#include "map/door_portal_tag.hpp"

#include <raylib.h>
#include <raymath.h>

#include <Recast.h>

#include <cmath>
#include <cstring>

namespace slopengine {

namespace {

// Recast/Sample_SoloMesh's own reference defaults, expressed in world units
// before being converted to voxels below.
constexpr float kEdgeMaxLenWorld = 12.0f;
constexpr float kEdgeMaxError = 1.3f;
constexpr float kRegionMinSize = 8.0f;
constexpr float kRegionMergeSize = 20.0f;
constexpr int kMaxVertsPerPoly = 6;
constexpr float kDetailSampleDistFactor = 6.0f;
constexpr float kDetailSampleMaxErrorFactor = 1.0f;

bool brushIsWalkableObstacle(const Brush& brush) {
    if (brush.role == BrushRole::Trigger || brush.role == BrushRole::Door) {
        return false;
    }
    return (brush.blocks & (BrushBlock::Player | BrushBlock::Actor)) != 0;
}

struct TriangleSoup {
    std::vector<float> verts; // (ax, ay, az, bx, by, bz, cx, cy, cz) * numTris
    int numTris = 0;
};

TriangleSoup gatherWalkableTriangles(const std::vector<Brush>& brushes) {
    TriangleSoup soup;
    for (const Brush& brush : brushes) {
        if (!brushIsWalkableObstacle(brush)) {
            continue;
        }
        for (const BrushFace& face : brush.faces) {
            for (const std::array<Vector3, 3>& tri : triangulateFace(face.vertices)) {
                for (const Vector3& v : tri) {
                    soup.verts.push_back(v.x);
                    soup.verts.push_back(v.y);
                    soup.verts.push_back(v.z);
                }
                ++soup.numTris;
            }
        }
    }
    return soup;
}

// Mirrors rcMarkWalkableTriangles' own slope test, done here per-triangle since our
// triangles are a flat soup rather than an indexed mesh. A triangle that passes the
// slope test is further required to border real open interior space (the same
// nudge-and-classify test bsp_build.cpp's buildSurfaceFaces uses) before being
// admitted as walkable -- otherwise a sealed level's own exterior shell (e.g. the
// outward-facing top of a ceiling brush, bordering nothing but void) gets rasterized
// as a "floor" with unbounded headroom above it, since nothing exists in the voxel
// grid to cap it. Non-upward-facing triangles were never walkable candidates, so
// they're untouched -- this only filters the specific failure mode.
//
// The BSP tree used for the openness probe is compiled from every brush, Door included
// -- a closed door is real solid geometry as far as the static hull is concerned. That
// makes the probe reject a door's own threshold floor outright (the nudge lands inside
// the door's compiled-solid volume), punching a permanent hole in the navmesh at every
// doorway regardless of the door's live open/closed state -- gatherWalkableTriangles
// already excludes the door from the obstacle soup for the same reason. A probe that
// lands inside a Door brush's own padded footprint is treated as open here too, same
// as the rest of the corridor; the door's actual open/closed gating still happens at
// the graph level via doorBrushIdAtPoint (see buildMapNavigationFromPolyMesh).
std::vector<unsigned char> classifyTriangleAreas(
    const TriangleSoup& soup,
    const BspTree& tree,
    const std::vector<Brush>& brushes,
    const std::vector<std::uint8_t>* exteriorEmpty,
    float walkableSlopeDegrees) {
    std::vector<unsigned char> areas(static_cast<std::size_t>(soup.numTris), RC_NULL_AREA);
    const float walkableThr = std::cos(walkableSlopeDegrees / 180.0f * static_cast<float>(RC_PI));
    constexpr float kInteriorProbeNudge = 0.05f;
    for (int i = 0; i < soup.numTris; ++i) {
        const float* v0 = &soup.verts[static_cast<std::size_t>(i) * 9 + 0];
        const float* v1 = &soup.verts[static_cast<std::size_t>(i) * 9 + 3];
        const float* v2 = &soup.verts[static_cast<std::size_t>(i) * 9 + 6];
        const Vector3 p0{v0[0], v0[1], v0[2]};
        const Vector3 p1{v1[0], v1[1], v1[2]};
        const Vector3 p2{v2[0], v2[1], v2[2]};
        const Vector3 e0 = Vector3Subtract(p1, p0);
        const Vector3 e1 = Vector3Subtract(p2, p0);
        const Vector3 n = Vector3Normalize(Vector3CrossProduct(e0, e1));
        if (n.y <= walkableThr) {
            continue;
        }
        const Vector3 centroid = Vector3Scale(Vector3Add(Vector3Add(p0, p1), p2), 1.0f / 3.0f);
        const Vector3 probe = Vector3Add(centroid, Vector3Scale(n, kInteriorProbeNudge));
        if (!doorBrushIdAtPoint(probe, brushes).empty()) {
            areas[static_cast<std::size_t>(i)] = RC_WALKABLE_AREA;
            continue;
        }
        const std::int32_t leaf = pointLeaf(tree, probe);
        if (leaf < 0 || !leafIsOpen(tree.leaves[static_cast<std::size_t>(leaf)].contents)) {
            continue;
        }
        if (exteriorEmpty != nullptr &&
            static_cast<std::size_t>(leaf) < exteriorEmpty->size() &&
            (*exteriorEmpty)[static_cast<std::size_t>(leaf)] != 0) {
            continue;
        }
        areas[static_cast<std::size_t>(i)] = RC_WALKABLE_AREA;
    }
    return areas;
}

NavPolyMesh extractNavPolyMesh(const rcPolyMesh& pmesh, const rcPolyMeshDetail& dmesh) {
    NavPolyMesh out;
    out.polys.resize(static_cast<std::size_t>(pmesh.npolys));
    out.polyDetail.resize(static_cast<std::size_t>(pmesh.npolys));

    auto vertAt = [&](unsigned short idx) -> Vector3 {
        const unsigned short* v = &pmesh.verts[static_cast<std::size_t>(idx) * 3];
        return {
            pmesh.bmin[0] + static_cast<float>(v[0]) * pmesh.cs,
            pmesh.bmin[1] + static_cast<float>(v[1]) * pmesh.ch,
            pmesh.bmin[2] + static_cast<float>(v[2]) * pmesh.cs,
        };
    };

    for (int i = 0; i < pmesh.npolys; ++i) {
        NavBakePoly& poly = out.polys[static_cast<std::size_t>(i)];
        const unsigned short* p = &pmesh.polys[static_cast<std::size_t>(i) * 2 * static_cast<std::size_t>(pmesh.nvp)];
        for (int j = 0; j < pmesh.nvp; ++j) {
            if (p[j] == RC_MESH_NULL_IDX) {
                break;
            }
            poly.vertices.push_back(vertAt(p[j]));
        }
        const int vertCount = static_cast<int>(poly.vertices.size());
        poly.neighbors.assign(static_cast<std::size_t>(vertCount), -1);
        for (int j = 0; j < vertCount; ++j) {
            const unsigned short nb = p[pmesh.nvp + j];
            poly.neighbors[static_cast<std::size_t>(j)] = (nb == RC_MESH_NULL_IDX) ? -1 : static_cast<int>(nb);
        }
    }

    for (int i = 0; i < dmesh.nmeshes; ++i) {
        const unsigned int* m = &dmesh.meshes[static_cast<std::size_t>(i) * 4];
        const unsigned int vertBase = m[0];
        const unsigned int triBase = m[2];
        const unsigned int triCount = m[3];
        NavBakePolyDetail& detail = out.polyDetail[static_cast<std::size_t>(i)];
        detail.triangles.reserve(triCount);
        for (unsigned int t = 0; t < triCount; ++t) {
            const unsigned char* tri = &dmesh.tris[(static_cast<std::size_t>(triBase) + t) * 4];
            std::array<Vector3, 3> triangle{};
            for (int k = 0; k < 3; ++k) {
                const float* v = &dmesh.verts[(static_cast<std::size_t>(vertBase) + tri[k]) * 3];
                triangle[static_cast<std::size_t>(k)] = {v[0], v[1], v[2]};
            }
            detail.triangles.push_back(triangle);
        }
    }

    return out;
}

}

std::optional<NavPolyMesh> buildNavPolyMesh(
    const BspTree& tree,
    const std::vector<Brush>& brushes,
    const std::vector<std::uint8_t>* exteriorEmpty,
    const NavBakeParams& params) {
    const TriangleSoup soup = gatherWalkableTriangles(brushes);
    if (soup.numTris == 0) {
        return std::nullopt;
    }

    float bmin[3];
    float bmax[3];
    rcCalcBounds(soup.verts.data(), soup.numTris * 3, bmin, bmax);

    rcConfig cfg{};
    cfg.cs = params.cellSize;
    cfg.ch = params.cellHeight;
    std::memcpy(cfg.bmin, bmin, sizeof(bmin));
    std::memcpy(cfg.bmax, bmax, sizeof(bmax));
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
    cfg.walkableSlopeAngle = params.agentMaxSlopeDegrees;
    cfg.walkableHeight = static_cast<int>(std::ceil(params.agentHeight / cfg.ch));
    cfg.walkableClimb = static_cast<int>(std::floor(params.agentMaxClimb / cfg.ch));
    cfg.walkableRadius = static_cast<int>(std::ceil(params.agentRadius / cfg.cs));
    cfg.maxEdgeLen = static_cast<int>(kEdgeMaxLenWorld / cfg.cs);
    cfg.maxSimplificationError = kEdgeMaxError;
    cfg.minRegionArea = static_cast<int>(kRegionMinSize * kRegionMinSize);
    cfg.mergeRegionArea = static_cast<int>(kRegionMergeSize * kRegionMergeSize);
    cfg.maxVertsPerPoly = kMaxVertsPerPoly;
    cfg.detailSampleDist = kDetailSampleDistFactor < 0.9f ? 0.0f : cfg.cs * kDetailSampleDistFactor;
    cfg.detailSampleMaxError = cfg.ch * kDetailSampleMaxErrorFactor;
    cfg.borderSize = 0;

    rcContext ctx(false);

    rcHeightfield solid;
    if (!rcCreateHeightfield(&ctx, solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        TraceLog(LOG_WARNING, "NAVBAKE: rcCreateHeightfield failed");
        return std::nullopt;
    }

    std::vector<unsigned char> areas =
        classifyTriangleAreas(soup, tree, brushes, exteriorEmpty, cfg.walkableSlopeAngle);

    if (!rcRasterizeTriangles(&ctx, soup.verts.data(), areas.data(), soup.numTris, solid, cfg.walkableClimb)) {
        TraceLog(LOG_WARNING, "NAVBAKE: rcRasterizeTriangles failed");
        return std::nullopt;
    }

    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, solid);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, solid);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, solid);

    rcCompactHeightfield chf;
    if (!rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, solid, chf)) {
        TraceLog(LOG_WARNING, "NAVBAKE: rcBuildCompactHeightfield failed");
        return std::nullopt;
    }

    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, chf)) {
        TraceLog(LOG_WARNING, "NAVBAKE: rcErodeWalkableArea failed");
        return std::nullopt;
    }

    if (!rcBuildDistanceField(&ctx, chf)) {
        TraceLog(LOG_WARNING, "NAVBAKE: rcBuildDistanceField failed");
        return std::nullopt;
    }

    if (!rcBuildRegions(&ctx, chf, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea)) {
        TraceLog(LOG_WARNING, "NAVBAKE: rcBuildRegions failed");
        return std::nullopt;
    }

    rcContourSet cset;
    if (!rcBuildContours(&ctx, chf, cfg.maxSimplificationError, cfg.maxEdgeLen, cset)) {
        TraceLog(LOG_WARNING, "NAVBAKE: rcBuildContours failed");
        return std::nullopt;
    }
    if (cset.nconts == 0) {
        return std::nullopt;
    }

    rcPolyMesh pmesh;
    if (!rcBuildPolyMesh(&ctx, cset, cfg.maxVertsPerPoly, pmesh)) {
        TraceLog(LOG_WARNING, "NAVBAKE: rcBuildPolyMesh failed");
        return std::nullopt;
    }
    if (pmesh.npolys == 0) {
        return std::nullopt;
    }

    rcPolyMeshDetail dmesh;
    if (!rcBuildPolyMeshDetail(&ctx, pmesh, chf, cfg.detailSampleDist, cfg.detailSampleMaxError, dmesh)) {
        TraceLog(LOG_WARNING, "NAVBAKE: rcBuildPolyMeshDetail failed");
        return std::nullopt;
    }

    return extractNavPolyMesh(pmesh, dmesh);
}

}
