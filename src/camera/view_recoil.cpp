#include "camera/view_recoil.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <raymath.h>

namespace slopengine {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kMaxFrame = 0.1f;

float clampedDamping(const RecoilSpringTuning& tuning) {
    return std::clamp(tuning.damping, 0.05f, 1.0f);
}

float angularFrequency(const RecoilSpringTuning& tuning) {
    return kTwoPi * std::max(tuning.frequency, 0.01f);
}

float peakFraction(float damping) {
    if (damping >= 0.999f) {
        return std::exp(-1.0f);
    }
    const float root = std::sqrt(1.0f - damping * damping);
    return std::exp(-damping / root * std::atan2(root, damping));
}

float approach(float timeConstant, float dt) {
    if (timeConstant <= 0.0f) {
        return 1.0f;
    }
    return 1.0f - std::exp(-dt / timeConstant);
}

float lattice(int channel, int index) {
    std::uint32_t h = static_cast<std::uint32_t>(index) * 374761393u +
        static_cast<std::uint32_t>(channel) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffu) / 32767.5f - 1.0f;
}

float smoothNoise(int channel, float t) {
    const float base = std::floor(t);
    const int index = static_cast<int>(base);
    float u = t - base;
    u = u * u * (3.0f - 2.0f * u);
    return lattice(channel, index) + (lattice(channel, index + 1) - lattice(channel, index)) * u;
}

}

void stepRecoilSpring(RecoilSpring& spring, const RecoilSpringTuning& tuning, float dt) {
    if (dt <= 0.0f) {
        return;
    }
    const float omega = angularFrequency(tuning);
    const float damping = clampedDamping(tuning);
    const float x0 = spring.position;
    const float v0 = spring.velocity;
    const float decay = std::exp(-damping * omega * dt);
    if (damping >= 0.999f) {
        const float b = v0 + omega * x0;
        spring.position = decay * (x0 + b * dt);
        spring.velocity = decay * (v0 - omega * b * dt);
        return;
    }
    const float omegaD = omega * std::sqrt(1.0f - damping * damping);
    const float c = std::cos(omegaD * dt);
    const float s = std::sin(omegaD * dt);
    spring.position = decay * (x0 * c + (v0 + damping * omega * x0) / omegaD * s);
    spring.velocity = decay * (v0 * c - (damping * omega * v0 + omega * omega * x0) / omegaD * s);
}

void kickRecoilSpring(RecoilSpring& spring, const RecoilSpringTuning& tuning, float amount) {
    spring.velocity += amount * angularFrequency(tuning) / peakFraction(clampedDamping(tuning));
}

void applyViewRecoilKick(ViewRecoil& recoil, const ViewRecoilKick& kick) {
    const ViewRecoilTuning& tuning = recoil.tuning;
    kickRecoilSpring(recoil.aimUp, tuning.aim, kick.punchUp);
    kickRecoilSpring(recoil.aimRight, tuning.aim, kick.punchRight);
    kickRecoilSpring(recoil.kickBack, tuning.kick, kick.back);
    kickRecoilSpring(recoil.kickLift, tuning.kick, kick.lift);
    kickRecoilSpring(recoil.kickSide, tuning.kick, kick.side);
    kickRecoilSpring(recoil.twistRise, tuning.twist, kick.rise);
    kickRecoilSpring(recoil.twistTurn, tuning.twist, kick.turn);
    kickRecoilSpring(recoil.twistRoll, tuning.twist, kick.roll);
    recoil.climbPendingUp += kick.climbUp;
    recoil.climbPendingRight += kick.climbRight;
    recoil.trauma = std::min(1.0f, recoil.trauma + std::max(kick.shake, 0.0f));
    recoil.sinceKick = 0.0f;
}

ViewRecoilLookDelta stepViewRecoil(ViewRecoil& recoil, float dt) {
    ViewRecoilLookDelta delta{};
    dt = std::clamp(dt, 0.0f, kMaxFrame);
    if (dt <= 0.0f) {
        return delta;
    }
    const ViewRecoilTuning& tuning = recoil.tuning;

    stepRecoilSpring(recoil.aimUp, tuning.aim, dt);
    stepRecoilSpring(recoil.aimRight, tuning.aim, dt);
    delta.up += recoil.aimUp.position - recoil.appliedAimUp;
    delta.right += recoil.aimRight.position - recoil.appliedAimRight;
    recoil.appliedAimUp = recoil.aimUp.position;
    recoil.appliedAimRight = recoil.aimRight.position;

    const float climbFraction = approach(tuning.climbTime, dt);
    const float climbUp = recoil.climbPendingUp * climbFraction;
    const float climbRight = recoil.climbPendingRight * climbFraction;
    recoil.climbPendingUp -= climbUp;
    recoil.climbPendingRight -= climbRight;
    delta.up += climbUp;
    delta.right += climbRight;
    const float recovery = std::clamp(tuning.recovery, 0.0f, 1.0f);
    recoil.recoverableUp += climbUp * recovery;
    recoil.recoverableRight += climbRight * recovery;

    recoil.sinceKick += dt;
    if (recoil.sinceKick >= tuning.recoveryDelay) {
        const float recoverFraction = approach(tuning.recoveryTime, dt);
        const float backUp = recoil.recoverableUp * recoverFraction;
        const float backRight = recoil.recoverableRight * recoverFraction;
        recoil.recoverableUp -= backUp;
        recoil.recoverableRight -= backRight;
        delta.up -= backUp;
        delta.right -= backRight;
    }

    stepRecoilSpring(recoil.kickBack, tuning.kick, dt);
    stepRecoilSpring(recoil.kickLift, tuning.kick, dt);
    stepRecoilSpring(recoil.kickSide, tuning.kick, dt);
    stepRecoilSpring(recoil.twistRise, tuning.twist, dt);
    stepRecoilSpring(recoil.twistTurn, tuning.twist, dt);
    stepRecoilSpring(recoil.twistRoll, tuning.twist, dt);

    recoil.trauma = std::max(0.0f, recoil.trauma - tuning.shakeDecay * dt);
    recoil.shakeTime += dt;
    return delta;
}

void resetViewRecoil(ViewRecoil& recoil) {
    const ViewRecoilTuning tuning = recoil.tuning;
    recoil = ViewRecoil{};
    recoil.tuning = tuning;
}

Vector3 viewRecoilShake(const ViewRecoil& recoil) {
    if (recoil.trauma <= 0.0f) {
        return {0.0f, 0.0f, 0.0f};
    }
    const float amount = recoil.trauma * recoil.trauma * recoil.tuning.shakeAngle;
    const float t = recoil.shakeTime * recoil.tuning.shakeFrequency;
    return {
        amount * smoothNoise(1, t),
        amount * smoothNoise(2, t),
        amount * 0.5f * smoothNoise(3, t),
    };
}

Quaternion viewRecoilKickRotation(const ViewRecoil& recoil) {
    const Quaternion rise = QuaternionFromAxisAngle({1.0f, 0.0f, 0.0f}, -recoil.twistRise.position);
    const Quaternion turn = QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, recoil.twistTurn.position);
    const Quaternion roll = QuaternionFromAxisAngle({0.0f, 0.0f, 1.0f}, -recoil.twistRoll.position);
    return QuaternionMultiply(turn, QuaternionMultiply(rise, roll));
}

Vector3 viewRecoilKickPosition(const ViewRecoil& recoil) {
    const Vector3 pivot = recoil.tuning.pivot;
    const Vector3 swung = Vector3RotateByQuaternion(pivot, viewRecoilKickRotation(recoil));
    return {
        recoil.kickSide.position + pivot.x - swung.x,
        recoil.kickLift.position + pivot.y - swung.y,
        -recoil.kickBack.position + pivot.z - swung.z,
    };
}

}
