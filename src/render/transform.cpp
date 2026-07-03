#include "render/transform.hpp"

namespace daggerlike {

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

    flecs::query<LocalTransformation, GlobalTransformation> childQuery =
        entity.world()
            .query_builder<LocalTransformation, GlobalTransformation>()
            .with(flecs::ChildOf, entity)
            .build();

    childQuery.each([](flecs::entity child, LocalTransformation& childLocal, GlobalTransformation& childGlobal) {
        updateTransform(child, childLocal, childGlobal);
    });
}

}
