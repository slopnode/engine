#include "test_assert.hpp"

#include "map/brush.hpp"
#include "physics/components.hpp"
#include "physics/physics_world.hpp"
#include "physics/rigid_mover.hpp"

#include <cmath>
#include <vector>

namespace slopengine {
namespace {

constexpr float kFixedDt = 1.0f / 60.0f;
constexpr float kSaneCoord = 1.0e6f;

bool finiteVec(Vector3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool aabbSane(Vector3 mn, Vector3 mx) {
    return finiteVec(mn) && finiteVec(mx) && mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z &&
        std::fabs(mn.x) < kSaneCoord && std::fabs(mn.y) < kSaneCoord &&
        std::fabs(mn.z) < kSaneCoord && std::fabs(mx.x) < kSaneCoord &&
        std::fabs(mx.y) < kSaneCoord && std::fabs(mx.z) < kSaneCoord;
}

void stepWithCharacter(PhysicsWorld& world, std::uint64_t characterId, CharacterMotor& motor, float dt) {
    CharacterStep step{};
    step.id = characterId;
    step.motor = &motor;
    step.noclip = false;
    world.update(dt, {step});
}

void runAutoCloseTests() {
    {
        RigidMover mover{};
        mover.duration = 0.5f;
        mover.autoClose = 1.0f;
        mover.target = 1.0f;

        for (int i = 0; i < 12; ++i) {
            tickRigidMover(mover, 0.05f);
        }
        CHECK(mover.progress >= 0.999f);
        CHECK(mover.target >= 0.5f);

        tickRigidMover(mover, 0.4f);
        CHECK(mover.target >= 0.5f);
        CHECK(mover.autoCloseTimer > 0.0f);

        tickRigidMover(mover, 0.7f);
        CHECK(mover.target < 0.5f);
        CHECK_EQ(mover.autoCloseTimer, 0.0f);

        for (int i = 0; i < 20; ++i) {
            tickRigidMover(mover, 0.05f);
        }
        CHECK(mover.progress <= 0.001f);
    }

    {
        RigidMover mover{};
        mover.duration = 0.2f;
        mover.autoClose = 0.0f;
        mover.target = 1.0f;
        for (int i = 0; i < 30; ++i) {
            tickRigidMover(mover, 0.05f);
        }
        CHECK(mover.progress >= 0.999f);
        CHECK(mover.target >= 0.5f);
        tickRigidMover(mover, 5.0f);
        CHECK(mover.target >= 0.5f);
        CHECK_EQ(mover.autoCloseTimer, 0.0f);
    }

    {
        RigidMover mover{};
        mover.duration = 0.2f;
        mover.autoClose = 2.0f;
        mover.target = 1.0f;
        for (int i = 0; i < 20; ++i) {
            tickRigidMover(mover, 0.05f);
        }
        CHECK(mover.progress >= 0.999f);
        tickRigidMover(mover, 0.5f);
        CHECK(mover.autoCloseTimer > 0.0f);

        mover.target = 0.0f;
        tickRigidMover(mover, 0.05f);
        CHECK_EQ(mover.autoCloseTimer, 0.0f);
    }
}

void runKinematicTests() {
    const Quaternion identity = QuaternionIdentity();
    const Vector3 doorCenter = {0.0f, 1.1f, 0.0f};
    const Vector3 doorHalf = {1.0f, 1.1f, 0.06f};
    constexpr std::uint64_t kDoorId = 42;
    constexpr std::uint64_t kCharId = 7;

    {
        PhysicsWorld world;
        world.createKinematicBox(kDoorId, doorCenter, doorHalf, identity);
        CHECK(world.hasKinematic(kDoorId));

        Vector3 mn{};
        Vector3 mx{};
        CHECK(world.tryGetKinematicAabb(kDoorId, mn, mx));
        CHECK(aabbSane(mn, mx));
        CHECK(std::fabs((mx.x - mn.x) - 2.0f * doorHalf.x) < 1.0e-3f);
        CHECK(std::fabs((mx.y - mn.y) - 2.0f * doorHalf.y) < 1.0e-3f);
        CHECK(std::fabs((mx.z - mn.z) - 2.0f * doorHalf.z) < 1.0e-3f);
        CHECK(std::fabs(0.5f * (mn.x + mx.x) - doorCenter.x) < 1.0e-3f);
        CHECK(std::fabs(0.5f * (mn.y + mx.y) - doorCenter.y) < 1.0e-3f);
        CHECK(std::fabs(0.5f * (mn.z + mx.z) - doorCenter.z) < 1.0e-3f);
    }

    {
        PhysicsWorld world;
        world.createKinematicBox(kDoorId, doorCenter, doorHalf, identity);
        CharacterMotor motor{};
        world.createCharacter(kCharId, 0.0f, 0.1f, 4.0f, motor);

        const float dts[] = {kFixedDt, 2.0f, 1.0e-5f};
        for (float poseDt : dts) {
            world.setKinematicPose(kDoorId, doorCenter, identity, poseDt);
            stepWithCharacter(world, kCharId, motor, kFixedDt);

            Vector3 mn{};
            Vector3 mx{};
            CHECK(world.tryGetKinematicAabb(kDoorId, mn, mx));
            CHECK(aabbSane(mn, mx));
            CHECK(std::fabs(0.5f * (mn.y + mx.y) - doorCenter.y) < 0.05f);
        }
    }

    {
        PhysicsWorld world;
        world.createKinematicBox(
            kDoorId,
            {NAN, 1.0f, 0.0f},
            doorHalf,
            identity);
        CHECK_FALSE(world.hasKinematic(kDoorId));

        world.createKinematicBox(
            kDoorId,
            {0.0f, 1.0e7f, 0.0f},
            doorHalf,
            identity);
        CHECK_FALSE(world.hasKinematic(kDoorId));

        world.createKinematicBox(kDoorId, doorCenter, doorHalf, identity);
        CHECK(world.hasKinematic(kDoorId));
        world.setKinematicPose(kDoorId, {NAN, 1.0f, 0.0f}, identity, kFixedDt);
        Vector3 mn{};
        Vector3 mx{};
        CHECK(world.tryGetKinematicAabb(kDoorId, mn, mx));
        CHECK(std::fabs(0.5f * (mn.y + mx.y) - doorCenter.y) < 0.05f);
    }

    {
        PhysicsWorld world;
        world.createKinematicBox(kDoorId, doorCenter, doorHalf, identity);
        CharacterMotor motor{};
        world.createCharacter(kCharId, 0.0f, 0.1f, 4.0f, motor);

        const Vector3 farPos = {0.0f, 8.0f, 0.0f};
        world.setKinematicPose(kDoorId, farPos, identity, kFixedDt);
        stepWithCharacter(world, kCharId, motor, kFixedDt);

        Vector3 mn{};
        Vector3 mx{};
        CHECK(world.tryGetKinematicAabb(kDoorId, mn, mx));
        CHECK(aabbSane(mn, mx));
        CHECK(std::fabs(0.5f * (mn.y + mx.y) - farPos.y) < 0.05f);
    }

    {
        PhysicsWorld world;
        world.createKinematicBox(kDoorId, doorCenter, doorHalf, identity);
        CharacterMotor motor{};
        world.createCharacter(kCharId, 0.0f, 0.1f, 4.0f, motor);

        for (int i = 1; i <= 12; ++i) {
            const Vector3 pos = {
                doorCenter.x,
                doorCenter.y + 0.1f * static_cast<float>(i),
                doorCenter.z,
            };
            world.setKinematicPose(kDoorId, pos, identity, kFixedDt);
            stepWithCharacter(world, kCharId, motor, kFixedDt);

            Vector3 mn{};
            Vector3 mx{};
            CHECK(world.tryGetKinematicAabb(kDoorId, mn, mx));
            CHECK(aabbSane(mn, mx));
            CHECK(std::fabs(0.5f * (mn.y + mx.y) - pos.y) < 0.15f);
        }
    }

    {
        PhysicsWorld world;
        const Brush floor = makeBrushBox(
            "floor",
            {-4.0f, -0.25f, -6.0f},
            {4.0f, 0.0f, 6.0f},
            "mat/a",
            {});
        world.addStaticBrushes({floor});
        world.createKinematicBox(kDoorId, doorCenter, doorHalf, identity);
        CharacterMotor motor{};
        world.createCharacter(kCharId, 0.0f, 0.1f, 4.0f, motor);

        for (int i = 0; i < 30; ++i) {
            world.setKinematicPose(kDoorId, doorCenter, identity, kFixedDt);
            stepWithCharacter(world, kCharId, motor, kFixedDt);

            Vector3 mn{};
            Vector3 mx{};
            CHECK(world.tryGetKinematicAabb(kDoorId, mn, mx));
            CHECK(aabbSane(mn, mx));
        }
    }
}

} // namespace

void runPhysicsTests() {
    runAutoCloseTests();
    runKinematicTests();
}

}
