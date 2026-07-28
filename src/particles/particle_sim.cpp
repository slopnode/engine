#include "particles/particle_sim.hpp"

#include <rlgl.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace slopengine {

namespace {

thread_local std::mt19937 g_rng{std::random_device{}()};

float rand01() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(g_rng);
}

float sampleRange(const ParticleFloatRange& range) {
    if (range.isConstant()) {
        return range.min;
    }
    return range.sample(rand01());
}

Color sampleColor(const ParticleColorRange& range) {
    const float t = rand01();
    const float r = range.rMin + (range.rMax - range.rMin) * t;
    const float g = range.gMin + (range.gMax - range.gMin) * t;
    const float b = range.bMin + (range.bMax - range.bMin) * t;
    const float a = range.aMin + (range.aMax - range.aMin) * t;
    return {
        static_cast<unsigned char>(std::clamp(r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(b, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(a, 0.0f, 1.0f) * 255.0f),
    };
}

Vector3 transformPoint(const Matrix& m, Vector3 p) {
    return {
        m.m0 * p.x + m.m4 * p.y + m.m8 * p.z + m.m12,
        m.m1 * p.x + m.m5 * p.y + m.m9 * p.z + m.m13,
        m.m2 * p.x + m.m6 * p.y + m.m10 * p.z + m.m14,
    };
}

Vector3 transformDirection(const Matrix& m, Vector3 d) {
    return {
        m.m0 * d.x + m.m4 * d.y + m.m8 * d.z,
        m.m1 * d.x + m.m5 * d.y + m.m9 * d.z,
        m.m2 * d.x + m.m6 * d.y + m.m10 * d.z,
    };
}

Vector3 emitterOrigin(const Matrix& world) {
    return {world.m12, world.m13, world.m14};
}

Vector3 sampleShapeOffset(const ParticleEmitterDef& def) {
    switch (def.shape) {
    case ParticleShapeKind::Point:
        return {0.0f, 0.0f, 0.0f};
    case ParticleShapeKind::Box:
        return {
            (rand01() * 2.0f - 1.0f) * def.shapeA * 0.5f,
            (rand01() * 2.0f - 1.0f) * def.shapeB * 0.5f,
            (rand01() * 2.0f - 1.0f) * def.shapeC * 0.5f,
        };
    case ParticleShapeKind::Sphere: {
        const float u = rand01();
        const float v = rand01();
        const float theta = u * 2.0f * PI;
        const float phi = std::acos(2.0f * v - 1.0f);
        const float r = def.shapeA * std::cbrt(rand01());
        return {
            r * std::sin(phi) * std::cos(theta),
            r * std::cos(phi),
            r * std::sin(phi) * std::sin(theta),
        };
    }
    case ParticleShapeKind::Circle: {
        const float angle = rand01() * 2.0f * PI;
        const float r = def.shapeA * std::sqrt(rand01());
        return {r * std::cos(angle), 0.0f, r * std::sin(angle)};
    }
    case ParticleShapeKind::Cone: {
        const float angle = rand01() * 2.0f * PI;
        const float r = def.shapeB * std::sqrt(rand01());
        return {r * std::cos(angle), 0.0f, r * std::sin(angle)};
    }
    }
    return {};
}

Vector3 sampleEmitDirection(const ParticleEmitterDef& def, const Matrix& world) {
    Vector3 localDir{0.0f, 1.0f, 0.0f};
    if (def.shape == ParticleShapeKind::Cone) {
        const float halfAngle = def.shapeA * DEG2RAD;
        const float cosMax = std::cos(halfAngle);
        const float cosTheta = cosMax + (1.0f - cosMax) * rand01();
        const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
        const float phi = rand01() * 2.0f * PI;
        localDir = {sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi)};
    } else if (def.shape == ParticleShapeKind::Sphere) {
        const float u = rand01();
        const float v = rand01();
        const float theta = u * 2.0f * PI;
        const float phi = std::acos(2.0f * v - 1.0f);
        localDir = {
            std::sin(phi) * std::cos(theta),
            std::cos(phi),
            std::sin(phi) * std::sin(theta),
        };
    } else {
        localDir = {
            (rand01() * 2.0f - 1.0f) * 0.15f,
            1.0f,
            (rand01() * 2.0f - 1.0f) * 0.15f,
        };
        const float len = Vector3Length(localDir);
        if (len > 1.0e-6f) {
            localDir = Vector3Scale(localDir, 1.0f / len);
        }
    }

    if (def.space == ParticleSpace::Local) {
        Vector3 worldDir = transformDirection(world, localDir);
        const float len = Vector3Length(worldDir);
        if (len > 1.0e-6f) {
            return Vector3Scale(worldDir, 1.0f / len);
        }
        return {0.0f, 1.0f, 0.0f};
    }
    return localDir;
}

int findFreeParticle(ParticleEmitterRuntime& emitter) {
    for (int i = 0; i < static_cast<int>(emitter.particles.size()); ++i) {
        if (!emitter.particles[static_cast<std::size_t>(i)].alive) {
            return i;
        }
    }
    return -1;
}

void spawnParticle(ParticleEmitterRuntime& emitter, const Matrix& world) {
    if (emitter.aliveCount >= emitter.def.maxParticles) {
        return;
    }
    const int slot = findFreeParticle(emitter);
    if (slot < 0) {
        return;
    }

    Particle& p = emitter.particles[static_cast<std::size_t>(slot)];
    const Vector3 localOffset = sampleShapeOffset(emitter.def);
    if (emitter.def.space == ParticleSpace::Local) {
        p.position = transformPoint(world, localOffset);
    } else {
        p.position = Vector3Add(emitterOrigin(world), localOffset);
    }

    const float speed = sampleRange(emitter.def.speed);
    p.velocity = Vector3Scale(sampleEmitDirection(emitter.def, world), speed);
    p.age = 0.0f;
    p.lifetime = std::max(0.01f, sampleRange(emitter.def.lifetime));
    p.size0 = std::max(0.001f, sampleRange(emitter.def.size));
    p.color0 = sampleColor(emitter.def.color);
    p.bounceCount = 0;
    p.alive = true;
    ++emitter.aliveCount;
}

void emitParticles(ParticleEmitterRuntime& emitter, const Matrix& world, float dt, bool allowEmit) {
    if (!allowEmit) {
        return;
    }
    if (!emitter.burstFired && emitter.def.burst > 0) {
        for (int i = 0; i < emitter.def.burst; ++i) {
            spawnParticle(emitter, world);
        }
        emitter.burstFired = true;
    }
    if (emitter.def.rate <= 0.0f) {
        return;
    }
    emitter.emitAccum += emitter.def.rate * dt;
    while (emitter.emitAccum >= 1.0f) {
        emitter.emitAccum -= 1.0f;
        spawnParticle(emitter, world);
    }
}

Vector3 reflectVelocity(Vector3 velocity, Vector3 normal, float restitution) {
    const float vn = Vector3DotProduct(velocity, normal);
    if (vn >= 0.0f) {
        return velocity;
    }
    Vector3 reflected = Vector3Subtract(velocity, Vector3Scale(normal, 2.0f * vn));
    return Vector3Scale(reflected, restitution);
}

void integrateEmitter(
    ParticleEmitterRuntime& emitter,
    float dt,
    const ParticleRaycastFn& raycast) {
    const bool collide = emitter.def.sim == ParticleSimMode::Cpu && static_cast<bool>(raycast);
    for (Particle& p : emitter.particles) {
        if (!p.alive) {
            continue;
        }
        p.age += dt;
        if (p.age >= p.lifetime) {
            p.alive = false;
            --emitter.aliveCount;
            continue;
        }

        p.velocity.y -= emitter.def.gravity * dt;
        const Vector3 delta = Vector3Scale(p.velocity, dt);
        const float moveLen = Vector3Length(delta);
        if (moveLen < 1.0e-8f) {
            continue;
        }

        if (!collide) {
            p.position = Vector3Add(p.position, delta);
            continue;
        }

        const Vector3 dir = Vector3Scale(delta, 1.0f / moveLen);
        const auto hit = raycast(p.position, dir, moveLen);
        if (!hit) {
            p.position = Vector3Add(p.position, delta);
            continue;
        }

        if (emitter.def.dieOnHit) {
            p.alive = false;
            --emitter.aliveCount;
            continue;
        }

        p.position = Vector3Add(hit->point, Vector3Scale(hit->normal, 0.01f));
        p.velocity = reflectVelocity(p.velocity, hit->normal, emitter.def.bounce);
        ++p.bounceCount;
        if (emitter.def.maxBounces > 0 &&
            static_cast<int>(p.bounceCount) >= emitter.def.maxBounces) {
            p.alive = false;
            --emitter.aliveCount;
        }
    }
}

bool resolveParticleSprite(
    AssetStore& assets,
    const ParticleEmitterDef& def,
    const Texture2D*& outTexture,
    Rectangle& outSource) {
    const SpriteAsset* sprite = assets.getSpriteAsset(def.sprite);
    const SpriteAtlas* atlas = assets.getSpriteAtlas(def.sprite);
    if (sprite == nullptr || atlas == nullptr || sprite->frames.empty()) {
        return false;
    }
    const SpriteFrame* frame = findSpriteFrame(*sprite, "A");
    if (frame == nullptr) {
        frame = &sprite->frames.front();
    }
    const SpriteRotation* rot = selectSpriteRotation(*frame, 0);
    if (rot == nullptr) {
        return false;
    }
    const auto rectIt = atlas->rects.find(rot->texturePath);
    if (rectIt == atlas->rects.end()) {
        return false;
    }
    const SpriteAtlasRect& rect = rectIt->second;
    if (rect.atlasIndex < 0 || rect.atlasIndex >= static_cast<int>(atlas->textures.size())) {
        return false;
    }
    outTexture = &atlas->textures[static_cast<std::size_t>(rect.atlasIndex)];
    outSource = rect.source;
    return outTexture != nullptr && outTexture->id != 0;
}

} // namespace

bool initParticleSystemInstance(
    ParticleSystemInstance& instance,
    AssetStore& assets,
    std::string_view path,
    bool playing) {
    const ParticleSystemAsset* asset = assets.getParticleAsset(path);
    if (asset == nullptr) {
        return false;
    }
    instance = {};
    instance.path = std::string(path);
    instance.playing = playing;
    instance.age = 0.0f;
    instance.emitters.reserve(asset->emitters.size());
    for (const ParticleEmitterDef& def : asset->emitters) {
        ParticleEmitterRuntime runtime{};
        runtime.def = def;
        runtime.particles.resize(static_cast<std::size_t>(std::max(1, def.maxParticles)));
        instance.emitters.push_back(std::move(runtime));
    }
    return true;
}

void resetParticleSystemInstance(ParticleSystemInstance& instance) {
    instance.age = 0.0f;
    for (ParticleEmitterRuntime& emitter : instance.emitters) {
        emitter.emitAccum = 0.0f;
        emitter.burstFired = false;
        emitter.aliveCount = 0;
        for (Particle& p : emitter.particles) {
            p.alive = false;
        }
    }
}

void tickParticleSystemInstance(
    ParticleSystemInstance& instance,
    AssetStore& assets,
    const Matrix& worldMatrix,
    float dt,
    const ParticleRaycastFn& raycast) {
    if (!instance.playing || dt <= 0.0f) {
        return;
    }
    const ParticleSystemAsset* asset = assets.getParticleAsset(instance.path);
    if (asset == nullptr) {
        return;
    }

    instance.age += dt;
    bool allowEmit = true;
    if (asset->duration > 0.0f && instance.age > asset->duration) {
        if (asset->loop) {
            const float wrapped = std::fmod(instance.age, asset->duration);
            resetParticleSystemInstance(instance);
            instance.playing = true;
            instance.age = wrapped;
        } else {
            allowEmit = false;
            bool anyAlive = false;
            for (const ParticleEmitterRuntime& emitter : instance.emitters) {
                if (emitter.aliveCount > 0) {
                    anyAlive = true;
                    break;
                }
            }
            if (!anyAlive) {
                instance.playing = false;
                return;
            }
        }
    }

    for (ParticleEmitterRuntime& emitter : instance.emitters) {
        emitParticles(emitter, worldMatrix, dt, allowEmit);
        integrateEmitter(emitter, dt, raycast);
    }
}

void appendParticleDrawItems(
    const ParticleSystemInstance& instance,
    AssetStore& assets,
    const Camera3D& camera,
    std::vector<ParticleDrawItem>& out) {
    for (const ParticleEmitterRuntime& emitter : instance.emitters) {
        if (emitter.aliveCount <= 0) {
            continue;
        }
        const Texture2D* texture = nullptr;
        Rectangle source{};
        if (!resolveParticleSprite(assets, emitter.def, texture, source)) {
            continue;
        }
        for (const Particle& p : emitter.particles) {
            if (!p.alive) {
                continue;
            }
            const float lifeT = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);
            const float sizeMul = emitter.def.sizeOverLife.evaluate(lifeT);
            const float alphaMul = emitter.def.alphaOverLife.evaluate(lifeT);
            Color color = p.color0;
            color.a = static_cast<unsigned char>(
                std::clamp(static_cast<float>(color.a) * alphaMul, 0.0f, 255.0f));
            const float dx = p.position.x - camera.position.x;
            const float dy = p.position.y - camera.position.y;
            const float dz = p.position.z - camera.position.z;
            out.push_back(ParticleDrawItem{
                .position = p.position,
                .size = p.size0 * sizeMul,
                .color = color,
                .texture = texture,
                .source = source,
                .distSq = dx * dx + dy * dy + dz * dz,
                .blend = emitter.def.blend,
                .billboard = emitter.def.billboard,
                .unlit = emitter.def.unlit,
            });
        }
    }
}

void drawParticleDrawItems(
    const std::vector<ParticleDrawItem>& items,
    const Camera3D& camera,
    bool depthTest) {
    if (items.empty()) {
        return;
    }
    std::vector<ParticleDrawItem> sorted = items;
    std::sort(sorted.begin(), sorted.end(), [](const ParticleDrawItem& a, const ParticleDrawItem& b) {
        if (a.blend != b.blend) {
            return static_cast<int>(a.blend) < static_cast<int>(b.blend);
        }
        return a.distSq > b.distSq;
    });

    rlDisableShader();
    rlDisableDepthMask();
    if (!depthTest) {
        rlDisableDepthTest();
    }

    const Texture2D* filterTex = nullptr;
    auto ensureBilinear = [&](const Texture2D* texture) {
        if (texture == nullptr || texture == filterTex) {
            return;
        }
        if (filterTex != nullptr) {
            SetTextureFilter(*filterTex, TEXTURE_FILTER_POINT);
        }
        SetTextureFilter(*texture, TEXTURE_FILTER_BILINEAR);
        filterTex = texture;
    };

    const Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
    const Vector3 screenUp = Vector3Normalize(Vector3{matView.m1, matView.m5, matView.m9});
    const Vector3 worldUp{0.0f, 1.0f, 0.0f};

    BlendMode activeBlend = BLEND_ALPHA_PREMULTIPLY;
    BeginBlendMode(activeBlend);
    for (const ParticleDrawItem& item : sorted) {
        if (item.texture == nullptr || item.texture->id == 0 || item.size <= 0.0f) {
            continue;
        }
        ensureBilinear(item.texture);

        const bool additive = item.blend == ParticleBlendMode::Additive;
        const BlendMode want = additive ? BLEND_ADD_COLORS : BLEND_ALPHA_PREMULTIPLY;
        if (want != activeBlend) {
            EndBlendMode();
            activeBlend = want;
            BeginBlendMode(activeBlend);
        }

        const float a = static_cast<float>(item.color.a) / 255.0f;
        const Color tint{
            static_cast<unsigned char>(std::clamp(static_cast<float>(item.color.r) * a, 0.0f, 255.0f)),
            static_cast<unsigned char>(std::clamp(static_cast<float>(item.color.g) * a, 0.0f, 255.0f)),
            static_cast<unsigned char>(std::clamp(static_cast<float>(item.color.b) * a, 0.0f, 255.0f)),
            item.color.a,
        };
        const Vector2 size{item.size, item.size};
        const Vector2 origin = Vector2Scale(size, 0.5f);
        const Vector3 up =
            item.billboard == SpriteBillboardMode::Screen ? screenUp : worldUp;
        DrawBillboardPro(
            camera,
            *item.texture,
            item.source,
            item.position,
            up,
            size,
            origin,
            0.0f,
            tint);
    }
    EndBlendMode();
    if (filterTex != nullptr) {
        SetTextureFilter(*filterTex, TEXTURE_FILTER_POINT);
    }
    if (!depthTest) {
        rlEnableDepthTest();
    }
    rlEnableDepthMask();
}

}
