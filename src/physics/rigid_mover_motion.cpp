#include "physics/rigid_mover.hpp"

#include <algorithm>
#include <cmath>

namespace slopengine {

void tickRigidMover(RigidMover& mover, float dt) {
    if (!(dt > 0.0f) || !std::isfinite(dt)) {
        return;
    }

    const float duration = mover.duration > 1.0e-4f ? mover.duration : 0.8f;
    const float step = dt / duration;
    if (mover.progress < mover.target) {
        mover.progress = std::min(mover.target, mover.progress + step);
    } else if (mover.progress > mover.target) {
        mover.progress = std::max(mover.target, mover.progress - step);
    }

    if (mover.autoClose > 0.0f && mover.target >= 0.5f && mover.progress >= 0.999f) {
        mover.autoCloseTimer += dt;
        if (mover.autoCloseTimer >= mover.autoClose) {
            mover.target = 0.0f;
            mover.autoCloseTimer = 0.0f;
        }
    } else {
        mover.autoCloseTimer = 0.0f;
    }
}

}
