#include "physics/physics_world.hpp"

#include "map/brush.hpp"
#include "physics/components.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
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
    int skippedNocollide = 0;

    for (const Brush& brush : brushes) {
        if (brush.nocollide) {
            ++skippedNocollide;
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
    }

    system_->OptimizeBroadPhase();
    TraceLog(
        LOG_INFO,
        "PHYSICS: added %d static brush bodies (box=%d hull=%d skipped %d nocollide)",
        static_cast<int>(staticBodies_.size()),
        boxCount,
        hullShapeCount,
        skippedNocollide);
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

} // namespace

void PhysicsWorld::createKinematicBox(
    std::uint64_t id,
    Vector3 center,
    Vector3 halfExtents,
    Quaternion rotation) {
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

void PhysicsWorld::destroyKinematic(std::uint64_t id) {
    if (!system_) {
        return;
    }
    auto it = kinematicBodies_.find(id);
    if (it == kinematicBodies_.end()) {
        return;
    }
    JPH::BodyInterface& bodies = system_->GetBodyInterface();
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
}

bool PhysicsWorld::hasKinematic(std::uint64_t id) const {
    return kinematicBodies_.find(id) != kinematicBodies_.end();
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

    const float radius = motor.radius > 0.0f ? motor.radius : 0.3f;
    const float height = motor.height > 0.0f ? motor.height : 1.1f;
    const float cylinderHalf = 0.5f * height;
    const float halfY = cylinderHalf + radius;
    CharacterEntry entry{};
    if (motor.hull == CharacterHull::Box) {
        entry.shape = JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0.0f, halfY, 0.0f),
            JPH::Quat::sIdentity(),
            new JPH::BoxShape(JPH::Vec3(radius, halfY, radius))).Create().Get();
    } else {
        entry.shape = JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0.0f, halfY, 0.0f),
            JPH::Quat::sIdentity(),
            new JPH::CapsuleShape(cylinderHalf, radius)).Create().Get();
    }

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
        motor.hull == CharacterHull::Box ? "box" : "capsule",
        motor.moveMode == CharacterMoveMode::TryMove ? "try-move" : "slide");
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

    const bool movingTowardsGround = (currentVertical.GetY() - groundVelocity.GetY()) < 0.1f;
    if (character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround &&
        movingTowardsGround) {
        newVelocity = groundVelocity;
    } else {
        newVelocity = currentVertical;
    }

    newVelocity += JPH::Vec3(0.0f, -motor.gravity, 0.0f) * dt;
    newVelocity += wish;
    character.SetLinearVelocity(newVelocity);
}

void PhysicsWorld::stepCharacterTryMove(
    JPH::CharacterVirtual& character,
    const CharacterMotor& motor) {
    applyCharacterInput(character, motor, motor.wishX, motor.wishZ, kFixedDt, false);

    const auto& broadPhaseFilter = system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
    const auto& objectFilter = system_->GetDefaultLayerFilter(Layers::MOVING);
    const JPH::BodyFilter bodyFilter{};
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
    bool noclip) {
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

    if (motor.moveMode == CharacterMoveMode::TryMove) {
        stepCharacterTryMove(character, motor);
        return;
    }

    applyCharacterInput(character, motor, motor.wishX, motor.wishZ, kFixedDt, false);

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    const float stepHeight = motor.stepHeight > 0.0f ? motor.stepHeight : 0.4f;
    updateSettings.mWalkStairsStepUp = character.GetUp() * stepHeight;
    character.ExtendedUpdate(
        kFixedDt,
        -character.GetUp() * system_->GetGravity().Length(),
        updateSettings,
        system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        system_->GetDefaultLayerFilter(Layers::MOVING),
        {},
        {},
        *tempAllocator_);
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

JPH::Vec3 wishVelocity(const CharacterMotor& motor) {
    JPH::Vec3 wish(motor.wishX, 0.0f, motor.wishZ);
    if (wish.LengthSq() > 1.0e-6f) {
        return wish.Normalized() * motor.moveSpeed;
    }
    return JPH::Vec3::sZero();
}

struct SoftSepBody {
    std::uint64_t id = 0;
    CharacterMotor* motor = nullptr;
    JPH::CharacterVirtual* character = nullptr;
};

std::uint64_t softSepCellKey(int cellX, int cellZ) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cellX)) << 32) |
        static_cast<std::uint32_t>(cellZ);
}

void resolveSoftSeparationPair(
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
            stepCharacter(*it->second.character, *step.motor, step.noclip);
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
    float radius) const {
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
    system_->GetNarrowPhaseQuery().CastShape(
        shapeCast,
        settings,
        shapeCast.mCenterOfMassStart.GetTranslation(),
        collector,
        system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        system_->GetDefaultLayerFilter(Layers::MOVING));

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
    float distance) const {
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
    system_->GetNarrowPhaseQuery().CastRay(
        ray,
        settings,
        collector,
        system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        system_->GetDefaultLayerFilter(Layers::MOVING));

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
