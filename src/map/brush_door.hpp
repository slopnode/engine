#pragma once

#include "map/brush.hpp"
#include "map/thing.hpp"
#include "physics/rigid_mover.hpp"

#include <raylib.h>

namespace slopengine {

void configureBrushDoorMover(
    RigidMover& mover,
    const Brush& brush,
    const BrushDoor& door,
    Vector3 closedCenter,
    const ThingDocument* things);

}
