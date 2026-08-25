#pragma once

#include <flecs.h>

#include <string>

struct ImFont;

namespace slopengine {

void registerUiModule(
    flecs::world& world,
    bool debugEnabled = false,
    std::string profile = "default",
    ImFont* monoFont = nullptr);
void prepareUiInput(flecs::world world);
void drawUi(flecs::world world);

}