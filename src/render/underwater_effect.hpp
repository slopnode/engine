#pragma once

#include <flecs.h>

namespace slopengine {

/** Drives the underwater post-process pass and audio muffle from the camera's
 *  position relative to MapWaterVolumes each frame. */
void registerUnderwaterEffectModule(flecs::world& world);

}
