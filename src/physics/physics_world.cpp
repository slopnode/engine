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

#include <cstdarg>
#include <cstdio>
#include <thread>
#include <utility>

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
    const JPH::RVec3 centerOnHit = shapeCast.GetPointOnRay(hit.mFraction);
    JPH::Vec3 normal = hit.mPenetrationAxis;
    if (normal.LengthSq() > 1.0e-12f) {
        normal = normal.Normalized();
    } else {
        normal = JPH::Vec3(0, 1, 0);
    }

    SphereCastHit result{};
    result.fraction = hit.mFraction;
    result.point = {
        static_cast<float>(centerOnHit.GetX()),
        static_cast<float>(centerOnHit.GetY()),
        static_cast<float>(centerOnHit.GetZ()),
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
