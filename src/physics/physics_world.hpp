#pragma once

#include "physics/motored_body.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace slopengine {

struct CharacterMotor;
struct Brush;

struct CharacterStep {
    std::uint64_t id = 0;
    const CharacterMotor* motor = nullptr;
    bool noclip = false;
};

struct RayCastHit {
    Vector3 point = {0.0f, 0.0f, 0.0f};
    Vector3 normal = {0.0f, 1.0f, 0.0f};
    float fraction = 1.0f;
};

/** Jolt world with static brush hulls and virtual character capsules. */
struct PhysicsWorld {
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    JPH::PhysicsSystem& system() { return *system_; }
    JPH::TempAllocator& allocator() { return *tempAllocator_; }

    void addStaticBrushes(const std::vector<Brush>& brushes);
    void clearStaticBrushes();

    void createCharacter(std::uint64_t id, float x, float y, float z, const CharacterMotor& motor);
    void destroyCharacter(std::uint64_t id);
    void destroyAllCharacters();

    void setPlayerId(std::uint64_t id) { playerId_ = id; }
    std::uint64_t playerId() const { return playerId_; }

    void createPlayerCharacter(float x, float y, float z, const CharacterMotor& motor);
    void destroyPlayerCharacter();

    void update(float frameDt, const std::vector<CharacterStep>& steps);

    std::optional<SphereCastHit> castSphere(
        Vector3 origin,
        Vector3 direction,
        float distance,
        float radius) const;

    std::optional<RayCastHit> castRay(
        Vector3 origin,
        Vector3 direction,
        float distance) const;

    bool hasCharacter(std::uint64_t id) const;
    bool hasPlayer() const;
    JPH::RVec3 characterPosition(std::uint64_t id) const;
    JPH::Vec3 characterVelocity(std::uint64_t id) const;
    bool characterSupported(std::uint64_t id) const;

    JPH::RVec3 playerPosition() const;
    JPH::Vec3 playerVelocity() const;
    bool playerSupported() const;

private:
    struct CharacterEntry {
        JPH::Ref<JPH::CharacterVirtual> character;
        JPH::RefConst<JPH::Shape> shape;
    };

    void applyCharacterInput(
        JPH::CharacterVirtual& character,
        const CharacterMotor& motor,
        float wishX,
        float wishZ,
        float dt,
        bool noclip);
    void stepCharacter(
        JPH::CharacterVirtual& character,
        const CharacterMotor& motor,
        bool noclip);
    void stepCharacterTryMove(
        JPH::CharacterVirtual& character,
        const CharacterMotor& motor);

    static constexpr float kFixedDt = 1.0f / 60.0f;
    static constexpr int kMaxSubsteps = 4;

    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::BroadPhaseLayerInterface> broadPhaseLayerInterface_;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> objectVsBroadPhaseLayerFilter_;
    std::unique_ptr<JPH::ObjectLayerPairFilter> objectLayerPairFilter_;
    std::unique_ptr<JPH::PhysicsSystem> system_;
    std::unordered_map<std::uint64_t, CharacterEntry> characters_;
    std::uint64_t playerId_ = 0;
    std::vector<JPH::BodyID> staticBodies_;
    float accumulator_ = 0.0f;
    bool factoryInitialized_ = false;
};

}
