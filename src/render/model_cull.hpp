#pragma once

#include "map/bsp.hpp"
#include "render/components.hpp"

#include <raylib.h>

namespace slopengine {

const BoundingBox& refreshModelCull(Model3D& model, const Matrix& globalMatrix, const BspTree* tree);

}
