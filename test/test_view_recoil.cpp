#include "test_assert.hpp"

#include "camera/view_recoil.hpp"

#include <algorithm>
#include <cmath>

#include <raymath.h>

namespace slopengine {

namespace {

float simulatePeak(const RecoilSpringTuning& tuning, float amount, float dt, float duration) {
    RecoilSpring spring{};
    kickRecoilSpring(spring, tuning, amount);
    float peak = 0.0f;
    for (float t = 0.0f; t < duration; t += dt) {
        stepRecoilSpring(spring, tuning, dt);
        peak = std::max(peak, spring.position);
    }
    return peak;
}

ViewRecoilLookDelta runLook(ViewRecoil& recoil, float dt, float duration) {
    ViewRecoilLookDelta total{};
    for (float t = 0.0f; t < duration; t += dt) {
        const ViewRecoilLookDelta step = stepViewRecoil(recoil, dt);
        total.up += step.up;
        total.right += step.right;
    }
    return total;
}

}

void runViewRecoilTests() {
    {
        const RecoilSpringTuning tuning{10.0f, 0.5f};
        CHECK(std::fabs(simulatePeak(tuning, 0.02f, 1.0f / 1000.0f, 1.0f) - 0.02f) < 0.0005f);
        const RecoilSpringTuning critical{10.0f, 1.0f};
        CHECK(std::fabs(simulatePeak(critical, 0.02f, 1.0f / 1000.0f, 1.0f) - 0.02f) < 0.0005f);

        RecoilSpring fast{};
        RecoilSpring slow{};
        kickRecoilSpring(fast, tuning, 0.02f);
        kickRecoilSpring(slow, tuning, 0.02f);
        for (int i = 0; i < 24; ++i) {
            stepRecoilSpring(fast, tuning, 1.0f / 240.0f);
        }
        for (int i = 0; i < 3; ++i) {
            stepRecoilSpring(slow, tuning, 1.0f / 30.0f);
        }
        CHECK(std::fabs(fast.position - slow.position) < 1e-5f);
    }

    {
        const RecoilSpringTuning tuning{8.0f, 0.6f};
        RecoilSpring spring{};
        kickRecoilSpring(spring, tuning, 0.05f);
        for (int i = 0; i < 120; ++i) {
            stepRecoilSpring(spring, tuning, 1.0f / 60.0f);
        }
        CHECK(std::fabs(spring.position) < 1e-4f);
        CHECK(std::fabs(spring.velocity) < 1e-3f);
    }

    {
        ViewRecoil recoil{};
        ViewRecoilKick kick{};
        kick.punchUp = 0.03f;
        kick.climbUp = 0.01f;
        kick.climbRight = -0.004f;
        applyViewRecoilKick(recoil, kick);
        const ViewRecoilLookDelta total = runLook(recoil, 1.0f / 144.0f, 2.0f);
        CHECK(std::fabs(total.up - 0.01f) < 1e-4f);
        CHECK(std::fabs(total.right + 0.004f) < 1e-4f);
    }

    {
        ViewRecoil recoil{};
        recoil.tuning.recovery = 0.75f;
        ViewRecoilKick kick{};
        kick.climbUp = 0.02f;
        applyViewRecoilKick(recoil, kick);
        const ViewRecoilLookDelta total = runLook(recoil, 1.0f / 60.0f, 3.0f);
        CHECK(std::fabs(total.up - 0.005f) < 1e-4f);
    }

    {
        ViewRecoil recoil{};
        ViewRecoilKick kick{};
        kick.back = 0.03f;
        kick.rise = 0.05f;
        kick.shake = 0.5f;
        applyViewRecoilKick(recoil, kick);
        stepViewRecoil(recoil, 1.0f / 60.0f);
        CHECK(viewRecoilKickPosition(recoil).z < 0.0f);
        const Vector3 muzzle =
            Vector3RotateByQuaternion({0.0f, 0.0f, 1.0f}, viewRecoilKickRotation(recoil));
        CHECK(muzzle.y > 0.0f);
        const Vector3 shake = viewRecoilShake(recoil);
        CHECK(std::fabs(shake.x) <= recoil.tuning.shakeAngle);
        resetViewRecoil(recoil);
        CHECK(recoil.kickBack.velocity == 0.0f);
        CHECK(recoil.trauma == 0.0f);
    }

    {
        ViewRecoil recoil{};
        setViewPunch(recoil, {-5.0f * DEG2RAD, 0.0f, 18.0f * DEG2RAD});
        const Vector3 first = viewRecoilShake(recoil);
        CHECK(std::fabs(first.z - 18.0f * DEG2RAD) < 1e-5f);
        stepViewRecoil(recoil, 0.1f);
        const float length = Vector3Length(recoil.punch) * RAD2DEG;
        const float start = std::sqrt(5.0f * 5.0f + 18.0f * 18.0f);
        CHECK(std::fabs(length - (start - (10.0f + start * 0.5f) * 0.1f)) < 1e-3f);
        CHECK(recoil.punch.x < 0.0f && recoil.punch.z > 0.0f);
        for (int i = 0; i < 120; ++i) {
            stepViewRecoil(recoil, 1.0f / 60.0f);
        }
        CHECK(Vector3Length(recoil.punch) == 0.0f);

        setViewPunch(recoil, {0.0f, 0.0f, 18.0f * DEG2RAD}, 3.0f);
        stepViewRecoil(recoil, 0.05f);
        CHECK(std::fabs(recoil.punch.z * RAD2DEG - (18.0f - (10.0f + 9.0f) * 3.0f * 0.05f)) < 1e-3f);
    }
}

}
