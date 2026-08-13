#pragma once

#include <raylib.h>

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
    Vector3 lastGoalPos{};
    bool haveLastGoalPos = false;
    float stuckTimer = 0.0f;
    Vector3 stuckLastPos{};
    bool haveStuckLastPos = false;
    float stuckSkipTime = 0.45f;
};

}
