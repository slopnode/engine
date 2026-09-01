#pragma once

namespace slopengine {

// Authoritative walkable-slope limit shared between the offline navmesh bake
// (Recast's rcConfig::walkableSlopeAngle) and the runtime character controller
// (JPH::CharacterVirtualSettings::mMaxSlopeAngle) so simulation and bake agree
// on what counts as a floor versus a wall.
constexpr float kNavMaxWalkableSlopeDegrees = 45.0f;

// Voxel cell size/height for the navmesh bake, in world units. Kept small
// enough to resolve stair treads and door-frame corridors after radius erosion.
constexpr float kNavCellSize = 0.1f;
constexpr float kNavCellHeight = 0.05f;

}
