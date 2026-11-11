#pragma once

#include <raylib.h>

#include <cstdint>
#include <vector>

namespace slopengine {

bool cutSkinnedMesh(
    const Mesh& source,
    const std::vector<std::uint8_t>& inside,
    bool keepInside,
    int fallbackBone,
    Mesh& kept,
    Mesh& cap);

void freeCutMesh(Mesh& mesh);

bool cutSkinnedModel(
    Model& model,
    const std::vector<std::uint8_t>& inside,
    bool keepInside,
    int fallbackBone,
    const Material& capMaterial);

void restoreSkinnedModel(Model& model, const Model& source);

}
