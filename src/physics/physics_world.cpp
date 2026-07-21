#include "physics/physics_world.hpp"

#include "map/brush.hpp"
#include "physics/components.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/RegisterTypes.h>

#include <raylib.h>

#include <cstdarg>
#include <cstdio>
#include <thread>

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
    character_ = nullptr;
    characterShape_ = nullptr;

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

void PhysicsWorld::createPlayerCharacter(float x, float y, float z, const CharacterMotor& motor) {
    const float radius = motor.radius;
    const float cylinderHalf = 0.5f * motor.height;
    characterShape_ = JPH::RotatedTranslatedShapeSettings(
        JPH::Vec3(0.0f, cylinderHalf + radius, 0.0f),
        JPH::Quat::sIdentity(),
        new JPH::CapsuleShape(cylinderHalf, radius)).Create().Get();

    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = characterShape_;
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
    settings->mMass = 70.0f;

    character_ = new JPH::CharacterVirtual(
        settings,
        JPH::RVec3(x, y, z),
        JPH::Quat::sIdentity(),
        0,
        system_.get());

    TraceLog(LOG_INFO, "PHYSICS: player character at (%.2f, %.2f, %.2f)", x, y, z);
}

void PhysicsWorld::applyPlayerInput(
    const CharacterMotor& motor,
    float wishX,
    float wishZ,
    float dt,
    bool noclip) {
    if (character_ == nullptr) {
        return;
    }

    JPH::Vec3 wish(wishX, 0.0f, wishZ);
    if (wish.LengthSq() > 1.0e-6f) {
        wish = wish.Normalized() * motor.moveSpeed;
    } else {
        wish = JPH::Vec3::sZero();
    }

    if (noclip) {
        character_->SetLinearVelocity(wish);
        return;
    }

    const JPH::Vec3 currentVertical =
        character_->GetLinearVelocity().Dot(character_->GetUp()) * character_->GetUp();
    const JPH::Vec3 groundVelocity = character_->GetGroundVelocity();
    JPH::Vec3 newVelocity;

    const bool movingTowardsGround = (currentVertical.GetY() - groundVelocity.GetY()) < 0.1f;
    if (character_->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround && movingTowardsGround) {
        newVelocity = groundVelocity;
    } else {
        newVelocity = currentVertical;
    }

    newVelocity += JPH::Vec3(0.0f, -motor.gravity, 0.0f) * dt;
    newVelocity += wish;
    character_->SetLinearVelocity(newVelocity);
}

void PhysicsWorld::update(float frameDt, const CharacterMotor& motor, bool noclip) {
    if (character_ == nullptr) {
        return;
    }

    constexpr float kMaxFrameDt = 0.25f;
    if (frameDt > kMaxFrameDt) {
        frameDt = kMaxFrameDt;
    } else if (frameDt < 0.0f) {
        frameDt = 0.0f;
    }

    accumulator_ += frameDt;
    int steps = 0;
    while (accumulator_ >= kFixedDt && steps < kMaxSubsteps) {
        applyPlayerInput(motor, motor.wishX, motor.wishZ, kFixedDt, noclip);

        if (noclip) {
            const JPH::RVec3 pos = character_->GetPosition();
            const JPH::Vec3 vel = character_->GetLinearVelocity();
            character_->SetPosition(JPH::RVec3(
                pos.GetX() + static_cast<double>(vel.GetX()) * static_cast<double>(kFixedDt),
                pos.GetY(),
                pos.GetZ() + static_cast<double>(vel.GetZ()) * static_cast<double>(kFixedDt)));
        } else {
            JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
            character_->ExtendedUpdate(
                kFixedDt,
                -character_->GetUp() * system_->GetGravity().Length(),
                updateSettings,
                system_->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                system_->GetDefaultLayerFilter(Layers::MOVING),
                {},
                {},
                *tempAllocator_);
        }

        system_->Update(kFixedDt, 1, tempAllocator_.get(), jobSystem_.get());

        accumulator_ -= kFixedDt;
        ++steps;
    }
}

JPH::RVec3 PhysicsWorld::playerPosition() const {
    if (character_ == nullptr) {
        return JPH::RVec3::sZero();
    }
    return character_->GetPosition();
}

JPH::Vec3 PhysicsWorld::playerVelocity() const {
    if (character_ == nullptr) {
        return JPH::Vec3::sZero();
    }
    return character_->GetLinearVelocity();
}

bool PhysicsWorld::playerSupported() const {
    return character_ != nullptr && character_->IsSupported();
}

}
