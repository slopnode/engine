#include "assets/asset_store.hpp"
#include "assets/material_loader.hpp"
#include "game/app_config.hpp"
#include "map/bsp_analyze.hpp"
#include "map/bsp_io.hpp"
#include "map/csg_script.hpp"
#include "map/lightmap.hpp"
#include "map/radiosity.hpp"
#include "map/radiosity_gpu.hpp"

#include <raylib.h>

#include <s7.h>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

struct RadCli {
    slopengine::AppConfig config;
    slopengine::RadiositySettings settings;
};

std::optional<RadCli> parseRadCli(int argc, char* argv[]) {
    RadCli cli;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "sloprad: missing value for " << name << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--base-game") {
            const char* value = needValue("--base-game");
            if (value == nullptr) {
                return std::nullopt;
            }
            cli.config.base_game = value;
        } else if (arg == "--mod") {
            const char* value = needValue("--mod");
            if (value == nullptr) {
                return std::nullopt;
            }
            cli.config.mods.emplace_back(value);
        } else if (arg == "--map") {
            const char* value = needValue("--map");
            if (value == nullptr) {
                return std::nullopt;
            }
            cli.config.map = value;
        } else if (arg == "--luxels-per-meter") {
            const char* value = needValue("--luxels-per-meter");
            if (value == nullptr) {
                return std::nullopt;
            }
            float parsed = 16.0f;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.luxelsPerMeter = parsed;
        } else if (arg == "--bounces") {
            const char* value = needValue("--bounces");
            if (value == nullptr) {
                return std::nullopt;
            }
            int parsed = 2;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.bounces = parsed;
        } else if (arg == "--samples") {
            const char* value = needValue("--samples");
            if (value == nullptr) {
                return std::nullopt;
            }
            int parsed = 32;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.samples = parsed;
        } else if (arg == "--gpu") {
            cli.settings.preferGpu = true;
        } else if (arg == "--cpu") {
            cli.settings.preferGpu = false;
        } else {
            return std::nullopt;
        }
    }

    if (cli.config.base_game.empty() || !cli.config.map) {
        return std::nullopt;
    }
    return cli;
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace slopengine;

    auto cli = parseRadCli(argc, argv);
    if (!cli) {
        std::cerr
            << "Usage: sloprad --base-game <path> [--mod <path>]... --map <name>\n"
            << "       [--luxels-per-meter N] [--bounces N] [--samples N] [--gpu|--cpu]\n";
        return 1;
    }

    SetTraceLogLevel(LOG_INFO);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(16, 16, "sloprad");
    if (!IsWindowReady()) {
        std::cerr << "sloprad: failed to create OpenGL context\n";
        return 1;
    }

    AssetStore assets(cli->config);

    if (cli->settings.preferGpu) {
        cli->settings.directComputeShaderSource = assets.getShaderSource("tools/rad_direct_comp");
        if (cli->settings.directComputeShaderSource.empty()) {
            TraceLog(
                LOG_WARNING,
                "sloprad: missing shaders/tools/rad_direct_comp.glsl; GPU direct lighting disabled");
            std::fflush(stdout);
        } else if (!radiosityGpuContextReady()) {
            TraceLog(
                LOG_WARNING,
                "sloprad: OpenGL compute unavailable; GPU direct lighting disabled");
            std::fflush(stdout);
        } else {
            TraceLog(LOG_INFO, "sloprad: GPU direct lighting enabled");
            std::fflush(stdout);
        }
    } else {
        TraceLog(LOG_INFO, "sloprad: CPU direct lighting forced");
        std::fflush(stdout);
    }

    TraceLog(LOG_INFO, "sloprad: map='%s'", cli->config.map->c_str());
    std::fflush(stdout);

    auto mapMeta = loadMapMeta(assets, *cli->config.map);
    if (!mapMeta) {
        std::cerr << "sloprad: failed to load map meta\n";
        CloseWindow();
        return 1;
    }
    TraceLog(
        LOG_INFO,
        "sloprad: meta id='%s' ambient=(%.3f %.3f %.3f)",
        mapMeta->id.c_str(),
        mapMeta->ambient.x,
        mapMeta->ambient.y,
        mapMeta->ambient.z);
    std::fflush(stdout);

    const std::string bspVirtualPath = *cli->config.map + "/static";
    auto bspPath = assets.resolvePath(AssetKind::MapBsp, bspVirtualPath);
    if (!bspPath) {
        std::cerr << "sloprad: missing maps/" << bspVirtualPath << ".bsp (run slopbsp first)\n";
        CloseWindow();
        return 1;
    }

    TraceLog(LOG_INFO, "sloprad: loading %s", bspPath->string().c_str());
    std::fflush(stdout);
    auto tree = readBspFile(*bspPath);
    if (!tree) {
        std::cerr << "sloprad: failed to read " << *bspPath << "\n";
        CloseWindow();
        return 1;
    }
    TraceLog(
        LOG_INFO,
        "sloprad: bsp nodes=%d leaves=%d surfaces=%d",
        static_cast<int>(tree->nodes.size()),
        static_cast<int>(tree->leaves.size()),
        static_cast<int>(tree->surfaceFaces.size()));
    std::fflush(stdout);

    s7_scheme* scheme = s7_init();
    if (scheme == nullptr) {
        std::cerr << "sloprad: failed to init scheme\n";
        CloseWindow();
        return 1;
    }
    auto brushes = loadMapBrushes(scheme, assets, *cli->config.map);
    s7_quit(scheme);
    if (!brushes) {
        std::cerr << "sloprad: failed to load map brushes\n";
        CloseWindow();
        return 1;
    }

    const MapHullAnalysis analysis = analyzeMapHull(*tree, *brushes);
    if (!analysis.sealed) {
        TraceLog(
            LOG_WARNING,
            "sloprad: map hull is not sealed; skipping auto-nodraw (authored nodraw only)");
        for (const std::string& step : analysis.leakPathFaceIds) {
            TraceLog(LOG_WARNING, "sloprad: leak path %s", step.c_str());
        }
    } else {
        applyInferredNodraw(*brushes, analysis);
        TraceLog(
            LOG_INFO,
            "sloprad: auto-nodraw faces=%d",
            static_cast<int>(analysis.inferredNodrawFaceIds.size()));
        for (const std::string& warning : analysis.detailOutsideWarnings) {
            TraceLog(LOG_WARNING, "sloprad: %s", warning.c_str());
        }
    }

    const std::vector<LightmapFace> faces = collectLightmapFaces(*brushes);
    TraceLog(LOG_INFO, "sloprad: lightmap faces=%d", static_cast<int>(faces.size()));
    std::fflush(stdout);

    auto resolveMaterial = [&assets](std::string_view materialPath) {
        MaterialBakeInfo info;
        const MaterialAsset* asset = assets.getMaterialAsset(materialPath);
        if (asset != nullptr) {
            info.asset = *asset;
        }
        if (!info.asset.albedoTexture.empty()) {
            const auto path = assets.resolvePath(AssetKind::Texture, info.asset.albedoTexture);
            if (path) {
                info.albedoImage = LoadImage(path->string().c_str());
                info.hasAlbedoImage = info.albedoImage.data != nullptr;
            }
        }
        if (!info.asset.emissionTexture.empty()) {
            const auto path = assets.resolvePath(AssetKind::Texture, info.asset.emissionTexture);
            if (path) {
                info.emissionImage = LoadImage(path->string().c_str());
                info.hasEmissionImage = info.emissionImage.data != nullptr;
            }
        }
        return info;
    };

    const auto mapDir = bspPath->parent_path();
    const auto radDir = mapDir / "rad";
    if (std::filesystem::exists(radDir)) {
        TraceLog(LOG_INFO, "sloprad: clearing %s", radDir.string().c_str());
        std::fflush(stdout);
        std::filesystem::remove_all(radDir);
    }
    std::filesystem::create_directories(radDir);

    RadiosityBakeResult baked =
        bakeRadiosity(faces, *mapMeta, resolveMaterial, cli->settings);

    const auto radPath = radDir / "static.rad";
    TraceLog(LOG_INFO, "sloprad: writing %s", radPath.string().c_str());
    std::fflush(stdout);
    if (!writeRadFile(radPath, baked.rad)) {
        std::cerr << "sloprad: failed to write " << radPath << "\n";
        for (Image& image : baked.atlasImages) {
            UnloadImage(image);
        }
        CloseWindow();
        return 1;
    }

    for (std::size_t i = 0; i < baked.atlasImages.size(); ++i) {
        const auto pngPath = radDir / (baked.rad.atlases[i].texturePath + ".png");
        if (!ExportImage(baked.atlasImages[i], pngPath.string().c_str())) {
            std::cerr << "sloprad: failed to write " << pngPath << "\n";
            for (Image& image : baked.atlasImages) {
                UnloadImage(image);
            }
            CloseWindow();
            return 1;
        }
        UnloadImage(baked.atlasImages[i]);
        TraceLog(LOG_INFO, "sloprad: wrote %s", pngPath.string().c_str());
        std::fflush(stdout);
    }

    TraceLog(
        LOG_INFO,
        "sloprad: done output=%s atlases=%d charts=%d",
        radDir.string().c_str(),
        static_cast<int>(baked.rad.atlases.size()),
        static_cast<int>(baked.rad.charts.size()));
    std::fflush(stdout);
    CloseWindow();
    return 0;
}
