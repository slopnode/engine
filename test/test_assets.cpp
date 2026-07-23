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
}

}
