#pragma once

#include "map/brush.hpp"

#include <vector>

namespace slopengine {

/** Returns the fragment(s) of @p polygon lying outside @p carver's convex volume.
 *  Clips sequentially against each of carver's face planes, keeping the outside
 *  half of each plane as a result fragment and carrying the still-possibly-inside
 *  half forward to the next plane (classic brush-CSG subtract). Whatever remains
 *  after all of carver's planes is fully embedded in carver and is discarded.
 *  Returns {polygon} unchanged if it doesn't overlap carver at all; empty if
 *  polygon is fully inside carver. */
std::vector<std::vector<Vector3>> clipPolygonOutsideBrush(
    const std::vector<Vector3>& polygon,
    const Brush& carver);

/** Clips every carve-eligible brush's (Hull/Window/Transparent) faces against
 *  every later, spatially-overlapping carve-eligible brush in the list, so only
 *  the exposed portion of each face survives. Non-eligible roles (Door/Detail/
 *  Trigger/Hint/Water) pass through unchanged, and never carve or get carved --
 *  Door is deliberately excluded despite sealing/splitting like Hull, since its
 *  volume routinely overlaps the floor/walls at a doorway and nothing else
 *  fills that gap while it's open. Brush order and ids are preserved; only face
 *  geometry and per-brush bounds/box-ness change on carved brushes. */
std::vector<Brush> carveBrushes(const std::vector<Brush>& brushes);

}
