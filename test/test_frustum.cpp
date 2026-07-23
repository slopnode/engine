#include "test_assert.hpp"

#include "render/render_frustum.hpp"

#include <cmath>

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

namespace {

Camera3D makeLookCamera(Vector3 position, Vector3 target, float fovy = 90.0f) {
    Camera3D camera{};
    camera.position = position;
    camera.target = target;
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = fovy;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

Frustum makeUnitAspectFrustum(const Camera3D& camera) {
    return makeFrustumFromCamera(camera, 1.0f);
}

} // namespace

void runFrustumTests() {
    {
        const BoundingBox local{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        const BoundingBox moved = transformAabb(local, MatrixTranslate(10.0f, 0.0f, 0.0f));
        CHECK(std::fabs(moved.min.x - 9.0f) < 1e-4f);
        CHECK(std::fabs(moved.max.x - 11.0f) < 1e-4f);
        CHECK(std::fabs(moved.min.y - -1.0f) < 1e-4f);
        CHECK(std::fabs(moved.max.y - 1.0f) < 1e-4f);
        CHECK(std::fabs(moved.min.z - -1.0f) < 1e-4f);
        CHECK(std::fabs(moved.max.z - 1.0f) < 1e-4f);
    }

    {
        const BoundingBox local{{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}};
        const BoundingBox rotated =
            transformAabb(local, MatrixRotateY(90.0f * DEG2RAD));
        CHECK(std::fabs(rotated.min.x) < 1e-4f);
        CHECK(std::fabs(rotated.max.x) < 1e-4f);
        CHECK(std::fabs(rotated.min.z + 2.0f) < 1e-4f ||
              std::fabs(rotated.max.z - 2.0f) < 1e-4f);
        CHECK(rotated.min.z < rotated.max.z);
        CHECK(std::fabs((rotated.max.z - rotated.min.z) - 2.0f) < 1e-4f);
    }

    {
        const Camera3D camera = makeLookCamera({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
        const Frustum frustum = makeUnitAspectFrustum(camera);

        CHECK(sphereInFrustum(frustum, {0.0f, 0.0f, 5.0f}, 0.0f));
        CHECK(sphereInFrustum(frustum, {0.0f, 0.0f, 5.0f}, 1.0f));
        CHECK_FALSE(sphereInFrustum(frustum, {0.0f, 0.0f, -5.0f}, 0.0f));
        CHECK_FALSE(sphereInFrustum(frustum, {100.0f, 0.0f, 5.0f}, 0.0f));
        CHECK_FALSE(sphereInFrustum(frustum, {0.0f, 100.0f, 5.0f}, 0.0f));
        CHECK(sphereInFrustum(frustum, {100.0f, 0.0f, 5.0f}, 200.0f));
    }

    {
        const Camera3D camera = makeLookCamera({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
        const Frustum frustum = makeUnitAspectFrustum(camera);

        CHECK(aabbInFrustum(
            frustum,
            BoundingBox{{-0.5f, -0.5f, 4.0f}, {0.5f, 0.5f, 5.0f}}));
        CHECK_FALSE(aabbInFrustum(
            frustum,
            BoundingBox{{-0.5f, -0.5f, -5.0f}, {0.5f, 0.5f, -4.0f}}));
        CHECK_FALSE(aabbInFrustum(
            frustum,
            BoundingBox{{50.0f, -0.5f, 4.0f}, {51.0f, 0.5f, 5.0f}}));
    }

    {
        const Camera3D camera = makeLookCamera({0.0f, 1.7f, 0.0f}, {0.0f, 1.7f, 1.0f}, 75.0f);
        const Frustum frustum = makeFrustumFromCamera(camera, 16.0f / 9.0f);

        CHECK(sphereInFrustum(frustum, {0.0f, 1.7f, 8.0f}, 1.0f));
        CHECK(sphereInFrustum(frustum, {0.0f, 2.2f, 4.0f}, 2.0f));
        CHECK_FALSE(sphereInFrustum(frustum, {0.0f, 1.7f, -8.0f}, 1.0f));
        CHECK_FALSE(sphereInFrustum(frustum, {40.0f, 1.7f, 8.0f}, 1.0f));
    }

    {
        const Camera3D camera = makeLookCamera({10.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f});
        const Frustum frustum = makeUnitAspectFrustum(camera);

        CHECK(sphereInFrustum(frustum, {5.0f, 0.0f, 5.0f}, 0.5f));
        CHECK_FALSE(sphereInFrustum(frustum, {20.0f, 0.0f, 20.0f}, 0.5f));
    }

    {
        const Camera3D camera = makeLookCamera({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 60.0f);
        const Frustum frustum = makeUnitAspectFrustum(camera);
        const float nearZ = 1.0f;
        const float halfHeight = nearZ * std::tan(30.0f * DEG2RAD);
        CHECK(sphereInFrustum(frustum, {0.0f, 0.0f, nearZ}, 0.0f));
        CHECK(sphereInFrustum(frustum, {0.0f, halfHeight * 0.5f, nearZ}, 0.0f));
        CHECK_FALSE(sphereInFrustum(frustum, {0.0f, halfHeight * 3.0f, nearZ}, 0.0f));
    }
}

}
