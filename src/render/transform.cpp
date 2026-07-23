#include "render/transform.hpp"

namespace slopengine {

void updateTransform(flecs::entity entity, LocalTransformation& local, GlobalTransformation& global) {
    Matrix mLocal = MatrixIdentity();
    mLocal = MatrixMultiply(mLocal, MatrixScale(local.scale.x, local.scale.y, local.scale.z));
    mLocal = MatrixMultiply(mLocal, QuaternionToMatrix(local.rotation));
    mLocal = MatrixMultiply(
        mLocal,
        MatrixTranslate(local.position.x, local.position.y, local.position.z));

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
