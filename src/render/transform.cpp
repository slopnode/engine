#include "render/transform.hpp"

#include <raymath.h>

namespace slopengine {

void updateTransform(flecs::entity entity, LocalTransformation& local, GlobalTransformation& global) {
    const float flipX = entity.has<ViewGeoRoot>() ? -1.0f : 1.0f;
    const Matrix mLocal = MatrixMultiply(
        MatrixScale(flipX * local.scale.x, local.scale.y, local.scale.z),
        MatrixMultiply(
            QuaternionToMatrix(local.rotation),
            MatrixTranslate(local.position.x, local.position.y, local.position.z)));

    if (entity.parent().is_valid()) {
        const flecs::entity parent = entity.parent();
        const GlobalTransformation& parentGlobal = parent.get<GlobalTransformation>();
        global.matrix = MatrixMultiply(mLocal, parentGlobal.matrix);
    } else {
        global.matrix = mLocal;
    }

    entity.children([](flecs::entity child) {
        if (!child.has<LocalTransformation>() || !child.has<GlobalTransformation>()) {
            return;
        }
        updateTransform(
            child,
            child.get_mut<LocalTransformation>(),
            child.get_mut<GlobalTransformation>());
    });
}

}
