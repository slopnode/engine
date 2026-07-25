#pragma once

#include "assets/asset_store.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string>

struct s7_scheme;

namespace slopengine {

enum class TitleImageFit {
    Stretch,
    Fit,
    Cover,
};

struct TitleCanvas {
    int width = 640;
    int height = 480;
};

struct MenuBackground {
    std::string mapName;
    std::string imagePath;
    TitleImageFit imageFit = TitleImageFit::Stretch;
    TitleCanvas canvas{};
    Texture2D image{};
    bool titleMapActive = false;
};

MenuBackground parsePackageTitleFromScheme(s7_scheme* scheme);

void loadMenuBackgroundConfig(flecs::world& world, AssetStore& assets, s7_scheme* scheme);

void applyMenuBackground(flecs::world& world, AssetStore& assets);

void clearMenuBackgroundImage(flecs::world& world);

void markTitleMapActive(flecs::world& world, bool active);

bool menuTitleMapActive(const flecs::world& world);

bool shouldDrawWorld(const flecs::world& world);

void drawMenuBackgroundImage(const flecs::world& world);

void drawMenuTitleCanvas(flecs::world& world);

}
