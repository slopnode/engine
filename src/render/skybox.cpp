#include "render/skybox.hpp"

#include "map/thing.hpp"

namespace slopengine {

namespace {

SkyboxSettings settingsFromInlineThing(const Thing& placement) {
    Thing normalized = placement;
    if (normalized.skyboxMode == SkyboxMode::Gradient &&
        normalized.skyGradientStopCount != 4) {
        ensureSkyboxGradientDefaults(normalized);
    }
    SkyboxSettings settings{};
    settings.mode = normalized.skyboxMode;
    settings.solidColor = normalized.color;
    settings.cubeFaces[0] = normalized.skyCubePx;
    settings.cubeFaces[1] = normalized.skyCubeNx;
    settings.cubeFaces[2] = normalized.skyCubePy;
    settings.cubeFaces[3] = normalized.skyCubeNy;
    settings.cubeFaces[4] = normalized.skyCubePz;
    settings.cubeFaces[5] = normalized.skyCubeNz;
    settings.gradientStops = normalized.skyGradientStops;
    settings.gradientStopCount = normalized.skyGradientStopCount;
    settings.cylinderTexture = normalized.skyCylinderTexture;
    settings.cylinderOffset = normalized.skyCylinderOffset;
    settings.cylinderScale = normalized.skyCylinderScale;
    settings.cylinderRepeat = normalized.skyCylinderRepeat;
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
        case SkyboxMode::Cylinder:
            settings.cylinderTexture = asset.skyCylinderTexture;
            settings.cylinderOffset = asset.skyCylinderOffset;
            settings.cylinderScale = asset.skyCylinderScale;
            settings.cylinderRepeat = asset.skyCylinderRepeat;
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
