#include "render/model_cull.hpp"

#include "map/pvs.hpp"
#include "render/render_frustum.hpp"

#include <cstring>

namespace slopengine {

const BoundingBox& refreshModelCull(Model3D& model, const Matrix& globalMatrix, const BspTree* tree) {
    const BoundingBox localBounds = modelLocalBounds(model);
    if (model.cullValid && std::memcmp(&model.cullMatrix, &globalMatrix, sizeof(Matrix)) == 0 &&
        std::memcmp(&model.cullLocalBounds, &localBounds, sizeof(BoundingBox)) == 0) {
        return model.cullWorldBounds;
    }
    model.cullMatrix = globalMatrix;
    model.cullLocalBounds = localBounds;
    model.cullWorldBounds = transformAabb(localBounds, globalMatrix);
    model.cullLeaves.clear();
    if (tree != nullptr) {
        model.cullLeaves = pvsBoxSampleLeaves(*tree, model.cullWorldBounds);
    }
    model.cullValid = true;
    return model.cullWorldBounds;
}

}
