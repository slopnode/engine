#include "game/script_boot.hpp"

#include "game/game_state.hpp"
#include "script/first_person_script.hpp"
#include "script/hud_script.hpp"
#include "script/input_script.hpp"
#include "script/save_script.hpp"
#include "script/script_scope.hpp"
#include "script/thing_script.hpp"
#include "script/ui_script.hpp"

#include <raylib.h>

namespace slopengine {

void registerScriptBoot(flecs::world& world, AssetStore& assets, s7_scheme* scheme) {
    bindFirstPersonApi(world, scheme);
    bindHudApi(world, scheme);
    bindInputApi(world, scheme);
    bindThingRuntimeApi(world, scheme);
    bindSaveApi(world, assets, scheme);
    bindUiApi(world, scheme);
    {
        ScriptScopeGuard bootScope(ScriptScope::Boot);
        if (scheme != nullptr && !assets.loadScript(scheme, "player")) {
            TraceLog(LOG_WARNING, "SCRIPT: player.s7 not loaded");
        }
        if (scheme != nullptr && !assets.loadScript(scheme, "menus")) {
            TraceLog(LOG_INFO, "SCRIPT: menus.s7 not loaded");
        }
    }

    TraceLog(LOG_INFO, "RENDER: entering menu (package on-startup / Debug → Map)");
    enterMenu(world);
}

}
