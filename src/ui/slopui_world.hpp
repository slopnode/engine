#pragma once

#include <flecs.h>
#include <imgui.h>

#include <string_view>

struct s7_scheme;

namespace slopengine {

class AssetStore;

enum class UiSoundKind {
    Hover,
    Confirm,
    Back,
};

struct SlopUiSoundState {
    ImGuiID lastHovered = 0;
};

/** Loads the merged package theme, applies it, and stores theme and fonts as world singletons. */
void installSlopUiTheme(flecs::world& world, AssetStore& assets, s7_scheme* scheme, std::string_view themeId);

void playUiSound(flecs::world world, UiSoundKind kind);

void updateUiHoverSound(flecs::world world);

}
