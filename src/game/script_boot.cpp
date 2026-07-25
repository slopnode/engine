#include "game/script_boot.hpp"

#include "core/package.hpp"
#include "game/game_state.hpp"
#include "game/menu_background.hpp"
#include "script/first_person_script.hpp"
#include "script/hook_registry.hpp"
#include "script/hud_script.hpp"
#include "script/input_script.hpp"
#include "script/save_script.hpp"
#include "script/script_scope.hpp"
#include "script/thing_script.hpp"
#include "script/ui_script.hpp"

#include <raylib.h>

#include <string>

namespace slopengine {

void registerScriptBoot(flecs::world& world, AssetStore& assets, s7_scheme* scheme) {
    bindFirstPersonApi(world, scheme);
    bindHudApi(world, scheme);
    bindInputApi(world, scheme);
    bindThingRuntimeApi(world, scheme);
    bindSaveApi(world, assets, scheme);
    bindHookApi(scheme);
    bindUiApi(world, scheme);
    {
        ScriptScopeGuard bootScope(ScriptScope::Boot);
        const std::string baseId{assets.basePackageId()};
        if (scheme != nullptr && !baseId.empty()) {
            if (!assets.loadScriptFromPackage(scheme, baseId, "player")) {
                TraceLog(LOG_WARNING, "SCRIPT: player.s7 not loaded");
            }
            if (!assets.loadScriptFromPackage(scheme, baseId, "menus")) {
                TraceLog(LOG_INFO, "SCRIPT: menus.s7 not loaded");
            }
            captureHookOwners(scheme);
        } else if (scheme != nullptr) {
            TraceLog(LOG_WARNING, "SCRIPT: base package id missing; player/menus not loaded");
        }

        if (scheme != nullptr) {
            for (const Package& package : assets.packages()) {
                if (package.role() != PackageRole::Mod) {
                    continue;
                }
                if (assets.loadScriptFromPackage(scheme, package.meta().id, "contrib")) {
                    TraceLog(
                        LOG_INFO,
                        "SCRIPT: loaded contrib.s7 from mod '%s'",
                        package.meta().id.c_str());
                }
            }
        }
    }

    loadMenuBackgroundConfig(world, assets, scheme);

    TraceLog(LOG_INFO, "RENDER: entering menu (package on-startup / Debug → Map)");
    enterMenu(world);
}

}
