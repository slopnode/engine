#pragma once

#include "map/brush.hpp"
#include "map/bsp.hpp"
#include "map/nav_bake_config.hpp"

#include <raylib.h>

#include <array>
#include <optional>
#include <vector>

namespace slopengine {

/** Per-agent shape/limits the voxelizer bakes the walkable surface to. Defaults
 *  match CharacterMotor's own defaults (see src/physics/components.hpp). */
struct NavBakeParams {
    float agentRadius = 0.3f;
    float agentHeight = 0.88f;
    float agentMaxClimb = 0.5f;
    float agentMaxSlopeDegrees = kNavMaxWalkableSlopeDegrees;
    float cellSize = kNavCellSize;
    float cellHeight = kNavCellHeight;
};

/** One walkable polygon from the bake: outward-wound world-space vertices, plus
 *  one neighbor entry per edge (edge i connects vertices[i] to vertices[(i+1) %
 *  vertices.size()]; -1 = boundary edge, no neighbor). */
struct NavBakePoly {
    std::vector<Vector3> vertices;
    std::vector<int> neighbors;
};

/** A polygon's own detail triangles (real per-triangle height data, since a
 *  poly spanning a ramp/stair isn't flat the way its simplified vertices are). */
struct NavBakePolyDetail {
    std::vector<std::array<Vector3, 3>> triangles;
};

struct NavPolyMesh {
    std::vector<NavBakePoly> polys;
    std::vector<NavBakePolyDetail> polyDetail; // parallel to polys
};

/** Voxelizes @p brushes' walkable static geometry (brushes whose @c blocks include
 *  Player or Actor, excluding Trigger and Door brushes -- door corridors are baked
 *  as open so they can be re-tagged for gating after the fact, see nav_navmesh_build.hpp)
 *  through Recast's heightfield -> compact heightfield -> region -> contour -> polymesh
 *  pipeline sized to @p params, returning connected walkable polygons with adjacency.
 *
 *  @p tree (the brushes' own compiled BSP, already built by the caller) is used to reject
 *  upward-facing candidate-floor triangles that don't actually border open interior space --
 *  e.g. the exterior top surface of a sealed level's outer shell/ceiling, which otherwise
 *  gets misclassified walkable by Recast (nothing exists above it in the voxel grid, so it
 *  looks like a floor with infinite headroom). Non-upward-facing geometry (walls, ceiling
 *  undersides) is unaffected since it was never a walkable candidate in the first place.
 *
 *  @p exteriorEmpty (from analyzeMapHull, same as buildMapNavigation's own parameter)
 *  additionally rejects probes landing in the unbounded exterior void outside a sealed
 *  hull -- without it, "no solid brush here" alone can't distinguish real interior air
 *  from open space above/outside the level's shell, which is also solid-free.
 *
 *  Nullopt if no walkable area results (e.g. an empty or fully unwalkable brush set). */
std::optional<NavPolyMesh> buildNavPolyMesh(
    const BspTree& tree,
    const std::vector<Brush>& brushes,
    const std::vector<std::uint8_t>* exteriorEmpty = nullptr,
    const NavBakeParams& params = {});

}
