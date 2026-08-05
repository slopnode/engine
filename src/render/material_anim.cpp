#include "render/material_anim.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "assets/texture_anim_loader.hpp"
#include "game/game_state.hpp"
#include "render/components.hpp"

#include <raylib.h>
#include <unordered_set>

namespace slopengine {

namespace {

constexpr const char* kDefaultTextureAnimClip = "default";

std::unordered_set<std::string> collectActiveTextureAnimPaths(flecs::world& world) {
    std::unordered_set<std::string> animPaths;
    world.query<MaterialAnimTargets>().each([&](flecs::entity, MaterialAnimTargets& targets) {
        for (const MaterialAnimTarget& target : targets.targets) {
            if (!target.animPath.empty()) {
                animPaths.insert(target.animPath);
            }
        }
    });
    return animPaths;
}

void advanceTextureAnimClocks(flecs::world& world, AssetStore& assets, const float delta) {
    MaterialAnimClocks& clocks = world.get_mut<MaterialAnimClocks>();
    for (const std::string& animPath : collectActiveTextureAnimPaths(world)) {
        const TextureAnimBank* bank = assets.getTextureAnimBank(animPath);
        if (bank == nullptr) {
            continue;
        }
        const auto clipIt = bank->clipIndexByName.find(kDefaultTextureAnimClip);
        if (clipIt == bank->clipIndexByName.end() || clipIt->second >= bank->clips.size()) {
            continue;
        }
        const TextureAnimClip& clip = bank->clips[clipIt->second];
        if (clip.frames.empty()) {
            continue;
        }

        MaterialAnimClocks::Entry& entry = clocks.byAnimPath[animPath];
        entry.time += delta;
        entry.frameIndex = textureAnimFrameIndexAt(clip, entry.time);
    }
}

void applyTextureAnimFrames(flecs::world& world, AssetStore& assets) {
    MaterialAnimClocks& clocks = world.get_mut<MaterialAnimClocks>();

    for (auto& [animPath, entry] : clocks.byAnimPath) {
        if (entry.frameIndex == entry.lastAppliedFrameIndex) {
            continue;
        }

        const Texture2D frameTexture =
            assets.resolveTextureAnimFrame(animPath, kDefaultTextureAnimClip, entry.frameIndex);
        if (frameTexture.id == 0) {
            continue;
        }

        entry.lastAppliedFrameIndex = entry.frameIndex;

        world.query<Model3D, MaterialAnimTargets>().each(
            [&](flecs::entity, Model3D& model3d, MaterialAnimTargets& targets) {
                if (model3d.model.meshCount <= 0) {
                    return;
                }

                for (const MaterialAnimTarget& target : targets.targets) {
                    if (target.animPath != animPath || target.meshIndex < 0 ||
                        target.meshIndex >= model3d.model.meshCount) {
                        continue;
                    }

                    SetTextureWrap(frameTexture, TEXTURE_WRAP_REPEAT);
                    SetMaterialTexture(
                        &model3d.model.materials[target.meshIndex],
                        MATERIAL_MAP_ALBEDO,
                        frameTexture);
                }
            });
    }
}

} // namespace

void attachMaterialAnimTargets(flecs::entity entity, MaterialAnimTargets&& targets) {
    if (targets.targets.empty()) {
        return;
    }
    entity.set<MaterialAnimTargets>(std::move(targets));
    flecs::world world = entity.world();
    if (!world.has<MaterialAnimClocks>()) {
        world.set<MaterialAnimClocks>({});
    }
}

void attachMaterialAnimTargetsFromGeo(flecs::entity entity, const GeoAsset& geo, AssetStore& assets) {
    MaterialAnimTargets targets{};
    collectMaterialAnimTargets(geo, assets, targets);
    attachMaterialAnimTargets(entity, std::move(targets));
}

void registerMaterialAnimSystem(flecs::world& world) {
    world.system("AdvanceMaterialAnim")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world ecsWorld = it.world();
            if (isSimulationPaused(ecsWorld)) {
                return;
            }
            if (!ecsWorld.has<AssetServices>() || ecsWorld.get<AssetServices>().store == nullptr) {
                return;
            }
            if (!ecsWorld.has<MaterialAnimClocks>()) {
                return;
            }

            AssetStore& assets = *ecsWorld.get_mut<AssetServices>().store;
            advanceTextureAnimClocks(ecsWorld, assets, GetFrameTime());
            applyTextureAnimFrames(ecsWorld, assets);
        });
}

}
