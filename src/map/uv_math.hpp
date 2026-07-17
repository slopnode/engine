#pragma once

#include "map/csg_compile.hpp"

#include <raylib.h>

namespace slopengine {

void axialUvAxes(Vector3 normal, Vector3& uAxis, Vector3& vAxis);

Vector2 worldPlanarUv(
    Vector3 position,
    Vector3 uAxis,
    Vector3 vAxis,
    Vector2 uvShiftPixels,
    const MaterialUvInfo& materialUv);

}
