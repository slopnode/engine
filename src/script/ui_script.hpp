#pragma once

#include <flecs.h>

struct s7_scheme;

namespace slopengine {

enum class PackageUiDrawSlot {
    None,
    FileMenu,
    PauseMenu,
    DebugMenu,
    Modals,
};

/** Binds ImGui helpers and menu/pause context predicates for package UI. */
void bindUiApi(flecs::world& world, s7_scheme* scheme);

/** Sets which package draw hook is active (validates ui-menu-item, etc.). */
void setPackageUiDrawSlot(PackageUiDrawSlot slot);

/** Calls (draw-file-menu) when defined. */
void callDrawFileMenu(flecs::world& world);

/** Calls (draw-pause-menu) when defined. */
void callDrawPauseMenu(flecs::world& world);

/** Calls (draw-debug-menu) when defined. */
void callDrawDebugMenu(flecs::world& world);

/** Calls (draw-modals) when defined. */
void callDrawModals(flecs::world& world);

}
