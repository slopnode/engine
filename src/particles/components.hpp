#pragma once

#include "assets/prt_loader.hpp"

#include <raylib.h>

#include <cstdint>
#include <string>
#include <vector>

namespace slopengine {

struct Particle {
    Vector3 position{};
    Vector3 velocity{};
    float age = 0.0f;
    float lifetime = 1.0f;
    float size0 = 0.25f;
    Color color0 = WHITE;
    Color tint = WHITE;
    Color bakeTint = WHITE;
    std::uint16_t bounceCount = 0;
    bool alive = false;
    bool stuck = false;
    bool lit = false;
    bool bakeCached = false;
};

struct ParticleEmitterRuntime {
    ParticleEmitterDef def{};
    std::vector<Particle> particles;
    float emitAccum = 0.0f;
    bool burstFired = false;
    int aliveCount = 0;
};

/** Playing instance of a .prt effect: loaded emitter defs plus their live particle pools.
 *  @ingroup particles_components
 */
struct ParticleSystemInstance {
    std::string path;
    bool playing = true;
    bool emitting = true;
    float age = 0.0f;
    std::vector<ParticleEmitterRuntime> emitters;
};

/** Pins a view-space particle system to a named first-person attachment point at a fixed depth.
 *  @ingroup particles_components
 */
struct ParticleFollowAttachPoint {
    std::uint64_t host = 0;
    std::string name;
    float depth = 0.35f;
};

/** Pins a world-space particle system to a named attachment point on a world sprite (actor).
 *  Unlike ParticleFollowAttachPoint, resolved from the host's real billboard geometry, not a
 *  camera-ray projection, so there is no depth parameter.
 *  @ingroup particles_components
 */
struct ParticleFollowWorldAttachPoint {
    std::uint64_t host = 0;
    std::string name;
};

/** Keeps a particle system on a bone joint of a rigged host, aimed out of the limb: from
 *  the reference bone's box centre through the joint. The reference is the bone itself, or
 *  its nearest parent with a box when @p fromParent is set, which aims out of a stump.
 *  @ingroup particles_components
 */
struct ParticleFollowEntity {
    std::uint64_t host = 0;
    float back = 0.0f;
    bool placed = false;
};

struct ParticleFollowBone {
    std::uint64_t host = 0;
    int bone = -1;
    int reference = -1;
    Vector3 referenceCenter{0.0f, 0.0f, 0.0f};
};

}
