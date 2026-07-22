#pragma once

#include "map/brush.hpp"
#include "map/csg_compile.hpp"

#include <raylib.h>

namespace slopengine {

void axialUvAxes(Vector3 normal, Vector3& uAxis, Vector3& vAxis);

void faceUvAxes(
    bool uvLock,
    Vector3 normal,
    Vector3 storedUAxis,
    Vector3 storedVAxis,
    Vector3& uAxis,
    Vector3& vAxis);

void faceUvAxes(const BrushFace& face, Vector3& uAxis, Vector3& vAxis);

void ensureFaceUvAxes(BrushFace& face);

Vector2 worldPlanarUv(
    Vector3 position,
    Vector3 uAxis,
    Vector3 vAxis,
    Vector2 uvShiftPixels,
    Vector2 uvScale,
    const MaterialUvInfo& materialUv);

void lockFaceUvShift(
    BrushFace& face,
    Vector3 oldRef,
    Vector3 oldUAxis,
    Vector3 oldVAxis,
    float pixelsPerMeter);

}
