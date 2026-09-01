#pragma once

#include "map/bsp.hpp"
#include "map/brush.hpp"
#include "map/nav_bake.hpp"
#include "map/nav_graph.hpp"

#include <vector>

namespace slopengine {

/** Per-poly detail-mesh triangles from the bake, parallel to the MapNavigation this
 *  builder returns (index i here is leaf i there). Kept separate from MapNavigation
 *  itself since a BSP-leaf-built MapNavigation has no equivalent data -- callers that
 *  need exact ramp/stair height (not just the single leafFloorY approximation) query
 *  navHeightAt against this alongside the MapNavigation. */
struct NavMeshHeightField {
    std::vector<NavBakePolyDetail> polyDetail;
};

/** Builds a MapNavigation from a baked NavPolyMesh, treating each navmesh polygon as
 *  what a BSP leaf used to be and each shared polygon edge as what a BSP portal used
 *  to be -- so the existing A* (findLeafPath), funnel (leafPathToWaypoints), and flow
 *  field code (nav_graph.hpp) keep working unmodified against a navmesh-derived graph.
 *  @p brushes re-tags door corridors by testing each edge midpoint against Door brush
 *  AABBs (door_portal_tag.hpp -- door geometry itself was excluded from the bake, see
 *  nav_bake.cpp, so the corridor already exists and only needs re-tagging) and marks
 *  water polygons by testing each poly centroid against Water brush volumes, mirroring
 *  bsp_build.cpp's own point-in-brush water test. */
MapNavigation buildMapNavigationFromPolyMesh(
    const NavPolyMesh& polyMesh,
    const std::vector<Brush>& brushes,
    NavMeshHeightField* heightFieldOut = nullptr);

/** Height of poly @p poly's own detail-mesh surface at (x, z) via barycentric
 *  interpolation over whichever detail triangle contains the point; @p fallbackY if
 *  none does (e.g. a point just outside the simplified poly boundary). */
float navHeightAt(const NavMeshHeightField& heightField, int poly, float x, float z, float fallbackY);

/** XZ point-in-polygon lookup against MapNavigation::leafBoundary -- the navmesh
 *  equivalent of BSP point classification (pointLeaf/pvsSampleLeaf), used by
 *  sampleNavLeaf below when @p nav was built from a baked navmesh. -1 if @p nav has no
 *  boundary data (a BSP-leaf-built graph -- callers should fall back to BSP point
 *  classification in that case) or no walkable polygon contains @p point. When more
 *  than one candidate matches (only possible at a shared-edge boundary, a measure-zero
 *  case in practice), the one whose floor is closest to @p point.y wins. */
int navSamplePoly(const MapNavigation& nav, Vector3 point);

/** Nearest walkable polygon to @p point when no polygon actually contains it (e.g. a
 *  point in the eroded margin Recast leaves along walls/doorways -- see
 *  buildNavPolyMesh's walkableRadius erosion -- or just outside a simplified boundary
 *  edge). Distance is to the polygon's closest edge in XZ; -1 if @p nav has no boundary
 *  data or nothing walkable lies within @p maxSnapDistance. Stays in navmesh poly index
 *  space, unlike falling back to a BSP leaf id would. */
int nearestWalkableNavPoly(const MapNavigation& nav, Vector3 point, float maxSnapDistance);

/** Samples the leaf/poly containing @p point in whichever index space @p nav actually
 *  is: navSamplePoly (falling back to nearestWalkableNavPoly for a near miss) against
 *  @p nav's baked boundary data if present, else exact BSP point classification via
 *  @p tree. This is the only correct way to turn a world point into a MapNavigation
 *  index once a map may be either BSP-leaf-built or navmesh-built -- callers must never
 *  call pvsSampleLeaf directly and feed the result into MapNavigation adjacency/lookup
 *  tables, since a raw BSP leaf id is a different index space than a baked navmesh's
 *  polygon ids: once @p nav has boundary data, the fallback must stay in poly space
 *  (nearestWalkableNavPoly) rather than crossing into pvsSampleLeaf's BSP leaf space. */
int sampleNavLeaf(const MapNavigation& nav, const BspTree& tree, Vector3 point);

}
