#include "test_assert.hpp"

#include "render/components.hpp"
#include "render/transform.hpp"

#include <cmath>

#include <flecs.h>
#include <raylib.h>
#include <raymath.h>

namespace slopengine {

namespace {

constexpr float kEps = 1e-4f;

LocalTransformation makeLocal(Vector3 position, Vector3 scale = {1.0f, 1.0f, 1.0f}, Quaternion rotation = QuaternionIdentity()) {
    LocalTransformation local{};
    local.position = position;
    local.scale = scale;
    local.rotation = rotation;
    return local;
}

Matrix expectedLocalMatrix(const LocalTransformation& local) {
    Matrix m = MatrixIdentity();
    m = MatrixMultiply(m, MatrixScale(local.scale.x, local.scale.y, local.scale.z));
    m = MatrixMultiply(m, QuaternionToMatrix(local.rotation));
    m = MatrixMultiply(
        m,
        MatrixTranslate(local.position.x, local.position.y, local.position.z));
    return m;
}

Vector3 translationOf(const Matrix& matrix) {
    return {matrix.m12, matrix.m13, matrix.m14};
}

bool nearEq(float a, float b) {
    return std::fabs(a - b) < kEps;
}

bool nearVec(Vector3 a, Vector3 b) {
    return nearEq(a.x, b.x) && nearEq(a.y, b.y) && nearEq(a.z, b.z);
}

bool nearMatrix(const Matrix& a, const Matrix& b) {
    return nearEq(a.m0, b.m0) && nearEq(a.m1, b.m1) && nearEq(a.m2, b.m2) && nearEq(a.m3, b.m3) &&
           nearEq(a.m4, b.m4) && nearEq(a.m5, b.m5) && nearEq(a.m6, b.m6) && nearEq(a.m7, b.m7) &&
           nearEq(a.m8, b.m8) && nearEq(a.m9, b.m9) && nearEq(a.m10, b.m10) && nearEq(a.m11, b.m11) &&
           nearEq(a.m12, b.m12) && nearEq(a.m13, b.m13) && nearEq(a.m14, b.m14) &&
           nearEq(a.m15, b.m15);
}

flecs::entity makeTransformEntity(flecs::world& world, const LocalTransformation& local) {
    GlobalTransformation global{};
    global.matrix = MatrixIdentity();
    return world.entity().set<LocalTransformation>(local).set<GlobalTransformation>(global);
}

void refresh(flecs::entity root) {
    updateTransform(
        root,
        root.get_mut<LocalTransformation>(),
        root.get_mut<GlobalTransformation>());
}

} // namespace

void runTransformTests() {
    {
        flecs::world world;
        const LocalTransformation local = makeLocal({10.0f, 2.0f, -3.0f});
        flecs::entity root = makeTransformEntity(world, local);
        refresh(root);
        CHECK(nearMatrix(root.get<GlobalTransformation>().matrix, expectedLocalMatrix(local)));
        CHECK(nearVec(translationOf(root.get<GlobalTransformation>().matrix), local.position));
    }

    {
        flecs::world world;
        const LocalTransformation local =
            makeLocal({0.0f, 0.0f, 0.0f}, {2.0f, 3.0f, 4.0f});
        flecs::entity root = makeTransformEntity(world, local);
        refresh(root);
        CHECK(nearMatrix(root.get<GlobalTransformation>().matrix, expectedLocalMatrix(local)));
    }

    {
        flecs::world world;
        const LocalTransformation local = makeLocal(
            {0.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 1.0f},
            QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, 90.0f * DEG2RAD));
        flecs::entity root = makeTransformEntity(world, local);
        refresh(root);
        CHECK(nearMatrix(root.get<GlobalTransformation>().matrix, expectedLocalMatrix(local)));
    }

    {
        flecs::world world;
        const LocalTransformation parentLocal = makeLocal({10.0f, 0.0f, 0.0f});
        const LocalTransformation childLocal = makeLocal({0.0f, 5.0f, 0.0f});
        flecs::entity parent = makeTransformEntity(world, parentLocal);
        flecs::entity child = makeTransformEntity(world, childLocal).child_of(parent);

        child.get_mut<GlobalTransformation>().matrix = MatrixTranslate(999.0f, 999.0f, 999.0f);
        refresh(parent);

        const Matrix expectedParent = expectedLocalMatrix(parentLocal);
        const Matrix expectedChild = MatrixMultiply(expectedLocalMatrix(childLocal), expectedParent);
        CHECK(nearMatrix(parent.get<GlobalTransformation>().matrix, expectedParent));
        CHECK(nearMatrix(child.get<GlobalTransformation>().matrix, expectedChild));
        CHECK(nearVec(translationOf(child.get<GlobalTransformation>().matrix), {10.0f, 5.0f, 0.0f}));
    }

    {
        flecs::world world;
        const LocalTransformation parentLocal =
            makeLocal({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f});
        const LocalTransformation childLocal = makeLocal({1.0f, 0.0f, 0.0f});
        flecs::entity parent = makeTransformEntity(world, parentLocal);
        flecs::entity child = makeTransformEntity(world, childLocal).child_of(parent);
        refresh(parent);

        const Matrix expectedChild = MatrixMultiply(
            expectedLocalMatrix(childLocal),
            expectedLocalMatrix(parentLocal));
        CHECK(nearMatrix(child.get<GlobalTransformation>().matrix, expectedChild));
        CHECK(nearVec(
            translationOf(child.get<GlobalTransformation>().matrix),
            translationOf(expectedChild)));
    }

    {
        flecs::world world;
        const LocalTransformation rootLocal = makeLocal({1.0f, 0.0f, 0.0f});
        const LocalTransformation midLocal = makeLocal({0.0f, 2.0f, 0.0f});
        const LocalTransformation leafLocal = makeLocal({0.0f, 0.0f, 3.0f});
        flecs::entity root = makeTransformEntity(world, rootLocal);
        flecs::entity mid = makeTransformEntity(world, midLocal).child_of(root);
        flecs::entity leaf = makeTransformEntity(world, leafLocal).child_of(mid);
        refresh(root);

        const Matrix expectedRoot = expectedLocalMatrix(rootLocal);
        const Matrix expectedMid = MatrixMultiply(expectedLocalMatrix(midLocal), expectedRoot);
        const Matrix expectedLeaf = MatrixMultiply(expectedLocalMatrix(leafLocal), expectedMid);
        CHECK(nearMatrix(root.get<GlobalTransformation>().matrix, expectedRoot));
        CHECK(nearMatrix(mid.get<GlobalTransformation>().matrix, expectedMid));
        CHECK(nearMatrix(leaf.get<GlobalTransformation>().matrix, expectedLeaf));
        CHECK(nearVec(translationOf(leaf.get<GlobalTransformation>().matrix), {1.0f, 2.0f, 3.0f}));
    }

    {
        flecs::world world;
        const LocalTransformation parentLocal = makeLocal({4.0f, 0.0f, 0.0f});
        flecs::entity parent = makeTransformEntity(world, parentLocal);
        flecs::entity left =
            makeTransformEntity(world, makeLocal({0.0f, 1.0f, 0.0f})).child_of(parent);
        flecs::entity right =
            makeTransformEntity(world, makeLocal({0.0f, -1.0f, 0.0f})).child_of(parent);
        refresh(parent);

        CHECK(nearVec(translationOf(left.get<GlobalTransformation>().matrix), {4.0f, 1.0f, 0.0f}));
        CHECK(nearVec(translationOf(right.get<GlobalTransformation>().matrix), {4.0f, -1.0f, 0.0f}));
    }

    {
        flecs::world world;
        const LocalTransformation parentLocal = makeLocal({0.0f, 0.0f, 0.0f});
        const LocalTransformation childLocal = makeLocal({0.0f, 0.0f, 7.0f});
        flecs::entity parent = makeTransformEntity(world, parentLocal);
        world.entity().child_of(parent);
        flecs::entity child = makeTransformEntity(world, childLocal).child_of(parent);
        refresh(parent);
        CHECK(nearVec(translationOf(child.get<GlobalTransformation>().matrix), {0.0f, 0.0f, 7.0f}));
    }

    {
        flecs::world world;
        const LocalTransformation rootLocal = makeLocal(
            {0.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 1.0f},
            QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, 90.0f * DEG2RAD));
        const LocalTransformation socketLocal = makeLocal({0.0f, 0.0f, 2.0f});
        const LocalTransformation viewLocal = makeLocal({0.5f, 0.0f, 0.0f});
        flecs::entity root = makeTransformEntity(world, rootLocal);
        flecs::entity socket = makeTransformEntity(world, socketLocal).child_of(root);
        flecs::entity view = makeTransformEntity(world, viewLocal).child_of(socket);
        refresh(root);

        const Matrix expectedRoot = expectedLocalMatrix(rootLocal);
        const Matrix expectedSocket = MatrixMultiply(expectedLocalMatrix(socketLocal), expectedRoot);
        const Matrix expectedView = MatrixMultiply(expectedLocalMatrix(viewLocal), expectedSocket);
        CHECK(nearMatrix(socket.get<GlobalTransformation>().matrix, expectedSocket));
        CHECK(nearMatrix(view.get<GlobalTransformation>().matrix, expectedView));
    }
}

}
