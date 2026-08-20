#include "map/brush_door.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace slopengine {

namespace {

const Thing* findThingById(const ThingDocument* doc, std::string_view id) {
    if (doc == nullptr || id.empty()) {
        return nullptr;
    }
    for (const Thing& thing : doc->things) {
        if (thing.id == id) {
            return &thing;
        }
    }
    return nullptr;
}

MoverRotAxis toMoverRotAxis(DoorAxis axis) {
    switch (axis) {
    case DoorAxis::Pitch:
        return MoverRotAxis::Pitch;
    case DoorAxis::Roll:
        return MoverRotAxis::Roll;
    case DoorAxis::Yaw:
    default:
        return MoverRotAxis::Yaw;
    }
}

} // namespace

void configureBrushDoorMover(
    RigidMover& mover,
    const Brush& brush,
    const BrushDoor& door,
    Vector3 closedCenter,
    const ThingDocument* things) {
    const float sx = std::max(brush.maxs.x - brush.mins.x, 0.1f);
    const float sy = std::max(brush.maxs.y - brush.mins.y, 0.1f);
    const float sz = std::max(brush.maxs.z - brush.mins.z, 0.1f);

    mover.duration = door.haveDuration ? (door.duration > 0.0f ? door.duration : 0.8f) : 0.6f;
    mover.autoClose = door.haveAutoClose ? std::max(0.0f, door.autoClose) : 0.0f;
    mover.groupId = door.group;
    mover.blockMode = MoverBlockMode::Shove;
    mover.pushMode = MoverPushMode::Horizontal;
    mover.slide = false;
    mover.collideHalfExtents = {sx * 0.5f, sy * 0.5f, sz * 0.5f};
    mover.collideCenterLocal = {0.0f, 0.0f, 0.0f};

    switch (door.motion) {
    case DoorMotion::Raise: {
        const float travel = door.haveTravel && door.travel > 0.0f
            ? door.travel
            : (sy > 0.1f ? sy + 0.1f : 2.2f);
        mover.openPosOffset = {0.0f, travel, 0.0f};
        break;
    }
    case DoorMotion::Slide: {
        if (sx >= sz) {
            const float travel = door.haveTravel && door.travel != 0.0f ? door.travel : sx;
            mover.openPosOffset = {travel, 0.0f, 0.0f};
        } else {
            const float travel = door.haveTravel && door.travel != 0.0f ? door.travel : sz;
            mover.openPosOffset = {0.0f, 0.0f, travel};
        }
        break;
    }
    case DoorMotion::Swing: {
        mover.openAngleRadians =
            door.haveAngle ? door.angle : PI * 0.5f;
        mover.rotAxis = toMoverRotAxis(door.axis);
        if (!door.hingeThingId.empty()) {
            const Thing* hinge = findThingById(things, door.hingeThingId);
            if (hinge != nullptr && hinge->haveAt) {
                mover.pivotLocal = Vector3Subtract(hinge->at, closedCenter);
            } else {
                TraceLog(
                    LOG_WARNING,
                    "DOOR: brush '%s' hinge '%s' missing; using center",
                    brush.id.c_str(),
                    door.hingeThingId.c_str());
            }
        }
        break;
    }
    }
}

}
