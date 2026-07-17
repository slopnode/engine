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

struct PhysicsWorld {
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    JPH::PhysicsSystem& system() { return *system_; }
    JPH::TempAllocator& allocator() { return *tempAllocator_; }

    void addStaticBrushes(const std::vector<Brush>& brushes);
    void createPlayerCharacter(float x, float y, float z, const CharacterMotor& motor);
    void applyPlayerInput(
        const CharacterMotor& motor,
        float wishX,
        float wishZ,
        float dt,
        bool noclip = false);
    void update(float dt, bool noclip = false);

    bool hasPlayer() const { return character_ != nullptr; }
    JPH::RVec3 playerPosition() const;
    bool playerSupported() const;

private:
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::BroadPhaseLayerInterface> broadPhaseLayerInterface_;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> objectVsBroadPhaseLayerFilter_;
    std::unique_ptr<JPH::ObjectLayerPairFilter> objectLayerPairFilter_;
    std::unique_ptr<JPH::PhysicsSystem> system_;
    JPH::Ref<JPH::CharacterVirtual> character_;
    JPH::RefConst<JPH::Shape> characterShape_;
    std::vector<JPH::BodyID> staticBodies_;
    bool factoryInitialized_ = false;
};

}
