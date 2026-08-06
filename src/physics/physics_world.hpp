#pragma once

#include "map/brush.hpp"
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
    CharacterMotor* motor = nullptr;
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

    void createKinematicBox(
        std::uint64_t id,
        Vector3 center,
        Vector3 halfExtents,
        Quaternion rotation,
        bool slide = true);
    void setKinematicPose(
        std::uint64_t id,
        Vector3 center,
        Quaternion rotation,
        float dt);
    void setKinematicSlide(std::uint64_t id, bool slide);
    void destroyKinematic(std::uint64_t id);
    void clearKinematics();
    bool hasKinematic(std::uint64_t id) const;
    bool tryGetKinematicAabb(std::uint64_t id, Vector3& outMin, Vector3& outMax) const;
    bool kinematicAllowsSlide(JPH::BodyID bodyId) const;

    void createCharacter(std::uint64_t id, float x, float y, float z, const CharacterMotor& motor);
    void destroyCharacter(std::uint64_t id);
    void destroyAllCharacters();

    void setPlayerId(std::uint64_t id) { playerId_ = id; }
    std::uint64_t playerId() const { return playerId_; }

    void createPlayerCharacter(float x, float y, float z, const CharacterMotor& motor);
    void destroyPlayerCharacter();

    void setCharacterPosition(std::uint64_t id, float x, float y, float z);

    void update(float frameDt, const std::vector<CharacterStep>& steps);

    std::optional<SphereCastHit> castSphere(
        Vector3 origin,
        Vector3 direction,
        float distance,
        float radius,
        std::uint8_t blockMask = BrushBlock::Los) const;

    std::optional<RayCastHit> castRay(
        Vector3 origin,
        Vector3 direction,
        float distance,
        std::uint8_t blockMask = BrushBlock::Los) const;

    bool hasCharacter(std::uint64_t id) const;
    bool hasPlayer() const;
    JPH::RVec3 characterPosition(std::uint64_t id) const;
    JPH::Vec3 characterVelocity(std::uint64_t id) const;
    bool characterSupported(std::uint64_t id) const;

    template<typename Fn>
    void forEachCharacter(Fn&& fn) const {
        for (const auto& entry : characters_) {
            if (entry.second.character == nullptr) {
                continue;
            }
            const JPH::RVec3 feet = entry.second.character->GetPosition();
            fn(
                entry.first,
                Vector3{
                    static_cast<float>(feet.GetX()),
                    static_cast<float>(feet.GetY()),
                    static_cast<float>(feet.GetZ()),
                });
        }
    }

    JPH::RVec3 playerPosition() const;
    void setPlayerPosition(float x, float y, float z);
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
        bool noclip,
        std::uint64_t characterId);
    void stepCharacterTryMove(
        JPH::CharacterVirtual& character,
        const CharacterMotor& motor,
        std::uint64_t characterId);
    void applyFlightInput(
        JPH::CharacterVirtual& character,
        const CharacterMotor& motor,
        float dt);
    void stepCharacterFlight(
        JPH::CharacterVirtual& character,
        const CharacterMotor& motor,
        std::uint64_t characterId);
    void applyCharacterSoftSeparation(const std::vector<CharacterStep>& steps);

    static constexpr float kFixedDt = 1.0f / 60.0f;
    static constexpr int kMaxSubsteps = 4;

    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::BroadPhaseLayerInterface> broadPhaseLayerInterface_;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> objectVsBroadPhaseLayerFilter_;
    std::unique_ptr<JPH::ObjectLayerPairFilter> objectLayerPairFilter_;
    std::unique_ptr<JPH::PhysicsSystem> system_;
    std::unordered_map<std::uint64_t, CharacterEntry> characters_;
    std::unordered_map<std::uint64_t, JPH::BodyID> kinematicBodies_;
    std::unordered_map<std::uint32_t, std::uint64_t> kinematicBodyToEntity_;
    std::unordered_map<std::uint64_t, bool> kinematicSlide_;
    std::uint64_t playerId_ = 0;
    std::vector<JPH::BodyID> staticBodies_;
    std::unordered_map<JPH::uint32, std::uint8_t> staticBodyBlocks_;
    float accumulator_ = 0.0f;
    bool factoryInitialized_ = false;
};

}
