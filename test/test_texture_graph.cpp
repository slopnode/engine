#include "test_assert.hpp"

#include "assets/material_loader.hpp"
#include "assets/texture_graph.hpp"

#include <raylib.h>

#include <cstdio>
#include <string>

namespace slopengine {

namespace {

Color pixelAt(const Image& image, int x, int y) {
    return static_cast<const Color*>(image.data)[y * image.width + x];
}

bool resolveSolidImages(std::string_view path, Image& out) {
    if (path == "solid/red") {
        out = GenImageColor(4, 4, Color{255, 0, 0, 255});
    } else if (path == "solid/half-blue") {
        out = GenImageColor(2, 2, Color{0, 0, 255, 128});
    } else {
        return false;
    }
    ImageFormat(&out, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    return true;
}

const char* kSampleGraph =
    "(texture-graph\n"
    "  (node \"base\" image (texture \"solid/red\"))\n"
    "  (node \"overlay\" image (texture \"solid/half-blue\"))\n"
    "  (node \"comp\" stack\n"
    "    (layer \"base\")\n"
    "    (layer \"overlay\" (blend normal) (opacity 1) (offset 1 1) (repeat 0)))\n"
    "  (node \"glow\" color (size 4 4) (color 0 1 0 1))\n"
    "  (output \"albedo\" \"comp\")\n"
    "  (output \"emission\" \"glow\"))\n";

} // namespace

void runTextureGraphTests() {
    {
        TextureGraph graph{};
        std::string error;
        const bool ok = parseTextureGraph(kSampleGraph, graph, &error);
        CHECK(ok);
        CHECK_EQ(error, std::string());
        CHECK_EQ(graph.nodes.size(), 4u);
        CHECK_EQ(graph.outputs.size(), 2u);
        CHECK(graph.findNode("comp") != nullptr);
        CHECK(graph.findNode("comp")->kind == TextureGraphNodeKind::Stack);
        CHECK_EQ(graph.findNode("comp")->layers.size(), 2u);
        CHECK_EQ(graph.findNode("comp")->layers[1].offsetX, 1);
        CHECK(graph.findOutput("albedo") != nullptr);
        CHECK_EQ(graph.findOutput("albedo")->source, std::string("comp"));

        const std::string serialized = serializeTextureGraph(graph);
        TextureGraph roundTrip{};
        CHECK(parseTextureGraph(serialized, roundTrip, &error));
        CHECK_EQ(roundTrip.nodes.size(), 4u);
        CHECK_EQ(roundTrip.findNode("glow")->color.y, 1.0f);
        CHECK_EQ(roundTrip.findNode("comp")->layers[1].offsetY, 1);
        CHECK_EQ(serializeTextureGraph(roundTrip), serialized);
    }

    {
        TextureGraph graph{};
        std::string error;
        CHECK(parseTextureGraph(kSampleGraph, graph, &error));

        Image albedo{};
        const bool ok = evaluateTextureGraph(graph, "albedo", resolveSolidImages, albedo, error);
        CHECK(ok);
        CHECK_EQ(error, std::string());
        CHECK_EQ(albedo.width, 4);
        CHECK_EQ(albedo.height, 4);
        const Color corner = pixelAt(albedo, 0, 0);
        CHECK_EQ(static_cast<int>(corner.r), 255);
        CHECK_EQ(static_cast<int>(corner.b), 0);
        const Color inside = pixelAt(albedo, 1, 1);
        CHECK(inside.r > 120 && inside.r < 136);
        CHECK(inside.b > 120 && inside.b < 136);
        CHECK_EQ(static_cast<int>(inside.a), 255);
        const Color outsideOverlay = pixelAt(albedo, 3, 3);
        CHECK_EQ(static_cast<int>(outsideOverlay.r), 255);
        UnloadImage(albedo);

        Image emission{};
        CHECK(evaluateTextureGraph(graph, "emission", resolveSolidImages, emission, error));
        CHECK_EQ(emission.width, 4);
        CHECK_EQ(static_cast<int>(pixelAt(emission, 2, 2).g), 255);
        UnloadImage(emission);

        Image missing{};
        CHECK_FALSE(evaluateTextureGraph(graph, "normal", resolveSolidImages, missing, error));
        CHECK(!error.empty());
    }

    {
        TextureGraph graph{};
        std::string error;
        const bool ok = parseTextureGraph(
            "(texture-graph\n"
            "  (node \"a\" stack (layer \"b\"))\n"
            "  (node \"b\" stack (layer \"a\"))\n"
            "  (output \"albedo\" \"a\"))\n",
            graph,
            &error);
        CHECK_FALSE(ok);
        CHECK(error.find("cycle") != std::string::npos);
    }

    {
        TextureGraph graph{};
        std::string error;
        CHECK_FALSE(parseTextureGraph("(texture-graph (node \"a\" color) (output \"albedo\" \"zzz\"))", graph, &error));
        CHECK(error.find("unknown node") != std::string::npos);
        CHECK_FALSE(parseTextureGraph("(texture-graph (node \"a\" color))", graph, &error));
        CHECK_FALSE(parseTextureGraph("(texture-graph (node \"a\" blob) (output \"albedo\" \"a\"))", graph, &error));
        CHECK_FALSE(parseTextureGraph("(texture-graph (node \"a\" color) (node \"a\" color) (output \"albedo\" \"a\"))", graph, &error));
    }

    {
        TextureGraph graph{};
        std::string error;
        CHECK(parseTextureGraph(
            "(texture-graph\n"
            "  (node \"checks\" checker (size 8 8) (cells 2 2) (color 1 1 1 1) (color-b 0 0 0 1))\n"
            "  (node \"grad\" gradient (size 8 8) (angle 0) (color 0 0 0 1) (color-b 1 1 1 1))\n"
            "  (node \"n\" noise (size 8 8) (scale 2) (seed 3))\n"
            "  (node \"tiled\" stack (size 8 8) (layer \"small\" (repeat 1)))\n"
            "  (node \"small\" image (texture \"solid/half-blue\"))\n"
            "  (output \"albedo\" \"checks\")\n"
            "  (output \"grad\" \"grad\")\n"
            "  (output \"noise\" \"n\")\n"
            "  (output \"tiled\" \"tiled\"))\n",
            graph,
            &error));

        Image checks{};
        CHECK(evaluateTextureGraph(graph, "albedo", resolveSolidImages, checks, error));
        CHECK_EQ(static_cast<int>(pixelAt(checks, 0, 0).r), 255);
        CHECK_EQ(static_cast<int>(pixelAt(checks, 4, 0).r), 0);
        CHECK_EQ(static_cast<int>(pixelAt(checks, 4, 4).r), 255);
        UnloadImage(checks);

        Image grad{};
        CHECK(evaluateTextureGraph(graph, "grad", resolveSolidImages, grad, error));
        CHECK(pixelAt(grad, 0, 0).r < pixelAt(grad, 0, 7).r);
        UnloadImage(grad);

        Image noise{};
        CHECK(evaluateTextureGraph(graph, "noise", resolveSolidImages, noise, error));
        CHECK_EQ(noise.width, 8);
        UnloadImage(noise);

        Image tiled{};
        CHECK(evaluateTextureGraph(graph, "tiled", resolveSolidImages, tiled, error));
        CHECK_EQ(tiled.width, 8);
        CHECK_EQ(static_cast<int>(pixelAt(tiled, 7, 7).b), 255);
        CHECK_EQ(static_cast<int>(pixelAt(tiled, 7, 7).a), 128);
        UnloadImage(tiled);
    }

    {
        TextureGraph graph{};
        std::string error;
        CHECK(parseTextureGraph(
            "(texture-graph\n"
            "  (node \"n\" noise (size 16 16) (type value) (scale 4) (octaves 3) (persistence 0.5) (lacunarity 2) (seed 11))\n"
            "  (node \"w\" noise (size 16 16) (type worley) (scale 2) (seed 1))\n"
            "  (node \"gm\" gradient-map (input \"n\") (stop 1 1 0 0 1) (stop 0 0 0 1 1))\n"
            "  (node \"b\" bricks (size 16 8) (brick 8 4) (mortar 1) (row-offset 0.5) (color 1 0 0 1) (color-b 0 0 0 1))\n"
            "  (node \"s\" stripes (size 8 8) (stripe 2) (angle 0) (color 1 1 1 1) (color-b 0 0 0 1))\n"
            "  (node \"t\" transform (input \"s\") (size 8 8) (scale 1 1) (angle 90) (flip 0 0))\n"
            "  (node \"lv\" levels (input \"s\") (in-black 0) (in-white 1) (gamma 1) (out-black 0.25) (out-white 0.75))\n"
            "  (node \"h\" hsv (input \"b\") (hue 120) (saturation 1) (value 1))\n"
            "  (node \"i\" invert (input \"s\"))\n"
            "  (node \"bl\" blur (input \"s\") (radius 1))\n"
            "  (node \"sh\" sharpen (input \"s\") (amount 1))\n"
            "  (node \"e\" edges (input \"s\") (amount 1))\n"
            "  (node \"m\" stack (size 8 8) (layer \"i\" (repeat 1)) (layer \"s\" (repeat 1) (mask \"i\")))\n"
            "  (node \"m2\" stack (size 8 8) (layer \"i\" (repeat 1)) (layer \"s\" (repeat 1)))\n"
            "  (output \"n\" \"n\") (output \"w\" \"w\") (output \"gm\" \"gm\") (output \"b\" \"b\") (output \"s\" \"s\")\n"
            "  (output \"t\" \"t\") (output \"lv\" \"lv\") (output \"h\" \"h\") (output \"i\" \"i\") (output \"bl\" \"bl\")\n"
            "  (output \"sh\" \"sh\") (output \"e\" \"e\") (output \"m\" \"m\") (output \"m2\" \"m2\"))\n",
            graph,
            &error));
        CHECK_EQ(error, std::string());
        CHECK_EQ(graph.findNode("gm")->stops.size(), 2u);
        CHECK_EQ(graph.findNode("gm")->stops[0].position, 0.0f);
        CHECK(graph.findNode("n")->noiseType == TextureGraphNoiseType::Value);
        CHECK_EQ(graph.findNode("n")->octaves, 3);

        const std::string serialized = serializeTextureGraph(graph);
        TextureGraph roundTrip{};
        CHECK(parseTextureGraph(serialized, roundTrip, &error));
        CHECK_EQ(serializeTextureGraph(roundTrip), serialized);
        CHECK_EQ(roundTrip.findNode("m")->layers[1].mask, std::string("i"));
        CHECK_EQ(roundTrip.findNode("t")->angle, 90.0f);

        auto eval = [&](const char* name, Image& img) {
            const bool ok = evaluateTextureGraph(graph, name, resolveSolidImages, img, error);
            CHECK(ok);
            if (!ok) {
                std::fprintf(stderr, "output %s: %s\n", name, error.c_str());
            }
            return ok;
        };

        Image n{};
        if (eval("n", n)) {
            bool varied = false;
            for (int i = 1; i < 16 * 16 && !varied; ++i) {
                varied = static_cast<const Color*>(n.data)[i].r != static_cast<const Color*>(n.data)[0].r;
            }
            CHECK(varied);
            UnloadImage(n);
        }
        Image w{};
        if (eval("w", w)) {
            CHECK_EQ(w.width, 16);
            UnloadImage(w);
        }
        Image gm{};
        if (eval("gm", gm)) {
            const Color c = pixelAt(gm, 3, 3);
            CHECK(static_cast<int>(c.r) + static_cast<int>(c.b) > 200);
            CHECK_EQ(static_cast<int>(c.g), 0);
            UnloadImage(gm);
        }
        Image b{};
        if (eval("b", b)) {
            CHECK_EQ(static_cast<int>(pixelAt(b, 0, 0).r), 0);
            CHECK_EQ(static_cast<int>(pixelAt(b, 2, 2).r), 255);
            CHECK_EQ(static_cast<int>(pixelAt(b, 4, 4).r), 0);
            UnloadImage(b);
        }
        Image s{};
        if (eval("s", s)) {
            CHECK_EQ(static_cast<int>(pixelAt(s, 0, 0).r), 255);
            CHECK_EQ(static_cast<int>(pixelAt(s, 0, 2).r), 0);
            CHECK_EQ(static_cast<int>(pixelAt(s, 5, 0).r), 255);
            UnloadImage(s);
        }
        Image t{};
        if (eval("t", t)) {
            CHECK_EQ(static_cast<int>(pixelAt(t, 0, 0).r), static_cast<int>(pixelAt(t, 0, 5).r));
            CHECK(static_cast<int>(pixelAt(t, 0, 0).r) != static_cast<int>(pixelAt(t, 2, 0).r));
            UnloadImage(t);
        }
        Image lv{};
        if (eval("lv", lv)) {
            CHECK(pixelAt(lv, 0, 0).r > 180 && pixelAt(lv, 0, 0).r < 200);
            CHECK(pixelAt(lv, 0, 2).r > 56 && pixelAt(lv, 0, 2).r < 72);
            UnloadImage(lv);
        }
        Image h{};
        if (eval("h", h)) {
            CHECK_EQ(static_cast<int>(pixelAt(h, 2, 2).g), 255);
            CHECK_EQ(static_cast<int>(pixelAt(h, 2, 2).r), 0);
            UnloadImage(h);
        }
        Image i{};
        if (eval("i", i)) {
            CHECK_EQ(static_cast<int>(pixelAt(i, 0, 0).r), 0);
            CHECK_EQ(static_cast<int>(pixelAt(i, 0, 2).r), 255);
            UnloadImage(i);
        }
        Image bl{};
        if (eval("bl", bl)) {
            CHECK(pixelAt(bl, 0, 1).r > 100 && pixelAt(bl, 0, 1).r < 200);
            UnloadImage(bl);
        }
        Image sh{};
        if (eval("sh", sh)) {
            CHECK_EQ(sh.width, 8);
            UnloadImage(sh);
        }
        Image e{};
        if (eval("e", e)) {
            CHECK(pixelAt(e, 0, 1).r > 0);
            CHECK_EQ(static_cast<int>(pixelAt(e, 0, 1).a), 255);
            UnloadImage(e);
        }
        Image m{};
        if (eval("m", m)) {
            CHECK_EQ(static_cast<int>(pixelAt(m, 0, 0).r), 0);
            CHECK_EQ(static_cast<int>(pixelAt(m, 0, 2).r), 0);
            UnloadImage(m);
        }
        Image m2{};
        if (eval("m2", m2)) {
            CHECK_EQ(static_cast<int>(pixelAt(m2, 0, 0).r), 255);
            CHECK_EQ(static_cast<int>(pixelAt(m2, 0, 2).r), 0);
            UnloadImage(m2);
        }
    }

    {
        TextureGraph graph{};
        std::string error;
        CHECK_FALSE(parseTextureGraph("(texture-graph (node \"a\" invert) (output \"x\" \"a\"))", graph, &error));
        CHECK(error.find("needs an input") != std::string::npos);
        CHECK_FALSE(parseTextureGraph(
            "(texture-graph (node \"a\" invert (input \"b\")) (node \"b\" blur (input \"a\")) (output \"x\" \"a\"))",
            graph,
            &error));
        CHECK(error.find("cycle") != std::string::npos);
    }

    {
        MaterialAsset asset{};
        CHECK(parseMaterialAsset(
            "(material (shader \"default\") (texture-graph \"walls/comp\") (texel-size 32) (base-color 1 1 1 1))",
            asset));
        CHECK_EQ(asset.textureGraphPath, std::string("walls/comp"));
        CHECK(asset.albedoTexture.empty());

        MaterialAsset conflict{};
        CHECK_FALSE(parseMaterialAsset(
            "(material (texture \"a\") (texture-graph \"b\"))",
            conflict));
    }

    {
        MaterialAsset asset{};
        CHECK(parseMaterialAsset(
            "(material\n"
            "  (shader \"default\")\n"
            "  (texture-graph \"walls/comp\")\n"
            "  (texel-size 32)\n"
            "  (base-color 1 0 0 1)\n"
            "  (ior 1.2)\n"
            "  (emission \"walls/comp-glow\")\n"
            "  (emission-color 1 0.5 0 1)\n"
            "  (emission-power 6)\n"
            "  (emission-range 12)\n"
            "  (precise-emission)\n"
            "  (fullbright)\n"
            "  (emission-blend \"multiply\" 1.5)\n"
            "  (bake-emission)\n"
            "  (sound (absorption 0.1 0.2 0.3) (scattering 0.4) (transmission 0.5 0.6 0.7)))\n",
            asset));
        const std::string serialized = serializeMaterialAsset(asset);
        MaterialAsset roundTrip{};
        CHECK(parseMaterialAsset(serialized, roundTrip));
        CHECK_EQ(roundTrip.textureGraphPath, std::string("walls/comp"));
        CHECK_EQ(roundTrip.pixelsPerMeter, 32.0f);
        CHECK_EQ(static_cast<int>(roundTrip.baseColor.r), 255);
        CHECK_EQ(static_cast<int>(roundTrip.baseColor.g), 0);
        CHECK_EQ(roundTrip.ior, 1.2f);
        CHECK_EQ(roundTrip.emissionTexture, std::string("walls/comp-glow"));
        CHECK_EQ(roundTrip.emissionPower, 6.0f);
        CHECK_EQ(roundTrip.emissionRange, 12.0f);
        CHECK(roundTrip.preciseEmission);
        CHECK_FALSE(roundTrip.exactEmission);
        CHECK(roundTrip.fullbright);
        CHECK(roundTrip.emissionMultiply);
        CHECK_EQ(roundTrip.emissionBlendScale, 1.5f);
        CHECK(roundTrip.bakeEmission);
        CHECK_EQ(roundTrip.sound.absorptionMid, 0.2f);
        CHECK_EQ(roundTrip.sound.scattering, 0.4f);
        CHECK_EQ(roundTrip.sound.transmissionHigh, 0.7f);
        CHECK_EQ(serializeMaterialAsset(roundTrip), serialized);
    }

    {
        MaterialAsset sky{};
        CHECK(parseMaterialAsset(
            "(material (shader \"sky\") (sky) (sky-gradient (stop 0 0.1 0.2 0.3) (stop 0.3 0.2 0.3 0.4) (stop 0.7 0.5 0.6 0.7) (stop 1 0.8 0.9 1)))",
            sky));
        MaterialAsset roundTrip{};
        CHECK(parseMaterialAsset(serializeMaterialAsset(sky), roundTrip));
        CHECK(roundTrip.sky);
        CHECK(roundTrip.skyMode == SkyboxMode::Gradient);
        CHECK_EQ(roundTrip.skyGradientStopCount, 4);
        CHECK_EQ(roundTrip.skyGradientStops[2].color.y, 0.6f);
    }
}

}
