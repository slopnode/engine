#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <memory>
#include <vector>

namespace slopengine {

struct CharacterMotor;
struct Brush;

/** Jolt world with static brush hulls and a virtual player character. */
struct PhysicsWorld {
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    JPH::PhysicsSystem& system() { return *system_; }
    JPH::TempAllocator& allocator() { return *tempAllocator_; }

    /** Registers convex static bodies for map brushes (skips nocollide). */
    void addStaticBrushes(const std::vector<Brush>& brushes);

    /** Creates the player capsule at feet position using @p motor sizes. */
    void createPlayerCharacter(float x, float y, float z, const CharacterMotor& motor);

    /** Applies horizontal wish from @p motor; skips gravity when @p noclip. */
    void applyPlayerInput(
        const CharacterMotor& motor,
        float wishX,
        float wishZ,
        float dt,
        bool noclip = false);

    /** Steps the simulation with a fixed timestep accumulator. */
    void update(float frameDt, const CharacterMotor& motor, bool noclip = false);

    bool hasPlayer() const { return character_ != nullptr; }
    /** Feet position of the virtual character. */
    JPH::RVec3 playerPosition() const;
    /** True when the character is supported by ground. */
    bool playerSupported() const;

private:
    static constexpr float kFixedDt = 1.0f / 60.0f;
    static constexpr int kMaxSubsteps = 4;

    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::BroadPhaseLayerInterface> broadPhaseLayerInterface_;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> objectVsBroadPhaseLayerFilter_;
    std::unique_ptr<JPH::ObjectLayerPairFilter> objectLayerPairFilter_;
    std::unique_ptr<JPH::PhysicsSystem> system_;
    JPH::Ref<JPH::CharacterVirtual> character_;
    JPH::RefConst<JPH::Shape> characterShape_;
    std::vector<JPH::BodyID> staticBodies_;
    float accumulator_ = 0.0f;
    bool factoryInitialized_ = false;
};

}
