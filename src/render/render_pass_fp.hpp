#pragma once

#include "render/components.hpp"
#include "render/render_context.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string>

namespace slopengine {

void drawFirstPersonPass(
    flecs::world& world,
    RenderContext& context,
    const Lens& lens,
    bool unlit);

void drawViewSpritesAndHud(flecs::world& world);

void drawSpriteAimHudText(const std::string& spriteAimStatus);

}
