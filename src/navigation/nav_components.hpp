#pragma once

#include <raylib.h>

#include <limits>
#include <string>
#include <vector>

namespace slopengine {

/** Pathfinding goal/state for an actor: waypoint list, replan timing, and stuck detection.
 *  @ingroup navigation_components
 */
struct NavigationAgent {
    bool enabled = true;
    Vector3 goalPos{};
    bool hasGoal = false;
    std::string goalEntityId;
    std::vector<Vector3> waypoints;
    std::vector<int> leafPath;
    std::vector<int> waypointToLeaf;
    int waypointIndex = 0;
    int agentLeaf = -1;
    int goalLeaf = -1;
    float replanTimer = 0.0f;
    float replanInterval = 0.25f;
    float goalMoveThreshold = 1.0f;
    float arriveRadius = 0.75f;
    bool forceReplan = false;
    bool flyer = false;
    /** Excess drop (world units) beyond which a routing step accrues a cost penalty rather than
     *  being blocked outright; see ThingDef::motorMaxFall. Infinity = no preference (default). */
    float maxFall = std::numeric_limits<float>::infinity();
    /** Cost multiplier applied to routing steps landing in a Water-content leaf; see
     *  ThingDef::motorWaterAversion. 1.0 = no preference (default). */
    float waterCostMultiplier = 1.0f;
    /** Named entry in the nav-profile catalog (data/nav-profiles.s7) this agent paths
     *  against; empty falls back to the map's single default-baked graph. Resolved once
     *  at spawn time from ThingDef::motorNavProfile / Thing::motorNavProfile, or -- when
     *  neither names one -- auto-selected as the smallest baked profile that still covers
     *  the actor's own CharacterMotor radius/height (see resolveAutoNavProfile). */
    std::string navProfile;
    Vector3 lastGoalPos{};
    bool haveLastGoalPos = false;
    /** False when the current waypoints/leafPath are a stale route kept from the last
     *  reachable goal rather than a fresh route to the live one -- i.e. replanAgent tried
     *  and failed to find a path (goal off the mesh, or its leaf unreachable from the
     *  agent's) but chose to keep following the last known-good path instead of dropping
     *  it outright. Gates nav-path-direction's past-the-last-waypoint home-in fallback,
     *  which would otherwise beeline the agent toward a frozen historical goal position
     *  forever once it runs out of stale waypoints to follow. */
    bool goalReachable = true;
    float stuckTimer = 0.0f;
    Vector3 stuckLastPos{};
    bool haveStuckLastPos = false;
    float stuckSkipTime = 0.45f;
    float lateralBias = 0.0f;
    bool haveLateralBias = false;
};

}
