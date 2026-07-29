#include "test_assert.hpp"

#include "assets/material_loader.hpp"
#include "assets/prt_loader.hpp"
#include "assets/sprite_anim_loader.hpp"

namespace slopengine {

void runAssetTests() {
    {
        MaterialAsset asset{};
        const bool ok = parseMaterialAsset(
            "(material\n"
            "  (shader \"default\")\n"
            "  (texture \"freedom/WALL\")\n"
            "  (texel-size 64)\n"
            "  (base-color 1 1 1 1))\n",
            asset);
        CHECK(ok);
        CHECK_EQ(asset.shader, std::string("default"));
        CHECK_EQ(asset.albedoTexture, std::string("freedom/WALL"));
        CHECK_EQ(asset.pixelsPerMeter, 64.0f);
    }

    {
        MaterialAsset asset{};
        const bool ok = parseMaterialAsset(
            "(material (shader \"default\") (texure \"typo\"))",
            asset);
        CHECK_FALSE(ok);
    }

    {
        ParticleSystemAsset asset{};
        const bool ok = parseParticleSystemAsset(
            "(particle-system\n"
            "  (duration 0)\n"
            "  (loop #t)\n"
            "  (emitter \"smoke\"\n"
            "    (sim gpu)\n"
            "    (sprite \"fx/smoke\")\n"
            "    (blend alpha)\n"
            "    (unlit #f)\n"
            "    (max-particles 128)\n"
            "    (rate 10)\n"
            "    (lifetime 1.0 2.0)\n"
            "    (speed 0.2)\n"
            "    (size 0.3 0.5)\n"
            "    (color 1 1 1 0.5)\n"
            "    (shape sphere 0.2)\n"
            "    (size-over-life 1.0 0.2)\n"
            "    (alpha-over-life 1.0 0.0))\n"
            "  (emitter \"bits\"\n"
            "    (sim cpu)\n"
            "    (sprite \"fx/smoke\")\n"
            "    (max-particles 32)\n"
            "    (burst 8)\n"
            "    (bounce 0.4)\n"
            "    (max-bounces 2)\n"
            "    (die-on-hit #f)))\n",
            asset);
        CHECK(ok);
        CHECK(asset.loop);
        CHECK_EQ(asset.emitters.size(), 2u);
        CHECK_EQ(asset.emitters[0].name, std::string("smoke"));
        CHECK(asset.emitters[0].sim == ParticleSimMode::Gpu);
        CHECK_EQ(asset.emitters[0].sprite, std::string("fx/smoke"));
        CHECK(asset.emitters[0].blend == ParticleBlendMode::Alpha);
        CHECK_FALSE(asset.emitters[0].unlit);
        CHECK_EQ(asset.emitters[0].maxParticles, 128);
        CHECK_EQ(asset.emitters[0].rate, 10.0f);
        CHECK_EQ(asset.emitters[0].lifetime.min, 1.0f);
        CHECK_EQ(asset.emitters[0].lifetime.max, 2.0f);
        CHECK(asset.emitters[0].shape == ParticleShapeKind::Sphere);
        CHECK_EQ(asset.emitters[0].direction.x, 0.0f);
        CHECK_EQ(asset.emitters[0].direction.y, 1.0f);
        CHECK_EQ(asset.emitters[0].direction.z, 0.0f);
        CHECK_EQ(asset.emitters[0].spread, -1.0f);
        CHECK_EQ(asset.emitters[1].name, std::string("bits"));
        CHECK(asset.emitters[1].sim == ParticleSimMode::Cpu);
        CHECK_EQ(asset.emitters[1].burst, 8);
        CHECK_EQ(asset.emitters[1].bounce, 0.4f);
        CHECK_EQ(asset.emitters[1].maxBounces, 2);
    }

    {
        ParticleSystemAsset asset{};
        const bool ok = parseParticleSystemAsset(
            "(particle-system\n"
            "  (duration 0.5)\n"
            "  (loop #f)\n"
            "  (emitter \"spray\"\n"
            "    (sprite \"fx/blood\")\n"
            "    (max-particles 12)\n"
            "    (burst 12)\n"
            "    (direction 0 0 1)\n"
            "    (spread 40)\n"
            "    (shape point)))\n",
            asset);
        CHECK(ok);
        CHECK_EQ(asset.emitters.size(), 1u);
        CHECK_EQ(asset.emitters[0].direction.x, 0.0f);
        CHECK_EQ(asset.emitters[0].direction.y, 0.0f);
        CHECK_EQ(asset.emitters[0].direction.z, 1.0f);
        CHECK_EQ(asset.emitters[0].spread, 40.0f);
    }

    {
        SpriteAsset asset{};
        const bool ok = parseSpriteAsset(
            "(sprite\n"
            "  (texel-size 32)\n"
            "  (fullbright)\n"
            "  (blend additive)\n"
            "  (billboard screen)\n"
            "  (frame \"A\"\n"
            "    (rot 0 \"fx/blood\" offset 8 8))\n"
            ")\n",
            asset);
        CHECK(ok);
        CHECK(asset.fullbright);
        CHECK(asset.blend == SpriteBlendMode::Additive);
        CHECK(asset.billboardMode == SpriteBillboardMode::Screen);
        const std::string serialized = serializeSpriteAsset(asset);
        SpriteAsset roundTrip{};
        CHECK(parseSpriteAsset(serialized, roundTrip));
        CHECK(roundTrip.blend == SpriteBlendMode::Additive);
    }

    {
        SpriteAsset asset{};
        const bool ok = parseSpriteAsset(
            "(sprite\n"
            "  (texel-size 32)\n"
            "  (tint 1 0.5 0.25 0.75)\n"
            "  (frame \"A\"\n"
            "    (rot 0 \"fx/blood\" offset 8 8))\n"
            ")\n",
            asset);
        CHECK(ok);
        CHECK_EQ(asset.tint.r, 255);
        CHECK_EQ(asset.tint.g, 128);
        CHECK_EQ(asset.tint.b, 64);
        CHECK_EQ(asset.tint.a, 191);
        const std::string serialized = serializeSpriteAsset(asset);
        SpriteAsset roundTrip{};
        CHECK(parseSpriteAsset(serialized, roundTrip));
        CHECK_EQ(roundTrip.tint.r, 255);
        CHECK_EQ(roundTrip.tint.g, 128);
        CHECK_EQ(roundTrip.tint.b, 64);
        CHECK_EQ(roundTrip.tint.a, 191);
    }

    {
        ParticleSystemAsset asset{};
        const bool ok = parseParticleSystemAsset(
            "(particle-system (duration 1))\n",
            asset);
        CHECK_FALSE(ok);
    }

    {
        SpriteAnimBank bank{};
        const bool ok = parseSpriteAnimBank(
            "(sprite-anim\n"
            "  (clip \"idle\"\n"
            "    (loop 1)\n"
            "    (frame \"A\" 1)\n"
            "  )\n"
            ")\n",
            bank);
        CHECK(ok);
        CHECK_EQ(bank.clips.size(), 1u);
        CHECK_EQ(bank.clips[0].name, std::string("idle"));
        CHECK_EQ(bank.clips[0].frames.size(), 1u);
        CHECK_EQ(bank.clips[0].frames[0].id, std::string("A"));
        CHECK_EQ(bank.clips[0].frames[0].duration, 1.0f);
    }

    {
        SpriteAnimBank bank{};
        const bool ok = parseSpriteAnimBank(
            "(sprite-anim\n"
            "  (clip \"idle\"\n"
            "    (frame \"A\" 0)\n"
            "  )\n"
            ")\n",
            bank);
        CHECK_FALSE(ok);
    }

    {
        SpriteAnimBank bank{};
        const bool ok = parseSpriteAnimBank(
            "(sprite-anim\n"
            "  (clip \"idle\"\n"
            "    (frame \"A\" 1 (bogus))\n"
            "  )\n"
            ")\n",
            bank);
        CHECK_FALSE(ok);
    }

    {
        SpriteAnimBank bank{};
        const bool ok = parseSpriteAnimBank(
            "(sprite-anim\n"
            "  (clip \"fire\"\n"
            "    (loop 0)\n"
            "    (frame \"B\" 0.05 (sound \"pistol/fire\") (hint \"fire\") "
            "(overlay 1 \"fx/muzzle\" \"flash\" 48 -36) "
            "(particle \"fx/generic-smoke\" 0.1 0.2 0.3))\n"
            "  )\n"
            ")\n",
            bank);
        CHECK(ok);
        CHECK_EQ(bank.clips.size(), 1u);
        CHECK_EQ(bank.clips[0].frames.size(), 1u);
        const SpriteAnimFrame& frame = bank.clips[0].frames[0];
        CHECK_EQ(frame.sound, std::string("pistol/fire"));
        CHECK_EQ(frame.hints.size(), 1u);
        CHECK_EQ(frame.hints[0], std::string("fire"));
        CHECK_EQ(frame.overlays.size(), 1u);
        CHECK_EQ(frame.overlays[0].layer, 1);
        CHECK_EQ(frame.overlays[0].sprite, std::string("fx/muzzle"));
        CHECK_EQ(frame.overlays[0].clip, std::string("flash"));
        CHECK_EQ(frame.overlays[0].x, 48.0f);
        CHECK_EQ(frame.overlays[0].y, -36.0f);
        CHECK_EQ(frame.particles.size(), 1u);
        CHECK_EQ(frame.particles[0].system, std::string("fx/generic-smoke"));
        CHECK_EQ(frame.particles[0].x, 0.1f);
        CHECK_EQ(frame.particles[0].y, 0.2f);
        CHECK_EQ(frame.particles[0].z, 0.3f);

        const std::string serialized = serializeSpriteAnimBank(bank);
        SpriteAnimBank roundTrip{};
        CHECK(parseSpriteAnimBank(serialized, roundTrip));
        CHECK_EQ(roundTrip.clips[0].frames[0].overlays.size(), 1u);
        CHECK_EQ(roundTrip.clips[0].frames[0].overlays[0].layer, 1);
        CHECK_EQ(roundTrip.clips[0].frames[0].overlays[0].x, 48.0f);
        CHECK_EQ(roundTrip.clips[0].frames[0].particles.size(), 1u);
        CHECK_EQ(roundTrip.clips[0].frames[0].particles[0].system, std::string("fx/generic-smoke"));
    }

    {
        SpriteAnimBank bank{};
        const bool ok = parseSpriteAnimBank(
            "(sprite-anim\n"
            "  (clip \"fire\"\n"
            "    (frame \"B\" 0.05 (overlay 0 \"fx/muzzle\" \"flash\" 0 0))\n"
            "  )\n"
            ")\n",
            bank);
        CHECK_FALSE(ok);
    }
}

}
