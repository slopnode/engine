#include "test_assert.hpp"

#include "assets/material_loader.hpp"
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
            "(overlay 1 \"fx/muzzle\" \"flash\" 48 -36))\n"
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

        const std::string serialized = serializeSpriteAnimBank(bank);
        SpriteAnimBank roundTrip{};
        CHECK(parseSpriteAnimBank(serialized, roundTrip));
        CHECK_EQ(roundTrip.clips[0].frames[0].overlays.size(), 1u);
        CHECK_EQ(roundTrip.clips[0].frames[0].overlays[0].layer, 1);
        CHECK_EQ(roundTrip.clips[0].frames[0].overlays[0].x, 48.0f);
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
