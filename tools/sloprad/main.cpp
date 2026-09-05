#include "assets/asset_store.hpp"
#include "assets/material_loader.hpp"
#include "core/log.hpp"
#include "game/app_config.hpp"
#include "map/bsp_analyze.hpp"
#include "map/bsp_io.hpp"
#include "map/map_script.hpp"
#include "map/lightmap.hpp"
#include "map/radiosity.hpp"
#include "map/radiosity_gpu.hpp"
#include "map/radiosity_lights.hpp"
#if defined(__linux__)
#include "headless_gl_context.hpp"
#endif

#include <raylib.h>
#include <raymath.h>

#include <s7.h>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct RadCli {
    slopengine::AppConfig config;
    slopengine::RadiositySettings settings;
    std::optional<bool> gpuSafeOverride;
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
        } else if (arg == "--emitter-direct-samples") {
            const char* value = needValue("--emitter-direct-samples");
            if (value == nullptr) {
                return std::nullopt;
            }
            int parsed = 4;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.emitterDirectSamples = parsed;
        } else if (arg == "--emitter-grid-luxels-per-meter") {
            const char* value = needValue("--emitter-grid-luxels-per-meter");
            if (value == nullptr) {
                return std::nullopt;
            }
            float parsed = 8.0f;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.emitterGridLuxelsPerMeter = parsed;
        } else if (arg == "--emitter-grid-max-size") {
            const char* value = needValue("--emitter-grid-max-size");
            if (value == nullptr) {
                return std::nullopt;
            }
            int parsed = 32;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.emitterGridMaxSize = parsed;
        } else if (arg == "--exact-emission-grid-max-size") {
            const char* value = needValue("--exact-emission-grid-max-size");
            if (value == nullptr) {
                return std::nullopt;
            }
            int parsed = 256;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.exactEmissionGridMaxSize = parsed;
        } else if (arg == "--exact-emission-max-samples") {
            const char* value = needValue("--exact-emission-max-samples");
            if (value == nullptr) {
                return std::nullopt;
            }
            int parsed = 8192;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.exactEmissionMaxSamples = parsed;
        } else if (arg == "--sun-shadow-softness") {
            const char* value = needValue("--sun-shadow-softness");
            if (value == nullptr) {
                return std::nullopt;
            }
            float parsed = 0.0f;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.sunShadowSoftness = parsed;
        } else if (arg == "--seam-stitch-radius") {
            const char* value = needValue("--seam-stitch-radius");
            if (value == nullptr) {
                return std::nullopt;
            }
            float parsed = 1.5f;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.seamStitchRadiusLuxels = parsed;
        } else if (arg == "--probe-cell-size") {
            const char* value = needValue("--probe-cell-size");
            if (value == nullptr) {
                return std::nullopt;
            }
            float parsed = 4.0f;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.probeCellSize = parsed;
        } else if (arg == "--probe-fine-cell-size") {
            const char* value = needValue("--probe-fine-cell-size");
            if (value == nullptr) {
                return std::nullopt;
            }
            float parsed = 2.0f;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.probeFineCellSize = parsed;
        } else if (arg == "--probe-sample-count") {
            const char* value = needValue("--probe-sample-count");
            if (value == nullptr) {
                return std::nullopt;
            }
            int parsed = 32;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{}) {
                return std::nullopt;
            }
            cli.settings.probeSampleCount = parsed;
        } else if (arg == "--gpu") {
            cli.settings.preferGpu = true;
        } else if (arg == "--cpu") {
            cli.settings.preferGpu = false;
        } else if (arg == "--gpu-safe") {
            cli.gpuSafeOverride = true;
        } else if (arg == "--gpu-fast") {
            cli.gpuSafeOverride = false;
        } else if (arg == "--gpu-watchdog-limit") {
            const char* value = needValue("--gpu-watchdog-limit");
            if (value == nullptr) {
                return std::nullopt;
            }
            float parsed = 2.0f;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{} || !(parsed > 0.0f)) {
                return std::nullopt;
            }
            cli.settings.gpuWatchdogLimitSeconds = parsed;
        } else if (arg == "--gpu-max-luxel-batch") {
            const char* value = needValue("--gpu-max-luxel-batch");
            if (value == nullptr) {
                return std::nullopt;
            }
            int parsed = 0;
            const auto result = std::from_chars(value, value + std::strlen(value), parsed);
            if (result.ec != std::errc{} || parsed <= 0) {
                return std::nullopt;
            }
            cli.settings.gpuMaxLuxelBatch = parsed;
        } else if (arg == "--verbose") {
            cli.config.verbose = true;
        } else {
            return std::nullopt;
        }
    }

    if (cli.config.base_game.empty() || !cli.config.map) {
        return std::nullopt;
    }
    return cli;
}

// EGL surfaceless context creation sidesteps GLFW's X11/Wayland window-system
// requirement, which CI containers running with no display server can't
// satisfy. Windows builds always run with a real desktop session available,
// so they keep using raylib's normal windowed context there instead.
#if defined(__linux__)
bool initGLContext() {
    return slopengine::InitHeadlessGLContext(16, 16);
}

void closeGLContext() {
    slopengine::CloseHeadlessGLContext();
}
#else
bool initGLContext() {
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(16, 16, "sloprad");
    return IsWindowReady();
}

void closeGLContext() {
    CloseWindow();
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    using namespace slopengine;

    auto cli = parseRadCli(argc, argv);
    if (!cli) {
        std::cerr
            << "Usage: sloprad --base-game <path> [--mod <path>]... --map <name>\n"
            << "       [--luxels-per-meter N] [--bounces N] [--samples N]\n"
            << "       [--emitter-direct-samples N] [--emitter-grid-luxels-per-meter N]\n"
            << "       [--emitter-grid-max-size N] [--exact-emission-grid-max-size N]\n"
            << "       [--exact-emission-max-samples N] [--sun-shadow-softness N]\n"
            << "       [--seam-stitch-radius N]\n"
            << "       [--probe-cell-size N] [--probe-fine-cell-size N] [--probe-sample-count N]\n"
            << "       [--gpu|--cpu]\n"
            << "       [--gpu-safe|--gpu-fast] [--gpu-watchdog-limit SECONDS]\n"
            << "       [--gpu-max-luxel-batch N]\n";
        return 1;
    }

    Log::init(cli->config.verbose ? LogLevel::Debug : LogLevel::Info);
    Log::addDefaultConsoleSink();
    if (!initGLContext()) {
        std::cerr << "sloprad: failed to create OpenGL context\n";
        return 1;
    }

    AssetStore assets(cli->config);

    if (cli->gpuSafeOverride.has_value()) {
        cli->settings.gpuSafeMode = *cli->gpuSafeOverride;
    } else if (cli->settings.preferGpu && radiosityGpuIsIntegrated()) {
        cli->settings.gpuSafeMode = true;
    }
    if (cli->settings.preferGpu) {
        const char* renderer = radiosityGpuRenderer();
        if (renderer[0] != '\0') {
            TraceLog(LOG_INFO, "sloprad: GL renderer '%s'", renderer);
            std::fflush(stdout);
        }
        if (cli->settings.gpuSafeMode) {
            TraceLog(LOG_INFO, "sloprad: GPU safe mode enabled (smaller batches)");
            std::fflush(stdout);
        }
        TraceLog(
            LOG_INFO,
            "sloprad: GPU watchdog limit %.2fs",
            cli->settings.gpuWatchdogLimitSeconds);
        std::fflush(stdout);
        if (cli->settings.gpuMaxLuxelBatch > 0) {
            TraceLog(
                LOG_INFO,
                "sloprad: GPU max luxel batch %d",
                cli->settings.gpuMaxLuxelBatch);
            std::fflush(stdout);
        }
    }

    if (cli->settings.preferGpu) {
        cli->settings.directComputeShaderSource = assets.getShaderSource("tools/rad_direct_comp");
        cli->settings.bounceComputeShaderSource = assets.getShaderSource("tools/rad_bounce_comp");
        if (cli->settings.directComputeShaderSource.empty()) {
            TraceLog(
                LOG_WARNING,
                "sloprad: missing shaders/tools/rad_direct_comp.glsl; GPU direct lighting disabled");
            std::fflush(stdout);
        }
        if (cli->settings.bounceComputeShaderSource.empty()) {
            TraceLog(
                LOG_WARNING,
                "sloprad: missing shaders/tools/rad_bounce_comp.glsl; GPU bounce lighting disabled");
            std::fflush(stdout);
        }
        if (!radiosityGpuContextReady()) {
            TraceLog(
                LOG_WARNING,
                "sloprad: OpenGL compute unavailable; GPU lighting disabled");
            std::fflush(stdout);
        } else if (
            !cli->settings.directComputeShaderSource.empty()
            || !cli->settings.bounceComputeShaderSource.empty()) {
            TraceLog(
                LOG_INFO,
                "sloprad: GPU lighting enabled (direct=%s bounce=%s)",
                cli->settings.directComputeShaderSource.empty() ? "no" : "yes",
                cli->settings.bounceComputeShaderSource.empty() ? "no" : "yes");
            std::fflush(stdout);
        }
    } else {
        TraceLog(LOG_INFO, "sloprad: CPU lighting forced");
        std::fflush(stdout);
    }

    TraceLog(LOG_INFO, "sloprad: map='%s'", cli->config.map->c_str());
    std::fflush(stdout);

    auto mapMeta = loadMapMeta(assets, *cli->config.map);
    if (!mapMeta) {
        std::cerr << "sloprad: failed to load map meta\n";
        assets.releaseGpuResources();
        closeGLContext();
        return 1;
    }
    TraceLog(
        LOG_INFO,
        "sloprad: meta id='%s' ambient=(%.3f %.3f %.3f) sun=%s",
        mapMeta->id.c_str(),
        mapMeta->ambient.x,
        mapMeta->ambient.y,
        mapMeta->ambient.z,
        mapMeta->sun.enabled ? "yes" : "no");
    std::fflush(stdout);

    const std::string bspVirtualPath = *cli->config.map + "/compiled/bsp";
    auto bspPath = assets.resolvePath(AssetKind::MapBsp, bspVirtualPath);
    if (!bspPath) {
        std::cerr << "sloprad: missing maps/" << bspVirtualPath << " (run slopbsp first)\n";
        assets.releaseGpuResources();
        closeGLContext();
        return 1;
    }

    TraceLog(LOG_INFO, "sloprad: loading %s", bspPath->string().c_str());
    std::fflush(stdout);
    auto tree = readBspFile(*bspPath);
    if (!tree) {
        std::cerr << "sloprad: failed to read " << *bspPath << "\n";
        assets.releaseGpuResources();
        closeGLContext();
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
        assets.releaseGpuResources();
        closeGLContext();
        return 1;
    }
    loadPackageMapHandlers(scheme, assets);
    loadPackageThings(scheme, assets);
    auto brushes = loadMapBrushes(scheme, assets, *cli->config.map);
    if (!brushes) {
        s7_quit(scheme);
        std::cerr << "sloprad: failed to load map brushes\n";
        assets.releaseGpuResources();
        closeGLContext();
        return 1;
    }
    slopengine::RadiosityThingLights thingLights =
        slopengine::collectRadiosityLights(scheme, assets, *cli->config.map);
    s7_quit(scheme);
    if (mapMeta->sun.enabled) {
        TraceLog(
            LOG_WARNING,
            "sloprad: map.meta (sun ...) is ignored; place a sun thing for directional bake light");
        std::fflush(stdout);
    }
    mapMeta->ambient = thingLights.hasAmbient ? thingLights.ambient : Vector3{0.0f, 0.0f, 0.0f};
    TraceLog(
        LOG_INFO,
        "sloprad: ambient=(%.3f %.3f %.3f) from=%s",
        mapMeta->ambient.x,
        mapMeta->ambient.y,
        mapMeta->ambient.z,
        thingLights.hasAmbient ? "ambient-light" : "none(black)");
    std::fflush(stdout);
    const std::vector<slopengine::RadiosityLight>& lights = thingLights.lights;
    int pointCount = 0;
    int spotCount = 0;
    int sunCount = 0;
    for (const slopengine::RadiosityLight& light : lights) {
        if (light.kind == slopengine::RadiosityLightKind::Spot) {
            ++spotCount;
        } else if (light.kind == slopengine::RadiosityLightKind::Sun) {
            ++sunCount;
        } else {
            ++pointCount;
        }
    }
    TraceLog(
        LOG_INFO,
        "sloprad: bake lights=%d (point=%d spot=%d sun=%d)",
        static_cast<int>(lights.size()),
        pointCount,
        spotCount,
        sunCount);
    std::fflush(stdout);

    const std::string visVirtualPath = *cli->config.map + "/compiled/vis";
    if (!assets.hasMapVis(visVirtualPath)) {
        std::cerr << "sloprad: missing maps/" << visVirtualPath << " (run slopvis first)\n";
        assets.releaseGpuResources();
        closeGLContext();
        return 1;
    }

    const MapHullAnalysis analysis = analyzeMapHull(*tree, *brushes);
    if (!analysis.sealed) {
        TraceLog(
            LOG_WARNING,
            "sloprad: map hull is not sealed; baking authored faces");
        for (const std::string& step : analysis.leakPathFaceIds) {
            TraceLog(LOG_WARNING, "sloprad: leak path %s", step.c_str());
        }
    } else {
        for (const std::string& warning : analysis.detailOutsideWarnings) {
            TraceLog(LOG_WARNING, "sloprad: %s", warning.c_str());
        }
    }

    std::vector<LightmapFace> faces = collectLightmapFaces(*brushes);
    const std::vector<LightmapFace> nodrawOcclusionFaces = collectNodrawOcclusionFaces(*brushes);
    TraceLog(
        LOG_INFO,
        "sloprad: lightmap faces=%d (from authored brushes), nodraw occlusion faces=%d",
        static_cast<int>(faces.size()),
        static_cast<int>(nodrawOcclusionFaces.size()));
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
        } else if (!info.asset.textureAnimPath.empty()) {
            const Texture2D texture = assets.resolveTextureAnimFrame(info.asset.textureAnimPath, "default", 0);
            if (texture.id != 0) {
                info.albedoImage = LoadImageFromTexture(texture);
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

    RadiosityBakeResult baked = bakeRadiosity(
        faces,
        *mapMeta,
        resolveMaterial,
        cli->settings,
        lights,
        &*tree,
        analysis.sealed,
        nodrawOcclusionFaces);

    const auto radPath = radDir / "lightmap";
    TraceLog(LOG_INFO, "sloprad: writing %s", radPath.string().c_str());
    std::fflush(stdout);
    if (!writeRadFile(radPath, baked.rad)) {
        std::cerr << "sloprad: failed to write " << radPath << "\n";
        for (Image& image : baked.atlasImages) {
            UnloadImage(image);
        }
        assets.releaseGpuResources();
        closeGLContext();
        return 1;
    }

    for (std::size_t i = 0; i < baked.atlasImages.size(); ++i) {
        const auto pngPath = radDir / (baked.rad.atlases[i].texturePath + ".png");
        ImageFormat(&baked.atlasImages[i], PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        if (!ExportImage(baked.atlasImages[i], pngPath.string().c_str())) {
            std::cerr << "sloprad: failed to write " << pngPath << "\n";
            for (Image& image : baked.atlasImages) {
                UnloadImage(image);
            }
            assets.releaseGpuResources();
            closeGLContext();
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
    assets.releaseGpuResources();
    closeGLContext();
    return 0;
}
