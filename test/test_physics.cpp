#include "test_assert.hpp"

#include "map/brush.hpp"
#include "physics/components.hpp"
#include "physics/motored_body.hpp"
#include "physics/motored_sweep.hpp"
#include "physics/physics_world.hpp"
#include "physics/rigid_mover.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

void runCastScanTests() {
    PhysicsWorld world;
    const Brush wall = makeBrushBox(
        "wall",
        {-0.5f, 0.0f, 2.0f},
        {0.5f, 2.0f, 2.2f},
        "mat/a",
        {});
    world.addStaticBrushes({wall});

    {
        const auto hit = world.castRay({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f);
        CHECK(hit.has_value());
        CHECK(hit->fraction > 0.0f);
        CHECK(hit->fraction < 1.0f);
        CHECK(std::fabs(hit->point.z - 2.0f) < 0.05f);
    }

    {
        const auto miss = world.castRay({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 10.0f);
        CHECK_FALSE(miss.has_value());
    }

    {
        const auto hit = world.castSphere({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f, 0.15f);
        CHECK(hit.has_value());
        CHECK(hit->fraction > 0.0f);
        CHECK(hit->fraction < 1.0f);
        CHECK(std::fabs(hit->point.z - 2.0f) < 0.05f);
        CHECK(hit->normal.z < -0.5f);

        const Vector3 effect = impactEffectPosition(hit->point, hit->normal);
        CHECK(effect.z < hit->point.z - 0.05f);
        CHECK(std::fabs(effect.z - (hit->point.z - kMotoredImpactClearance)) < 0.02f);
    }

    {
        const auto miss = world.castSphere({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 1.0f, 0.15f);
        CHECK_FALSE(miss.has_value());
    }
}

void runMotoredSweepTests() {
    CharacterMotor motor{};
    motor.radius = 0.35f;
    motor.height = 1.1f;
    const Vector3 feet{0.0f, 0.0f, 5.0f};
    const Vector3 axisA{feet.x, feet.y + motor.radius, feet.z};
    const Vector3 axisB{feet.x, feet.y + motor.radius + motor.height, feet.z};

    {
        const auto hit =
            raycastCapsule({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f, axisA, axisB, motor.radius);
        CHECK(hit.has_value());
        CHECK(std::fabs(*hit - (5.0f - motor.radius)) < 0.05f);
    }

    {
        const auto miss =
            raycastCapsule({3.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f, axisA, axisB, motor.radius);
        CHECK_FALSE(miss.has_value());
    }

    {
        const auto top =
            raycastCapsule({0.0f, 3.0f, 5.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, axisA, axisB, motor.radius);
        CHECK(top.has_value());
        CHECK(*top > 0.0f);
        CHECK(*top < 3.0f);
    }

    {
        constexpr float rocketR = 0.15f;
        const auto hit = sweepSphereActorCapsule(
            {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f, rocketR, feet, motor);
        CHECK(hit.has_value());
        CHECK(*hit > 0.0f);
        CHECK(*hit < 1.0f);
        const float expectedDist = 5.0f - motor.radius - rocketR;
        CHECK(std::fabs((*hit) * 10.0f - expectedDist) < 0.08f);
    }

    {
        const auto miss = sweepSphereActorCapsule(
            {3.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f, 0.15f, feet, motor);
        CHECK_FALSE(miss.has_value());
    }

    {
        const auto inside = sweepSphereActorCapsule(
            {0.0f, 1.0f, 5.0f}, {0.0f, 0.0f, 1.0f}, 2.0f, 0.15f, feet, motor);
        CHECK(inside.has_value());
        CHECK_EQ(*inside, 0.0f);
    }

    {
        CharacterMotor playerMotor{};
        playerMotor.radius = 0.3f;
        playerMotor.height = 0.88f;
        const Vector3 playerFeet{2.0f, 0.0f, 0.0f};
        constexpr float fireballR = 0.12f;
        const auto hit = sweepSphereActorCapsule(
            {0.0f, 0.8f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            4.0f,
            fireballR,
            playerFeet,
            playerMotor);
        CHECK(hit.has_value());
        CHECK(*hit > 0.0f);
        CHECK(*hit < 1.0f);
    }

    {
        CharacterMotor nearMotor = motor;
        const Vector3 nearFeet{0.0f, 0.0f, 2.0f};
        const Vector3 farFeet{0.0f, 0.0f, 8.0f};
        const auto nearHit = sweepSphereActorCapsule(
            {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f, 0.15f, nearFeet, nearMotor);
        const auto farHit = sweepSphereActorCapsule(
            {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f, 0.15f, farFeet, nearMotor);
        CHECK(nearHit.has_value());
        CHECK(farHit.has_value());
        CHECK(*nearHit < *farHit);
    }
}

void runMoverPushSlideTests() {
    {
        Vector3 delta{};
        CHECK(moverComputeShove(MoverPushMode::Full, {0.0f, 1.0f, 0.0f}, 0.1f, delta));
        CHECK(std::fabs(delta.y) > 0.1f);
        CHECK(std::fabs(delta.x) < 1.0e-5f);
        CHECK(std::fabs(delta.z) < 1.0e-5f);

        CHECK_FALSE(moverComputeShove(MoverPushMode::Horizontal, {0.0f, 1.0f, 0.0f}, 0.1f, delta));
        CHECK_EQ(delta.x, 0.0f);
        CHECK_EQ(delta.y, 0.0f);
        CHECK_EQ(delta.z, 0.0f);

        CHECK(moverComputeShove(MoverPushMode::Horizontal, {0.0f, 0.5f, 1.0f}, 0.2f, delta));
        CHECK(std::fabs(delta.y) < 1.0e-5f);
        CHECK(std::fabs(delta.z) > 0.2f);

        CHECK_FALSE(moverComputeShove(MoverPushMode::Off, {1.0f, 0.0f, 0.0f}, 0.3f, delta));
    }

    {
        const Quaternion identity = QuaternionIdentity();
        constexpr std::uint64_t kPlatId = 99;
        constexpr std::uint64_t kCharId = 11;
        const Brush floor = makeBrushBox(
            "floor",
            {-8.0f, -0.25f, -4.0f},
            {8.0f, 0.0f, 4.0f},
            "mat/a",
            {});

        auto settleAndSlideX = [&](bool slide) -> float {
            PhysicsWorld world;
            world.addStaticBrushes({floor});
            Vector3 platCenter = {0.0f, 0.5f, 0.0f};
            const Vector3 platHalf = {1.5f, 0.1f, 1.5f};
            world.createKinematicBox(kPlatId, platCenter, platHalf, identity, slide);
            CharacterMotor motor{};
            motor.gravity = 20.0f;
            motor.wishX = 0.0f;
            motor.wishZ = 0.0f;
            world.createCharacter(kCharId, 0.0f, 0.6f, 0.0f, motor);
            for (int i = 0; i < 30; ++i) {
                world.setKinematicPose(kPlatId, platCenter, identity, kFixedDt);
                stepWithCharacter(world, kCharId, motor, kFixedDt);
            }
            CHECK(world.characterSupported(kCharId));
            const float startX = static_cast<float>(world.characterPosition(kCharId).GetX());
            for (int i = 0; i < 45; ++i) {
                platCenter.x += 0.05f;
                world.setKinematicPose(kPlatId, platCenter, identity, kFixedDt);
                stepWithCharacter(world, kCharId, motor, kFixedDt);
            }
            const float endX = static_cast<float>(world.characterPosition(kCharId).GetX());
            return endX - startX;
        };

        const float carryWithSlide = settleAndSlideX(true);
        const float carryWithoutSlide = settleAndSlideX(false);
        CHECK(carryWithSlide > 1.0f);
        CHECK(carryWithoutSlide < carryWithSlide * 0.35f);
    }
}

void runBrushBlockFilterTests() {
    constexpr std::uint64_t kCharId = 3;

    {
        PhysicsWorld world;
        Brush wall = makeBrushBox(
            "los-only",
            {-0.5f, 0.0f, 2.0f},
            {0.5f, 2.0f, 2.2f},
            "mat/a",
            {});
        wall.blocks = BrushBlock::Los;
        syncBrushNocollide(wall);
        const Brush floor = makeBrushBox(
            "floor",
            {-4.0f, -0.25f, -6.0f},
            {4.0f, 0.0f, 6.0f},
            "mat/a",
            {});
        world.addStaticBrushes({floor, wall});

        CHECK(world.castRay({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f, BrushBlock::Los).has_value());
        CHECK_FALSE(
            world.castRay({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f, BrushBlock::Player).has_value());

        world.setPlayerId(kCharId);
        CharacterMotor motor{};
        motor.wishZ = 6.0f;
        world.createCharacter(kCharId, 0.0f, 0.1f, 0.0f, motor);
        for (int i = 0; i < 120; ++i) {
            stepWithCharacter(world, kCharId, motor, kFixedDt);
        }
        CHECK(static_cast<float>(world.characterPosition(kCharId).GetZ()) > 3.0f);
    }

    {
        PhysicsWorld world;
        Brush wall = makeBrushBox(
            "player-only",
            {-0.5f, 0.0f, 2.0f},
            {0.5f, 2.0f, 2.2f},
            "mat/a",
            {});
        wall.blocks = BrushBlock::Player;
        syncBrushNocollide(wall);
        const Brush floor = makeBrushBox(
            "floor",
            {-4.0f, -0.25f, -6.0f},
            {4.0f, 0.0f, 6.0f},
            "mat/a",
            {});
        world.addStaticBrushes({floor, wall});

        CHECK_FALSE(
            world.castRay({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f, BrushBlock::Los).has_value());
        CHECK_FALSE(
            world.castSphere({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 10.0f, 0.15f, BrushBlock::Projectile)
                .has_value());

        world.setPlayerId(kCharId);
        CharacterMotor motor{};
        motor.wishZ = 6.0f;
        world.createCharacter(kCharId, 0.0f, 0.1f, 0.0f, motor);
        for (int i = 0; i < 120; ++i) {
            stepWithCharacter(world, kCharId, motor, kFixedDt);
        }
        CHECK(static_cast<float>(world.characterPosition(kCharId).GetZ()) < 1.85f);
    }
}

void runFlightTests() {
    {
        const Brush floor = makeBrushBox(
            "floor",
            {-4.0f, -0.25f, -4.0f},
            {4.0f, 0.0f, 4.0f},
            "mat/a",
            {});
        const Brush ceiling = makeBrushBox(
            "ceiling",
            {-4.0f, 4.0f, -4.0f},
            {4.0f, 4.25f, 4.0f},
            "mat/a",
            {});

        PhysicsWorld world;
        world.addStaticBrushes({floor, ceiling});
        CharacterMotor motor{};
        motor.moveMode = CharacterMoveMode::Fly;
        motor.gravity = 0.0f;
        motor.verticalSpeed = 4.0f;
        motor.moveSpeed = 0.0f;
        constexpr std::uint64_t kCharId = 42;
        world.createCharacter(kCharId, 0.0f, 2.0f, 0.0f, motor);

        motor.wishY = 1.0f;
        for (int i = 0; i < 30; ++i) {
            stepWithCharacter(world, kCharId, motor, kFixedDt);
        }
        const float yUp = static_cast<float>(world.characterPosition(kCharId).GetY());
        CHECK(yUp > 2.1f);

        motor.wishY = -1.0f;
        for (int i = 0; i < 30; ++i) {
            stepWithCharacter(world, kCharId, motor, kFixedDt);
        }
        const float yDown = static_cast<float>(world.characterPosition(kCharId).GetY());
        CHECK(yDown < yUp);
    }

    {
        const Brush floor = makeBrushBox(
            "floor",
            {-4.0f, 0.0f, -4.0f},
            {4.0f, 0.25f, 4.0f},
            "mat/a",
            {});
        const Brush ceiling = makeBrushBox(
            "ceiling",
            {-4.0f, 3.0f, -4.0f},
            {4.0f, 3.25f, 4.0f},
            "mat/a",
            {});

        PhysicsWorld world;
        world.addStaticBrushes({floor, ceiling});

        const auto floorHit =
            world.castRay({0.0f, 2.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 64.0f, BrushBlock::Actor);
        const auto ceilingHit =
            world.castRay({0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 64.0f, BrushBlock::Actor);
        CHECK(floorHit.has_value());
        CHECK(ceilingHit.has_value());
        CHECK(std::fabs(floorHit->point.y - 0.25f) < 0.05f);
        CHECK(std::fabs(ceilingHit->point.y - 3.0f) < 0.05f);
    }

    {
        const Brush floor = makeBrushBox(
            "floor",
            {-4.0f, 0.0f, -4.0f},
            {4.0f, 0.25f, 4.0f},
            "mat/a",
            {});
        const Brush ceiling = makeBrushBox(
            "ceiling",
            {-4.0f, 3.0f, -4.0f},
            {4.0f, 3.25f, 4.0f},
            "mat/a",
            {});

        PhysicsWorld world;
        world.addStaticBrushes({floor, ceiling});
        CharacterMotor motor{};
        motor.hull = CharacterHull::Sphere;
        motor.moveMode = CharacterMoveMode::Fly;
        motor.gravity = 0.0f;
        motor.radius = 0.5f;
        motor.verticalSpeed = 4.0f;
        motor.moveSpeed = 0.0f;
        constexpr std::uint64_t kCharId = 43;
        world.createCharacter(kCharId, 0.0f, 0.5f, 0.0f, motor);

        motor.wishY = 1.0f;
        for (int i = 0; i < 120; ++i) {
            stepWithCharacter(world, kCharId, motor, kFixedDt);
        }
        const float feetY = static_cast<float>(world.characterPosition(kCharId).GetY());
        CHECK(feetY > 0.6f);
        CHECK(feetY + 2.0f * motor.radius <= 3.05f);
    }
}

void runSwimClimbTests() {
    // Pool floor the character starts resting on (feet at y=-10), a tall wall it swims into
    // (blocking forward at z=3, extending from below the floor up to y=0.25 -- 0.25 above the
    // water surface at y=0, matching the E1M1 exposed lip this reproduces), and a ledge just past
    // the wall whose top lines up with the wall's crest, within stepHeight of the water surface.
    constexpr float kWaterSurfaceY = 0.0f;
    const Brush floor = makeBrushBox("floor", {-3.0f, -10.5f, -3.0f}, {3.0f, -10.0f, 3.0f}, "mat/a", {});
    const Brush wall = makeBrushBox("wall", {-3.0f, -10.0f, 3.0f}, {3.0f, 0.25f, 3.3f}, "mat/a", {});
    const Brush ledge = makeBrushBox("ledge", {-3.0f, -2.0f, 3.3f}, {3.0f, 0.25f, 6.0f}, "mat/a", {});

    PhysicsWorld world;
    world.addStaticBrushes({floor, wall, ledge});
    constexpr std::uint64_t kCharId = 9;
    world.setPlayerId(kCharId);

    CharacterMotor motor{};
    // Spawn already pressed against the wall face (z=3) while still deep, so forward progress is
    // blocked from tick one.
    world.createCharacter(kCharId, 0.0f, -10.0f, 2.9f, motor);
    motor.wishY = 1.0f;
    motor.wishZ = 1.0f;

    // Submersion as a function of current feet height against a fixed water surface, the same
    // shape computeSubmersion (physics_module.cpp) produces by sampling the BSP water leaf up the
    // character's body -- crucially, this reaches 0 once the character is above the surface, so
    // buoyancy actually stops there like it does in real gameplay. An earlier version of this test
    // fed PhysicsWorld a constant submersion=1.0 for the whole run instead (PhysicsWorld itself
    // only consumes whatever fraction it's given, so that's all it takes to drive it directly);
    // that let the character simply float over the wall on undying buoyancy before WalkStairs was
    // ever needed, passing every assertion without exercising the mechanism this test exists to
    // check -- the real bug only shows up once buoyancy actually cuts out at the surface and the
    // character has to close the last stretch to the ledge via WalkStairs or fall back in.
    const float totalHeight = characterTotalHeight(motor);
    auto submersionAt = [&](float feetY) {
        return std::clamp((kWaterSurfaceY - feetY) / totalHeight, 0.0f, 1.0f);
    };

    auto stepWet = [&](int ticks) {
        for (int i = 0; i < ticks; ++i) {
            const float feetY = static_cast<float>(world.characterPosition(kCharId).GetY());
            CharacterStep step{};
            step.id = kCharId;
            step.motor = &motor;
            step.submersion = submersionAt(feetY);
            world.update(kFixedDt, {step});
        }
    };

    // Regression for "touched the bottom and could not ascend": starting grounded on the pool
    // floor, holding ascend must lift the character off it right away.
    stepWet(30);
    const float earlyY = static_cast<float>(world.characterPosition(kCharId).GetY());
    CHECK(earlyY > -9.5f);

    // Regression for "stuck against the wall, never gets out": walk the simulation forward one
    // tick at a time and record the character's y the moment it first gets past the wall's z
    // face. A genuine WalkStairs climb lands within stepHeight of the ledge (y=0.25); floating
    // over the wall on undying buoyancy instead -- the failure mode an earlier, looser version of
    // this test missed -- overshoots that by a wide margin since nothing would have bounded it.
    float crossingY = std::numeric_limits<float>::quiet_NaN();
    for (int i = 0; i < 300 && std::isnan(crossingY); ++i) {
        stepWet(1);
        const JPH::RVec3 p = world.characterPosition(kCharId);
        if (static_cast<float>(p.GetZ()) > 3.3f) {
            crossingY = static_cast<float>(p.GetY());
        }
    }
    CHECK_FALSE(std::isnan(crossingY));
    if (!std::isnan(crossingY)) {
        CHECK(crossingY > -0.25f);
        CHECK(crossingY < 0.75f);
    }

    // And it should actually stay there, not slide back off the ledge into the pool.
    stepWet(60);
    const float settledZ = static_cast<float>(world.characterPosition(kCharId).GetZ());
    CHECK(settledZ > 3.3f);

    // Regression for "requires a particular upward velocity, has to sink and launch": a character
    // holding a slow, steady ascend the whole time -- never dipping down first to build momentum,
    // the way a "sink and launch" technique would -- must still climb out, and promptly (within a
    // couple of seconds), not just eventually. The deterministic up/forward/down probe in
    // tryClimbBlockedStep only checks whether there's room to stand, not how fast the character is
    // moving, so it doesn't care either way; this is here to pin that down. (Jolt's own WalkStairs,
    // used here previously, gated success on a "made real forward progress" check performed at the
    // up-swept height: if that height hadn't already cleared the obstacle, forward progress there
    // was zero and the whole attempt was silently discarded -- in effect requiring the up-sweep,
    // capped at stepHeight from wherever the character *currently* was, to already reach past the
    // lip, which in practice took a burst of built-up vertical speed rather than steady swimming.)
    {
        PhysicsWorld slowWorld;
        slowWorld.addStaticBrushes({floor, wall, ledge});
        slowWorld.setPlayerId(kCharId);

        CharacterMotor slowMotor{};
        slowWorld.createCharacter(kCharId, 0.0f, -10.0f, 2.9f, slowMotor);
        slowMotor.wishZ = 1.0f;
        slowMotor.wishY = 0.3f; // a light, steady hold on ascend -- well short of full push

        float slowCrossingY = std::numeric_limits<float>::quiet_NaN();
        for (int i = 0; i < 300 && std::isnan(slowCrossingY); ++i) {
            const float feetY = static_cast<float>(slowWorld.characterPosition(kCharId).GetY());
            CharacterStep step{};
            step.id = kCharId;
            step.motor = &slowMotor;
            step.submersion = submersionAt(feetY);
            slowWorld.update(kFixedDt, {step});

            const JPH::RVec3 p = slowWorld.characterPosition(kCharId);
            if (static_cast<float>(p.GetZ()) > 3.3f) {
                slowCrossingY = static_cast<float>(p.GetY());
            }
        }
        CHECK_FALSE(std::isnan(slowCrossingY));
    }
}

} // namespace

void runPhysicsTests() {
    runAutoCloseTests();
    runKinematicTests();
    runCastScanTests();
    runMotoredSweepTests();
    runMoverPushSlideTests();
    runBrushBlockFilterTests();
    runFlightTests();
    runSwimClimbTests();
}

}

