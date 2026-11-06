#pragma once

#include <raylib.h>

namespace slopengine {

/** One axis of a damped spring, at rest at zero.
 *  @ingroup camera_components
 */
struct RecoilSpring {
    float position = 0.0f;
    float velocity = 0.0f;
};

/** Natural frequency in hertz and damping ratio, clamped to [0.05, 1]. */
struct RecoilSpringTuning {
    float frequency = 8.0f;
    float damping = 0.6f;
};

/** Per-weapon feel of the player's recoil, set from Scheme with fp-recoil-tune!.
 *  @ingroup camera_components
 */
struct ViewRecoilTuning {
    RecoilSpringTuning aim{9.0f, 0.7f};
    RecoilSpringTuning kick{12.0f, 0.5f};
    RecoilSpringTuning twist{9.0f, 0.45f};
    float climbTime = 0.04f;
    float recovery = 0.0f;
    float recoveryDelay = 0.12f;
    float recoveryTime = 0.25f;
    Vector3 pivot{0.0f, -0.08f, 0.25f};
    float shakeDecay = 3.0f;
    float shakeFrequency = 16.0f;
    float shakeAngle = 0.025f;
};

/** One shot's recoil. Angles are radians and lengths meters, in view space: up is +y,
 *  right is +x and the weapon's back is -z. Spring amounts are the peak a single kick
 *  reaches from rest.
 */
struct ViewRecoilKick {
    float punchUp = 0.0f;
    float punchRight = 0.0f;
    float climbUp = 0.0f;
    float climbRight = 0.0f;
    float back = 0.0f;
    float lift = 0.0f;
    float side = 0.0f;
    float rise = 0.0f;
    float turn = 0.0f;
    float roll = 0.0f;
    float shake = 0.0f;
};

/** Recoil state on the Player: aim punch and climb feed the look angles, the kick springs
 *  move the first-person scene and shake moves only the presented world camera.
 *  @ingroup camera_components
 */
struct ViewRecoil {
    ViewRecoilTuning tuning;
    RecoilSpring aimUp;
    RecoilSpring aimRight;
    RecoilSpring kickBack;
    RecoilSpring kickLift;
    RecoilSpring kickSide;
    RecoilSpring twistRise;
    RecoilSpring twistTurn;
    RecoilSpring twistRoll;
    float appliedAimUp = 0.0f;
    float appliedAimRight = 0.0f;
    float climbPendingUp = 0.0f;
    float climbPendingRight = 0.0f;
    float recoverableUp = 0.0f;
    float recoverableRight = 0.0f;
    float sinceKick = 1000.0f;
    float trauma = 0.0f;
    float shakeTime = 0.0f;
    Vector3 punch{0.0f, 0.0f, 0.0f};
    float punchRecover = 1.0f;
};

/** Look-angle change one step produced, in radians: up raises pitch, right turns right. */
struct ViewRecoilLookDelta {
    float up = 0.0f;
    float right = 0.0f;
};

void stepRecoilSpring(RecoilSpring& spring, const RecoilSpringTuning& tuning, float dt);

/** Adds velocity so a spring at rest peaks near @p amount. */
void kickRecoilSpring(RecoilSpring& spring, const RecoilSpringTuning& tuning, float amount);

void applyViewRecoilKick(ViewRecoil& recoil, const ViewRecoilKick& kick);

ViewRecoilLookDelta stepViewRecoil(ViewRecoil& recoil, float dt);

/** Clears every spring, pending climb and shake, keeping the tuning. */
void resetViewRecoil(ViewRecoil& recoil);

/** Camera shake as (up, right, roll) radians. */
Vector3 viewRecoilShake(const ViewRecoil& recoil);

void setViewPunch(ViewRecoil& recoil, Vector3 punch, float recover = 1.0f);

void stepViewPunch(Vector3& punch, float recover, float dt);

/** View-space offset of the first-person scene, including the swing about the pivot. */
Vector3 viewRecoilKickPosition(const ViewRecoil& recoil);

Quaternion viewRecoilKickRotation(const ViewRecoil& recoil);

}
