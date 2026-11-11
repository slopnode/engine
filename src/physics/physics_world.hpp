#pragma once

#include "core/sim_clock.hpp"
#include "map/brush.hpp"
#include "physics/motored_body.hpp"
#include "physics/prop_box.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace slopengine {

struct CharacterMotor;
struct Brush;

struct CharacterStep {
    std::uint64_t id = 0;
    CharacterMotor* motor = nullptr;
    bool noclip = false;
    float submersion = 0.0f; /**< Fraction (0..1) of body height inside a water volume. */
    /** True if a water surface exists within motor.waterExitReach of the character's body. Gates
     *  water-exit assistance so it can only ever fire near an actual shoreline/lip, never
     *  arbitrarily deep underwater. */
    bool nearWaterSurface = false;
};

/** One rigid part of a ragdoll, in world space. The joint to @p parent sits at @p pivot and
 *  twists about @p twistAxis, swinging up to @p swing radians and twisting up to @p twist. */
struct RagdollPartDesc {
    int parent = -1;
    Vector3 center{0.0f, 0.0f, 0.0f};
    Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vector3 halfExtents{0.05f, 0.05f, 0.05f};
    Vector3 pivot{0.0f, 0.0f, 0.0f};
    Vector3 twistAxis{0.0f, 1.0f, 0.0f};
    float swing = 0.5f;
    float twist = 0.3f;
    Vector3 velocity{0.0f, 0.0f, 0.0f};
    Vector3 angularVelocity{0.0f, 0.0f, 0.0f};
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

    /** Free-tumbling box on its own layer: collides with world hulls only, so it never
     *  blocks linescans, sight, characters or other debris. */
    void createDebrisBox(
        std::uint64_t id,
        Vector3 center,
        Vector3 halfExtents,
        Quaternion rotation,
        Vector3 velocity,
        Vector3 angularVelocity);
    bool debrisPose(std::uint64_t id, Vector3& center, Quaternion& rotation) const;
    bool hasDebris(std::uint64_t id) const;
    void destroyDebris(std::uint64_t id);
    void clearDebris();

    void createPropBoxes(
        std::uint64_t id,
        Vector3 position,
        Quaternion rotation,
        Vector3 scale,
        const std::vector<PropBox>& boxes);
    bool hasProp(std::uint64_t id) const;
    std::size_t propBoxCount(std::uint64_t id) const;
    void destroyProp(std::uint64_t id);
    void clearProps();

    /** Jointed ragdoll on the debris layer. Parts must list parents before children. */
    bool createRagdoll(std::uint64_t id, const std::vector<RagdollPartDesc>& parts);
    /** World rotation and translation of each part, in the order they were created. */
    bool ragdollPartTransforms(std::uint64_t id, std::vector<Matrix>& out) const;
    bool hasRagdoll(std::uint64_t id) const;
    void destroyRagdoll(std::uint64_t id);
    void clearRagdolls();
    int blast(Vector3 center, float radius, float speed, float lift);
    bool tryGetKinematicAabb(std::uint64_t id, Vector3& outMin, Vector3& outMax) const;
    bool kinematicAllowsSlide(JPH::BodyID bodyId) const;

    void createCharacter(std::uint64_t id, float x, float y, float z, const CharacterMotor& motor);
    void destroyCharacter(std::uint64_t id);
    void destroyAllCharacters();

    bool resizeCharacter(std::uint64_t id, const CharacterMotor& motor, float maxPenetrationDepth = 0.05f);

    void setPlayerId(std::uint64_t id) { playerId_ = id; }
    std::uint64_t playerId() const { return playerId_; }

    void createPlayerCharacter(float x, float y, float z, const CharacterMotor& motor);
    void destroyPlayerCharacter();
    bool resizePlayerCharacter(const CharacterMotor& motor, float maxPenetrationDepth = 0.05f) {
        return playerId_ != 0 && resizeCharacter(playerId_, motor, maxPenetrationDepth);
    }

    void setCharacterPosition(std::uint64_t id, float x, float y, float z);
    void placeCharacter(std::uint64_t id, float x, float y, float z);
    void resetCharacterState(std::uint64_t id, const JPH::RVec3& position, const JPH::Vec3& velocity);
    void stepCharacterAlone(const CharacterStep& step);
    void markPlayerCharacter(std::uint64_t id);
    std::uint8_t characterBlockMask(std::uint64_t id) const;

    void step(const std::vector<CharacterStep>& steps);
    void stepBodies();

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
    bool launchCharacter(std::uint64_t id, float upSpeed);

    /** Scans a ring of directions for one with a climbable water-exit ledge within
     *  motor.waterExitReach (the same probe tryWaterExitAssist uses for the character's current
     *  wish direction). Returns true and fills outDirX/outDirZ (a unit XZ vector) if one is found;
     *  read-only, does not move the character. For AI that needs to aim itself at a shoreline it
     *  isn't already facing. */
    bool findWaterExitDirection(
        std::uint64_t characterId,
        const CharacterMotor& motor,
        float& outDirX,
        float& outDirZ) const;

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
        std::uint64_t characterId,
        float submersion,
        bool nearWaterSurface);
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
    void applyBuoyantInput(
        JPH::CharacterVirtual& character,
        const CharacterMotor& motor,
        float dt,
        float submersion);
    void stepCharacterSwim(
        JPH::CharacterVirtual& character,
        const CharacterMotor& motor,
        std::uint64_t characterId,
        float submersion,
        bool nearWaterSurface);
    bool tryWaterExitAssist(
        JPH::CharacterVirtual& character,
        const CharacterMotor& motor,
        std::uint64_t characterId);
    std::optional<JPH::RVec3> probeWaterExitLedge(
        const JPH::CharacterVirtual& character,
        const CharacterMotor& motor,
        std::uint64_t characterId,
        JPH::Vec3 wishDir) const;
    void applyCharacterSoftSeparation(const std::vector<CharacterStep>& steps);

    static constexpr float kFixedDt = kSimTickDt;

    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::BroadPhaseLayerInterface> broadPhaseLayerInterface_;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> objectVsBroadPhaseLayerFilter_;
    std::unique_ptr<JPH::ObjectLayerPairFilter> objectLayerPairFilter_;
    std::unique_ptr<JPH::PhysicsSystem> system_;
    std::unordered_map<std::uint64_t, CharacterEntry> characters_;
    std::unordered_map<std::uint64_t, JPH::BodyID> kinematicBodies_;
    std::unordered_map<std::uint64_t, std::vector<JPH::BodyID>> propBodies_;
    std::unordered_map<std::uint32_t, std::uint64_t> kinematicBodyToEntity_;
    std::unordered_map<std::uint64_t, bool> kinematicSlide_;
    std::unordered_map<std::uint64_t, JPH::BodyID> debrisBodies_;
    std::unordered_map<std::uint64_t, JPH::Ref<JPH::Ragdoll>> ragdolls_;
    std::uint64_t playerId_ = 0;
    std::unordered_set<std::uint64_t> playerCharacters_;
    std::vector<JPH::BodyID> staticBodies_;
    std::unordered_map<JPH::uint32, std::uint8_t> staticBodyBlocks_;
    bool factoryInitialized_ = false;
};

}
