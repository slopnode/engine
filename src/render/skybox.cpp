#include "render/skybox.hpp"

namespace slopengine {

namespace {

SkyboxSettings settingsFromInlineThing(const Thing& placement) {
    SkyboxSettings settings{};
    settings.mode = placement.skyboxMode;
    settings.solidColor = placement.color;
    settings.cubeFaces[0] = placement.skyCubePx;
    settings.cubeFaces[1] = placement.skyCubeNx;
    settings.cubeFaces[2] = placement.skyCubePy;
    settings.cubeFaces[3] = placement.skyCubeNy;
    settings.cubeFaces[4] = placement.skyCubePz;
    settings.cubeFaces[5] = placement.skyCubeNz;
    settings.gradientStops = placement.skyGradientStops;
    settings.gradientStopCount = placement.skyGradientStopCount;
    return settings;
}

Vector3 colorFromMaterialBase(const MaterialAsset& asset) {
    return {
        static_cast<float>(asset.baseColor.r) / 255.0f,
        static_cast<float>(asset.baseColor.g) / 255.0f,
        static_cast<float>(asset.baseColor.b) / 255.0f,
    };
}

} // namespace

bool materialHasSkyAppearance(const MaterialAsset& asset) {
    return asset.haveSkyMode;
}

SkyboxSettings skyboxSettingsFromMaterial(const MaterialAsset& asset) {
    SkyboxSettings settings{};
    if (asset.haveSkyMode) {
        settings.mode = asset.skyMode;
        switch (asset.skyMode) {
        case SkyboxMode::Solid:
            settings.solidColor = asset.skySolidColor;
            break;
        case SkyboxMode::Cube:
            for (int i = 0; i < 6; ++i) {
                settings.cubeFaces[i] = asset.skyCubeFaces[i];
            }
            break;
        case SkyboxMode::Gradient:
            settings.gradientStops = asset.skyGradientStops;
            settings.gradientStopCount = asset.skyGradientStopCount;
            break;
        }
        return settings;
    }
    if (asset.sky) {
        settings.mode = SkyboxMode::Solid;
        settings.solidColor = colorFromMaterialBase(asset);
    }
    return settings;
}

SkyboxSettings skyboxSettingsFromThing(const Thing& placement, AssetStore* assets) {
    if (placement.haveSkyboxMode) {
        return settingsFromInlineThing(placement);
    }
    if (!placement.skyMaterial.empty() && assets != nullptr) {
        const MaterialAsset* material = assets->getMaterialAsset(placement.skyMaterial);
        if (material != nullptr) {
            return skyboxSettingsFromMaterial(*material);
        }
    }
    return {};
}

}
