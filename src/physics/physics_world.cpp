#include "physics/physics_world.hpp"

#include "map/brush.hpp"
#include "physics/components.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/RegisterTypes.h>

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

void TraceImpl(const char* fmt, ...) {
    char buffer[1024];
    va_list list;
    va_start(list, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, list);
    va_end(list);
    TraceLog(LOG_INFO, "Jolt: %s", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool AssertFailedImpl(const char* expression, const char* message, const char* file, unsigned int line) {
    TraceLog(
        LOG_ERROR,
        "Jolt assert %s:%u (%s) %s",
        file,
        line,
        expression,
        message != nullptr ? message : "");
    return true;
}
#endif

namespace Layers {
constexpr JPH::ObjectLayer NON_MOVING = 0;
constexpr JPH::ObjectLayer MOVING = 1;
constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer NON_MOVING(0);
constexpr JPH::BroadPhaseLayer MOVING(1);
constexpr unsigned int NUM_LAYERS = 2;
}

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        switch (a) {
        case Layers::NON_MOVING:
            return b == Layers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            return false;
        }
    }
};

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        objectToBroadPhase_[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        objectToBroadPhase_[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }

    unsigned int GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return objectToBroadPhase_[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        switch ((JPH::BroadPhaseLayer::Type)layer) {
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:
            return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:
            return "MOVING";
        default:
            return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer objectToBroadPhase_[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
        switch (layer) {
        case Layers::NON_MOVING:
            return broadPhase == BroadPhaseLayers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            return false;
        }
    }
};

} // namespace

void populateBrushBlockIgnoreFilter(
    JPH::IgnoreMultipleBodiesFilter& filter,
    const std::vector<JPH::BodyID>& staticBodies,
    const std::unordered_map<JPH::uint32, std::uint8_t>& staticBodyBlocks,
    std::uint8_t blockMask) {
    filter.Clear();
    filter.Reserve(static_cast<JPH::uint>(staticBodies.size()));
    for (JPH::BodyID id : staticBodies) {
        const auto it = staticBodyBlocks.find(id.GetIndex());
        if (it != staticBodyBlocks.end() && (it->second & blockMask) == 0) {
            filter.IgnoreBody(id);
        }
    }
}

PhysicsWorld::PhysicsWorld() {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = TraceImpl;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = AssertFailedImpl;
#endif

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    factoryInitialized_ = true;

    tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    const int threadCount = static_cast<int>(std::thread::hardware_concurrency());
    jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        threadCount > 1 ? threadCount - 1 : 1);

    broadPhaseLayerInterface_ = std::make_unique<BPLayerInterfaceImpl>();
    objectVsBroadPhaseLayerFilter_ = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
    objectLayerPairFilter_ = std::make_unique<ObjectLayerPairFilterImpl>();

    system_ = std::make_unique<JPH::PhysicsSystem>();
    system_->Init(
        1024,
        0,
        1024,
        1024,
        *broadPhaseLayerInterface_,
        *objectVsBroadPhaseLayerFilter_,
        *objectLayerPairFilter_);
    system_->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
}

PhysicsWorld::~PhysicsWorld() {
    characters_.clear();
    playerId_ = 0;

    if (system_) {
        JPH::BodyInterface& bodies = system_->GetBodyInterface();
        for (JPH::BodyID id : staticBodies_) {
            bodies.RemoveBody(id);
            bodies.DestroyBody(id);
        }
        staticBodies_.clear();
        for (const auto& [entityId, bodyId] : kinematicBodies_) {
            (void)entityId;
            bodies.RemoveBody(bodyId);
            bodies.DestroyBody(bodyId);
        }
        kinematicBodies_.clear();
        kinematicBodyToEntity_.clear();
        kinematicSlide_.clear();
    }

    system_.reset();
    jobSystem_.reset();
    tempAllocator_.reset();
    objectLayerPairFilter_.reset();
    objectVsBroadPhaseLayerFilter_.reset();
    broadPhaseLayerInterface_.reset();

    if (factoryInitialized_) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
        factoryInitialized_ = false;
    }
}

void PhysicsWorld::addStaticBrushes(const std::vector<Brush>& brushes) {
    JPH::BodyInterface& bodies = system_->GetBodyInterface();
    int boxCount = 0;
    int hullShapeCount = 0;
    int skippedNoBlocks = 0;

    for (const Brush& brush : brushes) {
        if (!brushBlocksAny(brush.blocks)) {
            ++skippedNoBlocks;
            continue;
        }

        JPH::RefConst<JPH::Shape> shape;
        JPH::RVec3 position = JPH::RVec3::sZero();

        if (brush.box) {
            const float hx = 0.5f * (brush.maxs.x - brush.mins.x);
            const float hy = 0.5f * (brush.maxs.y - brush.mins.y);
            const float hz = 0.5f * (brush.maxs.z - brush.mins.z);
            if (hx <= 0.0f || hy <= 0.0f || hz <= 0.0f) {
                TraceLog(LOG_WARNING, "PHYSICS: invalid box extents for brush '%s'", brush.id.c_str());
                continue;
            }

            JPH::BoxShapeSettings boxSettings(JPH::Vec3(hx, hy, hz));
            auto result = boxSettings.Create();
            if (result.HasError()) {
                TraceLog(
                    LOG_WARNING,
                    "PHYSICS: box shape failed for brush '%s': %s",
                    brush.id.c_str(),
                    result.GetError().c_str());
                continue;
            }
            shape = result.Get();
            position = JPH::RVec3(
                0.5 * (static_cast<double>(brush.mins.x) + static_cast<double>(brush.maxs.x)),
                0.5 * (static_cast<double>(brush.mins.y) + static_cast<double>(brush.maxs.y)),
                0.5 * (static_cast<double>(brush.mins.z) + static_cast<double>(brush.maxs.z)));
            ++boxCount;
        } else {
            JPH::Array<JPH::Vec3> points;
            for (const BrushFace& face : brush.faces) {
                for (const Vector3& v : face.vertices) {
                    points.push_back(JPH::Vec3(v.x, v.y, v.z));
                }
            }
            if (points.size() < 4) {
                continue;
            }

            JPH::ConvexHullShapeSettings hullSettings(points);
            hullSettings.mMaxConvexRadius = 0.05f;
            auto result = hullSettings.Create();
            if (result.HasError()) {
                TraceLog(
                    LOG_WARNING,
                    "PHYSICS: convex hull failed for brush '%s': %s",
                    brush.id.c_str(),
                    result.GetError().c_str());
                continue;
            }
            shape = result.Get();
            ++hullShapeCount;
        }

        JPH::BodyCreationSettings settings(
            shape,
            position,
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            Layers::NON_MOVING);
        settings.mFriction = 0.8f;

        const JPH::BodyID id = bodies.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        staticBodies_.push_back(id);
        staticBodyBlocks_[id.GetIndex()] = brush.blocks;
    }

    system_->OptimizeBroadPhase();
    TraceLog(
        LOG_INFO,
        "PHYSICS: added %d static brush bodies (box=%d hull=%d skipped %d no-blocks)",
        static_cast<int>(staticBodies_.size()),
        boxCount,
        hullShapeCount,
        skippedNoBlocks);
}

void PhysicsWorld::clearStaticBrushes() {
    if (!system_) {
        return;
    }
    JPH::BodyInterface& bodies = system_->GetBodyInterface();
    for (JPH::BodyID id : staticBodies_) {
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
    }
    staticBodies_.clear();
    staticBodyBlocks_.clear();
}

namespace {

JPH::Quat joltQuatFromRaylib(Quaternion q) {
    const float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!(lenSq > 1.0e-12f) || !std::isfinite(lenSq)) {
        return JPH::Quat::sIdentity();
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    return JPH::Quat(q.x * invLen, q.y * invLen, q.z * invLen, q.w * invLen);
}

bool finiteVec3(Vector3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

JPH::RefConst<JPH::Shape> makeCharacterShape(const CharacterMotor& motor) {
    const float radius = characterRadius(motor);
    const float height = characterBodyHeight(motor);
    const float cylinderHalf = 0.5f * height;
    const float halfY = cylinderHalf + radius;
    if (motor.hull == CharacterHull::Box) {
        return JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0.0f, halfY, 0.0f),
            JPH::Quat::sIdentity(),
            new JPH::BoxShape(JPH::Vec3(radius, halfY, radius))).Create().Get();
    }
    if (motor.hull == CharacterHull::Sphere) {
        return JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0.0f, radius, 0.0f),
            JPH::Quat::sIdentity(),
            new JPH::SphereShape(radius)).Create().Get();
    }
    return JPH::RotatedTranslatedShapeSettings(
        JPH::Vec3(0.0f, halfY, 0.0f),
        JPH::Quat::sIdentity(),
        new JPH::CapsuleShape(cylinderHalf, radius)).Create().Get();
}

} // namespace

void PhysicsWorld::createKinematicBox(
    std::uint64_t id,
    Vector3 center,
    Vector3 halfExtents,
    Quaternion rotation,
    bool slide) {
    if (!system_ || halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f) {
        return;
    }
    if (!finiteVec3(center) || !finiteVec3(halfExtents)) {
        TraceLog(LOG_WARNING, "PHYSICS: kinematic box rejected non-finite pose for id %llu",
            static_cast<unsigned long long>(id));
        return;
    }
    constexpr float kMaxCoord = 1.0e6f;
    if (std::fabs(center.x) > kMaxCoord || std::fabs(center.y) > kMaxCoord ||
        std::fabs(center.z) > kMaxCoord) {
        TraceLog(LOG_WARNING, "PHYSICS: kinematic box rejected out-of-range center for id %llu (%.3f %.3f %.3f)",
            static_cast<unsigned long long>(id),
            center.x,
            center.y,
            center.z);
        return;
    }

    destroyKinematic(id);

    JPH::BoxShapeSettings boxSettings(JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
    auto result = boxSettings.Create();
    if (result.HasError()) {
        TraceLog(LOG_WARNING, "PHYSICS: kinematic box shape failed for id %llu: %s",
            static_cast<unsigned long long>(id),
            result.GetError().c_str());
        return;
    }

    JPH::BodyCreationSettings settings(
        result.Get(),
        JPH::RVec3(center.x, center.y, center.z),
        joltQuatFromRaylib(rotation),
        JPH::EMotionType::Kinematic,
        Layers::MOVING);
    settings.mFriction = 0.8f;

    JPH::BodyInterface& bodies = system_->GetBodyInterface();
    const JPH::BodyID bodyId = bodies.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    bodies.SetLinearVelocity(bodyId, JPH::Vec3::sZero());
    bodies.SetAngularVelocity(bodyId, JPH::Vec3::sZero());
    kinematicBodies_[id] = bodyId;
    kinematicBodyToEntity_[bodyId.GetIndexAndSequenceNumber()] = id;
    kinematicSlide_[id] = slide;
    TraceLog(LOG_INFO, "PHYSICS: kinematic box %llu at (%.3f %.3f %.3f) he=(%.3f %.3f %.3f)",
        static_cast<unsigned long long>(id),
        center.x,
        center.y,
        center.z,
        halfExtents.x,
        halfExtents.y,
        halfExtents.z);
}

void PhysicsWorld::setKinematicPose(
    std::uint64_t id,
    Vector3 center,
    Quaternion rotation,
    float dt) {
    if (!system_) {
        return;
    }
    auto it = kinematicBodies_.find(id);
    if (it == kinematicBodies_.end()) {
        return;
    }
    if (!finiteVec3(center)) {
        TraceLog(LOG_WARNING, "PHYSICS: kinematic pose rejected non-finite center for id %llu",
            static_cast<unsigned long long>(id));
        return;
    }
    constexpr float kMaxCoord = 1.0e6f;
    if (std::fabs(center.x) > kMaxCoord || std::fabs(center.y) > kMaxCoord ||
        std::fabs(center.z) > kMaxCoord) {
        TraceLog(LOG_WARNING, "PHYSICS: kinematic pose rejected out-of-range center for id %llu",
            static_cast<unsigned long long>(id));
        return;
    }

    JPH::BodyInterface& bodies = system_->GetBodyInterface();
    const JPH::BodyID bodyId = it->second;
    JPH::Quat targetRot = joltQuatFromRaylib(rotation);
    const JPH::RVec3 targetPos(center.x, center.y, center.z);

    const JPH::RVec3 currentPos = bodies.GetPosition(bodyId);
    const JPH::Quat currentRot = bodies.GetRotation(bodyId);
    if (currentRot.Dot(targetRot) < 0.0f) {
        targetRot = -targetRot;
    }
    const JPH::Vec3 deltaPos = JPH::Vec3(targetPos - currentPos);
    const float posErrSq = deltaPos.LengthSq();
    constexpr float kSnapPosSq = 1.0e-8f;
    if (posErrSq <= kSnapPosSq && currentRot.IsClose(targetRot)) {
        bodies.SetLinearAndAngularVelocity(bodyId, JPH::Vec3::sZero(), JPH::Vec3::sZero());
        bodies.SetPositionAndRotationWhenChanged(
            bodyId, targetPos, targetRot, JPH::EActivation::DontActivate);
        return;
    }

    auto slideIt = kinematicSlide_.find(id);
    const bool allowCarry = slideIt == kinematicSlide_.end() || slideIt->second;
    if (!allowCarry) {
        bodies.SetLinearAndAngularVelocity(bodyId, JPH::Vec3::sZero(), JPH::Vec3::sZero());
        bodies.SetPositionAndRotation(
            bodyId, targetPos, targetRot, JPH::EActivation::Activate);
        return;
    }

    float stepDt = dt;
    if (!(stepDt > 1.0e-4f) || !std::isfinite(stepDt)) {
        stepDt = kFixedDt;
    } else if (stepDt > 0.25f) {
        stepDt = 0.25f;
    }

    constexpr float kMaxLin = 200.0f;
    const float maxStep = kMaxLin * stepDt;
    if (!(posErrSq <= maxStep * maxStep) || !std::isfinite(posErrSq)) {
        bodies.SetLinearAndAngularVelocity(bodyId, JPH::Vec3::sZero(), JPH::Vec3::sZero());
        bodies.SetPositionAndRotation(
            bodyId, targetPos, targetRot, JPH::EActivation::Activate);
        return;
    }

    bodies.MoveKinematic(bodyId, targetPos, targetRot, stepDt);
    const JPH::Vec3 lin = bodies.GetLinearVelocity(bodyId);
    const JPH::Vec3 ang = bodies.GetAngularVelocity(bodyId);
    constexpr float kMaxAng = 50.0f;
    if (!std::isfinite(lin.LengthSq()) || !std::isfinite(ang.LengthSq()) ||
        lin.LengthSq() > kMaxLin * kMaxLin || ang.LengthSq() > kMaxAng * kMaxAng) {
        bodies.SetLinearAndAngularVelocity(bodyId, JPH::Vec3::sZero(), JPH::Vec3::sZero());
        bodies.SetPositionAndRotation(
            bodyId, targetPos, targetRot, JPH::EActivation::Activate);
    }
}

void PhysicsWorld::setKinematicSlide(std::uint64_t id, bool slide) {
    if (kinematicBodies_.find(id) == kinematicBodies_.end()) {
        return;
    }
    kinematicSlide_[id] = slide;
}

void PhysicsWorld::destroyKinematic(std::uint64_t id) {
    if (!system_) {
        return;
    }
    auto it = kinematicBodies_.find(id);
    if (it == kinematicBodies_.end()) {
        return;
    }
    JPH::BodyInterface& bodies = system_->GetBodyInterface();
    kinematicBodyToEntity_.erase(it->second.GetIndexAndSequenceNumber());
    kinematicSlide_.erase(id);
    bodies.RemoveBody(it->second);
    bodies.DestroyBody(it->second);
    kinematicBodies_.erase(it);
}

void PhysicsWorld::clearKinematics() {
    if (!system_) {
        return;
    }
    JPH::BodyInterface& bodies = system_->GetBodyInterface();
    for (const auto& [entityId, bodyId] : kinematicBodies_) {
        (void)entityId;
        bodies.RemoveBody(bodyId);
        bodies.DestroyBody(bodyId);
    }
    kinematicBodies_.clear();
    kinematicBodyToEntity_.clear();
    kinematicSlide_.clear();
}

bool PhysicsWorld::hasKinematic(std::uint64_t id) const {
    return kinematicBodies_.find(id) != kinematicBodies_.end();
}

bool PhysicsWorld::kinematicAllowsSlide(JPH::BodyID bodyId) const {
    if (bodyId.IsInvalid()) {
        return true;
    }
    auto entityIt = kinematicBodyToEntity_.find(bodyId.GetIndexAndSequenceNumber());
    if (entityIt == kinematicBodyToEntity_.end()) {
        return true;
    }
    auto slideIt = kinematicSlide_.find(entityIt->second);
    if (slideIt == kinematicSlide_.end()) {
        return true;
    }
    return slideIt->second;
}

bool PhysicsWorld::tryGetKinematicAabb(std::uint64_t id, Vector3& outMin, Vector3& outMax) const {
    if (!system_) {
        return false;
    }
    auto it = kinematicBodies_.find(id);
    if (it == kinematicBodies_.end()) {
        return false;
    }
    const JPH::TransformedShape shape = system_->GetBodyInterface().GetTransformedShape(it->second);
    const JPH::AABox bounds = shape.GetWorldSpaceBounds();
    outMin = {
        bounds.mMin.GetX(),
        bounds.mMin.GetY(),
        bounds.mMin.GetZ(),
    };
    outMax = {
        bounds.mMax.GetX(),
        bounds.mMax.GetY(),
        bounds.mMax.GetZ(),
    };
    return true;
}

void PhysicsWorld::createCharacter(
    std::uint64_t id,
    float x,
    float y,
    float z,
    const CharacterMotor& motor) {
    characters_.erase(id);

    const float radius = characterRadius(motor);
    CharacterEntry entry{};
    entry.shape = makeCharacterShape(motor);

    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = entry.shape;
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
    settings->mMass = 70.0f;

    entry.character = new JPH::CharacterVirtual(
        settings,
        JPH::RVec3(x, y, z),
        JPH::Quat::sIdentity(),
        0,
        system_.get());

    characters_[id] = std::move(entry);
    TraceLog(
        LOG_INFO,
        "PHYSICS: character %llu at (%.2f, %.2f, %.2f) hull=%s move=%s",
        static_cast<unsigned long long>(id),
        x,
        y,
        z,
        motor.hull == CharacterHull::Box     ? "box"
        : motor.hull == CharacterHull::Sphere ? "sphere"
                                              : "capsule",
        motor.moveMode == CharacterMoveMode::TryMove ? "try-move"
        : motor.moveMode == CharacterMoveMode::Fly   ? "fly"
                                                     : "slide");
}

bool PhysicsWorld::resizeCharacter(std::uint64_t id, const CharacterMotor& motor, float maxPenetrationDepth) {
    const auto it = characters_.find(id);
    if (it == characters_.end() || it->second.character == nullptr) {
        return false;
    }

    const JPH::RefConst<JPH::Shape> newShape = makeCharacterShape(motor);
    if (newShape == nullptr) {
        return false;
    }

    const auto& broadPhaseFilter = system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
    const auto& objectFilter = system_->GetDefaultLayerFilter(Layers::MOVING);
    const std::uint8_t blockMask = id == playerId_ ? BrushBlock::Player : BrushBlock::Actor;
    JPH::IgnoreMultipleBodiesFilter bodyFilter;
    populateBrushBlockIgnoreFilter(bodyFilter, staticBodies_, staticBodyBlocks_, blockMask);
    const JPH::ShapeFilter shapeFilter{};

    JPH::CharacterVirtual& character = *it->second.character;
    if (!character.SetShape(
            newShape,
            maxPenetrationDepth,
            broadPhaseFilter,
            objectFilter,
            bodyFilter,
            shapeFilter,
            *tempAllocator_)) {
        return false;
    }

    character.SetSupportingVolume(JPH::Plane(JPH::Vec3::sAxisY(), -characterRadius(motor)));
    it->second.shape = newShape;
    return true;
}

void PhysicsWorld::destroyCharacter(std::uint64_t id) {
    characters_.erase(id);
    if (playerId_ == id) {
        playerId_ = 0;
    }
}

void PhysicsWorld::destroyAllCharacters() {
    characters_.clear();
    playerId_ = 0;
}

void PhysicsWorld::createPlayerCharacter(float x, float y, float z, const CharacterMotor& motor) {
    if (playerId_ == 0) {
        TraceLog(LOG_WARNING, "PHYSICS: createPlayerCharacter without player id");
        return;
    }
    createCharacter(playerId_, x, y, z, motor);
}

void PhysicsWorld::destroyPlayerCharacter() {
    if (playerId_ != 0) {
        destroyCharacter(playerId_);
    } else {
        destroyAllCharacters();
    }
}

void PhysicsWorld::applyCharacterInput(
    JPH::CharacterVirtual& character,
    const CharacterMotor& motor,
    float wishX,
    float wishZ,
    float dt,
    bool noclip) {
    JPH::Vec3 wish(wishX, 0.0f, wishZ);
    if (wish.LengthSq() > 1.0e-6f) {
        wish = wish.Normalized() * motor.moveSpeed;
    } else {
        wish = JPH::Vec3::sZero();
    }

    if (noclip) {
        character.SetLinearVelocity(wish);
        return;
    }

    const JPH::Vec3 currentVertical =
        character.GetLinearVelocity().Dot(character.GetUp()) * character.GetUp();
    const JPH::Vec3 groundVelocity = character.GetGroundVelocity();
    JPH::Vec3 newVelocity;

    const bool allowSlide = kinematicAllowsSlide(character.GetGroundBodyID());
    const bool movingTowardsGround = (currentVertical.GetY() - groundVelocity.GetY()) < 0.1f;
    if (character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround &&
        movingTowardsGround &&
        allowSlide) {
        newVelocity = groundVelocity;
    } else {
        newVelocity = currentVertical;
    }

    //newVelocity += JPH::Vec3(0.0f, -motor.gravity, 0.0f) * dt;
    if (character.GetGroundState() != JPH::CharacterVirtual::EGroundState::OnGround) {
        newVelocity += JPH::Vec3(0.0f, -motor.gravity, 0.0f) * dt;
    }
    newVelocity += wish;
    character.SetLinearVelocity(newVelocity);
}

void PhysicsWorld::stepCharacterTryMove(
    JPH::CharacterVirtual& character,
    const CharacterMotor& motor,
    std::uint64_t characterId) {
    applyCharacterInput(character, motor, motor.wishX, motor.wishZ, kFixedDt, false);

    const auto& broadPhaseFilter = system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
    const auto& objectFilter = system_->GetDefaultLayerFilter(Layers::MOVING);
    const std::uint8_t blockMask =
        characterId == playerId_ ? BrushBlock::Player : BrushBlock::Actor;
    JPH::IgnoreMultipleBodiesFilter bodyFilter;
    populateBrushBlockIgnoreFilter(bodyFilter, staticBodies_, staticBodyBlocks_, blockMask);
    const JPH::ShapeFilter shapeFilter{};
    const JPH::Vec3 gravity = -character.GetUp() * system_->GetGravity().Length();
    const JPH::Vec3 up = character.GetUp();
    const JPH::Vec3 stickDown = -up * 0.5f;
    const float stepHeight = motor.stepHeight > 0.0f ? motor.stepHeight : 0.4f;

    JPH::Vec3 wish(motor.wishX, 0.0f, motor.wishZ);
    const bool hasWish = wish.LengthSq() > 1.0e-6f;
    if (hasWish) {
        wish = wish.Normalized() * motor.moveSpeed;
    }

    const JPH::Vec3 fullVel = character.GetLinearVelocity();
    character.SetLinearVelocity(fullVel.Dot(up) * up);
    character.Update(
        kFixedDt,
        gravity,
        broadPhaseFilter,
        objectFilter,
        bodyFilter,
        shapeFilter,
        *tempAllocator_);
    const JPH::RVec3 afterVert = character.GetPosition();

    if (!hasWish) {
        character.StickToFloor(
            stickDown, broadPhaseFilter, objectFilter, bodyFilter, shapeFilter, *tempAllocator_);
        return;
    }

    character.SetLinearVelocity(wish);
    character.Update(
        kFixedDt,
        gravity,
        broadPhaseFilter,
        objectFilter,
        bodyFilter,
        shapeFilter,
        *tempAllocator_);
    const JPH::RVec3 afterMove = character.GetPosition();

    JPH::Vec3 delta = JPH::Vec3(afterMove - afterVert);
    delta -= delta.Dot(up) * up;
    const JPH::Vec3 wishDir = wish.Normalized();
    const float forward = delta.Dot(wishDir);
    const float desired = motor.moveSpeed * kFixedDt;
    constexpr float kAcceptFraction = 0.92f;
    if (forward + 1.0e-4f >= desired * kAcceptFraction) {
        character.StickToFloor(
            stickDown, broadPhaseFilter, objectFilter, bodyFilter, shapeFilter, *tempAllocator_);
        return;
    }

    character.SetPosition(JPH::RVec3(afterVert.GetX(), afterMove.GetY(), afterVert.GetZ()));
    character.SetLinearVelocity(wish);

    if (character.CanWalkStairs(wish)) {
        const JPH::Vec3 stepUp = up * stepHeight;
        const float forwardLen = desired > 0.02f ? desired : 0.02f;
        const JPH::Vec3 stepForward = wishDir * forwardLen;
        const JPH::Vec3 stepForwardTest = wishDir * 0.15f;
        character.WalkStairs(
            kFixedDt,
            stepUp,
            stepForward,
            stepForwardTest,
            JPH::Vec3::sZero(),
            broadPhaseFilter,
            objectFilter,
            bodyFilter,
            shapeFilter,
            *tempAllocator_);
    }

    character.StickToFloor(
        stickDown, broadPhaseFilter, objectFilter, bodyFilter, shapeFilter, *tempAllocator_);
    const JPH::Vec3 settled = character.GetLinearVelocity();
    character.SetLinearVelocity(settled.Dot(up) * up);
}

void PhysicsWorld::stepCharacter(
    JPH::CharacterVirtual& character,
    const CharacterMotor& motor,
    bool noclip,
    std::uint64_t characterId,
    float submersion,
    bool nearWaterSurface) {
    if (noclip) {
        applyCharacterInput(character, motor, motor.wishX, motor.wishZ, kFixedDt, true);
        const JPH::RVec3 pos = character.GetPosition();
        const JPH::Vec3 vel = character.GetLinearVelocity();
        character.SetPosition(JPH::RVec3(
            pos.GetX() + static_cast<double>(vel.GetX()) * static_cast<double>(kFixedDt),
            pos.GetY(),
            pos.GetZ() + static_cast<double>(vel.GetZ()) * static_cast<double>(kFixedDt)));
        return;
    }

    if (motor.moveMode == CharacterMoveMode::Fly) {
        stepCharacterFlight(character, motor, characterId);
        return;
    }

    // Take over with buoyant physics for any wetness at all, regardless of whether the character
    // happens to be touching a floor. Deferring to the normal walk path while grounded (an earlier
    // version of this gate did `&& !character.IsSupported()`) traps a character that swims down to
    // a pool's floor: there is no jump mechanic in this engine, wishY/ascend is only ever read by
    // applyBuoyantInput below, so once grounded there was no way to push back off the bottom.
    if (submersion > 0.0f) {
        stepCharacterSwim(character, motor, characterId, submersion, nearWaterSurface);
        return;
    }

    if (motor.moveMode == CharacterMoveMode::TryMove) {
        stepCharacterTryMove(character, motor, characterId);
        return;
    }

    applyCharacterInput(character, motor, motor.wishX, motor.wishZ, kFixedDt, false);

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    const float stepHeight = motor.stepHeight > 0.0f ? motor.stepHeight : 0.4f;
    updateSettings.mWalkStairsStepUp = character.GetUp() * stepHeight;
    const std::uint8_t blockMask =
        characterId == playerId_ ? BrushBlock::Player : BrushBlock::Actor;
    JPH::IgnoreMultipleBodiesFilter bodyFilter;
    populateBrushBlockIgnoreFilter(bodyFilter, staticBodies_, staticBodyBlocks_, blockMask);

    character.ExtendedUpdate(
        kFixedDt,
        -character.GetUp() * system_->GetGravity().Length(),
        updateSettings,
        system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        system_->GetDefaultLayerFilter(Layers::MOVING),
        bodyFilter,
        {},
        *tempAllocator_);
}

void PhysicsWorld::applyFlightInput(
    JPH::CharacterVirtual& character,
    const CharacterMotor& motor,
    float dt) {
    JPH::Vec3 horiz(motor.wishX, 0.0f, motor.wishZ);
    if (horiz.LengthSq() > 1.0e-6f) {
        horiz = horiz.Normalized() * motor.moveSpeed;
    } else {
        horiz = JPH::Vec3::sZero();
    }

    float vert = 0.0f;
    if (std::fabs(motor.wishY) > 1.0e-6f) {
        vert = (motor.wishY > 0.0f ? 1.0f : -1.0f) * motor.verticalSpeed;
    }

    JPH::Vec3 vel(horiz.GetX(), vert, horiz.GetZ());
    vel += JPH::Vec3(0.0f, -motor.gravity, 0.0f) * dt;
    character.SetLinearVelocity(vel);
}

void PhysicsWorld::stepCharacterFlight(
    JPH::CharacterVirtual& character,
    const CharacterMotor& motor,
    std::uint64_t characterId) {
    applyFlightInput(character, motor, kFixedDt);

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mWalkStairsStepUp = JPH::Vec3::sZero();
    const std::uint8_t blockMask =
        characterId == playerId_ ? BrushBlock::Player : BrushBlock::Actor;
    JPH::IgnoreMultipleBodiesFilter bodyFilter;
    populateBrushBlockIgnoreFilter(bodyFilter, staticBodies_, staticBodyBlocks_, blockMask);
    character.ExtendedUpdate(
        kFixedDt,
        -character.GetUp() * system_->GetGravity().Length(),
        updateSettings,
        system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        system_->GetDefaultLayerFilter(Layers::MOVING),
        bodyFilter,
        {},
        *tempAllocator_);
}

void PhysicsWorld::applyBuoyantInput(
    JPH::CharacterVirtual& character,
    const CharacterMotor& motor,
    float dt,
    float submersion) {
    JPH::Vec3 horiz(motor.wishX, 0.0f, motor.wishZ);
    if (horiz.LengthSq() > 1.0e-6f) {
        horiz = horiz.Normalized() * motor.swimSpeed;
    } else {
        horiz = JPH::Vec3::sZero();
    }

    float vertical = character.GetLinearVelocity().Dot(character.GetUp());

    // Buoyancy vs. gravity, blended by how submerged the character is: fully submerged drifts
    // to the surface on its own, partially submerged still feels most of its normal weight.
    vertical += (motor.buoyancy * submersion - motor.gravity * (1.0f - submersion)) * dt;

    // Water drag damps existing vertical momentum each step instead of Slide's hard ground-stick.
    // Applied after the accel so a fresh fall/rise impulse isn't chopped down before it acts.
    vertical *= std::exp(-motor.waterDrag * dt);

    // Player-driven ascend/descend stroke.
    vertical += motor.wishY * motor.swimSpeed * dt;

    character.SetLinearVelocity(JPH::Vec3(horiz.GetX(), vertical, horiz.GetZ()));
}

// Water-exit assistance: a purpose-built ledge probe, not a reuse of stair-step logic. It has its
// own reach/speed tunables (motor.waterExitReach, motor.waterExitSpeed) instead of motor.stepHeight,
// it only ever runs when the caller has already confirmed (via a fine-grained BSP surface scan in
// physics_module.cpp) that a water surface exists within waterExitReach of the character's body --
// so it can never fire arbitrarily deep underwater -- and it steers the character toward a confirmed
// ledge at a capped speed instead of teleporting there, so repeated ticks against the same wall
// cannot compound into an unbounded climb rate.
bool PhysicsWorld::tryWaterExitAssist(
    JPH::CharacterVirtual& character,
    const CharacterMotor& motor,
    std::uint64_t characterId) {
    const JPH::Vec3 wish(motor.wishX, 0.0f, motor.wishZ);
    if (wish.LengthSq() < 1.0e-6f) {
        return false;
    }
    const JPH::Vec3 wishDir = wish.Normalized();

    const auto& broadPhaseFilter = system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
    const auto& objectFilter = system_->GetDefaultLayerFilter(Layers::MOVING);
    const std::uint8_t blockMask =
        characterId == playerId_ ? BrushBlock::Player : BrushBlock::Actor;
    JPH::IgnoreMultipleBodiesFilter bodyFilter;
    populateBrushBlockIgnoreFilter(bodyFilter, staticBodies_, staticBodyBlocks_, blockMask);
    const JPH::Vec3 up = character.GetUp();
    const float reach = motor.waterExitReach > 0.0f ? motor.waterExitReach : 0.5f;

    const JPH::Shape* shape = character.GetShape();
    JPH::ShapeCastSettings castSettings;
    castSettings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
    castSettings.mBackFaceModeConvex = JPH::EBackFaceMode::CollideWithBackFaces;

    auto sweep = [&](JPH::RVec3 from, JPH::Vec3 delta) -> float {
        const JPH::RMat44 start = JPH::RMat44::sTranslation(from);
        const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(shape, JPH::Vec3::sOne(), start, delta);
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        system_->GetNarrowPhaseQuery().CastShape(
            cast,
            castSettings,
            cast.mCenterOfMassStart.GetTranslation(),
            collector,
            broadPhaseFilter,
            objectFilter,
            bodyFilter);
        return collector.HadHit() ? collector.mHit.mFraction : 1.0f;
    };

    // Cheap probe at the character's current height: is the wish direction actually blocked by
    // something solid nearby? Without this, open water above a merely-shallow floor would read as a
    // climbable ledge the moment the up/forward/down probe below happened to find that floor within
    // reach -- this is what tells "swimming into a wall" apart from "swimming near the bottom".
    constexpr float kBlockedProbeDist = 0.4f;
    const JPH::RVec3 currentPos = character.GetPosition();
    const float blockedFraction = sweep(currentPos, wishDir * kBlockedProbeDist);
    if (blockedFraction > 0.9f) {
        return false;
    }

    const float upFraction = sweep(currentPos, up * reach);
    if (upFraction < 1.0e-3f) {
        return false; // Can't even begin to rise (e.g. a low ceiling) -- no ledge to take.
    }
    const JPH::RVec3 raisedPos = currentPos + up * (reach * upFraction);

    const float forwardFraction = sweep(raisedPos, wishDir * reach);
    if (forwardFraction < 1.0e-3f) {
        return false; // Still blocked at the raised height -- the wall is taller than our reach.
    }
    const JPH::RVec3 advancedPos = raisedPos + wishDir * (reach * forwardFraction);

    const float downFraction = sweep(advancedPos, -up * reach);
    if (downFraction >= 1.0f) {
        return false; // No floor within reach on the far side -- would be climbing into open air.
    }
    const JPH::RVec3 landingPos = advancedPos - up * (reach * downFraction);

    const JPH::Vec3 toLanding = JPH::Vec3(landingPos - currentPos);
    const float distance = toLanding.Length();
    const float exitSpeed = motor.waterExitSpeed > 0.0f ? motor.waterExitSpeed : 3.0f;
    constexpr float kArriveDistance = 0.03f;
    if (distance <= kArriveDistance) {
        character.SetPosition(landingPos);
        character.SetLinearVelocity(JPH::Vec3::sZero());
        return true;
    }
    const float speed = std::min(exitSpeed, distance / kFixedDt);
    character.SetLinearVelocity((toLanding / distance) * speed);
    return true;
}

void PhysicsWorld::stepCharacterSwim(
    JPH::CharacterVirtual& character,
    const CharacterMotor& motor,
    std::uint64_t characterId,
    float submersion,
    bool nearWaterSurface) {
    // A floating character is never IsSupported(), so Jolt's automatic stair-step (ExtendedUpdate's
    // CanWalkStairs check) can never fire for it -- that would leave no way out of a pool bounded by
    // a vertical wall/lip. tryWaterExitAssist covers that case directly; when it takes over (a ledge
    // was actually found within reach), it owns this tick's velocity, so the normal buoyant velocity
    // is skipped rather than immediately overwriting the steer.
    const bool exiting = nearWaterSurface && tryWaterExitAssist(character, motor, characterId);
    if (!exiting) {
        applyBuoyantInput(character, motor, kFixedDt, submersion);
    }

    const auto& broadPhaseFilter = system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
    const auto& objectFilter = system_->GetDefaultLayerFilter(Layers::MOVING);
    const std::uint8_t blockMask =
        characterId == playerId_ ? BrushBlock::Player : BrushBlock::Actor;
    JPH::IgnoreMultipleBodiesFilter bodyFilter;
    populateBrushBlockIgnoreFilter(bodyFilter, staticBodies_, staticBodyBlocks_, blockMask);
    const JPH::ShapeFilter shapeFilter{};
    const JPH::Vec3 gravity = -character.GetUp() * system_->GetGravity().Length();

    character.Update(
        kFixedDt, gravity, broadPhaseFilter, objectFilter, bodyFilter, shapeFilter, *tempAllocator_);
}

namespace {

constexpr float kSoftSepRadiusSlack = 1.02f;
constexpr float kSoftSepPushGain = 2.5f;
constexpr float kSoftSepPushMax = 1.5f;
constexpr float kSoftSepPosCorr = 0.85f;
constexpr float kSoftSepEps = 1.0e-4f;
constexpr float kSoftSepMinCell = 0.5f;

void removeInwardWish(float& wishX, float& wishZ, float nx, float nz) {
    const float inward = wishX * nx + wishZ * nz;
    if (inward > 0.0f) {
        wishX -= inward * nx;
        wishZ -= inward * nz;
    }
}

void removeInwardWish3D(float& wishX, float& wishY, float& wishZ, float nx, float ny, float nz) {
    const float inward = wishX * nx + wishY * ny + wishZ * nz;
    if (inward > 0.0f) {
        wishX -= inward * nx;
        wishY -= inward * ny;
        wishZ -= inward * nz;
    }
}

JPH::Vec3 wishVelocity(const CharacterMotor& motor) {
    JPH::Vec3 wish(motor.wishX, 0.0f, motor.wishZ);
    if (wish.LengthSq() > 1.0e-6f) {
        return wish.Normalized() * motor.moveSpeed;
    }
    return JPH::Vec3::sZero();
}

JPH::Vec3 wishVelocityFly(const CharacterMotor& motor) {
    JPH::Vec3 horiz(motor.wishX, 0.0f, motor.wishZ);
    if (horiz.LengthSq() > 1.0e-6f) {
        horiz = horiz.Normalized() * motor.moveSpeed;
    } else {
        horiz = JPH::Vec3::sZero();
    }

    float vert = 0.0f;
    if (std::fabs(motor.wishY) > 1.0e-6f) {
        vert = (motor.wishY > 0.0f ? 1.0f : -1.0f) * motor.verticalSpeed;
    }
    return JPH::Vec3(horiz.GetX(), vert, horiz.GetZ());
}

bool usesFlySeparation(const CharacterMotor& motorA, const CharacterMotor& motorB) {
    return motorA.moveMode == CharacterMoveMode::Fly || motorB.moveMode == CharacterMoveMode::Fly ||
        motorA.hull == CharacterHull::Sphere || motorB.hull == CharacterHull::Sphere;
}

struct SoftSepBody {
    std::uint64_t id = 0;
    CharacterMotor* motor = nullptr;
    JPH::CharacterVirtual* character = nullptr;
};

void resolveSoftSeparationPairFly(
    SoftSepBody& a,
    SoftSepBody& b,
    float dt) {
    CharacterMotor& motorA = *a.motor;
    CharacterMotor& motorB = *b.motor;
    JPH::CharacterVirtual& charA = *a.character;
    JPH::CharacterVirtual& charB = *b.character;

    const JPH::RVec3 feetA = charA.GetPosition();
    const JPH::RVec3 feetB = charB.GetPosition();
    const float ax = static_cast<float>(feetA.GetX());
    const float ay = static_cast<float>(feetA.GetY());
    const float az = static_cast<float>(feetA.GetZ());
    const float bx = static_cast<float>(feetB.GetX());
    const float by = static_cast<float>(feetB.GetY());
    const float bz = static_cast<float>(feetB.GetZ());

    const float centerOffsetA = characterCenterOffset(motorA);
    const float centerOffsetB = characterCenterOffset(motorB);
    const float centerAx = ax;
    const float centerAy = ay + centerOffsetA;
    const float centerAz = az;
    const float centerBx = bx;
    const float centerBy = by + centerOffsetB;
    const float centerBz = bz;

    const float minDist =
        kSoftSepRadiusSlack * (characterRadius(motorA) + characterRadius(motorB));
    if (minDist <= kSoftSepEps) {
        return;
    }

    const float dx = centerAx - centerBx;
    const float dy = centerAy - centerBy;
    const float dz = centerAz - centerBz;
    const float distSq = dx * dx + dy * dy + dz * dz;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    float dist = 0.0f;
    if (distSq > kSoftSepEps * kSoftSepEps) {
        dist = std::sqrt(distSq);
        nx = dx / dist;
        ny = dy / dist;
        nz = dz / dist;
    } else {
        const std::uint64_t mix = a.id ^ (b.id << 1);
        const float angle = static_cast<float>(mix & 0xFFFFu) * (6.2831853f / 65536.0f);
        nx = std::cos(angle);
        ny = 0.35f;
        nz = std::sin(angle);
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > kSoftSepEps) {
            nx /= len;
            ny /= len;
            nz /= len;
        }
    }

    const JPH::Vec3 wishVelA = wishVelocityFly(motorA);
    const JPH::Vec3 wishVelB = wishVelocityFly(motorB);
    const float predDx = (centerAx + wishVelA.GetX() * dt) - (centerBx + wishVelB.GetX() * dt);
    const float predDy = (centerAy + wishVelA.GetY() * dt) - (centerBy + wishVelB.GetY() * dt);
    const float predDz = (centerAz + wishVelA.GetZ() * dt) - (centerBz + wishVelB.GetZ() * dt);
    const float predDistSq = predDx * predDx + predDy * predDy + predDz * predDz;
    const bool overlapping = dist < minDist;
    const bool wouldOverlap = predDistSq < minDist * minDist;
    if (!overlapping && !wouldOverlap) {
        return;
    }

    removeInwardWish3D(motorA.wishX, motorA.wishY, motorA.wishZ, -nx, -ny, -nz);
    removeInwardWish3D(motorB.wishX, motorB.wishY, motorB.wishZ, nx, ny, nz);

    if (!overlapping) {
        return;
    }

    const float penetration = minDist - dist;
    float push = kSoftSepPushGain * penetration;
    if (push > kSoftSepPushMax) {
        push = kSoftSepPushMax;
    }
    motorA.wishX += nx * push;
    motorA.wishY += ny * push;
    motorA.wishZ += nz * push;
    motorB.wishX -= nx * push;
    motorB.wishY -= ny * push;
    motorB.wishZ -= nz * push;

    const float corr = 0.5f * kSoftSepPosCorr * penetration;
    if (corr > kSoftSepEps) {
        charA.SetPosition(JPH::RVec3(
            feetA.GetX() + static_cast<double>(nx * corr),
            feetA.GetY() + static_cast<double>(ny * corr),
            feetA.GetZ() + static_cast<double>(nz * corr)));
        charB.SetPosition(JPH::RVec3(
            feetB.GetX() - static_cast<double>(nx * corr),
            feetB.GetY() - static_cast<double>(ny * corr),
            feetB.GetZ() - static_cast<double>(nz * corr)));
    }
}

std::uint64_t softSepCellKey(int cellX, int cellZ) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cellX)) << 32) |
        static_cast<std::uint32_t>(cellZ);
}

void resolveSoftSeparationPair(
    SoftSepBody& a,
    SoftSepBody& b,
    float dt) {
    if (usesFlySeparation(*a.motor, *b.motor)) {
        resolveSoftSeparationPairFly(a, b, dt);
        return;
    }

    CharacterMotor& motorA = *a.motor;
    CharacterMotor& motorB = *b.motor;
    JPH::CharacterVirtual& charA = *a.character;
    JPH::CharacterVirtual& charB = *b.character;

    const JPH::RVec3 feetA = charA.GetPosition();
    const JPH::RVec3 feetB = charB.GetPosition();
    const float ax = static_cast<float>(feetA.GetX());
    const float ay = static_cast<float>(feetA.GetY());
    const float az = static_cast<float>(feetA.GetZ());
    const float bx = static_cast<float>(feetB.GetX());
    const float by = static_cast<float>(feetB.GetY());
    const float bz = static_cast<float>(feetB.GetZ());

    const float dy = ay - by;
    const float maxDy = 0.5f * (motorA.height + motorB.height) + motorA.radius + motorB.radius;
    if (std::fabs(dy) > maxDy) {
        return;
    }

    const float minDist = kSoftSepRadiusSlack * (motorA.radius + motorB.radius);
    if (minDist <= kSoftSepEps) {
        return;
    }

    const float dx = ax - bx;
    const float dz = az - bz;
    const float distSq = dx * dx + dz * dz;
    float nx = 0.0f;
    float nz = 1.0f;
    float dist = 0.0f;
    if (distSq > kSoftSepEps * kSoftSepEps) {
        dist = std::sqrt(distSq);
        nx = dx / dist;
        nz = dz / dist;
    }

    const JPH::Vec3 wishVelA = wishVelocity(motorA);
    const JPH::Vec3 wishVelB = wishVelocity(motorB);
    const float predDx = (ax + wishVelA.GetX() * dt) - (bx + wishVelB.GetX() * dt);
    const float predDz = (az + wishVelA.GetZ() * dt) - (bz + wishVelB.GetZ() * dt);
    const float predDistSq = predDx * predDx + predDz * predDz;
    const bool overlapping = dist < minDist;
    const bool wouldOverlap = predDistSq < minDist * minDist;
    if (!overlapping && !wouldOverlap) {
        return;
    }

    removeInwardWish(motorA.wishX, motorA.wishZ, -nx, -nz);
    removeInwardWish(motorB.wishX, motorB.wishZ, nx, nz);

    if (!overlapping) {
        return;
    }

    const float penetration = minDist - dist;
    float push = kSoftSepPushGain * penetration;
    if (push > kSoftSepPushMax) {
        push = kSoftSepPushMax;
    }
    motorA.wishX += nx * push;
    motorA.wishZ += nz * push;
    motorB.wishX -= nx * push;
    motorB.wishZ -= nz * push;

    const float corr = 0.5f * kSoftSepPosCorr * penetration;
    if (corr > kSoftSepEps) {
        charA.SetPosition(JPH::RVec3(
            feetA.GetX() + static_cast<double>(nx * corr),
            feetA.GetY(),
            feetA.GetZ() + static_cast<double>(nz * corr)));
        charB.SetPosition(JPH::RVec3(
            feetB.GetX() - static_cast<double>(nx * corr),
            feetB.GetY(),
            feetB.GetZ() - static_cast<double>(nz * corr)));
    }
}

} // namespace

void PhysicsWorld::applyCharacterSoftSeparation(const std::vector<CharacterStep>& steps) {
    std::vector<SoftSepBody> bodies;
    bodies.reserve(steps.size());
    float maxRadius = 0.0f;

    for (const CharacterStep& step : steps) {
        if (step.motor == nullptr || step.noclip) {
            continue;
        }
        auto it = characters_.find(step.id);
        if (it == characters_.end() || it->second.character == nullptr) {
            continue;
        }
        SoftSepBody body{};
        body.id = step.id;
        body.motor = step.motor;
        body.character = it->second.character.GetPtr();
        if (step.motor->radius > maxRadius) {
            maxRadius = step.motor->radius;
        }
        bodies.push_back(body);
    }

    if (bodies.size() < 2) {
        return;
    }

    float cellSize = 2.0f * maxRadius;
    if (cellSize < kSoftSepMinCell) {
        cellSize = kSoftSepMinCell;
    }
    const float invCell = 1.0f / cellSize;

    std::unordered_map<std::uint64_t, std::vector<std::size_t>> cells;
    cells.reserve(bodies.size() * 2);
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const JPH::RVec3 feet = bodies[i].character->GetPosition();
        const int cellX = static_cast<int>(std::floor(static_cast<float>(feet.GetX()) * invCell));
        const int cellZ = static_cast<int>(std::floor(static_cast<float>(feet.GetZ()) * invCell));
        cells[softSepCellKey(cellX, cellZ)].push_back(i);
    }

    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const JPH::RVec3 feet = bodies[i].character->GetPosition();
        const int cellX = static_cast<int>(std::floor(static_cast<float>(feet.GetX()) * invCell));
        const int cellZ = static_cast<int>(std::floor(static_cast<float>(feet.GetZ()) * invCell));
        for (int ox = -1; ox <= 1; ++ox) {
            for (int oz = -1; oz <= 1; ++oz) {
                const auto cellIt = cells.find(softSepCellKey(cellX + ox, cellZ + oz));
                if (cellIt == cells.end()) {
                    continue;
                }
                for (const std::size_t j : cellIt->second) {
                    if (bodies[i].id >= bodies[j].id) {
                        continue;
                    }
                    resolveSoftSeparationPair(bodies[i], bodies[j], kFixedDt);
                }
            }
        }
    }
}

void PhysicsWorld::update(float frameDt, const std::vector<CharacterStep>& steps) {
    if (characters_.empty() || steps.empty()) {
        return;
    }

    constexpr float kMaxFrameDt = 0.25f;
    if (frameDt > kMaxFrameDt) {
        frameDt = kMaxFrameDt;
    } else if (frameDt < 0.0f) {
        frameDt = 0.0f;
    }

    accumulator_ += frameDt;
    int count = 0;
    while (accumulator_ >= kFixedDt && count < kMaxSubsteps) {
        applyCharacterSoftSeparation(steps);

        for (const CharacterStep& step : steps) {
            if (step.motor == nullptr) {
                continue;
            }
            auto it = characters_.find(step.id);
            if (it == characters_.end() || it->second.character == nullptr) {
                continue;
            }
            stepCharacter(
                *it->second.character,
                *step.motor,
                step.noclip,
                step.id,
                step.submersion,
                step.nearWaterSurface);
        }

        system_->Update(kFixedDt, 1, tempAllocator_.get(), jobSystem_.get());

        accumulator_ -= kFixedDt;
        ++count;
    }
}

bool PhysicsWorld::hasCharacter(std::uint64_t id) const {
    return characters_.find(id) != characters_.end();
}

bool PhysicsWorld::hasPlayer() const {
    return playerId_ != 0 && hasCharacter(playerId_);
}

JPH::RVec3 PhysicsWorld::characterPosition(std::uint64_t id) const {
    auto it = characters_.find(id);
    if (it == characters_.end() || it->second.character == nullptr) {
        return JPH::RVec3::sZero();
    }
    return it->second.character->GetPosition();
}

JPH::Vec3 PhysicsWorld::characterVelocity(std::uint64_t id) const {
    auto it = characters_.find(id);
    if (it == characters_.end() || it->second.character == nullptr) {
        return JPH::Vec3::sZero();
    }
    return it->second.character->GetLinearVelocity();
}

bool PhysicsWorld::characterSupported(std::uint64_t id) const {
    auto it = characters_.find(id);
    return it != characters_.end() && it->second.character != nullptr &&
        it->second.character->IsSupported();
}

JPH::RVec3 PhysicsWorld::playerPosition() const {
    return characterPosition(playerId_);
}

void PhysicsWorld::setPlayerPosition(float x, float y, float z) {
    setCharacterPosition(playerId_, x, y, z);
}

void PhysicsWorld::setCharacterPosition(std::uint64_t id, float x, float y, float z) {
    auto it = characters_.find(id);
    if (it == characters_.end() || it->second.character == nullptr) {
        return;
    }
    it->second.character->SetPosition(JPH::RVec3(x, y, z));
    it->second.character->SetLinearVelocity(JPH::Vec3::sZero());
}

JPH::Vec3 PhysicsWorld::playerVelocity() const {
    return characterVelocity(playerId_);
}

bool PhysicsWorld::playerSupported() const {
    return characterSupported(playerId_);
}

std::optional<SphereCastHit> PhysicsWorld::castSphere(
    Vector3 origin,
    Vector3 direction,
    float distance,
    float radius,
    std::uint8_t blockMask) const {
    if (system_ == nullptr || distance <= 1.0e-8f || radius <= 0.0f) {
        return std::nullopt;
    }

    const float dirLenSq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (dirLenSq <= 1.0e-12f) {
        return std::nullopt;
    }
    const Vector3 unitDir = Vector3Normalize(direction);

    const JPH::SphereShape sphere(radius);
    const JPH::RMat44 start = JPH::RMat44::sTranslation(JPH::RVec3(origin.x, origin.y, origin.z));
    const JPH::Vec3 castDir(unitDir.x * distance, unitDir.y * distance, unitDir.z * distance);
    const JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(&sphere, JPH::Vec3::sOne(), start, castDir);

    JPH::ShapeCastSettings settings;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mBackFaceModeConvex = JPH::EBackFaceMode::CollideWithBackFaces;

    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    JPH::IgnoreMultipleBodiesFilter bodyFilter;
    populateBrushBlockIgnoreFilter(bodyFilter, staticBodies_, staticBodyBlocks_, blockMask);
    system_->GetNarrowPhaseQuery().CastShape(
        shapeCast,
        settings,
        shapeCast.mCenterOfMassStart.GetTranslation(),
        collector,
        system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        system_->GetDefaultLayerFilter(Layers::MOVING),
        bodyFilter);

    if (!collector.HadHit()) {
        return std::nullopt;
    }

    const JPH::ShapeCastResult& hit = collector.mHit;
    const JPH::RVec3 baseOffset = shapeCast.mCenterOfMassStart.GetTranslation();
    const JPH::RVec3 surfaceOnHit = baseOffset + hit.mContactPointOn2;
    JPH::Vec3 normal = -hit.mPenetrationAxis;
    if (normal.LengthSq() > 1.0e-12f) {
        normal = normal.Normalized();
    } else {
        normal = JPH::Vec3(-unitDir.x, -unitDir.y, -unitDir.z);
    }

    SphereCastHit result{};
    result.fraction = hit.mFraction;
    result.point = {
        static_cast<float>(surfaceOnHit.GetX()),
        static_cast<float>(surfaceOnHit.GetY()),
        static_cast<float>(surfaceOnHit.GetZ()),
    };
    result.normal = {normal.GetX(), normal.GetY(), normal.GetZ()};
    return result;
}

std::optional<RayCastHit> PhysicsWorld::castRay(
    Vector3 origin,
    Vector3 direction,
    float distance,
    std::uint8_t blockMask) const {
    if (system_ == nullptr || distance <= 1.0e-8f) {
        return std::nullopt;
    }

    const float dirLenSq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (dirLenSq <= 1.0e-12f) {
        return std::nullopt;
    }
    const Vector3 unitDir = Vector3Normalize(direction);

    const JPH::RRayCast ray(
        JPH::RVec3(origin.x, origin.y, origin.z),
        JPH::Vec3(unitDir.x * distance, unitDir.y * distance, unitDir.z * distance));

    JPH::RayCastSettings settings;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mBackFaceModeConvex = JPH::EBackFaceMode::CollideWithBackFaces;

    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
    JPH::IgnoreMultipleBodiesFilter bodyFilter;
    populateBrushBlockIgnoreFilter(bodyFilter, staticBodies_, staticBodyBlocks_, blockMask);
    system_->GetNarrowPhaseQuery().CastRay(
        ray,
        settings,
        collector,
        system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        system_->GetDefaultLayerFilter(Layers::MOVING),
        bodyFilter);

    if (!collector.HadHit()) {
        return std::nullopt;
    }

    const JPH::RayCastResult& hit = collector.mHit;
    const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);

    RayCastHit result{};
    result.fraction = hit.mFraction;
    result.point = {
        static_cast<float>(point.GetX()),
        static_cast<float>(point.GetY()),
        static_cast<float>(point.GetZ()),
    };
    result.normal = {0.0f, 1.0f, 0.0f};
    return result;
}

}
